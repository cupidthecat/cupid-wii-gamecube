// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/types.hpp"
#include <algorithm>
#include <array>
#include <span>
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

  virtual void memory_access_error() {
    throw BusFault(0, true, "This bus does not implement memory-access interrupts", true);
  }

  // Cache identity can alias physical addresses without changing backing reads.
  virtual u32 instruction_cache_key(u32 physical) const {
    return physical;
  }

  // CPU data transactions can have different bounds from host and fetch access.
  virtual u64 read_cpu_data(u32 physical, unsigned byte_count) {
    switch (byte_count) {
    case 1:
      return read8(physical);
    case 2:
      return read16(physical);
    case 4:
      return read32(physical);
    case 8:
      return read64(physical);
    default:
      throw std::invalid_argument("Invalid CPU data read width");
    }
  }

  virtual void write_cpu_data(u32 physical, u32 value, unsigned byte_count) {
    switch (byte_count) {
    case 1:
      write8(physical, static_cast<u8>(value));
      return;
    case 2:
      write16(physical, static_cast<u16>(value));
      return;
    case 3:
      write8(physical, static_cast<u8>(value >> 16));
      write8(physical + 1, static_cast<u8>(value >> 8));
      write8(physical + 2, static_cast<u8>(value));
      return;
    case 4:
      write32(physical, value);
      return;
    default:
      throw std::invalid_argument("Invalid CPU data write width");
    }
  }

  // A rejected instruction backing copy retains all destination words.
  virtual bool read_instruction_words(u32 physical, std::span<u32> words) {
    for (std::size_t n = 0; n < words.size(); ++n)
      words[n] = read32(physical + static_cast<u32>(n * 4));
    return true;
  }

  virtual bool write_page_table32(u32 physical, u32 value) {
    write32(physical, value);
    return true;
  }

  // Stage bounded byte copies so a backing fault retains the whole destination.
  virtual bool read_instruction_bytes(u32 physical, std::span<u8> bytes) {
    if (bytes.empty() || bytes.size() > 32)
      throw std::invalid_argument("Invalid instruction backing byte count");
    std::array<u8, 32> incoming{};
    if (bytes.size() == 4) {
      const u32 value = read32(physical);
      for (unsigned n = 0; n < 4; ++n)
        incoming[n] = static_cast<u8>(value >> (24u - n * 8u));
    } else {
      for (std::size_t n = 0; n < bytes.size(); ++n)
        incoming[n] = read8(physical + static_cast<u32>(n));
    }
    std::copy_n(incoming.begin(), bytes.size(), bytes.begin());
    return true;
  }
};
} // namespace cupid
