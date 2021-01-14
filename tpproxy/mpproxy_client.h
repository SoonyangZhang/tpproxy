#include <string>
#include <atomic>
#include <vector>
#include <memory>
#include "base/base_alarm.h"
#include "base/epoll_api.h"
#include "base/socket_address.h"
#include "tcp/tcp_server.h"
#include "tcp/tcp_types.h"
#include "mpproxy_types.h"
namespace basic{
class TcpConnectionRight;

class TpServerLeft:public TpServerBase,
public EpollCallbackInterface{
public:
    TpServerLeft(BaseContext *context,int fd,uint64_t uuid,uint64_t sid,
                std::vector<SocketAddressPair> &address_vec);
    ~TpServerLeft();
    void Notify(TcpConnectionBase *from,uint8_t signal) override;
    // From EpollCallbackInterface
    void OnRegistration(EpollServer* eps, int fd, int event_mask) override{}
    void OnModification(int fd, int event_mask) override  {}
    void OnEvent(int fd,EpollEvent* event) override;
    void OnUnregistration(int fd, bool replaced) override {}
    void OnShutdown(basic::EpollServer* eps, int fd) override;
    std::string Name() const override {return "TpServerLeft";}
private:
    void CreateConnections(uint64_t uuid,uint64_t sid,std::vector<SocketAddressPair> &address_vec);
    bool RemoveFromWaitList(TcpConnectionBase* connection);
    std::deque<TcpConnectionBase*> wait_list_;
    sockaddr_storage origin_dst_;
    bool origin_connected_=false;
};
//offset+payload_len+payload
class TcpConnectionRight:public TcpConnectionBase,
public EpollCallbackInterface{
public:
    TcpConnectionRight(BaseContext *context,int fd,TpServerLeft * server,uint64_t uuid,uint64_t sid,
    sockaddr_storage &src,sockaddr_storage &dst,sockaddr_storage &origin_dst);
    ~TcpConnectionRight();
    void Notify(uint8_t signal) override;
    bool AsynConnect();
    // From EpollCallbackInterface
    void OnRegistration(EpollServer* eps, int fd, int event_mask) override{}
    void OnModification(int fd, int event_mask) override  {}
    void OnEvent(int fd,EpollEvent* event) override;
    void OnUnregistration(int fd, bool replaced) override {}
    void OnShutdown(basic::EpollServer* eps, int fd) override;
    std::string Name() const override {return "TcpConnectionRight";}
private:
    void Deliver();
    void WriteMetaData();
    void ParserMessage(uint64_t offset,const char *data,size_t size) override;
    uint64_t uuid_;
    uint64_t sid_;
    sockaddr_storage src_addr_;
    sockaddr_storage dst_addr_;
    sockaddr_storage origin_dst_;
    bool origin_connected_=false;
};
class TpClientBackend:public Backend{
public:
    //uuid starts from 1
    TpClientBackend(uint64_t uuid,std::vector<SocketAddressPair> addr):uuid_(uuid),address_vec_(addr){}
    void CreateEndpoint(BaseContext *context,int fd) override;
private:
    uint64_t uuid_=1;
    uint64_t sid_=1;
    std::vector<SocketAddressPair> address_vec_;
};
class TpClientSocketFactory:public SocketServerFactory{
public:
    TpClientSocketFactory(uint64_t uuid,std::vector<SocketAddressPair> &address);
    PhysicalSocketServer* CreateSocketServer(BaseContext *context) override;
private:
    uint64_t uuid_=1;
    std::vector<SocketAddressPair> address_vec_;
};
}
