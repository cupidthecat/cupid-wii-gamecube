// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/bus.hpp"
#include <array>

namespace cupid::cpu {
// Byte storage is separate from the instruction cache and DMA-visible backing.
class DataCache {
public:
  void read(Bus &bus, u32 physical, std::span<u8> bytes, bool locked);
  void write(Bus &bus, u32 physical, std::span<const u8> bytes, bool locked);
  void store(Bus &bus, u32 physical);
  void invalidate(Bus &bus, u32 physical);
  void flush(Bus &bus, u32 physical);
  void flush_all(Bus &bus);
  void reset();

private:
  struct Line {
    std::array<u8, 32> bytes{};
    u32 key = 0, physical = 0;
    bool valid = false, dirty = false;
  };

  std::array<std::array<Line, 8>, 128> sets_{};
  std::array<u8, 128> replacement_{};
  Line *obtain(Bus &bus, u32 physical, bool locked);
};
} // namespace cupid::cpu
