// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/floating.hpp"
#include <array>
#include <cmath>
#include <stdexcept>

namespace cupid::cpu {
namespace {
// A binary64 product and addend fit into this exact integer at a 2^-2148 scale.
struct Magnitude {
  std::array<u64, 70> limbs{};

  void insert(u64 word, unsigned shift) {
    if (!word)
      return;
    const unsigned index = shift / 64, offset = shift % 64;
    limbs.at(index) |= word << offset;
    if (offset)
      limbs.at(index + 1) |= word >> (64 - offset);
  }

  int compare(const Magnitude &other) const {
    for (unsigned n = static_cast<unsigned>(limbs.size()); n-- > 0;)
      if (limbs[n] != other.limbs[n])
        return limbs[n] > other.limbs[n] ? 1 : -1;
    return 0;
  }

  void add(const Magnitude &other) {
    u64 carry = 0;
    for (unsigned n = 0; n < limbs.size(); ++n) {
      const u64 partial = limbs[n] + other.limbs[n], result = partial + carry;
      carry = partial < limbs[n] || result < partial;
      limbs[n] = result;
    }
  }

  void subtract(const Magnitude &other) {
    u64 borrow = 0;
    for (unsigned n = 0; n < limbs.size(); ++n) {
      const u64 partial = limbs[n] - other.limbs[n], result = partial - borrow;
      borrow = limbs[n] < other.limbs[n] || partial < borrow;
      limbs[n] = result;
    }
  }
};

struct BinaryValue {
  u64 significand;
  int exponent;
  bool negative;
};

BinaryValue decode(double value) {
  const u64 bits = std::bit_cast<u64>(value);
  const int exponent = static_cast<int>((bits >> 52) & 2047);
  return {(bits & 0xfffffffffffffull) | (exponent ? 0x10000000000000ull : 0),
          exponent ? exponent - 1075 : -1074, (bits >> 63) != 0};
}

Magnitude product(BinaryValue a, BinaryValue c) {
  const u64 al = static_cast<u32>(a.significand), ah = a.significand >> 32;
  const u64 cl = static_cast<u32>(c.significand), ch = c.significand >> 32;
  const u64 cross_a = ah * cl, cross_c = al * ch;
  u64 low = al * cl;
  u64 next = low + (cross_a << 32), carry = next < low;
  low = next;
  next = low + (cross_c << 32);
  carry += next < low;
  low = next;
  const u64 high = ah * ch + (cross_a >> 32) + (cross_c >> 32) + carry;
  const unsigned shift = static_cast<unsigned>(a.exponent + c.exponent + 2148);
  Magnitude result;
  result.insert(low, shift);
  result.insert(high, shift + 64);
  return result;
}

Magnitude single_value(BinaryValue value) {
  Magnitude result;
  result.insert(value.significand, static_cast<unsigned>(value.exponent + 2148));
  return result;
}

Magnitude signed_sum(Magnitude first, bool first_negative, const Magnitude &second,
                     bool second_negative) {
  if (first_negative == second_negative)
    first.add(second);
  else if (first.compare(second) >= 0)
    first.subtract(second);
  else {
    auto result = second;
    result.subtract(first);
    return result;
  }
  return first;
}
} // namespace

bool binary64_arithmetic_is_tiny(unsigned operation, double a, double b, double c,
                                 u32 rounding_mode, bool negative) {
  if (!std::isfinite(a))
    return false;
  // Just below the minimum normal, unrestricted binary64 significands have
  // half a subnormal ULP of spacing. Nearest rounding reaches normal at the
  // quarter-ULP midpoint; outward rounding reaches it strictly above the
  // half-ULP boundary. Exact integer comparisons preserve those distinctions.
  const bool outward = (rounding_mode == 2 && !negative) || (rounding_mode == 3 && negative);
  const int adjustment_exponent = rounding_mode == 0 ? -1076 : outward ? -1075 : 0;
  const auto minimum_normal = decode(std::bit_cast<double>(0x0010000000000000ull));
  const auto av = decode(a);
  if (operation == 18) {
    if (!std::isfinite(b) || b == 0)
      return false;
    // A nonzero binary64 numerator cannot have a tiny quotient with this divisor.
    if ((std::bit_cast<u64>(b) & 0x7fffffffffffffffull) < 0x3cb0000000000000ull)
      return a == 0;
    const auto bv = decode(b);
    auto threshold = product(bv, minimum_normal);
    if (adjustment_exponent)
      threshold.subtract(product(bv, BinaryValue{1, adjustment_exponent, false}));
    const int comparison = single_value(av).compare(threshold);
    return outward ? comparison <= 0 : comparison < 0;
  }
  Magnitude exact;
  if (operation == 20 || operation == 21) {
    if (!std::isfinite(b))
      return false;
    const auto bv = decode(operation == 20 ? -b : b);
    exact = signed_sum(single_value(av), av.negative, single_value(bv), bv.negative);
  } else {
    if (!std::isfinite(c))
      return false;
    const auto cv = decode(c);
    exact = product(av, cv);
    if (operation >= 28 && operation <= 31) {
      if (!std::isfinite(b))
        return false;
      const auto bv = decode(operation == 28 || operation == 30 ? -b : b);
      exact = signed_sum(exact, av.negative != cv.negative, single_value(bv), bv.negative);
    } else if (operation != 25)
      throw std::logic_error("Invalid binary64 tininess operation");
  }
  auto threshold = single_value(minimum_normal);
  if (adjustment_exponent)
    threshold.subtract(single_value(BinaryValue{1, adjustment_exponent, false}));
  const int comparison = exact.compare(threshold);
  return outward ? comparison <= 0 : comparison < 0;
}

double single_fused_intermediate(double a, double c, double b, bool subtract) {
  const double addend = subtract ? -b : b;
  const double rounded = std::fma(a, c, addend);
  const u64 bits = std::bit_cast<u64>(rounded);
  if (!std::isfinite(rounded) || (bits & 0x1fffffff) != 0x10000000 || !std::isfinite(a) ||
      !std::isfinite(c) || !std::isfinite(b))
    return rounded;
  const auto av = decode(a), cv = decode(c), bv = decode(addend), rv = decode(rounded);
  auto exact = product(av, cv);
  const auto addend_magnitude = single_value(bv);
  bool negative = av.negative != cv.negative;
  if (negative == bv.negative)
    exact.add(addend_magnitude);
  else if (exact.compare(addend_magnitude) >= 0)
    exact.subtract(addend_magnitude);
  else {
    auto opposite = addend_magnitude;
    opposite.subtract(exact);
    exact = opposite;
    negative = bv.negative;
  }
  int order;
  if (negative != rv.negative)
    order = negative ? -1 : 1;
  else {
    order = exact.compare(single_value(rv));
    if (negative)
      order = -order;
  }
  if (!order)
    return rounded;
  const bool larger_bits = (order > 0) != rv.negative;
  return std::bit_cast<double>(larger_bits ? bits + 1 : bits - 1);
}
} // namespace cupid::cpu
