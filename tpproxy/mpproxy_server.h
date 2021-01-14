#pragma once
#include <string>
#include <atomic>
#include <memory>
#include <map>
#include <vector>
#include "base/epoll_api.h"
#include "tcp/tcp_server.h"
#include "mpproxy_types.h"
namespace basic{
class TpServerRight;
class TpServerBackend;

class TcpConnectionLeft:public TcpConnectionBase,
public EpollCallbackInterface{
public:
    TcpConnectionLeft(BaseContext *context,int fd,TpServerBackend *backend);
    ~TcpConnectionLeft();
    void Notify(uint8_t signal) override;
    size_t SendData(uint64_t offset,const char *pv,size_t size);
    // From EpollCallbackInterface
    void OnRegistration(EpollServer* eps, int fd, int event_mask) override{}
    void OnModification(int fd, int event_mask) override  {}
    void OnEvent(int fd,EpollEvent* event) override;
    void OnUnregistration(int fd, bool replaced) override {}
    void OnShutdown(basic::EpollServer* eps, int fd) override;
    std::string Name() const override {return "TcpConnectionLeft";}
private:
    void SendDstConnectMessage();
    void ParserMessage(uint64_t offset,const char *pv,uint64_t size) override;
    TpServerBackend *backend_;
    uint64_t uuid_=0;
    bool dst_connect_msg_sent=false;
};

class TpServerRight:public TpServerBase,
public EpollCallbackInterface{
public:
    TpServerRight(BaseContext *context,int fd,TpServerBackend *backend,
    uint64_t uuid,uint64_t sid,sockaddr_storage &origin_dst);
    ~TpServerRight();
    bool AsynConnect();
    void Register(TcpConnectionBase *from);
    void Notify(TcpConnectionBase *from,uint8_t signal) override;
    //from SocketBase
    void DeleteSelf() override;
    // From EpollCallbackInterface
    void OnRegistration(EpollServer* eps, int fd, int event_mask) override{}
    void OnModification(int fd, int event_mask) override  {}
    void OnEvent(int fd,EpollEvent* event) override;
    void OnUnregistration(int fd, bool replaced) override {}
    void OnShutdown(basic::EpollServer* eps, int fd) override;
    std::string Name() const override {return "TpServerRight";}
private:
    void CreateConnections(uint64_t uuid,uint64_t sid,std::vector<SocketAddressPair> &address_vec);
    TpServerBackend *backend_;
    uint64_t uuid_=0;
    uint64_t sid_=0;
    sockaddr_storage origin_dst_;
};
class TpServerBackend:public Backend{
public:
    TpServerBackend(){}
    ~TpServerBackend();
    void CreateEndpoint(BaseContext *context,int fd) override;
    TpServerBase* Find(uint64_t uuid,uint64_t sid,sockaddr_storage &origin_dst);
    void RegsiterSink(TpServerBase *sink,uint64_t uuid,uint64_t ssid);
    void UnRegsiterSink(TpServerBase *sink,uint64_t uuid,uint64_t ssid);
private:
    typedef std::map<uint64_t,TpServerBase*> SessionIdServerMap;
    std::map<uint64_t,SessionIdServerMap*> uuid_sessions_;
};
class TpServerSocketFactory:public SocketServerFactory{
public:
    TpServerSocketFactory(){}
    PhysicalSocketServer* CreateSocketServer(BaseContext *context) override;
};
}
