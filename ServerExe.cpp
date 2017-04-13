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
enum class WorkerCommand {
  kQuitting,
  kConsume,
  kNone
};

// State specific to worker-main thread exclusivity
struct CCState {
  CCState() :
    current_command(WorkerCommand::kNone),
    str_buffer(""),
    mutex_1(),
    mutex_2(),
    condition_1(),
    condition_2() {}

  WorkerCommand current_command;
  std::string str_buffer;
  std::mutex mutex_1, mutex_2;
  std::condition_variable condition_1, condition_2;
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

    { // worker-main critical section 1
      // worker thread sets a notification for the main thread
      std::unique_lock<std::mutex> lock1(cc_state_.mutex_1);
      cc_state_.current_command = WorkerCommand::kConsume;
      cc_state_.str_buffer = "";
      cc_state_.condition_1.notify_one(); // notifies the main thread to wake up
    }

    // worker-main critical section 2
    //
    // worker thread waits for the buffered message response from the main thread
    // critical section 2 gives ownership of the buffer
    std::unique_lock<std::mutex> lock2(cc_state_.mutex_2);
    while (cc_state_.current_command == WorkerCommand::kConsume) // this is the condition that will tell us that the main thread has finished working
      cc_state_.condition_2.wait(lock2);

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
    { // critical section 1
      std::unique_lock<std::mutex> lock(cc_state_.mutex_1);
      cc_state_.current_command = WorkerCommand::kQuitting;
      cc_state_.condition_1.notify_one();
    }

    // critical section 2, wait for main thread to give us the OKAY
    std::unique_lock<std::mutex> lock(cc_state_.mutex_2);
    while(WorkerCommand::kQuitting == cc_state_.current_command)
      cc_state_.condition_2.wait(lock);

    // now alert all other incoming workers that they must not proceed
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
      std::unique_lock<std::mutex> lock(server_cc_state.mutex_1);
      while (server_cc_state.current_command == WorkerCommand::kNone) {
        server_cc_state.condition_1.wait(lock);
        // calling wait releases the mutex, and re-gets it on the call return.
        // therefore, everything after acquiring the lock can be viewed as a critical section
      }
    }

    // Now, there is a command. We are now either quitting or performing some work.
    // The worker is now waiting on the condition_2 so we get exclusivity on critical section 2 by locking 2
    std::unique_lock<std::mutex> lock2(server_cc_state.mutex_2);
    if (server_cc_state.current_command == WorkerCommand::kQuitting) {
      std::cout << "server main thread woke up and got 'quitting'" << std::endl;
      server_cc_state.current_command = WorkerCommand::kNone;
      server_cc_state.condition_2.notify_all();
      lock2.unlock(); // release now otherwise we deadlock bc Shutdown waits for all RPC threads to finish
      server->Shutdown();
      break;
    } else if (server_cc_state.current_command == WorkerCommand::kConsume) {
      std::cout << "server main thread woke up and got 'consume'" << std::endl;
      server_cc_state.str_buffer = std::to_string(cnt++);
      server_cc_state.current_command = WorkerCommand::kNone;
      server_cc_state.condition_2.notify_one();
    }
  }
  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();
  std::cout << "server exited normally" << std::endl;
  return 0;
}