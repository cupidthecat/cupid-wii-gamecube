// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <bit>
#include <cstdint>

namespace quotient_test {
// Independent binary32 RN-even oracle. It uses integer quotient/remainder,
// without a floating estimate, adjacent-midpoint predicate or binary search.
constexpr std::uint32_t rounded_quotient(std::uint32_t numerator, std::uint32_t denominator) {
  using U64 = std::uint64_t;
  constexpr std::uint32_t infinity = 0x7f800000, quiet_nan = 0x7fc00000;
  const auto sign = (numerator ^ denominator) & 0x80000000;
  numerator &= 0x7fffffff;
  denominator &= 0x7fffffff;
  if (numerator > infinity || denominator > infinity || (numerator == 0 && denominator == 0) ||
      (numerator == infinity && denominator == infinity))
    return quiet_nan;
  if (denominator == 0 || numerator == infinity)
    return sign | infinity;
  if (denominator == infinity || numerator == 0)
    return sign;

  const auto significand = [](std::uint32_t bits) {
    return (bits & 0x7fffff) | ((bits >> 23) ? 0x800000 : 0);
  };
  const auto exponent = [](std::uint32_t bits) {
    return (bits >> 23) ? static_cast<int>(bits >> 23) - 150 : -149;
  };
  const auto n = significand(numerator), d = significand(denominator);
  const int exponent_delta = exponent(numerator) - exponent(denominator);
  const int leading_delta =
      static_cast<int>(std::bit_width(n)) - static_cast<int>(std::bit_width(d));
  const bool below_power =
      leading_delta >= 0 ? U64(n) < (U64(d) << leading_delta) : (U64(n) << -leading_delta) < U64(d);
  int power = exponent_delta + leading_delta - static_cast<int>(below_power);
  // Values below half the minimum subnormal round to signed zero. Values
  // at/above 2^128 overflow; the lower overflow tie is handled by rounding.
  if (power < -150)
    return sign;
  if (power >= 128)
    return sign | infinity;

  const int quantum = (power < -126 ? -126 : power) - 23;
  const int shift = exponent_delta - quantum;
  U64 scaled_n = n, scaled_d = d;
  // The selected quantum bounds shift to [-24,47]. The operand's leading
  // bit compensates for that shift, so both scaled integers fit in 49 bits.
  if (shift >= 0)
    scaled_n <<= shift;
  else
    scaled_d <<= -shift;
  U64 quotient = scaled_n / scaled_d;
  const U64 remainder = scaled_n % scaled_d;
  const U64 other_half = scaled_d - remainder;
  if (remainder > other_half || (remainder == other_half && (quotient & 1)))
    ++quotient;
  if (power < -126)
    return sign | static_cast<std::uint32_t>(quotient);
  if (quotient == 0x1000000) {
    quotient >>= 1;
    ++power;
  }
  if (power >= 128)
    return sign | infinity;
  return sign | (static_cast<std::uint32_t>(power + 127) << 23) |
         (static_cast<std::uint32_t>(quotient) & 0x7fffff);
}

static_assert(rounded_quotient(0x3f800000, 0x40400000) == 0x3eaaaaab);
static_assert(rounded_quotient(0x80000000, 0x3f800000) == 0x80000000);
static_assert(rounded_quotient(0x00000001, 0x40000000) == 0x00000000);
static_assert(rounded_quotient(0x00000003, 0x40000000) == 0x00000002);
static_assert(rounded_quotient(0x00000001, 0x3f000000) == 0x00000002);
static_assert(rounded_quotient(0x7f7fffff, 0x3f000000) == 0x7f800000);
static_assert(rounded_quotient(0x7f800000, 0xff800000) == 0x7fc00000);
static_assert(rounded_quotient(0x7fa00001, 0x3f800000) == 0x7fc00000);
} // namespace quotient_test
