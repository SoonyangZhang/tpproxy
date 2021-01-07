#!/usr/bin/python
from mininet.topo import Topo
from mininet.net import Mininet
from mininet.cli import CLI
from mininet.link import TCLink
import time
import datetime
import subprocess
import os,signal
import sys
# 
#    h1----s1----h2------h3             
#
bottleneckbw=6
nonbottlebw=500;  
buffer_size =bottleneckbw*1000*30/(1500*8) 
net = Mininet( cleanup=True )
h1 = net.addHost('h1',ip='10.0.1.1')
h2 = net.addHost('h2',ip='10.0.1.2')
h3 = net.addHost('h3',ip='10.0.2.2')
s1 = net.addSwitch( 's1' )
c0 = net.addController('c0')
net.addLink(h1,s1,intfName1='h1-eth0',intfName2='s1-eth0',cls=TCLink , bw=nonbottlebw, delay='10ms', max_queue_size=10*buffer_size)
net.addLink(s1,h2,intfName1='s1-eth1',intfName2='h2-eth0',cls=TCLink , bw=bottleneckbw, delay='10ms', max_queue_size=buffer_size) 
net.addLink(h2,h3,intfName1='h2-eth1',intfName2='h3-eth0',cls=TCLink , bw=nonbottlebw, delay='50ms', max_queue_size=buffer_size)
net.build()
h1.cmd("ifconfig h1-eth0 10.0.1.1/24")
h1.cmd("route add default gw 10.0.1.2 dev h1-eth0")
h1.cmd('sysctl net.ipv4.ip_forward=1')

#tproxy
h2.cmd("iptables -t nat -N MY_TCP")
h2.cmd("iptables -t nat -A PREROUTING -j MY_TCP")
h2.cmd("iptables -t nat -A MY_TCP -p tcp -d 10.0.2.2 -j REDIRECT --to-ports 2223")
h2.cmd("iptables -N MY_TCP")
h2.cmd("iptables -A INPUT -j MY_TCP")
h2.cmd("iptables -A MY_TCP -p tcp --dport 2223 -j ACCEPT")

h2.cmd("ifconfig h2-eth0 10.0.1.2/24")
h2.cmd("ifconfig h2-eth1 10.0.2.1/24")
h2.cmd("ip route add to 10.0.2.0/24 via 10.0.2.2")
h2.cmd("ip route add to 10.0.1.0/24 via 10.0.1.1")
h2.cmd('sysctl net.ipv4.ip_forward=1')

h3.cmd("ifconfig h3-eth0 10.0.2.2/24")
h3.cmd("route add default gw 10.0.2.1 dev h3-eth0")
h3.cmd('sysctl net.ipv4.ip_forward=1')

net.start()
time.sleep(1)
CLI(net)
net.stop()

