#include "stlink_probe.hpp"

#include <fff.h>
#include <stlink.h>

#include <cstring>
#include <stdexcept>

#include "gtest/gtest.h"

DEFINE_FFF_GLOBALS;

class StlinkProbeTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    RESET_FAKE(stlink_open_usb);
    RESET_FAKE(stlink_enter_swd_mode);
    RESET_FAKE(stlink_exit_debug_mode);
    RESET_FAKE(stlink_read_mem32);
    RESET_FAKE(stlink_write_mem32);
    RESET_FAKE(stlink_close);
    FFF_RESET_HISTORY();

    memset(&fake_sl, 0, sizeof(fake_sl));
    fake_sl.chip_id = 0x0410;
  }

  void GivenConnectSuccess()
  {
    stlink_open_usb_fake.return_val = &fake_sl;
    stlink_enter_swd_mode_fake.return_val = 0;
  }

  StlinkProbe reader;
  stlink_t fake_sl;
};

// --- connect() ---

TEST_F(StlinkProbeTest, ConnectSucceeds)
{
  GivenConnectSuccess();

  EXPECT_NO_THROW(reader.connect());
  EXPECT_EQ(stlink_open_usb_fake.call_count, 1u);
  EXPECT_EQ(stlink_enter_swd_mode_fake.call_count, 1u);
}

TEST_F(StlinkProbeTest, ConnectUsesDefaultFrequency)
{
  GivenConnectSuccess();

  reader.connect();

  EXPECT_EQ(stlink_open_usb_fake.arg3_val, 4000);
}

TEST_F(StlinkProbeTest, ConnectUsesCustomFrequency)
{
  StlinkProbe custom_reader(StlinkProbeFrequency::freq_100khz);
  GivenConnectSuccess();

  custom_reader.connect();

  EXPECT_EQ(stlink_open_usb_fake.arg3_val, 100);
}

TEST_F(StlinkProbeTest, ConnectFailsWhenUsbOpenReturnsNull)
{
  stlink_open_usb_fake.return_val = nullptr;

  EXPECT_THROW(reader.connect(), std::runtime_error);
  EXPECT_EQ(stlink_enter_swd_mode_fake.call_count, 0u);
}

TEST_F(StlinkProbeTest, ConnectFailsWhenSwdModeFailsAndClosesDevice)
{
  stlink_open_usb_fake.return_val = &fake_sl;
  stlink_enter_swd_mode_fake.return_val = -1;

  EXPECT_THROW(reader.connect(), std::runtime_error);
  EXPECT_EQ(stlink_close_fake.call_count, 1u);
  EXPECT_EQ(stlink_close_fake.arg0_val, &fake_sl);
}

// --- disconnect() ---

TEST_F(StlinkProbeTest, DisconnectAfterConnect)
{
  GivenConnectSuccess();
  reader.connect();

  reader.disconnect();

  EXPECT_EQ(stlink_exit_debug_mode_fake.call_count, 1u);
  EXPECT_EQ(stlink_close_fake.call_count, 1u);
}

TEST_F(StlinkProbeTest, DisconnectWithoutConnectIsNoop)
{
  reader.disconnect();

  EXPECT_EQ(stlink_exit_debug_mode_fake.call_count, 0u);
  EXPECT_EQ(stlink_close_fake.call_count, 0u);
}

TEST_F(StlinkProbeTest, DoubleDisconnectIsNoop)
{
  GivenConnectSuccess();
  reader.connect();

  reader.disconnect();
  reader.disconnect();

  EXPECT_EQ(stlink_exit_debug_mode_fake.call_count, 1u);
  EXPECT_EQ(stlink_close_fake.call_count, 1u);
}

// --- destructor ---

TEST_F(StlinkProbeTest, DestructorCallsDisconnect)
{
  {
    StlinkProbe scoped_reader;
    GivenConnectSuccess();
    scoped_reader.connect();
  }
  EXPECT_EQ(stlink_exit_debug_mode_fake.call_count, 1u);
  EXPECT_EQ(stlink_close_fake.call_count, 1u);
}

// --- read_bytes() ---

TEST_F(StlinkProbeTest, ReadBytesAligned)
{
  GivenConnectSuccess();
  reader.connect();

  // Populate q_buf with known data when stlink_read_mem32 is called
  uint8_t expected[] = {0xAA, 0xBB, 0xCC, 0xDD};
  memcpy(fake_sl.q_buf, expected, sizeof(expected));
  stlink_read_mem32_fake.return_val = 0;

  auto out = reader.read_bytes(0x20000000, 4);
  EXPECT_EQ(memcmp(out.data(), expected, 4), 0);

  EXPECT_EQ(stlink_read_mem32_fake.arg1_val, 0x20000000u);
  EXPECT_EQ(stlink_read_mem32_fake.arg2_val, 4u);
}

TEST_F(StlinkProbeTest, ReadBytesUnalignedAddress)
{
  GivenConnectSuccess();
  reader.connect();

  // addr=0x20000001, size=2 => aligned_addr=0x20000000, offset=1,
  // aligned_size=4
  uint8_t buf_data[4] = {0x11, 0x22, 0x33, 0x44};
  memcpy(fake_sl.q_buf, buf_data, sizeof(buf_data));
  stlink_read_mem32_fake.return_val = 0;

  auto out = reader.read_bytes(0x20000001, 2);

  // Should read bytes at offset 1: 0x22, 0x33
  EXPECT_EQ(out[0], 0x22);
  EXPECT_EQ(out[1], 0x33);

  EXPECT_EQ(stlink_read_mem32_fake.arg1_val, 0x20000000u);
  EXPECT_EQ(stlink_read_mem32_fake.arg2_val, 4u);
}

