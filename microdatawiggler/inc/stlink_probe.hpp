#pragma once

#include <cstdint>
#include <span>

#include "debug_probe.hpp"
#include "stlink.h"

// The int32_t is used in 3rdparty dependency

enum class StlinkProbeFrequency : int32_t
{
  freq_5khz = 5,
  freq_15khz = 15,
  freq_25khz = 25,
  freq_50khz = 50,
  freq_100khz = 100,
  freq_125khz = 125,
  freq_240khz = 240,
  freq_480khz = 480,
  freq_950khz = 950,
  freq_1200khz = 1200,
  freq_1800khz = 1800,
  freq_4000khz = 4000
};

class StlinkProbe : public DebugProbe
{
public:
  StlinkProbe(StlinkProbeFrequency frequency = StlinkProbeFrequency::freq_4000khz) : frequency_(frequency){};
  ~StlinkProbe() override;

  StlinkProbe(const StlinkProbe &) = delete;
  StlinkProbe &operator=(const StlinkProbe &) = delete;

  void connect() override;
  [[nodiscard]] bool is_connected() const override;
  void disconnect() override;
  [[nodiscard]] std::vector<uint8_t> read_bytes(uint32_t addr, uint32_t size) const override;
  void write_bytes(uint32_t addr, std::span<const uint8_t> data) const override;

private:
  stlink_t *probe_ = nullptr;
  const enum StlinkProbeFrequency frequency_;
};
