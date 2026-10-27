// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/memory.hpp"
#include <functional>
#include <span>

namespace cupid::gfx {
struct EfbCopySource {
  // Canonical 0xRRGGBBAA color; six-bit components are recovered before filtering.
  std::function<u32(unsigned x, unsigned y)> color;
  // Unsigned depth in the low 24 bits.
  std::function<u32(unsigned x, unsigned y)> depth;
};

struct EfbCopyLayout {
  u32 destination = 0;
  unsigned width = 0, height = 0;
  unsigned padded_width = 0, padded_height = 0;
  unsigned block_rows = 0, row_bytes = 0, stride = 0;
};

struct EfbCopyPlan {
  EfbCopyLayout layout;
  unsigned source_format = 0, copy_format = 0, scale = 1;
  unsigned block_width = 0, block_height = 0, block_bytes = 0;
  unsigned columns = 0, left = 0, top = 0;
  bool convert_intensity = false;
};

// Validates the complete texture footprint without sampling or changing RAM.
EfbCopyPlan plan_efb_texture(std::span<const u32> bp);

// Encodes BP texture-copy state into physical RAM. Clear and XFB copies belong to the caller.
// All coordinates, formats and destination bytes are checked before sampling; sampling failures
// leave RAM untouched. Complete tiles include pixels beyond the nominal source rectangle.
EfbCopyLayout copy_efb_texture(Memory &memory, std::span<const u32> bp,
                               const EfbCopySource &source);
} // namespace cupid::gfx
