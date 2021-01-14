#include <memory.h>
#include <unistd.h>
#include <error.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <linux/netfilter_ipv4.h>
#include <iostream>
#include "mpproxy_server.h"
#include "base/byte_codec.h"
#include  "logging/logging.h"
namespace basic{
TcpConnectionLeft::TcpConnectionLeft(BaseContext *context,int fd,TpServerBackend *backend)
:TcpConnectionBase(context,fd),backend_(backend){
    std::cout<<"TcpConnectionLeft "<<fd_<<std::endl;
    status_=TCP_CONNECTED;
    context_->epoll_server()->RegisterFD(fd_,this,EPOLLIN|EPOLLET|EPOLLRDHUP|EPOLLHUP|EPOLLERR);
}
TcpConnectionLeft::~TcpConnectionLeft(){
    std::cout<<"TcpConnectionLeft dtor"<<std::endl;
}
void TcpConnectionLeft::Notify(uint8_t signal){
    if(PROXY_CLOSE==signal){
        io_status_&=(~FD_WAIT_CLOSE_IF_ENTITY_CLOSED);
        io_status_|=FD_WAIT_CLOSE_IF_ENTITY_CLOSED;
        server_=nullptr;
        CheckCloseFd();
    }
    if(PROXY_CONNECTED==signal){
        SendDstConnectMessage();
    }
}
void TcpConnectionLeft::OnEvent(int fd,EpollEvent* event){
    if(event->in_events & EPOLLIN){
        OnReadEvent(fd);
    }
    if(event->in_events&EPOLLOUT){
        OnWriteEvent(fd);
    }
    if(event->in_events &(EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
        Deliver();
        CloseFd();
        CheckCloseEntity();
        std::cout<<"TcpConnectionLeft::OnEvent epoll error"<<std::endl;
    }  
}
void TcpConnectionLeft::OnShutdown(EpollServer* eps, int fd){
    CloseFd();
    DeleteSelf();
}
void TcpConnectionLeft::SendDstConnectMessage(){
    if(!dst_connect_msg_sent){
        std::cout<<"TcpConnectionLeft::SendDstConnectMessage"<<std::endl;
        WriteMessage(PROXY_MESSAGE_ORIGIN_CONNECTED,nullptr,0);
        OnWriteEvent(fd_);
    }
    dst_connect_msg_sent=true;
}
void TcpConnectionLeft::ParserMessage(uint64_t offset,const char *data,size_t size){
    uint8_t type=0;
    DataReader reader(data,size);
    bool success=reader.ReadUInt8(&type);
    if(success&&PROXY_MESSAGE_META==type){
        uint64_t uuid=0;
        uint64_t  sid=0;
        in_addr ip4;
        uint32_t ip_temp;
        uint16_t port=0;
        success=reader.ReadVarInt62(&uuid)&&reader.ReadVarInt62(&sid)&&
                reader.ReadUInt32(&ip_temp)&&reader.ReadUInt16(&port);
        memcpy((void*)&ip4,(void*)&ip_temp,sizeof(in_addr));
        IpAddress ip_addr(ip4);
        SocketAddress socket_addr(ip_addr,port);
        sockaddr_storage origin_dst=socket_addr.generic_address();
        std::cout<<"ParserMessage orgin dst "<<socket_addr.ToString()<<std::endl;
        if(0==uuid_){
            uuid_=uuid;
            TpServerRight *right=(TpServerRight*)backend_->Find(uuid_,sid,origin_dst);
            if(!right){
                int fd=socket(AF_INET, SOCK_STREAM,0);
                if(fd>=0){
                    right=new TpServerRight(context_,fd,backend_,uuid,sid,origin_dst);
                    server_=right;
                    right->Register(this);
                    if(!right->AsynConnect()){
                        CloseFd();
                        CloseEntity();
                    }else{
                        backend_->RegsiterSink(right,uuid,sid);
                    }
                }
            }else{
                server_=right;
                right->Register(this);
            }            
        }
    }
}
TpServerRight::TpServerRight(BaseContext *context,int fd,TpServerBackend *backend,
                uint64_t uuid,uint64_t sid,sockaddr_storage &origin_dst)
                :TpServerBase(context,fd),backend_(backend),
                uuid_(uuid),sid_(sid),origin_dst_(origin_dst){}
TpServerRight::~TpServerRight(){
    std::cout<<"TpServerRight dtor "<<send_bytes_<<" "<<recv_bytes_<<std::endl;  
}
bool TpServerRight::AsynConnect(){
    int yes=1;
    bool success=false;
    if(setsockopt(fd_,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int))!=0){
        CloseFd();
        DeleteSelf();
        return success;
    }
    context_->epoll_server()->RegisterFD(fd_, this,EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLERR|EPOLLET);
    size_t addr_size = sizeof(struct sockaddr_storage);
    if(connect(fd_,(struct sockaddr *)&origin_dst_,addr_size) == -1&&errno != EINPROGRESS){
        //connect doesn't work, are we running out of available ports ? if yes, destruct the socket   
        if (errno == EAGAIN){
            CloseFd();
            DeleteSelf();
            return success;                
        }   
    }
    status_=TCP_CONNECTING;
    return true; 
}
void TpServerRight::Register(TcpConnectionBase *from){
    RemoveFromReadyList(from);
    ready_list_.push_back(from);
    if(IsConnected()){
        from->Notify(PROXY_CONNECTED);
    }
}
void TpServerRight::Notify(TcpConnectionBase *from,uint8_t signal){
    if(PROXY_CLOSE==signal){
        RemoveFromReadyList(from);
        if(ready_list_.size()==0){
            io_status_&=(~FD_WAIT_CLOSE_IF_ENTITY_CLOSED);
            io_status_|=FD_WAIT_CLOSE_IF_ENTITY_CLOSED;
            if(sequencer_.HasBytesToRead()){
                std::cout<<"sequencer may encouter error"<<std::endl;
            }
            CheckCloseFd();
        }
    }
}
void TpServerRight::DeleteSelf(){
    if(destroyed_){
        return;
    }
    destroyed_=true;
    backend_->UnRegsiterSink(this,uuid_,sid_);
    context_->PostTask([this]{
        delete this;
    });
}
void TpServerRight::OnEvent(int fd,EpollEvent* event){  
    if(event->in_events&EPOLLOUT){
        if(TCP_CONNECTING==status_){
            status_=TCP_CONNECTED;
            for(auto it=ready_list_.begin();it!=ready_list_.end();it++){
                TcpConnectionBase* left=(*it);
                left->Notify(PROXY_CONNECTED);
            }
            std::cout<<"right connected "<<ready_list_.size()<<std::endl;
        }
        OnWriteEvent(fd);
    }
    if(event->in_events&EPOLLIN){
        OnReadEvent(fd);
    } 
    if (event->in_events&(EPOLLERR|EPOLLRDHUP| EPOLLHUP)){
        CloseFd();
        CloseEntity();      
    } 
}
void TpServerRight::OnShutdown(EpollServer* eps, int fd){
    CloseFd();
    DeleteSelf();
}

TpServerBackend::~TpServerBackend(){
    CHECK(uuid_sessions_.empty());
}
void TpServerBackend::CreateEndpoint(BaseContext *context,int fd){
    TcpConnectionLeft *connecton=new TcpConnectionLeft(context,fd,this);
    UNUSED(connecton);
}
TpServerBase* TpServerBackend::Find(uint64_t uuid,uint64_t sid,sockaddr_storage &origin_dst){
    TpServerBase *server=nullptr;
    auto it1=uuid_sessions_.find(uuid);
    if(it1!=uuid_sessions_.end()){
        SessionIdServerMap *sessions=it1->second;
        auto it2=sessions->find(sid);
        if(it2!=sessions->end()){
            server=it2->second;
        }
    }
    return server;
}
void TpServerBackend::RegsiterSink(TpServerBase *server,uint64_t uuid,uint64_t ssid){
    SessionIdServerMap *session=nullptr;
    auto it1=uuid_sessions_.find(uuid);
    if(it1!=uuid_sessions_.end()){
        session=it1->second;
        auto it2=session->find(ssid);
        if(it2==session->end()){
            session->insert(std::make_pair(ssid,server));
        }else{
            CHECK(0);
        }
    }else{
        session=new SessionIdServerMap();
        uuid_sessions_.insert(std::make_pair(uuid,session));
        session->insert(std::make_pair(ssid,server));
    }
    
}
void TpServerBackend::UnRegsiterSink(TpServerBase *server,uint64_t uuid,uint64_t ssid){
    auto it1=uuid_sessions_.find(uuid);
    if(it1!=uuid_sessions_.end()){
        SessionIdServerMap *session=it1->second;
        auto it2=session->find(ssid);
        if(it2!=session->end()){
            session->erase(it2);
            if(session->empty()){
                uuid_sessions_.erase(it1);
                delete session;
            }
        }else{
            CHECK(0);
        }
    }else{
        CHECK(0);
    }
}
PhysicalSocketServer* TpServerSocketFactory::CreateSocketServer(BaseContext *context){
    std::unique_ptr<TpServerBackend> backend(new TpServerBackend());
    return new PhysicalSocketServer(context,std::move(backend));
}
}
