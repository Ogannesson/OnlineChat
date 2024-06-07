cmake_minimum_required(VERSION 3.28)
project(OnlineChat)

set(CMAKE_CXX_STANDARD 20)

add_executable(Server Server.cpp)

add_executable(Client Client.cpp)

target_link_libraries(Server ws2_32)

target_link_libraries(Client ws2_32)