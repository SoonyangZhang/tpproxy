/*  
 * 
 *                       _oo0oo_ 
 *                      o8888888o 
 *                      88" . "88 
 *                      (| -_- |) 
 *                      0\  =  /0 
 *                    ___/`---'\___ 
 *                  .' \\|     |// '. 
 *                 / \\|||  :  |||// \ 
 *                / _||||| -卍-|||||- \ 
 *               |   | \\\  -  /// |   | 
 *               | \_|  ''\---/''  |_/ | 
 *               \  .-\__  '-'  ___/-. / 
 *             ___'. .'  /--.--\  `. .'___ 
 *          ."" '<  `.___\_<|>_/___.' >' "". 
 *         | | :  `- \`.;`\ _ /`;.`/ - ` : | | 
 *         \  \ `_.   \_ __\ /__ _/   .-` /  / 
 *     =====`-.____`.___ \_____/___.-`___.-'===== 
 *                       `=---=' 
 *                        
 *
 *  
 */
/*                   ---TcpConnectionRight1...............TcpConnectionLeft1--- 
                    |                                                          |
        intercept   |                                                          |     connect
src    TpServerLeft-----TcpConnectionRight2...............TcpConnectionLeft2------TpServerRight---origin_dst
                    |                                                          |
                    |                                                          |
                     ---TcpConnectionRight3...............TcpConnectionLeft3--- 
*/
#include <stdint.h>
#include <memory>
#include <string>
#include <atomic>
#include <deque>
#include "base/base_alarm.h"
#include "tcp/tcp_types.h"
#include "base/base_context.h"
#include "base/socket_address.h"
#include "sequencer/quic_stream_sequencer.h"
#define FILE_LINE __FILE__<<__LINE__<<" "
namespace basic{
enum ProxySingnal:uint8_t{
    PROXY_MIN,
    PROXY_CONNECT_FAIL,
    PROXY_CONNECTED,
    PROXY_CLOSE,
    PROXY_DST_CONNECTED,
    PROXY_DST_CONNECT_FAIL,
    PROXY_MAX,
};
enum ProxyMessage:uint8_t{
    PROXY_MESSAGE_MIN,
    PROXY_MESSAGE_META,
    PROXY_MESSAGE_ORIGIN_CONNECTED,
    PROXY_MESSAGE_ORIGIN_FAIL,
};
enum IoStatus:uint8_t{
    IO_MIN=0x00,
    FD_CLOSED=0x01,
    FD_WAIT_CLOSE=0x02,
    ENTITY_CLOSED=0x04,
    ENTITY_WAIT_CLOSE=0x08,
};
const uint8_t FD_WAIT_CLOSE_IF_ENTITY_CLOSED=FD_WAIT_CLOSE|ENTITY_CLOSED;
const uint8_t ENTITY_WAIT_CLOSE_IF_FD_CLOSED=ENTITY_WAIT_CLOSE|FD_CLOSED;
const uint64_t kReserveOffset=1;
const size_t kBatchSize=1500;
const size_t kBufferSize=1500;
struct SocketAddressPair{
   SocketAddressPair(sockaddr_storage a1,sockaddr_storage a2):src(a1),dst(a2){}
   sockaddr_storage src;
   sockaddr_storage dst;
};
class SocketBase{
public:
    SocketBase(BaseContext *context,int fd=-1);
    virtual ~SocketBase(){
        status_=TCP_DISCONNECT;
    }
    bool IsConnected(){return status_==TCP_CONNECTED;}
protected:
    void FlushBuffer();
    void OnWriteEvent(int fd);
    void CheckCloseFd();
    void CheckToDeleteSelf();
    void CloseFd();
    virtual void DeleteSelf();
    basic::BaseContext* context_=nullptr;
    uint8_t io_status_=0;
    TcpConnectionStatus status_=TCP_STATUS_MIN;
    int fd_=-1;
    std::string fd_write_buffer_;
    std::string fd_read_buffer_;
    std::atomic<bool> destroyed_{false};
    int send_bytes_=0;
    int recv_bytes_=0;
};
class TpServerBase;

class TcpConnectionBase:public SocketBase{
public:
    TcpConnectionBase(BaseContext *context,int fd);
    virtual ~TcpConnectionBase(){}
    virtual void Notify(uint8_t signal){}
    void SendData(uint64_t offset,const char *pv,size_t size);
    void WriteMessage(uint8_t type,const char *data,size_t size);
protected:
    void OnReadEvent(int fd);
    void Deliver();
    void CheckCloseEntity();
    void CloseEntity();
    virtual void ParserMessage(uint64_t offset,const char *pv,uint64_t size) {}
    TpServerBase *server_=nullptr;
};
class TpServerBase:public SocketBase,
public quic::QuicStreamSequencer::StreamInterface{
public:
    TpServerBase(BaseContext *context,int fd);
    virtual ~TpServerBase();
    virtual void Notify(TcpConnectionBase*from,uint8_t signal){}
    void RecvData(uint64_t offset,const char *pv,uint64_t size);
    //QuicStreamSequencer::StreamInterface
    void OnDataAvailable() override;
    void OnFinRead() override{}
    void AddBytesConsumed(quic::QuicByteCount bytes) override{}
    void Reset(quic::QuicRstStreamErrorCode error) override{}
    void OnUnrecoverableError(quic::QuicErrorCode error,const std::string& details) override{}
    quic::QuicStreamId id() const override { return stream_id_;}
    void OnSendAlarm();
protected:
    void OnReadEvent(int fd);
    
    size_t WriteDataToFd(const char *pv,size_t size);
    void OnDataFromFd(const char *pv,size_t size);
    
    void Schedule();
    bool RemoveFromReadyList(TcpConnectionBase* connection);
    
    void CheckCloseEntity();
    void CloseEntity();
    quic::QuicStreamSequencer sequencer_;
    std::unique_ptr<BaseAlarm> send_alarm_;
    uint32_t stream_id_=0;
    uint64_t write_offset_=kReserveOffset;
    int round_index_=0;
    std::deque<TcpConnectionBase*> ready_list_;
    int sequencer_bytes_=0;
};
}
