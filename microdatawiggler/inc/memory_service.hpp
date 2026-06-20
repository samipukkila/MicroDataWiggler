#pragma once

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "debug_probe.hpp"
#include "microdatawiggler.grpc.pb.h"

class MemoryServiceImpl final : public microdatawiggler::MemoryService::Service
{
public:
  explicit MemoryServiceImpl(DebugProbe &probe);
  ~MemoryServiceImpl() override;

  grpc::Status Session(
      grpc::ServerContext *context,
      grpc::ServerReaderWriter<microdatawiggler::MemoryResponse, microdatawiggler::MemoryRequest> *stream) override;

  grpc::Status do_session(
      grpc::ServerReaderWriterInterface<microdatawiggler::MemoryResponse, microdatawiggler::MemoryRequest> *stream);

private:
  struct ProbeCommand
  {
    microdatawiggler::MemoryRequest request;
    std::promise<microdatawiggler::MemoryResponse> promise;
  };

  DebugProbe &probe_;
  std::mutex session_mutex_;
  bool session_active_ = false;

  // Recording state — written before thread start or after join; safe without extra locking.
  std::atomic<bool> recording_{false};
  bool record_capped_ = false;
  uint32_t record_sample_rate_hz_ = 0;
  uint32_t record_max_samples_ = 0;
  std::vector<microdatawiggler::VariableSpec> record_vars_;
  std::vector<microdatawiggler::RecordSample> record_buffer_;
  std::chrono::steady_clock::time_point record_start_;
  std::thread recording_thread_;

  // Command queue: session thread enqueues probe requests while recording is active;
  // recording thread drains them between samples to avoid concurrent probe access.
  std::queue<ProbeCommand> command_queue_;
  std::mutex command_mutex_;

  void recording_loop();
  void drain_command_queue();
  microdatawiggler::MemoryResponse execute_probe_request(const microdatawiggler::MemoryRequest &request);
  microdatawiggler::MemoryResponse route_probe_request(const microdatawiggler::MemoryRequest &request);
};
