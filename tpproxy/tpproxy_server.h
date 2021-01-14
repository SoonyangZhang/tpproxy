#pragma once
#include <string>
#include <atomic>
#include "base/epoll_api.h"
#include "base/socket_address.h"
#include "tcp/tcp_server.h"
#include "tcp/tcp_types.h"
namespace basic{
class TpProxyRight;
class TpProxyBase{
public:
    TpProxyBase(basic::BaseContext *context,int fd);
    virtual ~TpProxyBase(){}
    virtual void Notify(uint8_t sig){}
    void SendData(const char *pv,size_t size);
    void set_peer(TpProxyBase *peer) {peer_=peer;}
protected:
    void FlushBuffer();
    void OnCanWrite(int fd);
    void CheckCloseFd();
    void CloseFd();
    void DeleteSelf();
    basic::BaseContext* context_=nullptr;
    int fd_=-1;
    std::string fd_write_buffer_;
    std::atomic<bool> destroyed_{false};
    int send_bytes_=0;
    int recv_bytes_=0;
    TcpConnectionStatus status_=TCP_DISCONNECT;
    uint8_t signal_=0;
    TpProxyBase *peer_=nullptr;
};
class TpProxyLeft:public TpProxyBase,
public EpollCallbackInterface{
public:
    TpProxyLeft(basic::BaseContext *context,int fd);
    ~TpProxyLeft();
    void Notify(uint8_t sig) override;
    // From EpollCallbackInterface
    void OnRegistration(basic::EpollServer* eps, int fd, int event_mask) override{}
    void OnModification(int fd, int event_mask) override {}
    void OnEvent(int fd, basic::EpollEvent* event) override;
    void OnUnregistration(int fd, bool replaced) override {}
    void OnShutdown(basic::EpollServer* eps, int fd) override;
    std::string Name() const override {return "TpProxyLeft";}
private:
    void OnReadEvent(int fd);
};
class TpProxyRight:public TpProxyBase,
public EpollCallbackInterface{
public:
    TpProxyRight(basic::BaseContext *context,int fd);
    ~TpProxyRight();
    void Notify(uint8_t sig) override;
    bool AsynConnect(SocketAddress &local,SocketAddress &remote);
    // From EpollCallbackInterface
    void OnRegistration(basic::EpollServer* eps, int fd, int event_mask) override {}
    void OnModification(int fd, int event_mask) override {}
    void OnEvent(int fd, basic::EpollEvent* event) override;
    void OnUnregistration(int fd, bool replaced) override {}
    void OnShutdown(basic::EpollServer* eps, int fd) override;
    std::string Name() const override {return "TpProxyRight";}
private:
    void OnReadEvent(int fd);
    struct sockaddr_storage src_addr_;
    struct sockaddr_storage dst_addr_;
};
class TpProxyBackend:public Backend{
public:
    TpProxyBackend(){}
    void CreateEndpoint(basic::BaseContext *context,int fd) override;
};
class TpProxyFactory: public SocketServerFactory{
public:
    ~TpProxyFactory(){}
    PhysicalSocketServer* CreateSocketServer(BaseContext *context) override;
};     
}
