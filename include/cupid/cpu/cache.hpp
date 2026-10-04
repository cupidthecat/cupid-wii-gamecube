// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/bus.hpp"
#include <array>

namespace cupid::cpu {
class InstructionCache {
public:
  u32 read(Bus &bus, u32 physical, bool enabled, bool locked);
  void invalidate(u32 effective);
  void reset();

private:
  struct Line {
    std::array<u32, 8> words{};
    u32 address = 0;
    bool valid = false;
  };

  Line *obtain(Bus &bus, u32 physical, bool locked);

  std::array<std::array<Line, 8>, 128> sets_{};
  std::array<u8, 128> replacement_{};
  // A host lookup hint. Tags and validity in sets_ remain authoritative.
  std::array<u8, 4096> lookup_{};
  u32 recent_address_ = 0;
  u8 recent_way_ = 8;
};
} // namespace cupid::cpu
