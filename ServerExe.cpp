#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

#include <grpc++/grpc++.h>

#include "SimpleServer.grpc.pb.h"
#include "SimpleServer.pb.h"

#define forever for(;;)

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

class CCState {
 public:
  CCState() :
    consumer_has_input_(false),
    consumer_has_output_(false),
    str_buffer(""),
    mutex(),
    condition() {}

  /**
   * Notifies that the consumer has finished consuming and that output is ready. To be called after the consumer has
   * executed.
   */
  void consumerOutputReady() {
    consumer_has_input_ = false;
    consumer_has_output_ = true;
    condition.notify_all();
  }

  /**
   * Notifies the consumer that a piece of work has been created, and that the consumer should execute the command.
   * @param command The command type to be executed. Should not be kNone.
   */
  void consumerInputReady(std::string to_consume) {
    consumer_has_input_ = true;
    consumer_has_output_ = false;
    str_buffer = to_consume;
    condition.notify_one();
  }

  bool consumer_has_input_;
  bool consumer_has_output_;
  std::string str_buffer;
  std::mutex mutex;
  std::condition_variable condition;
};

// Logic and data behind the server's behavior.
// The contained methods will be called from worker callback threads.
class CommandServiceImpl final : public SimpleServer::Service {
 public:
  CommandServiceImpl()
    : SimpleServer::Service(),
      running_(true),
      worker_exclusive_mtx_() {
  }

  Status Command(ServerContext* context,
                const CommandRequest* request,
                CommandReply* reply) override {
    auto worker_ex_lock = enter();
    if (!worker_ex_lock) {
      return Status::CANCELLED;
    }

    // Worker thread sets a notification for the main thread.
    // Because of worker exclusivity, we have exclusive rights to this data structure.
    cc_state_.consumerInputReady(request->args());

    // Worker-main critical section:
    // Worker thread waits for the buffered message response from the main thread. The main thread will set
    // consumer_ready_ when it is finished and released its exclusive lock on the communication data structure.
    std::unique_lock<std::mutex> lock(cc_state_.mutex);
    while (!cc_state_.consumer_has_output_)
      cc_state_.condition.wait(lock);

    if (!running_) {
      // main thread is killing the server
      reply->set_message("quit");
    } else {
      reply->set_message(cc_state_.str_buffer);
    }
    return Status::OK;
  }

  void kill() {
    running_ = false;
  }

  CCState& GetCCState() {
    return cc_state_;
  }

 private:
  /**
   * When a worker enters, it gains exclusive access to the main thread. That is, no other worker of this service
   * is allowed to interact with main thread.
   * @return A lock which grants the worker mutual exclusion.
   */
  std::unique_lock<std::mutex> enter() {
    std::unique_lock<std::mutex> lock(worker_exclusive_mtx_);
    if (!running_)
      lock.unlock();
    return std::move(lock);
  }

  std::mutex worker_exclusive_mtx_;
  bool running_;
  CCState cc_state_;
};


void RunServer() {
  std::string server_address("0.0.0.0:50051");
  CommandServiceImpl service;

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  CCState& server_cc_state = service.GetCCState();

  int cnt = 0;
  forever {
    { // critical section 1: wait for a command
      std::unique_lock<std::mutex> lock(server_cc_state.mutex);
      while (!server_cc_state.consumer_has_input_) {
        server_cc_state.condition.wait(lock);
        // calling wait releases the mutex, and re-gets it on the call return.
        // therefore, everything after acquiring the lock can be viewed as a critical section
      }
    }

    // Now, there is a command. We are now either quitting or performing some work.
    // The worker is now waiting on the condition, and because of worker exclusivity, we do not need to lock.
    if ("quit" == server_cc_state.str_buffer) {
      std::cout << "server main thread woke up and got 'quitting'" << std::endl;
      service.kill();
      server_cc_state.consumerOutputReady();
      server->Shutdown();
      break;
    } else {
      // echo
      std::cout << "server main thread woke up and got 'consume'" << std::endl;
      server_cc_state.str_buffer = server_cc_state.str_buffer + " " + std::to_string(cnt++);
      server_cc_state.consumerOutputReady();
    }
  }
  // a shutdown had been issued, wait for server to quit.
  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();
  std::cout << "server exited normally" << std::endl;
  return 0;
}