#include <chrono>
#include <cstdint>
#include <vector>

#include "debug_probe.hpp"
#include "stlink_probe.hpp"
#include "gtest/gtest.h"

// SRAM base address common to STM32 targets.
constexpr uint32_t kSramBase = 0x20000000;

class StlinkProbeIntegration : public ::testing::Test
{
protected:
  void SetUp() override
  {
  }

  void TearDown() override
  {
  }
};

TEST_F(StlinkProbeIntegration, ConnectDisconnectCycleSucceeds)
{
  StlinkProbe stlinkprobe;
  DebugProbe &probe = stlinkprobe;

  ASSERT_NO_THROW(probe.connect());
  ASSERT_TRUE(probe.is_connected());
  ASSERT_NO_THROW(probe.disconnect());
  ASSERT_FALSE(probe.is_connected());

  ASSERT_NO_THROW(probe.connect());
  ASSERT_TRUE(probe.is_connected());
  ASSERT_NO_THROW(probe.disconnect());
  ASSERT_FALSE(probe.is_connected());
}

TEST_F(StlinkProbeIntegration, MultipleInstanceCreationsSucceed)
{
  {
    StlinkProbe stlinkprobe;
    DebugProbe &probe = stlinkprobe;

    ASSERT_NO_THROW(probe.connect());
    ASSERT_TRUE(probe.is_connected());
    ASSERT_NO_THROW(probe.disconnect());
    ASSERT_FALSE(probe.is_connected());
  }

  {
    StlinkProbe stlinkprobe;
    DebugProbe &probe = stlinkprobe;

    ASSERT_NO_THROW(probe.connect());
    ASSERT_TRUE(probe.is_connected());
    ASSERT_NO_THROW(probe.disconnect());
    ASSERT_FALSE(probe.is_connected());
  }
}

TEST_F(StlinkProbeIntegration, DisconnectOnDestructionSucceeds)
{
  {
    StlinkProbe stlinkprobe;
    DebugProbe &probe = stlinkprobe;

    ASSERT_NO_THROW(probe.connect());
    ASSERT_TRUE(probe.is_connected());
  }

  {
    StlinkProbe stlinkprobe;
    DebugProbe &probe = stlinkprobe;

    ASSERT_NO_THROW(probe.connect());
    ASSERT_TRUE(probe.is_connected());
  }
}

TEST_F(StlinkProbeIntegration, ParallelInstanceFails)
{
  StlinkProbe stlinkprobe1;
  DebugProbe &probe1 = stlinkprobe1;
  StlinkProbe stlinkprobe2;
  DebugProbe &probe2 = stlinkprobe2;

  ASSERT_NO_THROW(probe1.connect());
  ASSERT_TRUE(probe1.is_connected());
  ASSERT_THROW(probe2.connect(), std::runtime_error);
}

TEST_F(StlinkProbeIntegration, ReadWriteRoundTrip32AlignedSucceeds)
{
  // Given the target is running
  StlinkProbe stlinkprobe;
  DebugProbe &probe = stlinkprobe;
  probe.connect();

  // When a known memory location is written with known data
  constexpr uint32_t addr = kSramBase;
  constexpr uint32_t size = 4;
  const std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF};
  probe.write_bytes(addr, pattern);

  // And the same location is read back
  auto readback = probe.read_bytes(addr, size);

  // Then the read data matches the written pattern
  EXPECT_EQ(readback, pattern);
  // And the read length matches the requested size
  EXPECT_EQ(readback.size(), size);
}

TEST_F(StlinkProbeIntegration, ReadWriteRoundTrip32UnalignedSucceeds)
{
  // Given the target is running
  StlinkProbe stlinkprobe;
  DebugProbe &probe = stlinkprobe;
  probe.connect();

  // When a known memory location is written with known data
  constexpr uint32_t addr = kSramBase + 1u;
  constexpr uint32_t size = 4;
  const std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF};
  probe.write_bytes(addr, pattern);

  // And the same location is read back
  auto readback = probe.read_bytes(addr, size);

  // Then the read data matches the written pattern
  EXPECT_EQ(readback, pattern);
  // And the read length matches the requested size
  EXPECT_EQ(readback.size(), size);
}

TEST_F(StlinkProbeIntegration, ReadSpeedTest)
{
  // Given the target is running
  StlinkProbe stlinkprobe;
  DebugProbe &probe = stlinkprobe;
  probe.connect();

  constexpr uint32_t addr = kSramBase;
  constexpr uint32_t size = 4;

  const auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < 100; i++)
  {
    auto readback = probe.read_bytes(addr, size);
  }
  const auto end = std::chrono::high_resolution_clock::now();

  long long duration_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

  constexpr long long recorded_duration = 17340;
  EXPECT_LT(duration_microseconds, recorded_duration * 1.9f);
}

TEST_F(StlinkProbeIntegration, WriteSpeedTest)
{
  // Given the target is running
  StlinkProbe stlinkprobe;
  DebugProbe &probe = stlinkprobe;
  probe.connect();

  constexpr uint32_t addr = kSramBase;
  constexpr uint32_t size = 4;
  const std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF};

  const auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < 100; i++)
  {
    probe.write_bytes(addr, pattern);
  }
  const auto end = std::chrono::high_resolution_clock::now();

  long long duration_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

  constexpr long long recorded_duration = 41926;
  EXPECT_LT(duration_microseconds, recorded_duration * 1.9f);
}
