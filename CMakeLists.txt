cmake_minimum_required(VERSION 3.7)
project(TcpServer)

set(CMAKE_CXX_STANDARD 11)

add_executable(Client Client.cpp)
add_executable(Server Server.cpp)
add_executable(Echoer Echoer.cpp)

#set(SOURCE_FILES main.cpp)
#add_executable(TcpServer ${SOURCE_FILES})