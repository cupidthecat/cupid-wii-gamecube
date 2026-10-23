// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "quotient_oracle.hpp"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace quotient_test {
struct Case {
  std::uint32_t numerator, denominator, seed, expected, flags;
};

inline std::vector<Case> division_cases() {
  using U32 = std::uint32_t;
  // Literal expectations independently guard the integer oracle's boundaries.
  constexpr std::array<std::array<U32, 3>, 29> hand{{
      {0x00000001, 0x40000000, 0x00000000}, {0x00000003, 0x40000000, 0x00000002},
      {0x00000005, 0x40000000, 0x00000002}, {0x00000007, 0x40000000, 0x00000004},
      {0x00fffffd, 0x40000000, 0x007ffffe}, {0x00ffffff, 0x40000000, 0x00800000},
      {0x00800001, 0x40000000, 0x00400000}, {0x00800003, 0x40000000, 0x00400002},
      {0x3f800000, 0x40400000, 0x3eaaaaab}, {0x40000000, 0x40400000, 0x3f2aaaab},
      {0x3f800000, 0x41200000, 0x3dcccccd}, {0x41200000, 0x40400000, 0x40555555},
      {0x00800000, 0x40400000, 0x002aaaab}, {0x7f7fffff, 0x40000000, 0x7effffff},
      {0x7f7fffff, 0x3f000000, 0x7f800000}, {0x7f7fffff, 0x3f7fffff, 0x7f800000},
      {0x7f7ffffe, 0x3f7fffff, 0x7f7fffff}, {0x4b7fffff, 0x4b7ffffe, 0x3f800001},
      {0x40800000, 0x40000000, 0x40000000}, {0x00000003, 0x3f800000, 0x00000003},
      {0x00000003, 0x00000003, 0x3f800000}, {0x00000000, 0x40000000, 0x00000000},
      {0x40000000, 0x00000000, 0x7f800000}, {0x7f800000, 0x40000000, 0x7f800000},
      {0x40000000, 0x7f800000, 0x00000000}, {0x00000000, 0x00000000, 0x7fc00000},
      {0x7f800000, 0x7f800000, 0x7fc00000}, {0x7fc12345, 0x40000000, 0x7fc00000},
      {0x40000000, 0x7f800001, 0x7fc00000},
  }};
  constexpr std::array<U32, 32> edges{
      0,          1,          2,          3,          4,          5,          7,
      0x003fffff, 0x00400000, 0x00400001, 0x007ffffd, 0x007ffffe, 0x007fffff, 0x00800000,
      0x00800001, 0x00800003, 0x00fffffd, 0x00ffffff, 0x01000000, 0x3eaaaaab, 0x3f000000,
      0x3f7fffff, 0x3f800000, 0x3f800001, 0x40000000, 0x40400000, 0x41200000, 0x4b7fffff,
      0x7f7ffffe, 0x7f7fffff, 0x7f800000, 0x7fc00001};
  constexpr auto route_flags = [](U32 n, U32 d) -> U32 {
    n &= 0x7fffffff;
    d &= 0x7fffffff;
    if (n == 0 || d == 0 || n >= 0x7f800000 || d >= 0x7f800000)
      return 2;
    return d == 0x3f800000 || n == d ? 1 : 0;
  };
  constexpr auto random_word = [](U32 word) {
    word ^= word << 13;
    word ^= word >> 17;
    return word ^ (word << 5);
  };
  std::vector<Case> cases;
  cases.reserve(71796);
  const auto add = [&](U32 n, U32 d, U32 extra_flags) {
    cases.push_back({n, d, 0, rounded_quotient(n, d), route_flags(n, d) | extra_flags});
  };
  for (const auto &literal : hand)
    for (U32 signs = 0; signs < 4; ++signs) {
      const U32 ns = (signs & 1) << 31, ds = (signs & 2) << 30;
      add(literal[0] ^ ns, literal[1] ^ ds, 8);
      const U32 expected = literal[2] == 0x7fc00000 ? literal[2] : literal[2] ^ ns ^ ds;
      if (cases.back().expected != expected)
        throw std::logic_error("Integer division oracle failed a literal boundary case");
    }
  for (U32 n : edges)
    for (U32 d : edges)
      for (U32 signs = 0; signs < 4; ++signs)
        add(n ^ ((signs & 1) << 31), d ^ ((signs & 2) << 30), 0);
  U32 random = 0xc0a71d35;
  for (unsigned i = 0; i < 65536; ++i) {
    const U32 n = random = random_word(random);
    random = random_word(random);
    add(n, random, 0);
  }
  // Force every estimator class, plus estimates just inside/outside the eight
  // adjacent-correction limit. Bypass/special cases cannot dilute this subset.
  std::set<std::pair<U32, U32>> seen;
  std::vector<Case> forced_pairs;
  for (const auto &test : cases)
    if ((test.flags & 3) == 0 && seen.emplace(test.numerator, test.denominator).second) {
      forced_pairs.push_back(test);
      if (forced_pairs.size() == 128)
        break;
    }
  if (forced_pairs.size() != 128)
    throw std::logic_error("Division fixture has too few unequal finite pairs");
  constexpr std::array<U32, 10> seeds{0,          1,          0x007fffff, 0x00800000, 0x3f800000,
                                      0x7f7fffff, 0x7f800000, 0x7fc00000, 0x7f800001, 0xffffffff};
  for (auto test : forced_pairs) {
    test.flags = 4;
    for (U32 seed : seeds) {
      test.seed = seed;
      cases.push_back(test);
    }
    for (int offset : {-9, -8, -1, 1, 8, 9}) {
      const auto value = std::int64_t(test.expected & 0x7fffffff) + offset;
      test.seed = static_cast<U32>(std::clamp<std::int64_t>(value, 0, 0x7f800000));
      cases.push_back(test);
    }
  }
  if (cases.size() != 71796)
    throw std::logic_error("Division fixture row count changed");
  return cases;
}
} // namespace quotient_test
