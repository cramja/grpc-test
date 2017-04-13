#include <iostream>
#include <memory>
#include <string>

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
//
static WorkerCommand current_command = WorkerCommand::kNone;
std::string str_buffer;

static std::mutex mutex_1, mutex_2;
static std::condition_variable condition_1, condition_2;

// State and functions specific to worker-worker exclusivity
//
static std::condition_variable worker_exclusive_cv_;
static std::mutex worker_exclusive_mtx_;
static bool worker_has_exclusive_ = false;

static bool main_thread_exiting = false;

/**
 * Worker enters its command. This method gives it exclusivity from other workers.
 * ! Must be called with Exit();
 * @return A lock protecting its worker-worker exclusivity.
 */
std::unique_lock<decltype(worker_exclusive_mtx_)> Enter() {
  std::unique_lock<decltype(worker_exclusive_mtx_)> worker_lock(worker_exclusive_mtx_);
  while(worker_has_exclusive_)
    worker_exclusive_cv_.wait(worker_lock);
  worker_has_exclusive_ = true;
  return std::move(worker_lock);
}

void Exit() {
  worker_has_exclusive_ = false;
  worker_exclusive_cv_.notify_one();
}

// Logic and data behind the server's behavior.
// The contained methods will be called from worker callback threads.
class EchoServiceImpl final : public SimpleServer::Service {

  Status Echo(ServerContext* context,
              const EchoRequest* request,
              EchoReply* reply) override {
    auto worker_ex = Enter();

    { // worker-main critical section 1

      // worker thread sets a notification for the main thread
      std::unique_lock<decltype(mutex_1)> lock1(mutex_1);
      current_command = WorkerCommand::kConsume;
      str_buffer = "";
      condition_1.notify_one(); // notifies the main thread to wake up
    }

    // worker-main critical section 2
    //
    // worker thread waits for the buffered message response from the main thread
    // critical section 2 gives ownership of the buffer
    std::unique_lock<decltype(mutex_2)> lock2(mutex_2);
    while (current_command == WorkerCommand::kConsume) // this is the condition that will tell us that the main thread has finished working
      condition_2.wait(lock2);

    reply->set_echo_response(request->echo_message() + ": " + str_buffer);
    Exit();
    return Status::OK;
  }

  Status Command(ServerContext* context,
                 const CommandRequest* request,
                 CommandReply* reply) override {
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
    auto worker_ex = Enter();

    { // critical section 1
      std::unique_lock<decltype(mutex_1)> lock(mutex_1);
      current_command = WorkerCommand::kQuitting;
      condition_1.notify_one();
    }

    // critical section 2
    std::unique_lock<decltype(mutex_2)> lock(mutex_2);
    while(WorkerCommand::kQuitting == current_command)
      condition_2.wait(lock);

    // do cleanup, if any
    Exit();
  }

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

  int cnt = 0;
  forever {
    { // critical section 1: wait for a command
      std::unique_lock<decltype(mutex_1)> lock(mutex_1);
      while (current_command == WorkerCommand::kNone) {
        std::cout << "server main thread woke up" << std::endl;
        condition_1.wait(lock);
        // calling wait releases the mutex, and re-gets it on the call return.
        // therefore, everything after acquiring the lock can be viewed as a critical section
      }
    }

    // Now, there is a command. We are now either quitting or performing some work.
    // The worker is now waiting on the condition_2 so we get exclusivity on critical section 2 by locking 2
    std::unique_lock<decltype(mutex_2)> lock2(mutex_2);
    if (current_command == WorkerCommand::kQuitting) {
      std::cout << "server main thread woke up and got 'quitting'" << std::endl;
      current_command = WorkerCommand::kNone;
      condition_2.notify_all();
      lock2.unlock(); // release now otherwise we deadlock bc Shutdown waits for all RPC threads to finish
      server->Shutdown();
      break;
    } else if (current_command == WorkerCommand::kConsume) {
      std::cout << "server main thread woke up and got 'consume'" << std::endl;
      str_buffer = std::to_string(cnt++);
      current_command = WorkerCommand::kNone;
      condition_2.notify_one();
    }
  }
  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();
  std::cout << "server exited normally" << std::endl;
  return 0;
}