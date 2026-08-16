#include "memory_service.hpp"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <mutex>
#include <span>
#include <thread>
#include <utility>

#include "debug_probe.hpp"
#include "microdatawiggler.pb.h"

static constexpr uint32_t k_default_max_samples = 1'000'000;

MemoryServiceImpl::MemoryServiceImpl(DebugProbe &probe) : probe_(probe)
{
}

MemoryServiceImpl::~MemoryServiceImpl()
{
  recording_.store(false);
  if (recording_thread_.joinable())
  {
    recording_thread_.join();
  }
}

grpc::Status MemoryServiceImpl::Session(
    grpc::ServerContext * /*context*/,
    grpc::ServerReaderWriter<microdatawiggler::MemoryResponse, microdatawiggler::MemoryRequest> *stream)
{
  return do_session(stream);
}

// ---------------------------------------------------------------------------
// Probe helpers
// ---------------------------------------------------------------------------

microdatawiggler::MemoryResponse MemoryServiceImpl::execute_probe_request(
    const microdatawiggler::MemoryRequest &request)
{
  microdatawiggler::MemoryResponse response;
  if (request.has_read())
  {
    const auto &r = request.read();
    auto bytes = probe_.read_bytes(static_cast<uint32_t>(r.address()), r.length());
    response.mutable_read()->set_data(bytes.data(), bytes.size());
  }
  else if (request.has_write())
  {
    const auto &w = request.write();
    const auto &data = w.data();
    probe_.write_bytes(static_cast<uint32_t>(w.address()),
                       std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(data.data()), data.size()));
    response.mutable_write();
  }
  else
  {
    response.mutable_error()->set_message("Request must set read or write");
  }
  return response;
}

// Routes a read/write request through the command queue when recording is active
// (so the recording thread is the sole caller of the probe), or directly otherwise.
microdatawiggler::MemoryResponse MemoryServiceImpl::route_probe_request(const microdatawiggler::MemoryRequest &request)
{
  if (!recording_.load())
  {
    return execute_probe_request(request);
  }

  ProbeCommand cmd;
  cmd.request = request;
  auto future = cmd.promise.get_future();
  {
    const std::lock_guard<std::mutex> lk(command_mutex_);
    command_queue_.push(std::move(cmd));
  }
  return future.get();
}

// ---------------------------------------------------------------------------
// Recording thread
// ---------------------------------------------------------------------------

void MemoryServiceImpl::drain_command_queue()
{
  while (true)
  {
    std::unique_lock<std::mutex> lk(command_mutex_);
    if (command_queue_.empty())
    {
      break;
    }
    auto cmd = std::move(command_queue_.front());
    command_queue_.pop();
    lk.unlock();

    microdatawiggler::MemoryResponse response;
    try
    {
      response = execute_probe_request(cmd.request);
    }
    catch (const std::exception &e)
    {
      response.mutable_error()->set_message(e.what());
    }
    cmd.promise.set_value(std::move(response));
  }
}

void MemoryServiceImpl::recording_loop()
{
  using clock = std::chrono::steady_clock;
  const auto interval = std::chrono::microseconds(1'000'000 / static_cast<int64_t>(record_sample_rate_hz_));
  auto next_tick = clock::now();
  const auto start_time = next_tick;

  while (recording_.load())
  {
    if (!record_capped_)
    {
      microdatawiggler::RecordSample sample;
      sample.set_timestamp_us(static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - start_time).count()));

      try
      {
        for (const auto &var : record_vars_)
        {
          auto bytes = probe_.read_bytes(static_cast<uint32_t>(var.address()), var.length());
          sample.add_data(std::string(bytes.begin(), bytes.end()));
        }
        record_buffer_.push_back(std::move(sample));
      }
      catch (const std::exception &e)
      {
        fprintf(stderr, "Recording probe error: %s\n", e.what());
        record_capped_ = true;
      }

      if (record_buffer_.size() >= record_max_samples_)
      {
        record_capped_ = true;
      }
    }

    next_tick += interval;
    std::this_thread::sleep_until(next_tick);
    drain_command_queue();
  }

  drain_command_queue();
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

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
      if (request.has_read() || request.has_write())
      {
        response = route_probe_request(request);
      }
      else if (request.has_start_record())
      {
        if (recording_.load())
        {
          response.mutable_error()->set_message("Recording already active");
        }
        else
        {
          const auto &rec = request.start_record();
          record_vars_.assign(rec.variables().begin(), rec.variables().end());
          record_sample_rate_hz_ = rec.sample_rate_hz() > 0 ? rec.sample_rate_hz() : 1000;
          record_max_samples_ = rec.max_samples() > 0 ? rec.max_samples() : k_default_max_samples;
          record_buffer_.clear();
          record_capped_ = false;
          record_start_ = std::chrono::steady_clock::now();
          recording_.store(true);
          recording_thread_ = std::thread(&MemoryServiceImpl::recording_loop, this);
          response.mutable_write();
        }
      }
      else if (request.has_stop_record())
      {
        if (!recording_.load())
        {
          response.mutable_error()->set_message("No recording active");
        }
        else
        {
          recording_.store(false);
          if (recording_thread_.joinable())
          {
            recording_thread_.join();
          }
          drain_command_queue();
          response.mutable_write();
        }
      }
      else if (request.has_fetch_record())
      {
        if (recording_.load())
        {
          response.mutable_error()->set_message("Recording is still active; call stop_recording() first");
        }
        else
        {
          auto *rec_resp = response.mutable_record_data();
          for (const auto &sample : record_buffer_)
          {
            *rec_resp->add_samples() = sample;
          }
          rec_resp->set_capped(record_capped_);
        }
      }
      else
      {
        response.mutable_error()->set_message(
            "Request must set read, write, start_record, stop_record, or fetch_record");
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

  // Clean up any active recording if the client disconnects mid-session.
  if (recording_.load())
  {
    recording_.store(false);
    if (recording_thread_.joinable())
    {
      recording_thread_.join();
    }
  }

  fprintf(stderr, "Client disconnected\n");

  const std::lock_guard<std::mutex> lock(session_mutex_);
  session_active_ = false;
  return grpc::Status::OK;
}
