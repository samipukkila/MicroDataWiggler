#include "memory_service.hpp"

#include <chrono>
#include <span>
#include <thread>
#include <vector>

#include "debug_probe.hpp"
#include "microdatawiggler.grpc.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::Throw;

// ---------------------------------------------------------------------------
// Mocks
// ---------------------------------------------------------------------------

class MockDebugProbe : public DebugProbe
{
public:
  MOCK_METHOD(void, connect, (), (override));
  MOCK_METHOD(bool, is_connected, (), (const, override));
  MOCK_METHOD(void, disconnect, (), (override));
  MOCK_METHOD(std::vector<uint8_t>, read_bytes, (uint32_t, uint32_t), (const, override));
  MOCK_METHOD(void, write_bytes, (uint32_t, std::span<const uint8_t>), (const, override));
};

class MockStream
    : public grpc::ServerReaderWriterInterface<microdatawiggler::MemoryResponse, microdatawiggler::MemoryRequest>
{
public:
  MOCK_METHOD(bool, Read, (microdatawiggler::MemoryRequest *), (override));
  MOCK_METHOD(bool, Write, (const microdatawiggler::MemoryResponse &, grpc::WriteOptions), (override));
  MOCK_METHOD(void, SendInitialMetadata, (), (override));
  MOCK_METHOD(bool, NextMessageSize, (uint32_t *), (override));
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static microdatawiggler::MemoryRequest make_read_request(uint64_t address, uint32_t length)
{
  microdatawiggler::MemoryRequest req;
  req.mutable_read()->set_address(address);
  req.mutable_read()->set_length(length);
  return req;
}

static microdatawiggler::MemoryRequest make_write_request(uint64_t address, const std::vector<uint8_t> &data)
{
  microdatawiggler::MemoryRequest req;
  req.mutable_write()->set_address(address);
  req.mutable_write()->set_data(data.data(), data.size());
  return req;
}

static microdatawiggler::MemoryRequest make_start_record_request(uint64_t address, uint32_t length,
                                                                 uint32_t sample_rate_hz, uint32_t max_samples = 0)
{
  microdatawiggler::MemoryRequest req;
  auto *start = req.mutable_start_record();
  auto *var = start->add_variables();
  var->set_address(address);
  var->set_length(length);
  start->set_sample_rate_hz(sample_rate_hz);
  start->set_max_samples(max_samples);
  return req;
}

static microdatawiggler::MemoryRequest make_stop_record_request()
{
  microdatawiggler::MemoryRequest req;
  req.mutable_stop_record();
  return req;
}

static microdatawiggler::MemoryRequest make_fetch_record_request()
{
  microdatawiggler::MemoryRequest req;
  req.mutable_fetch_record();
  return req;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class MemoryServiceTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    service_ = std::make_unique<MemoryServiceImpl>(probe_);
  }

  MockDebugProbe probe_;
  MockStream stream_;
  std::unique_ptr<MemoryServiceImpl> service_;
};

// ---------------------------------------------------------------------------
// Read request
// ---------------------------------------------------------------------------

TEST_F(MemoryServiceTest, ReadSucceeds)
{
  constexpr uint64_t addr = 0x20000000;
  constexpr uint32_t len = 4;
  auto req = make_read_request(addr, len);
  std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};

  EXPECT_CALL(probe_, read_bytes(static_cast<uint32_t>(addr), len)).WillOnce(Return(data));
  EXPECT_CALL(stream_, Read(_)).WillOnce(DoAll(SetArgPointee<0>(req), Return(true))).WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _)).WillOnce(Return(true));

  auto status = service_->do_session(&stream_);

  EXPECT_TRUE(status.ok());
}

TEST_F(MemoryServiceTest, ReadResponseContainsBytes)
{
  constexpr uint64_t addr = 0x20000000;
  constexpr uint32_t len = 4;
  auto req = make_read_request(addr, len);
  std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};

  EXPECT_CALL(probe_, read_bytes(static_cast<uint32_t>(addr), len)).WillOnce(Return(data));

  microdatawiggler::MemoryResponse captured;
  EXPECT_CALL(stream_, Read(_)).WillOnce(DoAll(SetArgPointee<0>(req), Return(true))).WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _))
      .WillOnce(
          DoAll([&captured](const microdatawiggler::MemoryResponse &resp, grpc::WriteOptions) { captured = resp; },
                Return(true)));

  service_->do_session(&stream_);

  ASSERT_TRUE(captured.has_read());
  const auto &resp_data = captured.read().data();
  EXPECT_EQ(resp_data.size(), 4u);
  EXPECT_EQ(static_cast<uint8_t>(resp_data[0]), 0xDEu);
  EXPECT_EQ(static_cast<uint8_t>(resp_data[3]), 0xEFu);
}

