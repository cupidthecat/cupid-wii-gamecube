// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/gfx/rasterizer.hpp"

namespace cupid::gfx::detail {
// The rasterizer's bounded fixed-point edges fit these signed intermediates.
// Bit (y * 2 + x) identifies a covered sample in the complete derivative quad.
inline unsigned coverage_mask(const std::array<RasterWork::Edge, 3> &edges, int block_x,
                              int block_y) {
  unsigned mask = 15;
  for (const auto &edge : edges) {
    const s64 first = edge.constant + edge.dx * (s64(block_y) * 16) - edge.dy * (s64(block_x) * 16);
    const s64 right = first - edge.dy * 16;
    const s64 down = edge.dx * 16;
    const unsigned covered = unsigned(first > 0) | (unsigned(right > 0) << 1) |
                             (unsigned(first + down > 0) << 2) | (unsigned(right + down > 0) << 3);
    mask &= covered;
    if (!mask)
      break;
  }
  return mask;
}
} // namespace cupid::gfx::detail
