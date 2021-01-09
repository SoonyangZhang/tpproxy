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
    int remain=write_buffer_.size();
    int offset=0;
    const char *data=write_buffer_.data();
    bool flushed=false;
    while(remain>0){
        int intend=std::min(kBufferSize,(int)(remain-offset));
        int sent=write(fd_,data,intend);
        if(sent<=0){
            break;
        }
        send_bytes_+=sent;
        flushed=true;
        data+=sent;
        offset+=sent;
        remain-=sent;
    }
    if(flushed){
        if(remain>0){
            std::string copy(data,remain);
            copy.swap(write_buffer_);
            
        }else{
            std::string null_str;
            null_str.swap(write_buffer_);
        }        
    }
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
void TpProxyBase::OnCanWrite(int fd){
    FlushBuffer();
    if(write_buffer_.size()>0){
       context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLOUT|EPOLLET|EPOLLRDHUP|EPOLLERR); 
    }else{
        context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLET|EPOLLRDHUP|EPOLLERR);
    }
    if((signal_==TPPROXY_CLOSE)&&write_buffer_.size()==0){
        context_->epoll_server()->UnregisterFD(fd_);
    }
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
        context_->epoll_server()->UnregisterFD(fd_);
        return;
    }
    int right_fd=socket(AF_INET, SOCK_STREAM, 0);
    if(0>right_fd){
        context_->epoll_server()->UnregisterFD(fd_);
        return;        
    }
    SocketAddress local(IpAddress::Any4(),0);
    SocketAddress remote(remote_addr);
    std::cout<<remote.ToString()<<std::endl;
    right_=new TpProxyRight(context,right_fd);
    bool success=right_->AsynConnect(local,remote);
    if(!success){
        context_->epoll_server()->UnregisterFD(fd_);
        right_=nullptr;
        std::cout<<"asyn failed"<<std::endl;
        return;         
    }else{
        right_->set_left(this);
    }
    
}
TpProxyLeft::~TpProxyLeft(){
    std::cout<<"left dtor "<<recv_bytes_<<" "<<send_bytes_<<std::endl;
}
void TpProxyLeft::SendData(const char *pv,size_t size){
    if(fd_<0){
        return;
    }
    FlushBuffer();
    size_t old_size=write_buffer_.size();
    if(old_size>0){
        write_buffer_.resize(old_size+size);
        memcpy(&write_buffer_[old_size],pv,size);
        return;
    }
    if(old_size==0){
        size_t sent=write(fd_,pv,size);
        if(sent<size){
            const char *data=pv+sent;
            size_t remain=size-sent;
            send_bytes_+=sent;
            write_buffer_.resize(old_size+remain);
            memcpy(&write_buffer_[old_size],data,remain);
            context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLOUT|EPOLLET|EPOLLRDHUP|EPOLLERR);
        }
    }
}
void TpProxyLeft::Notify(uint8_t sig){
    if(TPPROXY_CLOSE==sig||TPPROXY_CONNECT_FAIL==sig){
        right_=nullptr;
        signal_=sig;
    }
}
void TpProxyLeft::OnRegistration(basic::EpollServer* eps, int fd, int event_mask){}
void TpProxyLeft::OnModification(int fd, int event_mask){}
void TpProxyLeft::OnEvent(int fd, basic::EpollEvent* event){
    if(event->in_events & EPOLLIN){
        OnReadEvent(fd);
    }
    if(event->in_events&EPOLLOUT){
        OnCanWrite(fd);
    }
    if(event->in_events &(EPOLLRDHUP|EPOLLHUP)){
        context_->epoll_server()->UnregisterFD(fd_);   
    }    
}
void TpProxyLeft::OnUnregistration(int fd, bool replaced){
    Close();
    DeleteSelf();
}
void TpProxyLeft::OnShutdown(basic::EpollServer* eps, int fd){
    Close();
    DeleteSelf();
}
std::string TpProxyLeft::Name() const{
    return "left";
}
void TpProxyLeft::OnReadEvent(int fd){
    char buffer[kBufferSize];
    while(true){
        size_t nbytes=read(fd,buffer,kBufferSize);  
        if (nbytes == -1) {
            //if(errno == EWOULDBLOCK|| errno == EAGAIN){}
            break;            
        }else if(nbytes==0){
            context_->epoll_server()->UnregisterFD(fd_);            
        }else{
            recv_bytes_+=nbytes;
            if(right_){
                right_->SendData(buffer,nbytes);
            }
        }       
    }    
}
void TpProxyLeft::Close(){
    if(right_){
        right_->Notify(TPPROXY_CLOSE);
        right_=nullptr;
    }
    if(fd_>0){
        close(fd_);
        fd_=-1;
    }    
}

