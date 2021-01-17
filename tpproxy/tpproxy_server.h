#pragma once
#include <string>
#include <atomic>
#include "base/epoll_api.h"
#include "base/socket_address.h"
#include "tcp/tcp_server.h"
#include "tcp/tcp_types.h"
#include "tcp/tcp_info.h"
#include "tpproxy/bandwidth.h"
#include "tpproxy/windowed_filter.h"
#include "tpproxy/interval_budget.h"
namespace basic{ 
class TpProxyBase{
public:
    TpProxyBase(basic::BaseContext *context,int fd);
    virtual ~TpProxyBase();
    virtual void Notify(uint8_t sig){}
    void set_peer(TpProxyBase *peer) {peer_=peer;}
    void SendData(const char *pv,size_t size);
    void OnGoodputAlarm();
    void OnReadBudgetAlarm();
    void OnWriteBudgetAlarm();
    QuicBandwidth GoodputWithMinimum() const;
    bool IsBufferAboveThreshold() const;
protected:
    using MaxBandwidthFilter = WindowedFilter<QuicBandwidth,
                                            MaxFilter<QuicBandwidth>,
                                            int64_t,
                                            int64_t>;
    void FlushBuffer();
    void OnReadEvent(int fd);
    void OnWriteEvent(int fd);
    bool CanSend(int64_t bytes) const;
    void OnPacketSent(int64_t bytes);
    void CreateReadAlarm();
    void CheckCloseFd();
    void CloseFd();
    void DeleteSelf();
    basic::BaseContext* context_=nullptr;
    int fd_=-1;
    MaxBandwidthFilter max_goodput_;
    IntervalBudget read_budget_;
    IntervalBudget write_budget_;
    std::unique_ptr<BaseAlarm> read_budget_alarm_;
    std::unique_ptr<BaseAlarm> write_budget_alarm_;
    std::unique_ptr<BaseAlarm> goodput_alarm_;
    QuicTime last_goodput_time_=QuicTime::Zero();
    QuicTime last_read_budget_time_=QuicTime::Zero();
    QuicTime last_write_budget_time_=QuicTime::Zero();
    std::string fd_write_buffer_;
    TpProxyBase *peer_=nullptr;
    std::atomic<bool> destroyed_{false};
    TcpConnectionStatus status_=TCP_DISCONNECT;
    uint8_t signal_=0;
    int send_bytes_=0;
    int recv_bytes_=0;
    uint64_t last_bytes_acked_=0;
    uint64_t goodput_sample_count_=0;
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
