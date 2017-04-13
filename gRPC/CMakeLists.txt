# Minimum CMake required
cmake_minimum_required(VERSION 2.8)

# Project
project(mygrpc CXX)
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# Protobuf
set(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf3 REQUIRED)
message(STATUS "Using protobuf ${protobuf_VERSION}")

# gRPC
find_package(gRPC REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")

# gRPC C++ plugin
get_target_property(gRPC_CPP_PLUGIN_EXECUTABLE gRPC::grpc_cpp_plugin
        IMPORTED_LOCATION_RELEASE)

# Proto file
get_filename_component(my_proto "protos/hello.proto" ABSOLUTE)
get_filename_component(my_proto_path "${my_proto}" PATH)

# Generated sources
protobuf_generate_cpp(my_proto_srcs my_proto_hdrs "${my_proto}")
set(my_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/hello.grpc.pb.cc")
set(my_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/hello.grpc.pb.h")
add_custom_command(
        OUTPUT "${my_grpc_srcs}" "${my_grpc_hdrs}"
        COMMAND protobuf::protoc
        ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}" -I "${my_proto_path}"
        --plugin=protoc-gen-grpc="${gRPC_CPP_PLUGIN_EXECUTABLE}"
        "${my_proto}"
        DEPENDS "${my_proto}")

# Generated include directory
include_directories("${CMAKE_CURRENT_BINARY_DIR}")


add_executable(gserver GServer.cpp GServer.hpp)