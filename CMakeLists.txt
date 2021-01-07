PROJECT(project)
cmake_minimum_required(VERSION 2.6)
SET(CMAKE_BUILD_TYPE "Debug")
add_definitions(-std=c++11)
add_definitions(-DNDEBUG)
include_directories(${CMAKE_SOURCE_DIR}/)
include_directories(${CMAKE_SOURCE_DIR}/logging)
include_directories(${CMAKE_SOURCE_DIR}/base)
include_directories(${CMAKE_SOURCE_DIR}/tcp)
include_directories(${CMAKE_SOURCE_DIR}/tpproxy)
set(base_LIB
base/base_alarm.cc
base/base_clock.cc
base/base_epoll_alarm_factory.cc  
base/base_epoll_clock.cc
base/base_thread.cc
base/base_time.cc
base/byte_codec.cc 
base/epoll_thread.cc
base/ip_address.cc
base/random.cc
base/simple_epoll_server.cc
base/socket_address.cc
tcp/tcp_client.cc
tcp/tcp_handle.cc
tcp/tcp_server.cc
)
add_library(base STATIC ${base_LIB})
set(tpproxy_src
tpproxy/tpproxy_server_test.cc
tpproxy/tpproxy_server.cc
)
set(EXECUTABLE_NAME "t_proxy")
add_executable(${EXECUTABLE_NAME} ${tpproxy_src})
target_link_libraries(${EXECUTABLE_NAME} base  pthread)

