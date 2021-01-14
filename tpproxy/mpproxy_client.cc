#include <memory.h>
#include <unistd.h>
#include <error.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <linux/netfilter_ipv4.h>
#include<arpa/inet.h>
#include <iostream>
#include <stdio.h>
#include "mpproxy_client.h"
#include "base/byte_codec.h"
#include  "logging/logging.h"
namespace basic{
TpServerLeft::TpServerLeft(BaseContext *context,int fd,uint64_t uuid,uint64_t sid,
                std::vector<SocketAddressPair> &address_vec):TpServerBase(context,fd){
    CHECK(address_vec.size());
    // wait to connect origin dst, do not read buffer for a short time.
    context_->epoll_server()->ModifyCallback(fd_,EPOLLRDHUP|EPOLLERR);
    socklen_t n=sizeof(origin_dst_);
    int ret =getsockopt(fd_, SOL_IP, SO_ORIGINAL_DST, &origin_dst_, &n);
    if(ret!=0){
        CloseFd();
        DeleteSelf();
        return;
    }
    SocketAddress remote(origin_dst_);
    std::cout<<FILE_LINE<<remote.ToString()<<std::endl;
    status_=TCP_CONNECTED;
    CreateConnections(uuid,sid,address_vec);
    if(0==wait_list_.size()){
        CloseFd();
        CloseEntity();
    }
}
TpServerLeft::~TpServerLeft(){
    std::cout<<"TpServerLeft dtor "<<send_bytes_<<" "<<recv_bytes_<<std::endl;
}
void TpServerLeft::Notify(TcpConnectionBase *from,uint8_t signal){
    if(PROXY_DST_CONNECTED==signal){
        if(RemoveFromWaitList(from)){
            ready_list_.push_back(from);
        }
        if(wait_list_.size()>0){
            auto it=wait_list_.begin();
            while(it!=wait_list_.end()){
                TcpConnectionBase* right=(*it);
                if(right->IsConnected()){
                    it=wait_list_.erase(it);
                    ready_list_.push_back(right);
                }else{
                    it++;
                }
            }            
        }
        if(!origin_connected_){
            origin_connected_=true;
            //read in data
            context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLRDHUP|EPOLLERR);
            //warning, read data from buffer.
            OnReadEvent(fd_);
        }
    }
    if(PROXY_CONNECTED==signal&&origin_connected_){
        if(RemoveFromWaitList(from)){
            ready_list_.push_back(from);
        }        
    }
    if(PROXY_CONNECT_FAIL==signal||PROXY_DST_CONNECT_FAIL==signal){
        bool ret=RemoveFromWaitList(from);
        CHECK(ret);
        if((wait_list_.size()==0)&&(ready_list_.size()==0)){
            io_status_&=(~FD_WAIT_CLOSE_IF_ENTITY_CLOSED);
            io_status_|=FD_WAIT_CLOSE_IF_ENTITY_CLOSED;
            CheckCloseFd();
        }
    }
    if(PROXY_CLOSE==signal){
        RemoveFromWaitList(from);
        RemoveFromReadyList(from);
        if((wait_list_.size()==0)&&(ready_list_.size()==0)){
            io_status_&=(~FD_WAIT_CLOSE_IF_ENTITY_CLOSED);
            io_status_|=FD_WAIT_CLOSE_IF_ENTITY_CLOSED;
            if(sequencer_.HasBytesToRead()){
                std::cout<<"sequencer may encouter error"<<std::endl;
            }
            CheckCloseFd();
        }
    }
}
void TpServerLeft::OnEvent(int fd,EpollEvent* event){
    if(event->in_events &(EPOLLRDHUP|EPOLLHUP)){
        CloseFd();
        CheckCloseEntity();
    } 
    if(event->in_events & EPOLLIN){
        OnReadEvent(fd);
    }
    if(event->in_events&EPOLLOUT){
        OnWriteEvent(fd);
    }   
}
void TpServerLeft::OnShutdown(EpollServer* eps, int fd){
    CloseFd();
    DeleteSelf();
}
void TpServerLeft::CreateConnections(uint64_t uuid,uint64_t sid,std::vector<SocketAddressPair> &address_vec){
    int parellel=address_vec.size();
    for(int i=0;i<parellel;i++){
        int fd=socket(AF_INET, SOCK_STREAM, 0);
        if(fd>=0){
            sockaddr_storage src=address_vec[i].src;
            sockaddr_storage dst=address_vec[i].dst;
            TcpConnectionRight *connection=new TcpConnectionRight(context_,fd,this,uuid,sid,src,dst,origin_dst_);
            if(connection->AsynConnect()){
                wait_list_.push_back((TcpConnectionBase*)connection);
            }        
        }       
    }
    std::cout<<"wait_list: "<<wait_list_.size()<<std::endl;
}
bool TpServerLeft::RemoveFromWaitList(TcpConnectionBase* connection){
    bool removed=false;
    if(wait_list_.size()>0){
        auto it=wait_list_.begin();
        while(it!=wait_list_.end()){
            if((*it)==connection){
                it =wait_list_.erase(it);
                removed=true;
                break;
            }else{
                it++;
            }
        }        
    }
    return removed;
}


TcpConnectionRight::TcpConnectionRight(BaseContext *context,int fd,TpServerLeft * server,uint64_t uuid,uint64_t sid,
                sockaddr_storage &src,sockaddr_storage &dst,sockaddr_storage &origin_dst)
