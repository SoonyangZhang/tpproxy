#pragma once
#include <string>
#include <atomic>
#include "base/epoll_api.h"
#include "base/socket_address.h"
#include "tcp/tcp_server.h"
#include "tcp/tcp_types.h"
namespace basic{
class TpProxyRight;
class TpProxyLeft:public EpollCallbackInterface{
public:
    TpProxyLeft(basic::BaseContext *context,int fd);
    ~TpProxyLeft();
    void Notify(uint8_t sig);
    void SendData(const char *pv,size_t size);
    // From EpollCallbackInterface
    void OnRegistration(basic::EpollServer* eps, int fd, int event_mask) override;
    void OnModification(int fd, int event_mask) override;
    void OnEvent(int fd, basic::EpollEvent* event) override;
    void OnUnregistration(int fd, bool replaced) override;
    void OnShutdown(basic::EpollServer* eps, int fd) override;
    std::string Name() const override;
private:
    void OnReadEvent(int fd);
    void OnCanWrite(int fd);
    void FlushBuffer();
    void Close();
    void DeleteSelf();
    basic::BaseContext* context_=nullptr;
    int fd_=-1;
    std::atomic<bool> destroyed_{false};
    TpProxyRight *right_=nullptr;
    std::string write_buffer_;
    int send_bytes_=0;
    int recv_bytes_=0;
    uint8_t signal_=0;
};
class TpProxyRight:public EpollCallbackInterface{
public:
    TpProxyRight(basic::BaseContext *context,int fd);
    ~TpProxyRight();
    void Notify(uint8_t sig);
    void SendData(const char *pv,size_t size);
    void set_left(TpProxyLeft *left);
    bool AsynConnect(SocketAddress &local,SocketAddress &remote);
    // From EpollCallbackInterface
    void OnRegistration(basic::EpollServer* eps, int fd, int event_mask) override;
    void OnModification(int fd, int event_mask) override;
    void OnEvent(int fd, basic::EpollEvent* event) override;
    void OnUnregistration(int fd, bool replaced) override;
    void OnShutdown(basic::EpollServer* eps, int fd) override;
    std::string Name() const override;
private:
    void OnReadEvent(int fd);
    void OnCanWrite(int fd);
    void FlushBuffer();
    void Close();
    void DeleteSelf();
    basic::BaseContext* context_=nullptr;
    struct sockaddr_storage src_addr_;
    struct sockaddr_storage dst_addr_;
    int fd_=-1;
    std::atomic<bool> destroyed_{false};
    TpProxyLeft *left_=nullptr;
    std::string write_buffer_;
    int send_bytes_=0;
    int recv_bytes_=0;
    TcpConnectionStatus status_{DISCONNECT};
    uint8_t signal_=0;
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
private:
    TpProxyBackend backend_;
};     
}