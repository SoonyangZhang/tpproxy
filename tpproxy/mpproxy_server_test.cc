#include <signal.h>
#include <string.h>
#include <vector>
#include <memory>
#include "base/cmdline.h"
#include "base/socket_address.h"
#include "logging/logging.h"
#include "tpproxy/mpproxy_server.h"
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
    a.add<string>("local", 'l', "local ip", false, "0.0.0.0");
    a.add<uint16_t>("port", 'p', "port number", false, 3333, cmdline::range(1,65535));   
    a.parse_check(argc, argv); 
    std::string local_str=a.get<string>("local");
    uint16_t local_port=a.get<uint16_t>("port");
    IpAddress src_ip;
    src_ip.FromString(local_str);
    std::cout<<src_ip<<" "<<local_port<<std::endl;
    std::unique_ptr<basic::TpServerSocketFactory> socket_facotry(new basic::TpServerSocketFactory());
    TcpServer server(std::move(socket_facotry));
    PhysicalSocketServer *socket_server=server.socket_server();
    CHECK(socket_server);
    bool success=server.Init(src_ip,local_port);
    if(success){
        while(g_running){
            server.HandleEvent();
        }
    }
    return 0;
}
