#include <memory.h>
#include <unistd.h>
#include <error.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <linux/netfilter_ipv4.h>
#include <iostream>
#include "tpproxy/tpproxy_server.h"
namespace basic{
const int kBufferSize=1500;
enum TPPROXY_SIGNAL: uint8_t{
    TPPROXY_MIN,
    TPPROXY_CONNECT_FAIL,
    TPPROXY_CONNECTED,
    TPPROXY_CLOSE,
    TPPROXY_MAX,
};
/*EPOLLHUP
Hang  up   happened   on   the   associated   file   descriptor.
epoll_wait(2)  will always wait for this event; it is not neces-
sary to set it in events.
*/
TpProxyBase::TpProxyBase(basic::BaseContext *context,int fd):context_(context),fd_(fd){}
void TpProxyBase::FlushBuffer(){
    if(fd_<0){
        return ;
    }
    int remain=fd_write_buffer_.size();
    const char *data=fd_write_buffer_.data();
    bool flushed=false;
    while(remain>0){
        int intend=std::min(kBufferSize,remain);
        int sent=write(fd_,data,intend);
        if(sent<=0){
            break;
        }
        send_bytes_+=sent;
        flushed=true;
        data+=sent;
        remain-=sent;
    }
    if(flushed){
        if(remain>0){
            std::string copy(data,remain);
            copy.swap(fd_write_buffer_);
            
        }else{
            std::string null_str;
            null_str.swap(fd_write_buffer_);
        }        
    }
}
void TpProxyBase::SendData(const char *pv,size_t size){
    if(fd_<0){
        return ;
    }
    if(status_!=TCP_CONNECTED){
        size_t old_size=fd_write_buffer_.size();
        fd_write_buffer_.resize(old_size+size);
        memcpy(&fd_write_buffer_[old_size],pv,size);
    }
    if(status_==TCP_CONNECTED){
        FlushBuffer();
        size_t old_size=fd_write_buffer_.size();
        if(old_size>0){
            fd_write_buffer_.resize(old_size+size);
            memcpy(&fd_write_buffer_[old_size],pv,size);
            return;
        }
        if(old_size==0){
            size_t sent=write(fd_,pv,size);
            if(sent<size){
                send_bytes_+=sent;
                const char *data=pv+sent;
                size_t remain=size-sent;
                fd_write_buffer_.resize(old_size+remain);
                memcpy(&fd_write_buffer_[old_size],data,remain);
                context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLOUT|EPOLLET|EPOLLRDHUP|EPOLLERR);
            }
        }    
    }
}
void TpProxyBase::OnCanWrite(int fd){
    FlushBuffer();
    if(fd_write_buffer_.size()>0){
       context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLOUT|EPOLLET|EPOLLRDHUP|EPOLLERR); 
    }else{
        context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLET|EPOLLRDHUP|EPOLLERR);
    }
    CheckCloseFd();
}
void TpProxyBase::CheckCloseFd(){
    if((TPPROXY_CLOSE==signal_)&&(fd_write_buffer_.size()==0)){
        CloseFd();
    }
}
void TpProxyBase::CloseFd(){
    if(fd_>0){
        context_->epoll_server()->UnregisterFD(fd_);        
        close(fd_);
        fd_=-1;
        status_=TCP_DISCONNECT;
    }
    if(peer_){
        peer_->Notify(TPPROXY_CLOSE);
        peer_=nullptr;
    }
    DeleteSelf();
}
void TpProxyBase::DeleteSelf(){
    if(destroyed_){
        return;
    }
    destroyed_=true;
    context_->PostTask([this]{
        delete this;
    });
}

