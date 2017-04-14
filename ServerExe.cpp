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

class RequestState {
 public:
  RequestState() :
    request_ready_(false),
    response_ready_(false),
    buffer_(""),
    mutex_(),
    condition_() {}

  /**
   * Notifies waiter that a piece of work has been created and added to the buffer.
   * @param to_consume The arguments for the consuming thread.
   */
  void requestReady(std::string to_consume) {
    request_ready_ = true;
    response_ready_ = false;
    buffer_ = to_consume;
    condition_.notify_one();
  }

  /**
   * Notifies that the consumer has finished consuming and that a response is ready.
   * To be called after the consumer has executed.
   */
  void responseReady() {
    request_ready_ = false;
    response_ready_ = true;
    condition_.notify_one();
  }

  bool request_ready_;
  bool response_ready_;
  std::string buffer_;
  std::mutex mutex_;
  std::condition_variable condition_;
};

// Logic and data behind the server's behavior.
// The contained methods will be called from worker callback threads.
class CommandServiceImpl final : public SimpleServer::Service {
 public:
  CommandServiceImpl()
    : SimpleServer::Service(),
      running_(false),
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
    request_state_.requestReady(request->args());

    // Worker-main critical section:
    // Worker thread waits for the buffered message response from the main thread. The main thread will set
    // consumer_ready_ when it is finished and released its exclusive lock on the communication data structure.
    std::unique_lock<std::mutex> lock(request_state_.mutex_);
    while (!request_state_.response_ready_)
      request_state_.condition_.wait(lock);

    if (!running_) {
      // main thread is killing the server
      reply->set_message("quit");
    } else {
      reply->set_message(request_state_.buffer_);
    }
    return Status::OK;
  }

  void ready() {
    running_ = true;
  }

  void kill() {
    running_ = false;
  }

  RequestState& getRequestResponseState() {
    return request_state_;
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
  RequestState request_state_;
};


void RunServer() {
  std::string server_address("0.0.0.0:50051");
  CommandServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  RequestState& request_state = service.getRequestResponseState();
  int count = 0;
  forever {
    { // critical section 1: wait for a command
      std::unique_lock<std::mutex> lock(request_state.mutex_);
      // we are now ready to handle requests, allow workers to produce.
      service.ready();
      while (!request_state.request_ready_) {
        request_state.condition_.wait(lock);
        // calling wait releases the mutex, and re-gets it on the call return.
        // therefore, everything after acquiring the lock can be viewed as a critical section
      }
    }

    // Now, there is a command. We are now either quitting or performing some work.
    // The worker is now waiting on the condition, and because of worker exclusivity, we do not need to lock.
    if ("quit" == request_state.buffer_) {
      std::cout << "server main thread woke up and got 'quitting'" << std::endl;
      service.kill();
      request_state.responseReady();
      server->Shutdown();
      break;
    } else {
      // echo
      std::cout << "server main thread woke up and got 'consume'" << std::endl;
      request_state.buffer_ = request_state.buffer_ + " " + std::to_string(count++);
      request_state.responseReady();
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