// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/types.hpp"
#include <bit>

namespace cupid::cpu {
double reciprocal_estimate(double value);
double reciprocal_square_root_estimate(double value);
// C has already been rounded to the CPU's multiplier precision.
double single_fused_intermediate(double a, double c, double b, bool subtract);
// Test NI tininess after significand rounding with an unrestricted exponent.
bool binary64_arithmetic_is_tiny(unsigned operation, double a, double b, double c,
                                 u32 rounding_mode, bool negative);

// Register transfers retain NaN payloads and do not quiet signaling NaNs.
inline u64 binary32_to_binary64(u32 value) {
  const u64 sign = static_cast<u64>(value & 0x80000000) << 32;
  const unsigned exponent = (value >> 23) & 255;
  u64 fraction = value & 0x7fffff;
  if (exponent == 255)
    return sign | 0x7ff0000000000000ull | (fraction << 29);
  if (exponent)
    return sign | (static_cast<u64>(exponent + 896) << 52) | (fraction << 29);
  if (!fraction)
    return sign;
  const unsigned leading = 63 - static_cast<unsigned>(std::countl_zero(fraction));
  return sign | (static_cast<u64>(leading + 874) << 52) |
         ((fraction ^ (1ull << leading)) << (52 - leading));
}

inline u32 binary64_to_binary32_store(u64 value, bool flush_subnormal = false) {
  const u32 sign = static_cast<u32>(value >> 32) & 0x80000000;
  const unsigned exponent = static_cast<unsigned>((value >> 52) & 2047);
  if (exponent <= 896 && flush_subnormal)
    return sign;
  if (exponent >= 874 && exponent <= 896) {
    const u64 significand = (value & 0xfffffffffffffull) | 0x10000000000000ull;
    return sign | static_cast<u32>(significand >> (926 - exponent));
  }
  const u32 guest_exponent = ((exponent & 0x400) >> 3) | (exponent & 0x7f);
  return sign | (guest_exponent << 23) | (static_cast<u32>(value >> 29) & 0x7fffff);
}
} // namespace cupid::cpu
