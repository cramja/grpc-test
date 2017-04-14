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

/**
 * The commands which with worker can issue to the main thread.
 */
enum class ProducerConsumerStatus {
  kQuit,
  kProduceConsume,
  kNone
};

class CCState {
 public:
  CCState() :
    consumer_has_input_(false),
    consumer_has_output_(false),
    current_command(ProducerConsumerStatus::kNone),
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
    current_command = ProducerConsumerStatus::kNone;
    condition.notify_all();
  }

  /**
   * Notifies the consumer that a piece of work has been created, and that the consumer should execute the command.
   * @param command The command type to be executed. Should not be kNone.
   */
  void consumerInputReady(ProducerConsumerStatus command) {
    consumer_has_input_ = true;
    consumer_has_output_ = false;
    current_command = command;
    condition.notify_one();
  }

  bool consumer_has_input_;
  bool consumer_has_output_;
  ProducerConsumerStatus current_command;
  std::string str_buffer;
  std::mutex mutex;
  std::condition_variable condition;
};

// Logic and data behind the server's behavior.
// The contained methods will be called from worker callback threads.
class EchoServiceImpl final : public SimpleServer::Service {
 public:
  EchoServiceImpl()
    : SimpleServer::Service(),
      running_(true),
      worker_exclusive_mtx_() {
  }

  Status Echo(ServerContext* context,
              const EchoRequest* request,
              EchoReply* reply) override {
    auto worker_ex_lock = enter();
    if (!worker_ex_lock) {
      return Status::CANCELLED;
    }

    // Worker thread sets a notification for the main thread.
    // Because of worker exclusivity, we have exclusive rights to this data structure.
    cc_state_.str_buffer = "";
    cc_state_.consumerInputReady(ProducerConsumerStatus::kProduceConsume);

    // Worker-main critical section:
    // Worker thread waits for the buffered message response from the main thread. The main thread will set
    // consumer_ready_ when it is finished and released its exclusive lock on the communication data structure.
    std::unique_lock<std::mutex> lock(cc_state_.mutex);
    while (!cc_state_.consumer_has_output_)
      cc_state_.condition.wait(lock);

    reply->set_echo_response(request->echo_message() + ": " + cc_state_.str_buffer);
    return Status::OK;
  }

  Status Command(ServerContext* context,
                 const CommandRequest* request,
                 CommandReply* reply) override {
    auto worker_ex_lock = enter();
    if (!worker_ex_lock) {
      return Status::CANCELLED;
    }

    std::string const kQuit = "quit";
    if (request->command() == kQuit) {
      DoQuit();
      reply->set_executed(true);
    } else {
      // unknown command. Do not notify main thread, just RPC return an error.
      reply->set_executed(false);
    }

    return Status::OK;
  }

  void DoQuit() {
    cc_state_.consumerInputReady(ProducerConsumerStatus::kQuit);

    // Critical section, wait for main thread to give us OKAY
    std::unique_lock<std::mutex> lock(cc_state_.mutex);
    while(!cc_state_.consumer_has_output_)
      cc_state_.condition.wait(lock);

    // Now alert all other incoming workers that they must not proceed
    running_ = false;
  }

  CCState& GetCCState() {
    return cc_state_;
  }

 private:
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
  EchoServiceImpl service;

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
    if (server_cc_state.current_command == ProducerConsumerStatus::kQuit) {
      std::cout << "server main thread woke up and got 'quitting'" << std::endl;
      server_cc_state.consumerOutputReady();
      server->Shutdown();
      break;
    } else if (server_cc_state.current_command == ProducerConsumerStatus::kProduceConsume) {
      std::cout << "server main thread woke up and got 'consume'" << std::endl;
      server_cc_state.str_buffer = std::to_string(cnt++);
      server_cc_state.consumerOutputReady();
    }
  }
  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();
  std::cout << "server exited normally" << std::endl;
  return 0;
}