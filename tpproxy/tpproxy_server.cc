#include <memory.h>
#include <unistd.h>
#include <error.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <linux/netfilter_ipv4.h>
#include <algorithm>
#include <iostream>
#include "tpproxy/tpproxy_server.h"
namespace basic{
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
namespace{
const QuicTime::Delta kGoodputInterval=QuicTime::Delta::FromMilliseconds(500);
const QuicTime::Delta kWriteBudgetInterval=QuicTime::Delta::FromMilliseconds(50);
const QuicTime::Delta kReadBudgetInterval=QuicTime::Delta::FromMilliseconds(10);
const QuicBandwidth kMinGoodput=QuicBandwidth::FromKBitsPerSecond(500);
const int kBandwithWindowSize=10;
const int kWriteBufferThreshold=1500*10;
const size_t kBufferSize=1500;
const double kBandwidthGain=1.25;
}
class GoogputAlarmDelegate:public BaseAlarm::Delegate{
public:
    GoogputAlarmDelegate(TpProxyBase *entity):entity_(entity){}
    void OnAlarm() override{
        entity_->OnGoodputAlarm();
    }
private:
    TpProxyBase *entity_=nullptr;    
};
class ReadBudgetAlarmDelegate:public BaseAlarm::Delegate{
public:
    ReadBudgetAlarmDelegate(TpProxyBase *entity):entity_(entity){}
    void OnAlarm() override{
        entity_->OnReadBudgetAlarm();
    }
private:
    TpProxyBase *entity_=nullptr;    
};
class WriteBudgetAlarmDelegate:public BaseAlarm::Delegate{
public:
    WriteBudgetAlarmDelegate(TpProxyBase *entity):entity_(entity){}
    void OnAlarm() override{
        entity_->OnWriteBudgetAlarm();
    }
private:
    TpProxyBase *entity_=nullptr;    
};

TpProxyBase::TpProxyBase(basic::BaseContext *context,int fd):context_(context),fd_(fd),
max_goodput_(kBandwithWindowSize,QuicBandwidth::Zero(),0),
read_budget_(0),
write_budget_(0){
    struct tcp_info_copy info;
    memset(&info,0,sizeof(info));
    socklen_t info_size=sizeof(info);
    uint64_t bytes_acked=0;
    if(getsockopt(fd_,IPPROTO_TCP,TCP_INFO,(void*)&info,&info_size)==0){
        bytes_acked=info.tcpi_bytes_acked;
    }
    QuicTime now=context_->clock()->ApproximateNow();
    last_bytes_acked_=bytes_acked;
    last_goodput_time_=now;
    goodput_alarm_.reset(context_->alarm_factory()->CreateAlarm(new GoogputAlarmDelegate(this)));
    goodput_alarm_->Update(now+kGoodputInterval,QuicTime::Delta::Zero());
}
TpProxyBase::~TpProxyBase(){
    if(read_budget_alarm_){
        read_budget_alarm_->Cancel();
    }
    if(write_budget_alarm_){
        write_budget_alarm_->Cancel();
    }
    if(goodput_alarm_){
        goodput_alarm_->Cancel();
    }
}
void TpProxyBase::SendData(const char *pv,size_t size){
    if(fd_<0){
        return ;
    }
    if(!write_budget_alarm_){
        QuicTime now=context_->clock()->ApproximateNow();
        last_write_budget_time_=now;
        write_budget_alarm_.reset(context_->alarm_factory()->CreateAlarm(new WriteBudgetAlarmDelegate(this)));
        QuicBandwidth target=kBandwidthGain*GoodputWithMinimum();
        write_budget_.set_target_rate_kbps(target.ToKBitsPerSecond());
        write_budget_.IncreaseBudget(kWriteBudgetInterval.ToMilliseconds());
        write_budget_alarm_->Update(now+kWriteBudgetInterval,QuicTime::Delta::Zero());
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
void TpProxyBase::OnGoodputAlarm(){
    struct tcp_info_copy info;
    memset(&info,0,sizeof(info));
    socklen_t info_size=sizeof(info);
    uint64_t bytes_acked=0;
    if(getsockopt(fd_,IPPROTO_TCP,TCP_INFO,(void*)&info,&info_size)==0){
        bytes_acked=info.tcpi_bytes_acked;
    }
    QuicTime now=context_->clock()->ApproximateNow();
    goodput_alarm_->Update(now+kGoodputInterval,QuicTime::Delta::Zero());
    QuicBandwidth b=QuicBandwidth::Zero();
    if((bytes_acked>last_bytes_acked_)&&(now>last_goodput_time_)){
        b=QuicBandwidth::FromBytesAndTimeDelta(bytes_acked-last_bytes_acked_,now-last_goodput_time_);
    }
    max_goodput_.Update(b,goodput_sample_count_);
    QuicBandwidth target=kBandwidthGain*GoodputWithMinimum();
    write_budget_.set_target_rate_kbps(target.ToKBitsPerSecond());
    goodput_sample_count_++;
    last_bytes_acked_=bytes_acked;
    last_goodput_time_=now;
}
void TpProxyBase::OnReadBudgetAlarm(){
    QuicTime now=context_->clock()->ApproximateNow();
    QuicBandwidth target=QuicBandwidth::Zero();
    if(peer_){
        target=kBandwidthGain*peer_->GoodputWithMinimum();
    }
    if(target<kMinGoodput){
        target=kMinGoodput;
    }
    read_budget_.set_target_rate_kbps(target.ToKBitsPerSecond());
    if(last_read_budget_time_==QuicTime::Zero()){
        read_budget_.IncreaseBudget(kReadBudgetInterval.ToMilliseconds());
    }else{
        if(now>last_read_budget_time_){
            int64_t delta_time_ms=(now-last_read_budget_time_).ToMilliseconds();
            read_budget_.IncreaseBudget(delta_time_ms);
        }else{
            read_budget_.IncreaseBudget(kReadBudgetInterval.ToMilliseconds());
        }
    }
    last_read_budget_time_=now;
    read_budget_alarm_->Update(now+kReadBudgetInterval,QuicTime::Delta::Zero());
    bool buffer_full=IsBufferAboveThreshold();
    if(fd_>0&&(!buffer_full)&&(status_==TCP_CONNECTED)){
        OnReadEvent(fd_);
    }
    CheckCloseFd();
}
void TpProxyBase::OnWriteBudgetAlarm(){
    QuicTime now=context_->clock()->ApproximateNow();
    if(now>last_write_budget_time_){
        QuicBandwidth target=kBandwidthGain*GoodputWithMinimum();
        write_budget_.set_target_rate_kbps(target.ToKBitsPerSecond());
        int64_t delta_time_ms=(now-last_write_budget_time_).ToMilliseconds();
        write_budget_.IncreaseBudget(delta_time_ms);
    }
    last_write_budget_time_=now;
    write_budget_alarm_->Update(now+kWriteBudgetInterval,QuicTime::Delta::Zero());
}
QuicBandwidth TpProxyBase::GoodputWithMinimum() const{
    QuicBandwidth b=max_goodput_.GetBest();
    if(b<kMinGoodput){
        b=kMinGoodput;
    }
    return b;
}
bool TpProxyBase::IsBufferAboveThreshold() const{
    bool ret=false;
    if(fd_write_buffer_.size()>kWriteBufferThreshold){
        ret=true;
    }
    return ret;
}
void TpProxyBase::FlushBuffer(){
    if(fd_<0){
        return ;
    }
    size_t remain=fd_write_buffer_.size();
    const char *data=fd_write_buffer_.data();
    bool flushed=false;
    while(remain>0){
        size_t intend=std::min(kBufferSize,remain);
        if(CanSend(intend)){
            int sent=write(fd_,data,intend);
            if(sent<=0){
                break;
            }
            send_bytes_+=sent;
            flushed=true;
            data+=sent;
            remain-=sent;
            OnPacketSent(sent);
        }else{
            break;
        }

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
void TpProxyBase::OnReadEvent(int fd){
    char buffer[kBufferSize];
    int64_t read_bytes=0;
    int64_t target=read_budget_.bytes_remaining();
    if(target<0){
        return ;
    }
    while(true){
        size_t nbytes=read(fd,buffer,kBufferSize);  
        if (nbytes == -1) {
            //if(errno == EWOULDBLOCK|| errno == EAGAIN){}
            break;            
        }else if(nbytes==0){
            CloseFd();
        }else{
            recv_bytes_+=nbytes;
            read_bytes+=nbytes;
            if(peer_){
                peer_->SendData(buffer,nbytes);
            }
            if(read_bytes>=target){
                break;
            }
        }
    }
    if(read_bytes>0){
        read_budget_.UseBudget(read_bytes);
    }
}
void TpProxyBase::OnWriteEvent(int fd){
    FlushBuffer();
    if(fd_write_buffer_.size()>0){
       context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLOUT|EPOLLET|EPOLLRDHUP|EPOLLERR); 
    }else{
        context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLET|EPOLLRDHUP|EPOLLERR);
    }
    CheckCloseFd();
}
bool TpProxyBase::CanSend(int64_t bytes) const{
    bool ret=false;
    if(write_budget_.IsStarted()){
        if(write_budget_.bytes_remaining()>0){
            ret=true;
        }
    }else{
        ret=true;
    }
    return ret;
}
void TpProxyBase::OnPacketSent(int64_t bytes){
    if(write_budget_.IsStarted()){
        write_budget_.UseBudget(bytes);
    }
}
void TpProxyBase::CreateReadAlarm(){
    if(!read_budget_alarm_){
        read_budget_alarm_.reset(context_->alarm_factory()->CreateAlarm(new ReadBudgetAlarmDelegate(this)));
        QuicBandwidth target=QuicBandwidth::Zero();
        if(peer_){
            target=kBandwidthGain*peer_->GoodputWithMinimum();
        }
        if(target<kMinGoodput){
            target=kMinGoodput;
        }
        read_budget_.set_target_rate_kbps(target.ToKBitsPerSecond());
        read_budget_.IncreaseBudget(kReadBudgetInterval.ToMilliseconds());
        QuicTime now=context_->clock()->ApproximateNow();
        read_budget_alarm_->Update(now,QuicTime::Delta::Zero());            
    }
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
    context_->epoll_server()->RegisterFD(fd_,this,EPOLLET|EPOLLOUT|EPOLLRDHUP|EPOLLERR);
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
    if(TPPROXY_CONNECTED==sig){
        CreateReadAlarm();
    }
}
void TpProxyLeft::OnEvent(int fd, basic::EpollEvent* event){
    if(event->in_events & EPOLLIN){
        OnReadEvent(fd);
    }
    if(event->in_events&EPOLLOUT){
        OnWriteEvent(fd);
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
void TpProxyRight::OnShutdown(basic::EpollServer* eps, int fd){
    if(fd_>0){
        close(fd_);
        fd_=-1;
    }
    DeleteSelf();
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
            CreateReadAlarm();
            if(peer_){
                peer_->Notify(TPPROXY_CONNECTED);
            }
        }
        OnWriteEvent(fd);
    }
    if(event->in_events&EPOLLIN){
        OnReadEvent(fd);
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
