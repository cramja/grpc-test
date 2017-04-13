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
    : stub_(SimpleServer::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  std::string RequestEcho(const std::string& user) {
    // Data we are sending to the server.
    EchoRequest request;
    request.set_echo_message(user);

    // Container for the data we expect from the server.
    EchoReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The actual RPC.
    Status status = stub_->Echo(&context, request, &reply);

    // Act upon its status.
    if (status.ok()) {
      return reply.echo_response();
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return "RPC failed";
    }
  }

  bool SendQuitCommand() {
    CommandRequest request;
    request.set_command("quit");
    CommandReply reply;
    ClientContext context;
    Status status = stub_->Command(&context, request, &reply);
    if(status.ok()) {
      return reply.executed();
    } else {
      std::cout << "on quit: " << status.error_code()
                << ": " << status.error_message()
                << std::endl;
      return false;
    }
  }

 private:
  std::unique_ptr<SimpleServer::Stub> stub_;
};

int main(int argc, char** argv) {
  std::string user_input = "hello"; // readInput();
  std::cout << "Echo client sending: " << user_input << std::endl;
  EchoClient echoer(grpc::CreateChannel(
    "localhost:50051", grpc::InsecureChannelCredentials()));
  std::string reply = echoer.RequestEcho(user_input);
  std::cout << "Echo client received: " << reply << std::endl;

  if (argc > 1) {
    // now issue a quit command
    std::cout << "issuing quit command, response: " << (echoer.SendQuitCommand() ? "good" : "bad") << std::endl;
  }
  return 0;
}