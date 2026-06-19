#include "stlink_probe.hpp"

extern "C"
{
// stlink.h must be included before read_write.h because it defines stlink_t,
// which is used in read_write.h
// clang-format off
#include <stlink.h>
#include <read_write.h>
  // clang-format on
}

#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

StlinkProbe::~StlinkProbe()
{
  disconnect();
}

void StlinkProbe::connect()
{
  if (probe_)
  {
    throw std::runtime_error("First disconnect from ST-Link");
  }

  probe_ = stlink_open_usb(UINFO, CONNECT_HOT_PLUG, nullptr, static_cast<int32_t>(frequency_));
  if (!probe_)
  {
    throw std::runtime_error("Failed to open ST-Link device");
  }

  if (stlink_enter_swd_mode(probe_) != 0)
  {
    stlink_close(probe_);
    throw std::runtime_error("Failed to enter SWD mode");
  }

  fprintf(stderr, "Connected to ST-Link (chip ID: 0x%04x)\n", probe_->chip_id);
}

bool StlinkProbe::is_connected() const
{
  return probe_ != nullptr;
}

void StlinkProbe::disconnect()
{
  if (probe_)
  {
    stlink_exit_debug_mode(probe_);
    stlink_close(probe_);
    probe_ = nullptr;
  }
}

// https://en.wikipedia.org/wiki/Data_structure_alignment#Computing_padding

template <uint32_t Alignment> struct AlignedAddress
{
  AlignedAddress(uint32_t addr, uint32_t size)
      : aligned_addr(addr & ~(Alignment - 1)), offset_to_original_addr(addr - aligned_addr),
        aligned_size((offset_to_original_addr + size + Alignment - 1) & ~(Alignment - 1))
  {
  }
  const uint32_t aligned_addr;
  const uint32_t offset_to_original_addr;
  const uint32_t aligned_size;
};

std::vector<uint8_t> StlinkProbe::read_bytes(uint32_t addr, uint32_t size) const
{
  if (!probe_)
  {
    throw std::runtime_error("Not connected to ST-Link");
  }

  // stlink_read_mem32 requires 4-byte aligned address and size
  const AlignedAddress<4> aligned_addr{addr, size};

  if (stlink_read_mem32(probe_, aligned_addr.aligned_addr, static_cast<uint16_t>(aligned_addr.aligned_size)) != 0)
  {
    std::array<char, 64> msg{};
    snprintf(msg.data(), msg.size(), "Failed to read %" PRIu32 " bytes at 0x%08" PRIX32, size, addr);
    throw std::runtime_error(msg.data());
  }

  uint8_t *data_start = probe_->q_buf + aligned_addr.offset_to_original_addr;
  uint8_t *data_end = probe_->q_buf + aligned_addr.offset_to_original_addr + size;
  return {data_start, data_end};
}

void StlinkProbe::write_bytes(uint32_t addr, std::span<const uint8_t> data) const
{
  if (!probe_)
  {
    throw std::runtime_error("Not connected to ST-Link");
  }

  // Read-modify-write for alignment: read aligned region, patch bytes, write
  // back
  const AlignedAddress<4> aligned_addr{addr, static_cast<uint32_t>(data.size())};

  if (stlink_read_mem32(probe_, aligned_addr.aligned_addr, static_cast<uint16_t>(aligned_addr.aligned_size)) != 0)
  {
    std::array<char, 80> msg{};
    snprintf(msg.data(), msg.size(), "Failed to read %" PRIu32 " bytes at 0x%08" PRIX32 " for read-modify-write",
             static_cast<uint32_t>(data.size()), addr);
    throw std::runtime_error(msg.data());
  }

  memcpy(probe_->q_buf + aligned_addr.offset_to_original_addr, data.data(), data.size());

  if (stlink_write_mem32(probe_, aligned_addr.aligned_addr, static_cast<uint16_t>(aligned_addr.aligned_size)) != 0)
  {
    std::array<char, 64> msg{};
    snprintf(msg.data(), msg.size(), "Failed to write %" PRIu32 " bytes at 0x%08" PRIX32,
             static_cast<uint32_t>(data.size()), addr);
    throw std::runtime_error(msg.data());
  }
}
