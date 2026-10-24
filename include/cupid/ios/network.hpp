// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/types.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cupid {
class Memory;

namespace ios {
enum class NetworkDevice { Request, Time };

class UnsupportedNetwork final : public std::runtime_error {
public:
  UnsupportedNetwork(NetworkDevice device, u32 operation);
  const NetworkDevice device;
  const u32 operation;
};

class NetworkStorageError final : public std::runtime_error {
public:
  explicit NetworkStorageError(s32 error);
  const s32 error;
};

class Network {
public:
  explicit Network(Memory &memory);
  ~Network();
  Network(const Network &) = delete;
  Network &operator=(const Network &) = delete;

  // The clock returns Unix seconds from the same RTC used by the device bus.
  std::function<u64()> unix_time;
  // nullopt means absent; existing files that cannot be read must report a storage error.
  std::function<std::optional<std::vector<u8>>(std::string_view)> read_file;
  std::function<bool(std::string_view, std::span<const u8>)> write_file;
  std::function<std::optional<u32>()> device_id;

  s32 ioctl(NetworkDevice device, u32 operation, u32 input, u32 input_size, u32 output,
            u32 output_size);
  bool scheduler_suspended() const;
  u64 adjusted_utc() const;
  void reset_storage();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Produces the console-specific identifier from the public WC24 algorithm.
u64 make_wc24_id(u32 hardware_id, u16 generation, u8 hardware_model, u8 area_code);
} // namespace ios
} // namespace cupid