TpProxyRight::TpProxyRight(basic::BaseContext *context,int fd):TpProxyBase(context,fd){}
TpProxyRight::~TpProxyRight(){
    std::cout<<"right dtor "<<recv_bytes_<<" "<<send_bytes_<<std::endl;
}
void TpProxyRight::Notify(uint8_t sig){
    if(TPPROXY_CLOSE==sig){
        left_=nullptr;
        signal_=sig;
    }
}
void TpProxyRight::SendData(const char *pv,size_t size){
    if(fd_<0){
        return ;
    }
    if(status_!=CONNECTED){
        size_t old_size=write_buffer_.size();
        write_buffer_.resize(old_size+size);
        memcpy(&write_buffer_[old_size],pv,size);
    }
    if(status_==CONNECTED){
        FlushBuffer();
        size_t old_size=write_buffer_.size();
        if(old_size>0){
            write_buffer_.resize(old_size+size);
            memcpy(&write_buffer_[old_size],pv,size);
            return;
        }
        if(old_size==0){
            size_t sent=write(fd_,pv,size);
            if(sent<size){
                send_bytes_+=sent;
                const char *data=pv+sent;
                size_t remain=size-sent;
                write_buffer_.resize(old_size+remain);
                memcpy(&write_buffer_[old_size],data,remain);
                context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLOUT|EPOLLET|EPOLLRDHUP|EPOLLERR);
            }
        }    
    }
}
void TpProxyRight::set_left(TpProxyLeft *left){
    left_=left;
}
bool TpProxyRight::AsynConnect(SocketAddress &local,SocketAddress &remote){
    src_addr_=local.generic_address();
    dst_addr_=remote.generic_address();
    int yes=1;
    bool success=false;
    size_t addr_size = sizeof(struct sockaddr_storage);
    if(bind(fd_, (struct sockaddr *)&src_addr_, addr_size)<0){
        Close();
        return success;
    }
    if(setsockopt(fd_,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int))!=0){
        Close();
        return success;        
    }
    context_->epoll_server()->RegisterFD(fd_, this,EPOLLIN|EPOLLOUT| EPOLLRDHUP | EPOLLERR | EPOLLET);
    if(connect(fd_,(struct sockaddr *)&dst_addr_,addr_size) == -1&& errno != EINPROGRESS){
        //connect doesn't work, are we running out of available ports ? if yes, destruct the socket   
        if (errno == EAGAIN){
            context_->epoll_server()->UnregisterFD(fd_);
            Close();
            return success;                
        }   
    }
    status_=CONNECTING;
    return true;    
}
void TpProxyRight::OnRegistration(basic::EpollServer* eps, int fd, int event_mask){}
void TpProxyRight::OnModification(int fd, int event_mask){}
void TpProxyRight::OnEvent(int fd, basic::EpollEvent* event){
    if (event->in_events&(EPOLLERR|EPOLLRDHUP| EPOLLHUP)){
        context_->epoll_server()->UnregisterFD(fd_);
        Close();       
    }   
    if(event->in_events&EPOLLOUT){
        if(status_==CONNECTING){
            status_=CONNECTED;
            std::cout<<"right connected"<<std::endl;
            context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLRDHUP|EPOLLERR | EPOLLET);
        }
        OnCanWrite(fd);
    }
    if(event->in_events&EPOLLIN){
        OnReadEvent(fd);
    }    
}
void TpProxyRight::OnUnregistration(int fd, bool replaced){
    Close();
    DeleteSelf();
}
void TpProxyRight::OnShutdown(basic::EpollServer* eps, int fd){
    Close();
    DeleteSelf();
}
std::string TpProxyRight::Name() const{
    return "right";
}
void TpProxyRight::OnReadEvent(int fd){
    char buffer[kBufferSize];
    while(true){
        size_t nbytes=read(fd,buffer,kBufferSize);  
        if (nbytes == -1) {
            //if(errno == EWOULDBLOCK|| errno == EAGAIN){}
            break;            
        }else if(nbytes==0){
            context_->epoll_server()->UnregisterFD(fd_);            
        }else{
            recv_bytes_+=nbytes;
            if(left_){
                left_->SendData(buffer,nbytes);
            }
        }       
    }    
}
void TpProxyRight::Close(){
    if(left_){
        left_->Notify(TPPROXY_CLOSE);
        left_=nullptr;
    }
    if(fd_>0){
        status_=DISCONNECT;
        close(fd_);
        fd_=-1;
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
