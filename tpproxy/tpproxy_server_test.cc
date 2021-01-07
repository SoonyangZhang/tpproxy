#include <signal.h>
#include <string.h>
#include "tpproxy/tpproxy_server.h"
#include "base/cmdline.h"
#include "base/ip_address.h"
#include "logging/logging.h"
/*
    ./t_proxy_server -p 3333
*/
using namespace basic;
using namespace std;
static volatile bool g_running=true;
void signal_exit_handler(int sig)
{
    g_running=false;
}
int main(int argc, char *argv[]){
    signal(SIGTERM, signal_exit_handler);
    signal(SIGINT, signal_exit_handler);
    signal(SIGTSTP, signal_exit_handler);    
    cmdline::parser a;
    a.add<string>("host", 'h', "host name", false, "0.0.0.0");
    a.add<uint16_t>("port", 'p', "port number", false, 3333, cmdline::range(1, 65535));
    a.parse_check(argc, argv); 
    std::string host=a.get<string>("host");
    uint16_t port=a.get<uint16_t>("port");
    IpAddress ip;
    ip.FromString(host);
    std::cout<<host<<" "<<port<<std::endl;
    std::unique_ptr<basic::TpProxyFactory> socket_facotry(new TpProxyFactory());
    TcpServer server(std::move(socket_facotry));
    PhysicalSocketServer *socket_server=server.socket_server();
    CHECK(socket_server);
    if(socket_server){
        int yes=1;
        if(socket_server->SetSocketOption(SOL_IP, IP_TRANSPARENT, &yes, sizeof(yes))){
            std::cout<<"set option error"<<std::endl;
            return 0;
        }
    }   
    bool success=server.Init(ip,port);
    if(success){
        while(g_running){
            server.HandleEvent();
        }
    }
    return 0;
}