TEST_F(StlinkProbeTest, ReadBytesUnalignedSpansTwoWords)
{
  GivenConnectSuccess();
  reader.connect();

  // addr=0x20000003, size=3 => aligned_addr=0x20000000, offset=3,
  // aligned_size=8
  uint8_t buf_data[8] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
  memcpy(fake_sl.q_buf, buf_data, sizeof(buf_data));
  stlink_read_mem32_fake.return_val = 0;

  auto out = reader.read_bytes(0x20000003, 3);

  EXPECT_EQ(out[0], 0x40);
  EXPECT_EQ(out[1], 0x50);
  EXPECT_EQ(out[2], 0x60);
  EXPECT_EQ(stlink_read_mem32_fake.arg2_val, 8u);
}

TEST_F(StlinkProbeTest, ReadBytesFailsOnStlinkError)
{
  GivenConnectSuccess();
  reader.connect();

  stlink_read_mem32_fake.return_val = -1;

  EXPECT_THROW(reader.read_bytes(0x20000000, 4), std::runtime_error);
}

// --- write_bytes() ---

TEST_F(StlinkProbeTest, WriteBytesAligned)
{
  GivenConnectSuccess();
  reader.connect();

  stlink_read_mem32_fake.return_val = 0;
  stlink_write_mem32_fake.return_val = 0;

  uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
  EXPECT_NO_THROW(reader.write_bytes(0x20000000, data));

  EXPECT_EQ(stlink_read_mem32_fake.call_count, 1u);
  EXPECT_EQ(stlink_write_mem32_fake.call_count, 1u);
  EXPECT_EQ(stlink_write_mem32_fake.arg1_val, 0x20000000u);
  EXPECT_EQ(stlink_write_mem32_fake.arg2_val, 4u);

  // Verify data was copied into q_buf
  EXPECT_EQ(memcmp(fake_sl.q_buf, data, 4), 0);
}

TEST_F(StlinkProbeTest, WriteBytesUnalignedPatchesCorrectOffset)
{
  GivenConnectSuccess();
  reader.connect();

  // Pre-fill q_buf to simulate existing memory content after read
  uint8_t existing[4] = {0x11, 0x22, 0x33, 0x44};
  memcpy(fake_sl.q_buf, existing, sizeof(existing));
  stlink_read_mem32_fake.return_val = 0;
  stlink_write_mem32_fake.return_val = 0;

  // Write 1 byte at offset 2 within the word
  uint8_t data[] = {0xFF};
  EXPECT_NO_THROW(reader.write_bytes(0x20000002, data));

  // q_buf should be patched at offset 2
  EXPECT_EQ(fake_sl.q_buf[0], 0x11);
  EXPECT_EQ(fake_sl.q_buf[1], 0x22);
  EXPECT_EQ(fake_sl.q_buf[2], 0xFF);
  EXPECT_EQ(fake_sl.q_buf[3], 0x44);
}

TEST_F(StlinkProbeTest, WriteBytesFailsOnReadError)
{
  GivenConnectSuccess();
  reader.connect();

  stlink_read_mem32_fake.return_val = -1;

  uint8_t data[] = {0x01};
  EXPECT_THROW(reader.write_bytes(0x20000000, data), std::runtime_error);
  EXPECT_EQ(stlink_write_mem32_fake.call_count, 0u);
}

TEST_F(StlinkProbeTest, WriteBytesFailsOnWriteError)
{
  GivenConnectSuccess();
  reader.connect();

  stlink_read_mem32_fake.return_val = 0;
  stlink_write_mem32_fake.return_val = -1;

  uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  EXPECT_THROW(reader.write_bytes(0x20000000, data), std::runtime_error);
}

TEST_F(StlinkProbeTest, WriteBytesUnalignedSpansTwoWords)
{
  GivenConnectSuccess();
  reader.connect();

  // addr=0x20000003, size=3 => aligned_addr=0x20000000, aligned_size=8
  uint8_t existing[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  memcpy(fake_sl.q_buf, existing, sizeof(existing));
  stlink_read_mem32_fake.return_val = 0;
  stlink_write_mem32_fake.return_val = 0;

  uint8_t data[] = {0xAA, 0xBB, 0xCC};
  EXPECT_NO_THROW(reader.write_bytes(0x20000003, data));

  // Read and write should cover the full 8-byte aligned region
  EXPECT_EQ(stlink_read_mem32_fake.arg1_val, 0x20000000u);
  EXPECT_EQ(stlink_read_mem32_fake.arg2_val, 8u);
  EXPECT_EQ(stlink_write_mem32_fake.arg1_val, 0x20000000u);
  EXPECT_EQ(stlink_write_mem32_fake.arg2_val, 8u);

  // Bytes outside the written range must be preserved
  EXPECT_EQ(fake_sl.q_buf[0], 0x11);
  EXPECT_EQ(fake_sl.q_buf[1], 0x22);
  EXPECT_EQ(fake_sl.q_buf[2], 0x33);
  // Written bytes at offset 3, 4, 5
  EXPECT_EQ(fake_sl.q_buf[3], 0xAA);
  EXPECT_EQ(fake_sl.q_buf[4], 0xBB);
  EXPECT_EQ(fake_sl.q_buf[5], 0xCC);
  // Preserved tail
  EXPECT_EQ(fake_sl.q_buf[6], 0x77);
  EXPECT_EQ(fake_sl.q_buf[7], 0x88);
}
