#pragma once

#include <cstdint>
#include <span>
#include <vector>

/**
 * @brief Interface for a debug probe hardware.
 *
 * Defines the contract for connecting to a target device and performing
 * raw memory reads and writes through a debug probe.
 */
class DebugProbe
{
public:
  virtual ~DebugProbe() = default;

  /**
   * @brief Establish a connection to the target device.
   *
   * Any parameters required for connection (e.g., serial number, interface
   * type) should be handled by the implementation for example through
   * constructor parameters or configuration methods.
   */
  virtual void connect() = 0;

  /**
   * @brief Check whether the probe is currently connected.
   * @return true if connected, false otherwise.
   */
  [[nodiscard]] virtual bool is_connected() const = 0;

  /**
   * @brief Disconnect from the target device.
   *
   * Disconnecting and connecting may be required to be able to flash the device
   * with external tools
   */
  virtual void disconnect() = 0;

  /**
   * @brief Read a block of bytes from target memory.
   * @param addr Start address to read from.
   * @param size Number of bytes to read.
   * @return The read data.
   */
  [[nodiscard]] virtual std::vector<uint8_t> read_bytes(uint32_t addr, uint32_t size) const = 0;

  /**
   * @brief Write a block of bytes to target memory.
   * @param addr Start address to write to.
   * @param[in] data Buffer containing the data to write.
   */
  virtual void write_bytes(uint32_t addr, std::span<const uint8_t> data) const = 0;
};