TpProxyLeft::TpProxyLeft(basic::BaseContext *context,int fd):TpProxyBase(context,fd){
    context_->epoll_server()->RegisterFD(fd_,this,EPOLLIN|EPOLLET|EPOLLRDHUP| EPOLLERR);
    struct sockaddr_storage remote_addr;
    /*IpAddress ip_addr;
    ip_addr.FromString("10.0.2.2");
    SocketAddress socket_addr(ip_addr,3333);
    remote_addr=socket_addr.generic_address();*/
    socklen_t n=sizeof(remote_addr);
    int ret =getsockopt(fd_, SOL_IP, SO_ORIGINAL_DST, &remote_addr, &n);
    if(ret!=0){
        CloseFd();
        return;
    }
    int right_fd=socket(AF_INET, SOCK_STREAM, 0);
    if(0>right_fd){
        CloseFd();
        return;        
    }
    SocketAddress local(IpAddress::Any4(),0);
    SocketAddress remote(remote_addr);
    std::cout<<remote.ToString()<<std::endl;
    peer_=new TpProxyRight(context,right_fd);
    bool success=((TpProxyRight*)peer_)->AsynConnect(local,remote);
    if(!success){
        CloseFd();
        std::cout<<"asyn failed"<<std::endl;
        return;         
    }else{
        peer_->set_peer(this);
    }
    status_=TCP_CONNECTED;
}
TpProxyLeft::~TpProxyLeft(){
    std::cout<<"left dtor "<<recv_bytes_<<" "<<send_bytes_<<std::endl;
}
void TpProxyLeft::Notify(uint8_t sig){
    if(TPPROXY_CLOSE==sig||TPPROXY_CONNECT_FAIL==sig){
        peer_=nullptr;
        signal_=TPPROXY_CLOSE;
        CheckCloseFd();
    }
}
void TpProxyLeft::OnEvent(int fd, basic::EpollEvent* event){
    if(event->in_events & EPOLLIN){
        OnReadEvent(fd);
    }
    if(event->in_events&EPOLLOUT){
        OnCanWrite(fd);
    }
    if(event->in_events &(EPOLLRDHUP|EPOLLHUP)){
        CloseFd(); 
    }    
}
void TpProxyLeft::OnShutdown(basic::EpollServer* eps, int fd){
    if(fd_>0){
        close(fd_);
        fd_=-1;
    }
    DeleteSelf();
}
void TpProxyLeft::OnReadEvent(int fd){
    char buffer[kBufferSize];
    while(true){
        size_t nbytes=read(fd,buffer,kBufferSize);  
        if (nbytes == -1) {
            //if(errno == EWOULDBLOCK|| errno == EAGAIN){}
            break;            
        }else if(nbytes==0){
            CloseFd();
        }else{
            recv_bytes_+=nbytes;
            if(peer_){
                peer_->SendData(buffer,nbytes);
            }
        }       
    }    
}

TpProxyRight::TpProxyRight(basic::BaseContext *context,int fd):TpProxyBase(context,fd){}
TpProxyRight::~TpProxyRight(){
    std::cout<<"right dtor "<<recv_bytes_<<" "<<send_bytes_<<std::endl;
}
void TpProxyRight::Notify(uint8_t sig){
    if(TPPROXY_CLOSE==sig){
        signal_=sig;
        peer_=nullptr;
        CheckCloseFd();
    }
}
bool TpProxyRight::AsynConnect(SocketAddress &local,SocketAddress &remote){
    src_addr_=local.generic_address();
    dst_addr_=remote.generic_address();
    int yes=1;
    bool success=false;
    size_t addr_size = sizeof(struct sockaddr_storage);
    if(bind(fd_, (struct sockaddr *)&src_addr_, addr_size)<0){
        CloseFd();
        return success;
    }
    if(setsockopt(fd_,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int))!=0){
        CloseFd();
        return success;        
    }
    context_->epoll_server()->RegisterFD(fd_, this,EPOLLIN|EPOLLOUT| EPOLLRDHUP | EPOLLERR | EPOLLET);
    if(connect(fd_,(struct sockaddr *)&dst_addr_,addr_size) == -1&& errno != EINPROGRESS){
        //connect doesn't work, are we running out of available ports ? if yes, destruct the socket   
        if (errno == EAGAIN){
            CloseFd();
            return success;                
        }   
    }
    status_=TCP_CONNECTING;
    return true;    
}
void TpProxyRight::OnEvent(int fd, basic::EpollEvent* event){
    if (event->in_events&(EPOLLERR|EPOLLRDHUP| EPOLLHUP)){
        CloseFd();       
    }   
    if(event->in_events&EPOLLOUT){
        if(status_==TCP_CONNECTING){
            status_=TCP_CONNECTED;
            std::cout<<"right connected"<<std::endl;
            context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLRDHUP|EPOLLERR | EPOLLET);
        }
        OnCanWrite(fd);
    }
    if(event->in_events&EPOLLIN){
        OnReadEvent(fd);
    }    
}
void TpProxyRight::OnShutdown(basic::EpollServer* eps, int fd){
    if(fd_>0){
        close(fd_);
        fd_=-1;
    }
    DeleteSelf();
}
void TpProxyRight::OnReadEvent(int fd){
    char buffer[kBufferSize];
    while(true){
        size_t nbytes=read(fd,buffer,kBufferSize);  
        if (nbytes == -1) {
            //if(errno == EWOULDBLOCK|| errno == EAGAIN){}
            break;            
        }else if(nbytes==0){
            CloseFd();
        }else{
            recv_bytes_+=nbytes;
            if(peer_){
                peer_->SendData(buffer,nbytes);
            }
        }       
    }    
}
void TpProxyBackend::CreateEndpoint(basic::BaseContext *context,int fd){
    TpProxyLeft *endpoint=new TpProxyLeft(context,fd);
    UNUSED(endpoint);
}
PhysicalSocketServer* TpProxyFactory::CreateSocketServer(BaseContext *context){
    std::unique_ptr<TpProxyBackend> backend(new TpProxyBackend());
    return new PhysicalSocketServer(context,std::move(backend));
}
}
