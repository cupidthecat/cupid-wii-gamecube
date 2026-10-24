// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "quotient_oracle.hpp"
#include <array>
#include <stdexcept>
#include <vector>

namespace quotient_test {
struct Case {
  std::uint32_t numerator, denominator, expected;
};

inline std::vector<Case> division_cases() {
  using U32 = std::uint32_t;
  // Literal expectations independently guard the 64-bit integer oracle.
  constexpr std::array<std::array<U32, 3>, 32> hand{{
      {0x00000001, 0x40000000, 0x00000000},
      {0x00000003, 0x40000000, 0x00000002},
      {0x00000005, 0x40000000, 0x00000002},
      {0x00000007, 0x40000000, 0x00000004},
      {0x00fffffd, 0x40000000, 0x007ffffe},
      {0x00ffffff, 0x40000000, 0x00800000},
      {0x00800001, 0x40000000, 0x00400000},
      {0x00800003, 0x40000000, 0x00400002},
      {0x3f800000, 0x40400000, 0x3eaaaaab},
      {0x40000000, 0x40400000, 0x3f2aaaab},
      {0x3f800000, 0x41200000, 0x3dcccccd},
      {0x41200000, 0x40400000, 0x40555555},
      {0x00800000, 0x40400000, 0x002aaaab},
      {0x7f7fffff, 0x40000000, 0x7effffff},
      {0x7f7fffff, 0x3f000000, 0x7f800000},
      {0x7f7fffff, 0x3f7fffff, 0x7f800000},
      {0x7f7ffffe, 0x3f7fffff, 0x7f7fffff},
      {0x4b7fffff, 0x4b7ffffe, 0x3f800001},
      {0x40800000, 0x40000000, 0x40000000},
      {0x00000003, 0x3f800000, 0x00000003},
      {0x00000003, 0x00000003, 0x3f800000},
      {0x00000000, 0x40000000, 0x00000000},
      {0x40000000, 0x00000000, 0x7f800000},
      {0x7f800000, 0x40000000, 0x7f800000},
      {0x40000000, 0x7f800000, 0x00000000},
      {0x00000000, 0x00000000, 0x7fc00000},
      {0x7f800000, 0x7f800000, 0x7fc00000},
      {0x7fc12345, 0x40000000, 0x7fc00000},
      {0x40000000, 0x7f800001, 0x7fc00000},
      // Independent anchors for sticky, tie-even and underflow-cutoff errors.
      {0x00500003, 0x40a00000, 0x00100001},
      {0x00000003, 0x40000000, 0x00000002},
      {0x00000002, 0x40400000, 0x00000001},
  }};
  constexpr std::array<U32, 32> edges{
      0,          1,          2,          3,          4,          5,          7,
      0x003fffff, 0x00400000, 0x00400001, 0x007ffffd, 0x007ffffe, 0x007fffff, 0x00800000,
      0x00800001, 0x00800003, 0x00fffffd, 0x00ffffff, 0x01000000, 0x3eaaaaab, 0x3f000000,
      0x3f7fffff, 0x3f800000, 0x3f800001, 0x40000000, 0x40400000, 0x41200000, 0x4b7fffff,
      0x7f7ffffe, 0x7f7fffff, 0x7f800000, 0x7fc00001};
  std::vector<Case> cases;
  cases.reserve(1123224);
  const auto add = [&](U32 n, U32 d) { cases.push_back({n, d, rounded_quotient(n, d)}); };
  const auto signed_cases = [&](U32 n, U32 d) {
    for (U32 signs = 0; signs < 4; ++signs)
      add(n ^ ((signs & 1u) << 31), d ^ ((signs & 2u) << 30));
  };
  for (const auto &literal : hand)
    for (U32 signs = 0; signs < 4; ++signs) {
      const U32 ns = (signs & 1u) << 31, ds = (signs & 2u) << 30;
      add(literal[0] ^ ns, literal[1] ^ ds);
      const U32 expected = literal[2] == 0x7fc00000 ? literal[2] : literal[2] ^ ns ^ ds;
      if (cases.back().expected != expected)
        throw std::logic_error("Integer division oracle failed a literal boundary case");
    }
  for (U32 n : edges)
    for (U32 d : edges)
      signed_cases(n, d);
  U32 random = 0xc0a71d35;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return random;
  };
  for (unsigned i = 0; i < 65536; ++i) {
    const U32 n = next(), d = next();
    add(n, d);
  }
  // Every encoded input-exponent pair, with independent significand boundaries
  // and every sign combination. This is bounded rather than an all-bit sweep.
  constexpr std::array<std::array<U32, 2>, 4> tails{
      {{0, 0}, {1, 0x7fffff}, {0x7fffff, 1}, {0x3fffff, 0x400001}}};
  for (U32 ne = 0; ne < 256; ++ne)
    for (U32 de = 0; de < 256; ++de)
      for (const auto &pair : tails)
        signed_cases((ne << 23) | pair[0], (de << 23) | pair[1]);
  // Every subnormal leading bit in both operand positions, including remainder
  // tails, signed zero results and normal/subnormal/overflow result ranges.
  constexpr std::array<U32, 8> divisors{1,          3,          0x3f800000, 0x3f800001,
                                        0x40000000, 0x40400000, 0x40a00000, 0x7f7fffff};
  for (unsigned bit = 0; bit < 23; ++bit) {
    const U32 leading = 1u << bit;
    for (U32 n : {leading, leading | (leading - 1u)})
      for (U32 d : divisors) {
        signed_cases(n, d);
        signed_cases(d, n);
      }
  }
  // Result biased exponents straddle the entire gradual-underflow interval,
  // its half-minimum cutoff, and the carry into the least normal value.
  constexpr std::array<U32, 6> mantissas{0, 1, 3, 0x3fffff, 0x7ffffe, 0x7fffff};
  for (int result_exponent = -25; result_exponent <= 1; ++result_exponent)
    for (U32 n : mantissas)
      for (U32 d : {0u, 1u, 0x7fffffu})
        signed_cases(0x00800000u | n, (U32(128 - result_exponent) << 23) | d);
  if (cases.size() != 1123224)
    throw std::logic_error("Division fixture row count changed");
  return cases;
}
} // namespace quotient_test
