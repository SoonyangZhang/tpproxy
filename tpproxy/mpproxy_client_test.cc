#include <signal.h>
#include <string.h>
#include <vector>
#include <memory>
#include "base/cmdline.h"
#include "base/socket_address.h"
#include "logging/logging.h"
#include "tpproxy/mpproxy_client.h"
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
    a.add<string>("remote", 'r', "remote ip", true, "10.0.2.2");
    a.add<string>("local", 'l', "local ip", false, "0.0.0.0");
    a.add<uint16_t>("port", 'p', "port number", false, 3333, cmdline::range(1,65535));
    a.add<uint16_t>("cport", 'c', "cport number", false, 2223, cmdline::range(1,65535));     
    a.parse_check(argc, argv); 
    std::string remote_str=a.get<string>("remote");
    std::string local_str=a.get<string>("local");
    uint16_t local_port=a.get<uint16_t>("cport");
    uint16_t remote_port=a.get<uint16_t>("port");
    IpAddress src_ip;
    src_ip.FromString(local_str);
    std::cout<<src_ip<<" "<<local_port<<std::endl;
    IpAddress dst_ip;
    dst_ip.FromString(remote_str);
    SocketAddress dst_addr(dst_ip,remote_port);
    
    uint64_t uuid=1;
    std::vector<SocketAddressPair> address_table;
    {
        std::string local("10.0.2.1");
        IpAddress ip;
        ip.FromString(local);
        uint16_t local_port=0;
        SocketAddress socket_addr1(ip,local_port);
        sockaddr_storage addr_from=socket_addr1.generic_address();
        sockaddr_storage addr_to=dst_addr.generic_address();
        address_table.emplace_back(addr_from,addr_to);
    }
    {
        std::string local("10.0.2.1");
        IpAddress ip;
        ip.FromString(local);
        uint16_t local_port=0;
        SocketAddress socket_addr1(ip,local_port);
        sockaddr_storage addr_from=socket_addr1.generic_address();
        sockaddr_storage addr_to=dst_addr.generic_address();
        address_table.emplace_back(addr_from,addr_to);
    }
    std::unique_ptr<basic::TpClientSocketFactory> socket_facotry(new basic::TpClientSocketFactory(uuid,
                                                  address_table));
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
    bool success=server.Init(src_ip,local_port);
    if(success){
        while(g_running){
            server.HandleEvent();
        }
    }
    return 0;
}
