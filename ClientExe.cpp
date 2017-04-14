#include <grpc++/grpc++.h>

#include "SimpleServer.grpc.pb.h"
#include "SimpleServer.pb.h"

#include <iostream>
#include <memory>
#include <string>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

std::string readInput() {
  std::string input, line;
  while (std::getline(std::cin, line))
    input.append(line);
  return input;
}

class EchoClient {
 public:
  EchoClient(std::shared_ptr<Channel> const & channel)
    : stub_(SimpleServer::NewStub(channel)),
      last_return_code_(0) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  std::string RequestEcho(const std::string& user) {
    // Data we are sending to the server.
    CommandRequest request;
    request.set_args(user);
    request.set_type(CommandRequest::kEcho);

    // Container for the data we expect from the server.
    CommandReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The actual RPC.
    Status status = stub_->Command(&context, request, &reply);

    // Act upon its status.
    if (status.ok()) {
      last_return_code_ = reply.return_code();
      return reply.message();
    } else {
      std::cout << status.error_code()
                << ": " << status.error_message()
                << std::endl;
      return "RPC failed";
    }
  }

  bool SendQuit() {
    CommandRequest request;
    request.set_args("quit");
    request.set_type(CommandRequest::kQuit);
    CommandReply reply;
    ClientContext context;
    Status status = stub_->Command(&context, request, &reply);
    if(status.ok()) {
      last_return_code_ = reply.return_code();
      return last_return_code_ == 0;
    } else {
      std::cout << "on quit: " << status.error_code()
                << ": " << status.error_message()
                << std::endl;
      return false;
    }
  }

  int lastReturnCode() const {
    return last_return_code_;
  }

 private:
  int last_return_code_;
  std::unique_ptr<SimpleServer::Stub> stub_;
};

int main(int argc, char** argv) {
  EchoClient client(grpc::CreateChannel(
    "localhost:50051", grpc::InsecureChannelCredentials()));
  if (argc < 2){
    std::string user_input = "client says hello how many times?";
    std::cout << "Client sending: " << user_input << std::endl;
    std::string reply = client.RequestEcho(user_input);
    std::cout << "Client received: " << reply << std::endl;
  } else {
    std::cout << "issuing quit command, response: " << (client.SendQuit() ? "good" : "bad") << std::endl;
  }
  return client.lastReturnCode();
}