// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/types.hpp"
#include <stdexcept>
#include <string>

namespace cupid {
class BusFault : public std::runtime_error {
public:
  BusFault(u32 address, bool write, const std::string &reason, bool unsupported_reason = false)
      : std::runtime_error(reason), address(address), write(write),
        unsupported_reason(unsupported_reason) {
  }

  u32 address;
  bool write;
  bool unsupported_reason;
};

// All addresses on this interface are physical; values use guest byte order.
class Bus {
public:
  virtual ~Bus() = default;
  virtual u8 read8(u32 address) = 0;
  virtual void write8(u32 address, u8 value) = 0;

  virtual u16 read16(u32 address) {
    return static_cast<u16>((static_cast<u16>(read8(address)) << 8) | read8(address + 1));
  }

  virtual u32 read32(u32 address) {
    return (static_cast<u32>(read16(address)) << 16) | read16(address + 2);
  }

  virtual u64 read64(u32 address) {
    return (static_cast<u64>(read32(address)) << 32) | read32(address + 4);
  }

  virtual void write16(u32 address, u16 value) {
    write8(address, static_cast<u8>(value >> 8));
    write8(address + 1, static_cast<u8>(value));
  }

  virtual void write32(u32 address, u32 value) {
    write16(address, static_cast<u16>(value >> 16));
    write16(address + 2, static_cast<u16>(value));
  }

  virtual void write64(u32 address, u64 value) {
    write32(address, static_cast<u32>(value >> 32));
    write32(address + 4, static_cast<u32>(value));
  }

  virtual void locked_cache_dma(u32 memory_address, u32 cache_address, u32 bytes, bool load) {
    (void)cache_address;
    (void)bytes;
    throw BusFault(memory_address, !load, "This bus does not implement locked-cache DMA", true);
  }
};
} // namespace cupid
