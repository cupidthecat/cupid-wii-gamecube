// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/memory.hpp"
#include <array>
#include <functional>
#include <span>

namespace cupid::gfx {
struct XfbCopyLayout {
  u32 destination = 0;
  unsigned width = 0, height = 0, encoded_height = 0;
  unsigned row_bytes = 0, copied_row_bytes = 0, stride = 0;
  float y_scale = 1;
};

struct XfbCopyPlan {
  XfbCopyLayout layout;
  unsigned top = 0, source_height = 0, gamma = 0;
  std::array<unsigned, 3> weights{};
  double row_ratio = 1;
};

// Shared source and staging validation precedes CPU or GPU encoding.
XfbCopyPlan plan_efb_xfb(std::span<const u32> bp);
const std::array<std::array<u32, 256>, 4> &xfb_gamma_tables();

// BP-controlled XFB encoding and staging readback into physical RAM. Colors are 0xRRGGBBAA.
// Validates all observable source and destination bytes before sampling or modifying RAM.
// Undefined offset/staging edges fail explicitly. Copy-trigger clear remains caller-owned.
XfbCopyLayout copy_efb_xfb(Memory &memory, std::span<const u32> bp,
                           const std::function<u32(unsigned, unsigned)> &color);
} // namespace cupid::gfx
