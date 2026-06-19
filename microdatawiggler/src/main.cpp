#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <string>
#include <thread>

#include "memory_service.hpp"
#include "stlink_probe.hpp"

namespace
{

std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int /*signum*/)
{
  g_shutdown_requested.store(true);
}

} // namespace

int main(int argc, char *argv[])
{
  std::string port = "50051";

  for (int i = 1; i < argc; ++i)
  {
    if (std::string(argv[i]) == "--port")
    {
      if (i + 1 >= argc)
      {
        fprintf(stderr, "Error: --port requires a value\n");
        return 1;
      }
      port = argv[++i];
    }
  }

  try
  {
    fprintf(stderr, "Connecting to ST-Link probe...\n");
    StlinkProbe probe;
    probe.connect();

    const std::string server_address = "0.0.0.0:" + port;
    MemoryServiceImpl service(probe);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    auto server = builder.BuildAndStart();
    if (!server)
    {
      fprintf(stderr, "Failed to start gRPC server\n");
      return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::thread shutdown_thread([&server]() {
      while (!g_shutdown_requested.load())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      server->Shutdown();
    });

    fprintf(stderr, "Server listening on %s\n", server_address.c_str());
    server->Wait();

    shutdown_thread.join();
    fprintf(stderr, "Shutting down...\n");
    probe.disconnect();

    return 0;
  }
  catch (const std::exception &e)
  {
    fprintf(stderr, "Error: %s\n", e.what());
    return 1;
  }
}
