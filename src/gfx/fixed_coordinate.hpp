// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/gfx/vertex.hpp"
#include <cmath>

namespace cupid::gfx::detail {
inline s32 fixed_coordinate(float value) {
  if (!std::isfinite(value))
    throw VertexError("Non-finite texture coordinate");
  // Retain truncation before conversion so fractional inputs keep the existing
  // host exception behavior. Scaling a float by this power of two is exact here.
  const double truncated = std::trunc(static_cast<double>(value) * 128);
  u32 bits;
  if (truncated >= -16777216.0 && truncated <= 16777216.0)
    bits = static_cast<u32>(static_cast<s32>(truncated));
  else {
    // Wrapped finite coordinates keep the modular call, including its host
    // status flags. The direct path needs no remainder and fits signed range.
    const double modulo = std::fmod(truncated, 16777216.0);
    bits = static_cast<u32>(static_cast<s64>(modulo));
  }
  bits &= 0xffffff;
  return static_cast<s32>(bits ^ 0x800000) - 0x800000;
}
} // namespace cupid::gfx::detail
