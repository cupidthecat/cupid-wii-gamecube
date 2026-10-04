// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/bus.hpp"
#include <functional>
#include <span>
#include <vector>

namespace cupid {
class Memory final : public Bus {
public:
  static constexpr u32 mem1_size = 0x01800000;
  static constexpr u32 mem2_size = 0x04000000;
  static constexpr u32 cache_size = 0x00040000;
  explicit Memory(Console console);
  std::function<bool(u32)> mmio_handles;
  std::function<u32(u32, unsigned)> mmio_read;
  std::function<void(u32, u32, unsigned)> mmio_write;
  std::function<void(u8)> fifo_write;
  u8 read8(u32 address) override;
  u16 read16(u32 address) override;
  u32 read32(u32 address) override;
  u64 read64(u32 address) override;
  void write8(u32 address, u8 value) override;
  void write16(u32 address, u16 value) override;
  void write32(u32 address, u32 value) override;
  void write64(u32 address, u64 value) override;
  void locked_cache_dma(u32 memory_address, u32 cache_address, u32 bytes, bool load) override;
  std::span<u8> range(u32 address, std::size_t length);
  std::span<const u8> range(u32 address, std::size_t length) const;
  void copy_in(u32 physical, std::span<const u8> data);
  void copy_out(u32 physical, std::span<u8> data) const;
  void clear();
  // Initial boot BAT aliases, for host loaders rather than guest CPU accesses.
  static u32 boot_physical(u32 address);

private:
  std::vector<u8> mem1_, mem2_, cache_;
  u64 read(u32 address, unsigned width);
  void write(u32 address, u64 value, unsigned width);
};
} // namespace cupid
