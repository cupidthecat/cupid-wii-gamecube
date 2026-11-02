// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/types.hpp"
#include <bit>
#include <cmath>

namespace cupid::cpu::floating_predicates {
// Avoid strict-mode CRT calls on MSVC x64 without evaluating the operand.
// Other compiler targets retain their standard predicate behavior.
#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__)
inline bool isnan(double value) noexcept {
  return (std::bit_cast<u64>(value) & 0x7fffffffffffffffull) > 0x7ff0000000000000ull;
}

inline bool isnan(float value) noexcept {
  return (std::bit_cast<u32>(value) & 0x7fffffffu) > 0x7f800000u;
}

inline bool isinf(double value) noexcept {
  return (std::bit_cast<u64>(value) & 0x7fffffffffffffffull) == 0x7ff0000000000000ull;
}

inline bool isinf(float value) noexcept {
  return (std::bit_cast<u32>(value) & 0x7fffffffu) == 0x7f800000u;
}

inline bool isfinite(double value) noexcept {
  return (std::bit_cast<u64>(value) & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
}

inline bool isfinite(float value) noexcept {
  return (std::bit_cast<u32>(value) & 0x7f800000u) != 0x7f800000u;
}
#else
using std::isfinite;
using std::isinf;
using std::isnan;
#endif
} // namespace cupid::cpu::floating_predicates
