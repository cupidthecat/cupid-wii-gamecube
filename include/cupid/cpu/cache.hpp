// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/bus.hpp"
#include <array>

namespace cupid::cpu {
class InstructionCache {
public:
  u32 read(Bus &bus, u32 physical, bool enabled, bool locked) {
    if (!enabled) {
      u32 value = 0;
      bus.read_instruction_words(physical, std::span<u32>(&value, 1));
      return value;
    }
    if ((physical & 3) == 0) {
      if (auto *line = obtain(bus, physical, locked))
        return line->words[(physical >> 2) & 7];
      return read_locked_miss(bus, physical);
    }
    return read_unaligned(bus, physical, locked);
  }

  void invalidate(u32 effective);
  void reset();

private:
  struct Line {
    std::array<u32, 8> words{};
    u32 address = 0;
    bool valid = false;
  };

  Line *obtain(Bus &bus, u32 physical, bool locked) {
    const u32 address = bus.instruction_cache_key(physical) & ~31u;
    const unsigned set_index = (address >> 5) & 127;
    auto &set = sets_[set_index];
    // Repeating the last hit leaves its replacement bits unchanged. Every
    // allocation and other hit replaces the hint; invalidation clears validity.
    if (recent_way_ < 8 && recent_address_ == address && set[recent_way_].valid)
      return &set[recent_way_];
    return obtain_other(bus, physical, address, locked);
  }

  Line *obtain_other(Bus &bus, u32 physical, u32 address, bool locked);
  u32 read_locked_miss(Bus &bus, u32 physical);
  u32 read_unaligned(Bus &bus, u32 physical, bool locked);

  std::array<std::array<Line, 8>, 128> sets_{};
  std::array<u8, 128> replacement_{};
  // A host lookup hint. Tags and validity in sets_ remain authoritative.
  std::array<u8, 4096> lookup_{};
  u32 recent_address_ = 0;
  u8 recent_way_ = 8;
};
} // namespace cupid::cpu
