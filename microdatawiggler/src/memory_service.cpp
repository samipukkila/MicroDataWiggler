#include "memory_service.hpp"

#include <grpcpp/grpcpp.h>

#include <cstdio>
#include <exception>
#include <mutex>
#include <span>

#include "debug_probe.hpp"
#include "microdatawiggler.pb.h"

MemoryServiceImpl::MemoryServiceImpl(DebugProbe &probe) : probe_(probe)
{
}

grpc::Status MemoryServiceImpl::Session(
    grpc::ServerContext * /*context*/,
    grpc::ServerReaderWriter<microdatawiggler::MemoryResponse, microdatawiggler::MemoryRequest> *stream)
{
  return do_session(stream);
}

grpc::Status MemoryServiceImpl::do_session(
    grpc::ServerReaderWriterInterface<microdatawiggler::MemoryResponse, microdatawiggler::MemoryRequest> *stream)
{
  {
    const std::lock_guard<std::mutex> lock(session_mutex_);
    if (session_active_)
    {
      return {grpc::StatusCode::UNAVAILABLE, "Another client is already connected"};
    }
    session_active_ = true;
  }

  fprintf(stderr, "Client connected\n");

  microdatawiggler::MemoryRequest request;
  while (stream->Read(&request))
  {
    microdatawiggler::MemoryResponse response;

    try
    {
      if (request.has_read())
      {
        const auto &read_req = request.read();
        auto bytes = probe_.read_bytes(static_cast<uint32_t>(read_req.address()), read_req.length());
        response.mutable_read()->set_data(bytes.data(), bytes.size());
      }
      else if (request.has_write())
      {
        const auto &write_req = request.write();
        const auto &data = write_req.data();
        probe_.write_bytes(static_cast<uint32_t>(write_req.address()),
                           std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(data.data()), data.size()));
        response.mutable_write();
      }
      else
      {
        response.mutable_error()->set_message("Request must set read or write");
      }
    }
    catch (const std::exception &e)
    {
      fprintf(stderr, "Error processing request: %s\n", e.what());
      response.mutable_error()->set_message(e.what());
    }

    if (!stream->Write(response))
    {
      break;
    }
  }

  fprintf(stderr, "Client disconnected\n");

  const std::lock_guard<std::mutex> lock(session_mutex_);
  session_active_ = false;
  return grpc::Status::OK;
}
