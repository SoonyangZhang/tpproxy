PROJECT(project)
cmake_minimum_required(VERSION 2.6)
SET(CMAKE_BUILD_TYPE "Debug")
add_definitions(-std=c++14)
add_definitions(-DNDEBUG)
include_directories(${CMAKE_SOURCE_DIR}/)
include_directories(${CMAKE_SOURCE_DIR}/logging)
include_directories(${CMAKE_SOURCE_DIR}/base)
include_directories(${CMAKE_SOURCE_DIR}/tcp)
include_directories(${CMAKE_SOURCE_DIR}/tpproxy)
add_subdirectory(base)
set(tpproxy_src
tpproxy/interval_budget.cc
tpproxy/tpproxy_server_test.cc
tpproxy/tpproxy_server.cc
)
set(EXECUTABLE_NAME "t_proxy")
add_executable(${EXECUTABLE_NAME} ${tpproxy_src})
target_link_libraries(${EXECUTABLE_NAME} base  pthread)

