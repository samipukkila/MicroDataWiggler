#pragma once

#include <grpcpp/grpcpp.h>

#include <mutex>

#include "debug_probe.hpp"
#include "microdatawiggler.grpc.pb.h"

class MemoryServiceImpl final : public microdatawiggler::MemoryService::Service
{
public:
  explicit MemoryServiceImpl(DebugProbe &probe);

  grpc::Status Session(
      grpc::ServerContext *context,
      grpc::ServerReaderWriter<microdatawiggler::MemoryResponse, microdatawiggler::MemoryRequest> *stream) override;

  grpc::Status do_session(
      grpc::ServerReaderWriterInterface<microdatawiggler::MemoryResponse, microdatawiggler::MemoryRequest> *stream);

private:
  DebugProbe &probe_;
  std::mutex session_mutex_;
  bool session_active_ = false;
};
