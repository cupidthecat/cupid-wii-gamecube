// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/floating.hpp"
#include <array>
#include <cmath>
#include <limits>

namespace cupid::cpu {
namespace {
struct EstimateSegment {
  u64 fraction;
  u16 slope;
};

// Fractions and slopes measured at segment boundaries of the behavioral reference.
// See docs/cpu-reference.md for the sampling and differential verification procedure.
constexpr std::array<EstimateSegment, 32> reciprocal_segments{
    {{0xfff0000000000, 0x3e1}, {0xf070000000000, 0x3a7}, {0xe1d4000000000, 0x371},
     {0xd410000000000, 0x340}, {0xc710000000000, 0x313}, {0xbac4000000000, 0x2ea},
     {0xaf20000000000, 0x2c4}, {0xa410000000000, 0x2a0}, {0x9990000000000, 0x27f},
     {0x8f94000000000, 0x261}, {0x8610000000000, 0x245}, {0x7d00000000000, 0x22a},
     {0x7458000000000, 0x212}, {0x6c10000000000, 0x1fb}, {0x6428000000000, 0x1e5},
     {0x5c94000000000, 0x1d1}, {0x5550000000000, 0x1be}, {0x4e58000000000, 0x1ac},
     {0x47ac000000000, 0x19b}, {0x413c000000000, 0x18b}, {0x3b10000000000, 0x17c},
     {0x3520000000000, 0x16e}, {0x2f5c000000000, 0x15b}, {0x29f0000000000, 0x15b},
     {0x2488000000000, 0x143}, {0x1f7c000000000, 0x143}, {0x1a70000000000, 0x12d},
     {0x15bc000000000, 0x12d}, {0x1108000000000, 0x11a}, {0x0ca0000000000, 0x11a},
     {0x0838000000000, 0x108}, {0x0418000000000, 0x106}}};
constexpr std::array<EstimateSegment, 32> square_root_segments{
    {{0x69fa000000000, 0x568}, {0x5f2e000000000, 0x4f3}, {0x554a000000000, 0x48d},
     {0x4c30000000000, 0x435}, {0x43c8000000000, 0x3e7}, {0x3bfc000000000, 0x3a2},
     {0x34b8000000000, 0x365}, {0x2df0000000000, 0x32e}, {0x2794000000000, 0x2fc},
     {0x219c000000000, 0x2d0}, {0x1bfc000000000, 0x2a8}, {0x16ae000000000, 0x283},
     {0x11a8000000000, 0x261}, {0x0ce6000000000, 0x243}, {0x0862000000000, 0x226},
     {0x0416000000000, 0x20b}, {0xffe8000000000, 0x7a4}, {0xf0a4000000000, 0x700},
     {0xe2a8000000000, 0x670}, {0xd5c8000000000, 0x5f2}, {0xc9e4000000000, 0x584},
     {0xbedc000000000, 0x524}, {0xb498000000000, 0x4cc}, {0xab00000000000, 0x47e},
     {0xa204000000000, 0x43a}, {0x9994000000000, 0x3fa}, {0x91a0000000000, 0x3c2},
     {0x8a1c000000000, 0x38e}, {0x8304000000000, 0x35e}, {0x7c48000000000, 0x332},
     {0x75e4000000000, 0x30a}, {0x6fd0000000000, 0x2e6}}};
} // namespace

double reciprocal_estimate(double value) {
  const u64 bits = std::bit_cast<u64>(value), sign = bits & 0x8000000000000000ull;
  const unsigned exponent = static_cast<unsigned>((bits >> 52) & 2047);
  const u64 fraction = bits & 0xfffffffffffffull;
  if (!exponent && !fraction)
    return std::copysign(std::numeric_limits<double>::infinity(), value);
  if (exponent == 2047)
    return std::bit_cast<double>(fraction ? bits | 0x8000000000000ull : sign);
  if (exponent < 895)
    return std::bit_cast<double>(sign | binary32_to_binary64(0x7f7fffff));
  if (exponent >= 1149)
    return std::bit_cast<double>(sign);
  const auto &segment = reciprocal_segments[fraction >> 47];
  const u64 decrease = (static_cast<u64>(segment.slope) * ((fraction >> 37) & 1023) + 1) / 2;
  return std::bit_cast<double>(sign | (static_cast<u64>(2045 - exponent) << 52) |
                               (segment.fraction - (decrease << 29)));
}

double reciprocal_square_root_estimate(double value) {
  const u64 bits = std::bit_cast<u64>(value), sign = bits & 0x8000000000000000ull;
  int exponent = static_cast<int>((bits >> 52) & 2047);
  u64 fraction = bits & 0xfffffffffffffull;
  if (!exponent && !fraction)
    return std::copysign(std::numeric_limits<double>::infinity(), value);
  if (exponent == 2047 && fraction)
    return std::bit_cast<double>(bits | 0x8000000000000ull);
  if (sign)
    return std::bit_cast<double>(0x7ff8000000000000ull);
  if (exponent == 2047)
    return 0;
  if (!exponent) {
    const unsigned highest = 63 - static_cast<unsigned>(std::countl_zero(fraction));
    exponent = static_cast<int>(highest) - 51;
    fraction = (fraction << (52 - highest)) & 0xfffffffffffffull;
  }
  const unsigned segment_index =
      static_cast<unsigned>(exponent & 1) * 16 + static_cast<unsigned>(fraction >> 48);
  const auto &segment = square_root_segments[segment_index];
  const u64 decrease = static_cast<u64>(segment.slope) * ((fraction >> 37) & 2047);
  return std::bit_cast<double>((static_cast<u64>((3068 - exponent) / 2) << 52) |
                               (segment.fraction - (decrease << 26)));
}
} // namespace cupid::cpu