:TcpConnectionBase(context,fd),
uuid_(uuid),
sid_(sid),
src_addr_(src),
dst_addr_(dst),
origin_dst_(origin_dst){
    server_=server;
}
TcpConnectionRight::~TcpConnectionRight(){
    std::cout<<"TcpConnectionRight dtor"<<std::endl;
}
void TcpConnectionRight::Notify(uint8_t signal){
    if(PROXY_CLOSE==signal){
        io_status_&=(~FD_WAIT_CLOSE_IF_ENTITY_CLOSED);
        io_status_|=FD_WAIT_CLOSE_IF_ENTITY_CLOSED;
        server_=nullptr;
        CheckCloseFd();
    }
}
bool TcpConnectionRight::AsynConnect(){
    int yes=1;
    bool success=false;
    size_t addr_size = sizeof(struct sockaddr_storage);
    if(setsockopt(fd_,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int))!=0){
        CloseFd();
        DeleteSelf();
        return success;
    }
    if(bind(fd_, (struct sockaddr *)&src_addr_, addr_size)<0){
        CloseFd();
        DeleteSelf();
        return success;
    }
    context_->epoll_server()->RegisterFD(fd_, this,EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLERR|EPOLLET);
    if(connect(fd_,(struct sockaddr *)&dst_addr_,addr_size) == -1&&errno != EINPROGRESS){
        //connect doesn't work, are we running out of available ports ? if yes, destruct the socket   
        if (errno == EAGAIN){
            CloseFd();
            DeleteSelf();
            return success;                
        }   
    }
    {
        struct sockaddr_in local;
        socklen_t len= sizeof(local);
        int ret=-1;
        ret=getsockname(fd_, (struct sockaddr *)&local, &len);
        printf("connected server address = %s:%d\n", inet_ntoa(local.sin_addr), ntohs(local.sin_port));
    }
    /*{
        struct sockaddr_in remote;
        socklen_t peer_len=sizeof(remote);
        int ret=-1;
        ret=getpeername(fd_, (struct sockaddr *)&remote, &peer_len);
        if(0==ret){
            SocketAddress addr((struct sockaddr *)&remote,peer_len);
            std::cout<<"peer addr "<<addr.ToString()<<std::endl;
        }else{
            std::cout<<"ret "<<ret<<std::endl;
        }      
    }*/

    status_=TCP_CONNECTING;
    return true; 
}
//https://cr.yp.to/docs/connect.html
void TcpConnectionRight::OnEvent(int fd,EpollEvent* event){
    if (event->in_events&(EPOLLERR|EPOLLRDHUP| EPOLLHUP)){
        CloseFd();
        CloseEntity();      
    }   
    if(event->in_events&EPOLLOUT){
        if(TCP_CONNECTING==status_){

    {
        sockaddr_in remote;
        socklen_t peer_len=sizeof(remote);
        int ret=-1;
        ret=getpeername(fd_, (struct sockaddr *)&remote, &peer_len);
        if(0==ret){
            SocketAddress addr((struct sockaddr *)&remote,peer_len);
            char ip_str[INET_ADDRSTRLEN];
            std::cout<<"peer addr "<<addr.ToString()<<std::endl;
        }else{
            std::cout<<"ret1 "<<ret<<std::endl;
        }      
    }

            status_=TCP_CONNECTED;
            WriteMetaData();
            if(server_){
                server_->Notify(this,PROXY_CONNECTED);
            }
            std::cout<<"right connected"<<std::endl;
        }
        OnWriteEvent(fd);
    }
    if(event->in_events&EPOLLIN){
        OnReadEvent(fd);
    }    
}
void TcpConnectionRight::OnShutdown(EpollServer* eps, int fd){
    CloseFd();
    DeleteSelf();
}
//offset len type uuid sid ip port
void TcpConnectionRight::WriteMetaData(){
    uint8_t type=PROXY_MESSAGE_META;
    char buffer[kBufferSize];
    DataWriter writer(buffer,kBufferSize);
    SocketAddress socket_addr(origin_dst_);
    IpAddress host=socket_addr.host();
    in_addr ip4=host.GetIPv4();
    uint32_t ip_temp;
    memcpy((void*)&ip_temp,(void*)&ip4,sizeof(in_addr));
    uint16_t port=socket_addr.port();
    bool success=writer.WriteVarInt62(uuid_)&&writer.WriteVarInt62(sid_)&&
                 writer.WriteUInt32(ip_temp)&&writer.WriteUInt16(port);
    WriteMessage(type,writer.data(),writer.length());
    std::cout<<"TcpConnectionRight::WriteMetaData"<<std::endl;
    CHECK(success);
}
void TcpConnectionRight::ParserMessage(uint64_t offset,const char *data,size_t size){
    uint8_t type=0;
    DataReader reader(data,size);
    bool success=reader.ReadUInt8(&type);
    if(PROXY_MESSAGE_ORIGIN_CONNECTED==type){
        if(!origin_connected_){
            origin_connected_=true;
            std::cout<<"origin connected"<<std::endl;
            if(server_){
                server_->Notify(this,PROXY_DST_CONNECTED);
            }
        }
    }
    if(PROXY_MESSAGE_ORIGIN_FAIL==type){
        CloseFd();
        CloseEntity();
    }
}
void TpClientBackend::CreateEndpoint(BaseContext *context,int fd){
    TpServerBase *server=new TpServerLeft(context,fd,uuid_,sid_,address_vec_);
    UNUSED(server);
    sid_++;
}
TpClientSocketFactory::TpClientSocketFactory(uint64_t uuid,std::vector<SocketAddressPair> &address)
:uuid_(uuid),address_vec_(address){}
PhysicalSocketServer* TpClientSocketFactory::CreateSocketServer(BaseContext *context){
    std::unique_ptr<TpClientBackend> backend(new TpClientBackend(uuid_,address_vec_));
    return new PhysicalSocketServer(context,std::move(backend));
}
}