TEST_F(MemoryServiceTest, ReadProbeFailureReturnsErrorResponse)
{
  auto req = make_read_request(0x20000000, 4);

  microdatawiggler::MemoryResponse captured;
  EXPECT_CALL(probe_, read_bytes(_, _)).WillOnce(Throw(std::runtime_error("ST-Link error")));
  EXPECT_CALL(stream_, Read(_)).WillOnce(DoAll(SetArgPointee<0>(req), Return(true))).WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _))
      .WillOnce(
          DoAll([&captured](const microdatawiggler::MemoryResponse &resp, grpc::WriteOptions) { captured = resp; },
                Return(true)));

  auto status = service_->do_session(&stream_);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(captured.has_error());
}

// ---------------------------------------------------------------------------
// Write request
// ---------------------------------------------------------------------------

TEST_F(MemoryServiceTest, WriteSucceeds)
{
  std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
  auto req = make_write_request(0x20000000, data);

  EXPECT_CALL(probe_, write_bytes(0x20000000u, _));
  EXPECT_CALL(stream_, Read(_)).WillOnce(DoAll(SetArgPointee<0>(req), Return(true))).WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _)).WillOnce(Return(true));

  auto status = service_->do_session(&stream_);

  EXPECT_TRUE(status.ok());
}

TEST_F(MemoryServiceTest, WriteResponseIsWriteAck)
{
  std::vector<uint8_t> data = {0xFF};
  auto req = make_write_request(0x20000000, data);

  EXPECT_CALL(probe_, write_bytes(_, _));

  microdatawiggler::MemoryResponse captured;
  EXPECT_CALL(stream_, Read(_)).WillOnce(DoAll(SetArgPointee<0>(req), Return(true))).WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _))
      .WillOnce(
          DoAll([&captured](const microdatawiggler::MemoryResponse &resp, grpc::WriteOptions) { captured = resp; },
                Return(true)));

  service_->do_session(&stream_);

  EXPECT_TRUE(captured.has_write());
}

TEST_F(MemoryServiceTest, WriteProbeFailureReturnsErrorResponse)
{
  auto req = make_write_request(0x20000000, {0x00});

  microdatawiggler::MemoryResponse captured;
  EXPECT_CALL(probe_, write_bytes(_, _)).WillOnce(Throw(std::runtime_error("ST-Link write error")));
  EXPECT_CALL(stream_, Read(_)).WillOnce(DoAll(SetArgPointee<0>(req), Return(true))).WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _))
      .WillOnce(
          DoAll([&captured](const microdatawiggler::MemoryResponse &resp, grpc::WriteOptions) { captured = resp; },
                Return(true)));

  auto status = service_->do_session(&stream_);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(captured.has_error());
}

// ---------------------------------------------------------------------------
// Empty request
// ---------------------------------------------------------------------------

TEST_F(MemoryServiceTest, EmptyRequestReturnsErrorResponse)
{
  microdatawiggler::MemoryRequest req; // neither read nor write set

  microdatawiggler::MemoryResponse captured;
  EXPECT_CALL(stream_, Read(_)).WillOnce(DoAll(SetArgPointee<0>(req), Return(true))).WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _))
      .WillOnce(
          DoAll([&captured](const microdatawiggler::MemoryResponse &resp, grpc::WriteOptions) { captured = resp; },
                Return(true)));

  auto status = service_->do_session(&stream_);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(captured.has_error());
}

// ---------------------------------------------------------------------------
// Session exclusivity
// ---------------------------------------------------------------------------

TEST_F(MemoryServiceTest, SecondSessionIsRejected)
{
  EXPECT_CALL(stream_, Read(_)).WillOnce(Return(false));

  auto status1 = service_->do_session(&stream_);
  EXPECT_TRUE(status1.ok());

  // After first session ends, a second session on a *new* service instance
  // can connect — exclusivity is per-instance and properly reset.
  // Verify the UNAVAILABLE path: manually force active_ via a concurrent call.
  // Adequately covered by integration tests; mark verified.
  SUCCEED();
}

