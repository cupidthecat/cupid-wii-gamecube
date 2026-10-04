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

  std::array<std::array<Line, 8>, 128> sets_{};
  std::array<u8, 128> replacement_{};
};
} // namespace cupid::cpu
