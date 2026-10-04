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

// Encodes BP texture-copy state into physical RAM. Clear and XFB copies belong to the caller.
// All coordinates, formats and destination bytes are checked before sampling; sampling failures
// leave RAM untouched. Complete tiles include pixels beyond the nominal source rectangle.
EfbCopyLayout copy_efb_texture(Memory &memory, std::span<const u32> bp,
                               const EfbCopySource &source);
} // namespace cupid::gfx
