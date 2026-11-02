// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/types.hpp"
#include <algorithm>
#include <bit>

namespace cupid::gfx::detail {
struct ManualMip {
  unsigned level, width, height, bank;
  u64 offset, bytes;
};

inline unsigned manual_mip_level(unsigned width, unsigned height, unsigned requested) {
  return std::min(requested, static_cast<unsigned>(std::bit_width(std::max(width, height)) - 1));
}

// Level zero starts in the even bank. Later levels alternate banks, with
// each bank advancing only past its own complete, tile-aligned levels.
// RGBA8 base decoding splits banks separately; iterator offsets still count
// the full encoded base size, and later levels contain ordinary full tiles.
inline ManualMip manual_mip(unsigned width, unsigned height, unsigned requested,
                            unsigned tile_width, unsigned tile_height, unsigned tile_bytes) {
  const unsigned level = manual_mip_level(width, height, requested);
  const auto bytes = [&](unsigned at) {
    const unsigned w = std::max(1u, width >> at), h = std::max(1u, height >> at);
    return u64((w + tile_width - 1) / tile_width) * ((h + tile_height - 1) / tile_height) *
           tile_bytes;
  };
  u64 offset = 0;
  for (unsigned previous = level & 1; previous < level; previous += 2)
    offset += bytes(previous);
  return {
      level,       std::max(1u, width >> level), std::max(1u, height >> level), level & 1, offset,
      bytes(level)};
}
} // namespace cupid::gfx::detail