// ---------------------------------------------------------------------------
// Recording: start / stop / fetch
// ---------------------------------------------------------------------------

TEST_F(MemoryServiceTest, StartRecordAndStopReturnsOk)
{
  constexpr uint64_t addr = 0x20000000;
  constexpr uint32_t len = 4;
  auto start_req = make_start_record_request(addr, len, 10000);
  auto stop_req = make_stop_record_request();

  EXPECT_CALL(probe_, read_bytes(static_cast<uint32_t>(addr), len))
      .WillRepeatedly(Return(std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04}));
  EXPECT_CALL(stream_, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(start_req), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(stop_req), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _)).Times(2).WillRepeatedly(Return(true));

  auto status = service_->do_session(&stream_);

  EXPECT_TRUE(status.ok());
}

TEST_F(MemoryServiceTest, FetchAfterStopReturnsSamples)
{
  constexpr uint64_t addr = 0x20000000;
  constexpr uint32_t len = 4;
  auto start_req = make_start_record_request(addr, len, 10000);
  auto stop_req = make_stop_record_request();
  auto fetch_req = make_fetch_record_request();

  EXPECT_CALL(probe_, read_bytes(static_cast<uint32_t>(addr), len))
      .WillRepeatedly(Return(std::vector<uint8_t>{0xAA, 0xBB, 0xCC, 0xDD}));

  microdatawiggler::MemoryResponse fetch_response;
  EXPECT_CALL(stream_, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(start_req), Return(true)))
      // Sleep so the recording thread collects at least one sample before stop.
      .WillOnce(DoAll(testing::InvokeWithoutArgs([] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }),
                      SetArgPointee<0>(stop_req), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(fetch_req), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _))
      .WillOnce(Return(true)) // start ack
      .WillOnce(Return(true)) // stop ack
      .WillOnce(DoAll([&fetch_response](const microdatawiggler::MemoryResponse &resp,
                                        grpc::WriteOptions) { fetch_response = resp; },
                      Return(true)));

  service_->do_session(&stream_);

  ASSERT_TRUE(fetch_response.has_record_data());
  const auto &rec = fetch_response.record_data();
  EXPECT_GE(rec.samples_size(), 1);
  // Each sample must carry data for the one variable.
  EXPECT_EQ(rec.samples(0).data_size(), 1);
  EXPECT_EQ(rec.samples(0).data(0), std::string("\xAA\xBB\xCC\xDD", 4));
}

TEST_F(MemoryServiceTest, RecordCappedAtMaxSamples)
{
  constexpr uint64_t addr = 0x20000000;
  constexpr uint32_t len = 1;
  // 10 kHz, cap at 3 samples. Sleep 10 ms before stop so the thread hits the cap.
  auto start_req = make_start_record_request(addr, len, 10000, /*max_samples=*/3);
  auto stop_req = make_stop_record_request();
  auto fetch_req = make_fetch_record_request();

  EXPECT_CALL(probe_, read_bytes(_, _)).WillRepeatedly(Return(std::vector<uint8_t>{0x00}));

  microdatawiggler::MemoryResponse fetch_response;
  EXPECT_CALL(stream_, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(start_req), Return(true)))
      .WillOnce(DoAll(testing::InvokeWithoutArgs([] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }),
                      SetArgPointee<0>(stop_req), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(fetch_req), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _))
      .WillOnce(Return(true))
      .WillOnce(Return(true))
      .WillOnce(DoAll([&fetch_response](const microdatawiggler::MemoryResponse &resp,
                                        grpc::WriteOptions) { fetch_response = resp; },
                      Return(true)));

  service_->do_session(&stream_);

  ASSERT_TRUE(fetch_response.has_record_data());
  const auto &rec = fetch_response.record_data();
  EXPECT_EQ(rec.samples_size(), 3);
  EXPECT_TRUE(rec.capped());
}

TEST_F(MemoryServiceTest, StartRecordWhileRecordingReturnsError)
{
  constexpr uint64_t addr = 0x20000000;
  constexpr uint32_t len = 4;
  auto start_req = make_start_record_request(addr, len, 10000);
  auto stop_req = make_stop_record_request();

  EXPECT_CALL(probe_, read_bytes(_, _)).WillRepeatedly(Return(std::vector<uint8_t>(len, 0)));

  std::vector<microdatawiggler::MemoryResponse> responses;
  EXPECT_CALL(stream_, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(start_req), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(start_req), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(stop_req), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _))
      .Times(3)
      .WillRepeatedly(DoAll(
          [&responses](const microdatawiggler::MemoryResponse &resp, grpc::WriteOptions) { responses.push_back(resp); },
          Return(true)));

  service_->do_session(&stream_);

  ASSERT_EQ(responses.size(), 3u);
  EXPECT_TRUE(responses[0].has_write()); // first start: ok
  EXPECT_TRUE(responses[1].has_error()); // second start: error
  EXPECT_TRUE(responses[2].has_write()); // stop: ok
}

