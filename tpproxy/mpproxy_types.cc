#include <unistd.h>
#include <memory.h>
#include "base/epoll_api.h"
#include "mpproxy_types.h"
#include "base/byte_codec.h"
#include  "logging/logging.h"
namespace basic{
bool IsDeliverable(const char *data,size_t size,uint64_t *offset,uint16_t *offset_bytes,uint64_t *len,uint16_t *len_bytes){
    bool ret=false;
    if(size>0){
    DataReader reader(data,size);
    bool success=reader.ReadVarInt62(offset)&&reader.ReadVarInt62(len);
    if(success){
        *offset_bytes=DataWriter::GetVarInt62Len(*offset);
        *len_bytes=DataWriter::GetVarInt62Len(*len);
        size_t frame_size=(*offset_bytes)+(*len_bytes)+(*len);
        if(size>=frame_size){
            ret=true;
        }
    }
    }
    if(!ret){
        *offset=0;
        *offset_bytes=0;
        *len=0;
        *len_bytes=0;
    }
    return ret;
}
SocketBase::SocketBase(basic::BaseContext *context,int fd):context_(context),fd_(fd){}
void SocketBase::FlushBuffer(){
    if(fd_<0){
        return ;
    }
    size_t remain=fd_write_buffer_.size();
    const char *data=fd_write_buffer_.data();
    bool flushed=false;
    while(remain>0){
        size_t intend=std::min(kBufferSize,remain);
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
void SocketBase::OnWriteEvent(int fd){
    FlushBuffer();
    if(fd_write_buffer_.size()>0){
       context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLERR); 
    }else{
        context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLRDHUP|EPOLLERR);
    }
    CheckCloseFd();
}
void SocketBase::CheckCloseFd(){
    if((io_status_&FD_WAIT_CLOSE)&&(fd_write_buffer_.size()==0)){
        CloseFd();
    }
}
void SocketBase::CheckToDeleteSelf(){
    if((io_status_&FD_CLOSED)&&(io_status_&ENTITY_CLOSED)){
        DeleteSelf();
    }
}
void SocketBase::CloseFd(){
    if(fd_>0){
        io_status_&=(~ENTITY_WAIT_CLOSE_IF_FD_CLOSED);
        io_status_|=ENTITY_WAIT_CLOSE_IF_FD_CLOSED;
        context_->epoll_server()->UnregisterFD(fd_);        
        close(fd_);
        fd_=-1;
    }
    CheckToDeleteSelf(); 
}
void SocketBase::DeleteSelf(){
    if(destroyed_){
        return;
    }
    destroyed_=true;
    context_->PostTask([this]{
        delete this;
    });
}



TcpConnectionBase::TcpConnectionBase(BaseContext *context,int fd):SocketBase(context,fd){}
//offset+len+pv
void TcpConnectionBase::SendData(uint64_t offset,const char *pv,size_t size){
    CHECK(fd_>0);
    std::cout<<"SendData "<<fd_<<" "<<size<<std::endl;
    size_t offset_bytes=DataWriter::GetVarInt62Len(offset);
    size_t size_bytes=DataWriter::GetVarInt62Len(size);
    size_t buffer_size=offset_bytes+size_bytes+size;
    size_t old_size=fd_write_buffer_.size();
    fd_write_buffer_.resize(old_size+buffer_size);
    DataWriter writer(&fd_write_buffer_[old_size],buffer_size);
    bool success=writer.WriteVarInt62(offset)&&writer.WriteVarInt62(size)&&
                writer.WriteBytes((const void*)pv,size);
    CHECK(success);
    if(IsConnected()){
        OnWriteEvent(fd_);
    }
}
void TcpConnectionBase::WriteMessage(uint8_t type,const char *data,size_t size){
    char buffer[kBufferSize];
    uint64_t offset=0;
    size_t len=sizeof(type)+size;
    size_t offset_bytes=DataWriter::GetVarInt62Len(offset);
    size_t len_bytes=DataWriter::GetVarInt62Len(len);
    DataWriter writer(buffer,kBufferSize);
    bool success=writer.WriteVarInt62(offset)&&writer.WriteVarInt62((uint64_t)len)&&
                writer.WriteUInt8(type);
    CHECK(success);
    if(size>0&&data){
        success=writer.WriteBytes((const void*)data,size);
        CHECK(success);
    }
    size_t old_size=fd_write_buffer_.size();
    fd_write_buffer_.resize(old_size+writer.length());
    memcpy(&fd_write_buffer_[old_size],writer.data(),writer.length());
    if(IsConnected()){
        OnWriteEvent(fd_);
    }
}
void TcpConnectionBase::OnReadEvent(int fd){
    char buffer[kBufferSize+500];
    bool update=false;
    while(true){
        size_t nbytes=read(fd,buffer,kBufferSize+500);  
        if (nbytes == -1) {
            //if(errno == EWOULDBLOCK|| errno == EAGAIN){}
            break;            
        }else if(nbytes==0){
            CloseFd();
            CHECK(fd_write_buffer_.size()==0);
        }else{
            recv_bytes_+=nbytes;
            size_t old=fd_read_buffer_.size();
            fd_read_buffer_.resize(old+nbytes);
            memcpy(&fd_read_buffer_[old],buffer,nbytes);
            update=true;
        }       
    }
    if(update){
        Deliver();
    }
    if(io_status_&FD_CLOSED){
        std::cout<<fd<<" "<<fd_read_buffer_.size()<<std::endl;
        CHECK(fd_read_buffer_.size()==0);
    }
}
void TcpConnectionBase::Deliver(){
    const char *data=fd_read_buffer_.data();
    size_t remain=fd_read_buffer_.size();
    uint64_t offset=0;
    uint16_t offset_bytes=0;
    uint64_t len=0;
    uint16_t len_bytes=0;
    bool update=false;
    while((remain>0)&&IsDeliverable(data,remain,&offset,&offset_bytes,&len,&len_bytes)){
        size_t header_size=offset_bytes+len_bytes;
        size_t frame_size=header_size+(size_t)len;
        const char*pv=data+header_size;
        if(offset>=kReserveOffset){
            CHECK(server_);
            server_->RecvData(offset,pv,(size_t)len);
            std::cout<<"TcpConnectionBase::Deliver "<<(uint32_t)offset<<" "<<frame_size<<" "<<(int)len<<std::endl;
        }else{
            if(len>0){
                ParserMessage(offset,pv,(size_t)len);
            }
        }
        data+=frame_size;
        remain-=frame_size;
        update=true;
    }
    if(update){
        if(remain>0){
            std::string copy(data,remain);
            copy.swap(fd_read_buffer_);
        }else{
            std::string null_str;
            null_str.swap(fd_read_buffer_);
            CheckCloseEntity();
        }        
    }
}
void TcpConnectionBase::CheckCloseEntity(){
    if((io_status_&ENTITY_WAIT_CLOSE)&&(fd_read_buffer_.size()==0)){
        CloseEntity();
    }
}
void TcpConnectionBase::CloseEntity(){
    io_status_&=(~ENTITY_CLOSED);
    io_status_|=ENTITY_CLOSED;
    if(server_){
        server_->Notify(this,PROXY_CLOSE);
        server_=nullptr;
    }
    CheckToDeleteSelf();
}


TpServerBase::TpServerBase(BaseContext *context,int fd):SocketBase(context,fd),sequencer_(this){}
TpServerBase::~TpServerBase(){
    std::cout<<"~TpServerBase: "<<sequencer_bytes_<<std::endl;
    if(send_alarm_){
        send_alarm_->Cancel();
    }
}
void TpServerBase::RecvData(uint64_t offset,const char *pv,uint64_t size){
    if(offset>=kReserveOffset){
        offset-=kReserveOffset;
        quic::QuicStreamFrame frame(stream_id_,false,offset,pv,(uint16_t)size);
        sequencer_.OnStreamFrame(frame);
    }
}
void TpServerBase::OnDataAvailable(){
    std::string buffer;
    sequencer_.Read(&buffer);
    sequencer_bytes_+=buffer.size();
    WriteDataToFd(buffer.data(),buffer.size());
}
void TpServerBase::OnReadEvent(int fd){
    char buffer[kBufferSize];
    while(true){
        size_t nbytes=read(fd,buffer,kBufferSize);  
        if (nbytes == -1) {
            //if(errno == EWOULDBLOCK|| errno == EAGAIN){}
            break;            
        }else if(nbytes==0){
            CloseFd();
            CheckCloseEntity();
        }else{
            recv_bytes_+=nbytes;
            std::cout<<"TpServerBase::OnReadEvent "<<nbytes<<std::endl;
            OnDataFromFd(buffer,nbytes);
        }       
    }    
}
size_t TpServerBase::WriteDataToFd(const char *pv,size_t size){
    if(fd_<0){
        return 0;
    }
    FlushBuffer();
    size_t old_size=fd_write_buffer_.size();
    if(old_size>0){
        fd_write_buffer_.resize(old_size+size);
        memcpy(&fd_write_buffer_[old_size],pv,size);
    }else if(old_size==0){
        size_t sent=write(fd_,pv,size);
        if(sent<size){
            const char *data=pv+sent;
            size_t remain=size-sent;
            send_bytes_+=sent;
            fd_write_buffer_.resize(old_size+remain);
            memcpy(&fd_write_buffer_[old_size],data,remain);
            context_->epoll_server()->ModifyCallback(fd_,EPOLLIN|EPOLLOUT|EPOLLET|EPOLLRDHUP|EPOLLERR);
        }
    }
    return size;
}
class SendDelegate:public BaseAlarm::Delegate{
public:
    SendDelegate(TpServerBase *entity):entity_(entity){}
    void OnAlarm() override{
        entity_->OnSendAlarm();
    }
private:
    TpServerBase *entity_=nullptr;    
};
void TpServerBase::OnDataFromFd(const char *pv,size_t size){
    size_t old_size=fd_read_buffer_.size();
    fd_read_buffer_.resize(old_size+size);
    memcpy(&fd_read_buffer_[old_size],pv,size);
    if(!send_alarm_){
        send_alarm_.reset(context_->alarm_factory()->CreateAlarm(new SendDelegate(this)));
    }
    if((send_alarm_)&&(!send_alarm_->IsSet())&&(ready_list_.size()>0)){
        QuicTime now=context_->clock()->ApproximateNow();
        send_alarm_->Update(now,QuicTime::Delta::Zero());
    }
}
void TpServerBase::OnSendAlarm(){
    Schedule();
    if(fd_read_buffer_.size()>0){
        QuicTime now=context_->clock()->ApproximateNow();
        QuicTime::Delta delay=QuicTime::Delta::FromMilliseconds(10);
        send_alarm_->Update(now+delay,QuicTime::Delta::Zero());        
    }
    if(io_status_&ENTITY_CLOSED){
        CHECK(fd_read_buffer_.size()==0);
    }
    if(io_status_&FD_CLOSED){
        CheckCloseEntity();
    }
}
void TpServerBase::Schedule(){
    const char *data=fd_read_buffer_.data();
    size_t remain=fd_read_buffer_.size();
    bool flushed=false;
    int availale=ready_list_.size();
    if(availale==0){
        return ;
    }
    round_index_=round_index_%availale;
    int count=0;
    while(remain>0){
        size_t sent=std::min(kBatchSize,remain);
        TcpConnectionBase *connection=ready_list_.at(round_index_);
        round_index_=(round_index_+1)%availale;
        connection->SendData(write_offset_,data,sent);
        //uint32_t len_bytes=DataWriter::GetVarInt62Len(sent);
        //write_offset_+=(len_bytes+sent);
        write_offset_+=sent;
        flushed=true;
        data+=sent;
        remain-=sent;
        count++;
        if(count>=availale){
            break;
        }
    }
    if(flushed){
        if(remain>0){
            std::string copy(data,remain);
            copy.swap(fd_read_buffer_);
        }else{
            std::string null_str;
            null_str.swap(fd_read_buffer_);
        }
    }
}
bool TpServerBase::RemoveFromReadyList(TcpConnectionBase* connection){
    bool removed=false;
    if(ready_list_.size()>0){
        std::deque<TcpConnectionBase*>::iterator it=ready_list_.begin();
        while(it!=ready_list_.end()){
            if((*it)==connection){
                it =ready_list_.erase(it);
                removed=true;
                break;
            }else{
                it++;
            }
        }        
    }
    return removed;
}
void TpServerBase::CheckCloseEntity(){
    if((io_status_&ENTITY_WAIT_CLOSE)&&(fd_read_buffer_.size()==0)){
        CloseEntity();
    }
}
void TpServerBase::CloseEntity(){
    io_status_&=(~ENTITY_CLOSED);
    io_status_|=ENTITY_CLOSED;
    for(auto it=ready_list_.begin();it!=ready_list_.end();it++){
        TcpConnectionBase *connection=(*it);
        connection->Notify(PROXY_CLOSE);
    }
    ready_list_.clear();
    CheckToDeleteSelf();    
}
}
