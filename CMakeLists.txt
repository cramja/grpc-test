cmake_minimum_required(VERSION 3.7)
project(ExampleServer)
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
set(CMAKE_CXX_STANDARD 11)

include_directories("${CMAKE_CURRENT_BINARY_DIR}")

## Find required libraries
#

# Protobuf
set(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf3 REQUIRED)
message(STATUS "Using protobuf ${protobuf_VERSION}")

# gRPC
find_package(gRPC REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")

# Compile the protos, add to a library
#
GRPC_GENERATE_CPP(SimpleServer_proto_srcs SimpleServer_proto_hdrs . "SimpleServer.proto")

add_library(SimpleServer_proto "${SimpleServer_proto_srcs}" "${SimpleServer_proto_hdrs}")
target_link_libraries(SimpleServer_proto
                      ${GRPCPLUSPLUS_LIBRARIES}
                      ${PROTOBUF3_LIBRARY})

## Add Executables
#

add_executable(client ClientExe.cpp)
add_executable(server ServerExe.cpp)

set(LIBS
    ${PROTOBUF3_LIBRARIES}
    ${GRPCPLUSPLUS_LIBRARIES}
    SimpleServer_proto)

foreach(EXE client server)
  target_link_libraries(${EXE}
                        ${LIBS})
endforeach(EXE)