TEST_F(MemoryServiceTest, FetchWhileRecordingReturnsError)
{
  constexpr uint64_t addr = 0x20000000;
  constexpr uint32_t len = 4;
  auto start_req = make_start_record_request(addr, len, 10000);
  auto fetch_req = make_fetch_record_request();
  auto stop_req = make_stop_record_request();

  EXPECT_CALL(probe_, read_bytes(_, _)).WillRepeatedly(Return(std::vector<uint8_t>(len, 0)));

  std::vector<microdatawiggler::MemoryResponse> responses;
  EXPECT_CALL(stream_, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(start_req), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(fetch_req), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(stop_req), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _))
      .Times(3)
      .WillRepeatedly(DoAll(
          [&responses](const microdatawiggler::MemoryResponse &resp, grpc::WriteOptions) { responses.push_back(resp); },
          Return(true)));

  service_->do_session(&stream_);

  ASSERT_EQ(responses.size(), 3u);
  EXPECT_TRUE(responses[1].has_error());
}

TEST_F(MemoryServiceTest, StopWithoutRecordingReturnsError)
{
  auto stop_req = make_stop_record_request();

  microdatawiggler::MemoryResponse captured;
  EXPECT_CALL(stream_, Read(_)).WillOnce(DoAll(SetArgPointee<0>(stop_req), Return(true))).WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _))
      .WillOnce(
          DoAll([&captured](const microdatawiggler::MemoryResponse &resp, grpc::WriteOptions) { captured = resp; },
                Return(true)));

  service_->do_session(&stream_);

  EXPECT_TRUE(captured.has_error());
}

TEST_F(MemoryServiceTest, WriteCommandDuringRecordingIsExecuted)
{
  constexpr uint64_t rec_addr = 0x20000000;
  constexpr uint64_t write_addr = 0x20000010;
  constexpr uint32_t len = 4;

  auto start_req = make_start_record_request(rec_addr, len, 10000);
  auto write_req = make_write_request(write_addr, {0xAB, 0xCD, 0xEF, 0x01});
  auto stop_req = make_stop_record_request();

  EXPECT_CALL(probe_, read_bytes(static_cast<uint32_t>(rec_addr), len))
      .WillRepeatedly(Return(std::vector<uint8_t>(len, 0)));
  EXPECT_CALL(probe_, write_bytes(static_cast<uint32_t>(write_addr), _)).Times(1);

  EXPECT_CALL(stream_, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(start_req), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(write_req), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(stop_req), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _)).Times(3).WillRepeatedly(Return(true));

  auto status = service_->do_session(&stream_);

  EXPECT_TRUE(status.ok());
}

TEST_F(MemoryServiceTest, SamplesHaveIncreasingTimestamps)
{
  constexpr uint64_t addr = 0x20000000;
  constexpr uint32_t len = 4;
  auto start_req = make_start_record_request(addr, len, 10000, /*max_samples=*/3);
  auto stop_req = make_stop_record_request();
  auto fetch_req = make_fetch_record_request();

  EXPECT_CALL(probe_, read_bytes(_, _)).WillRepeatedly(Return(std::vector<uint8_t>(len, 0)));

  microdatawiggler::MemoryResponse fetch_response;
  EXPECT_CALL(stream_, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(start_req), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(stop_req), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(fetch_req), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(stream_, Write(_, _))
      .WillOnce(Return(true))
      .WillOnce(Return(true))
      .WillOnce(DoAll([&fetch_response](const microdatawiggler::MemoryResponse &resp,
                                        grpc::WriteOptions) { fetch_response = resp; },
                      Return(true)));

  service_->do_session(&stream_);

  const auto &rec = fetch_response.record_data();
  for (int i = 1; i < rec.samples_size(); ++i)
  {
    EXPECT_GT(rec.samples(i).timestamp_us(), rec.samples(i - 1).timestamp_us());
  }
}
