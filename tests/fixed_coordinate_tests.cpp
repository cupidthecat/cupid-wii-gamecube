// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/gfx/fixed_coordinate.hpp"
#include "cupid/gfx/fragment.hpp"
#include "test_support.hpp"
#include <bit>
#include <cfenv>
#include <iostream>
#include <limits>
#include <vector>
#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

namespace {
using namespace cupid;
u64 comparisons = 0, environments = 0, shader_cases = 0;

// Retained pre-optimization algorithm, independent of the guarded conversion.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
s32 previous(float value) {
  if (!std::isfinite(value))
    throw gfx::VertexError("Non-finite texture coordinate");
  const double truncated = std::trunc(static_cast<double>(value) * 128);
  const double modulo = std::fmod(truncated, 16777216.0);
  const auto bits = static_cast<u32>(static_cast<s64>(modulo)) & 0xffffff;
  return static_cast<s32>(bits ^ 0x800000) - 0x800000;
}

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
s32 current(float value) {
  return gfx::detail::fixed_coordinate(value);
}

struct Result {
  s32 value = 0;
  bool rejected = false;
  bool operator==(const Result &) const = default;
};

Result invoke(s32 (*operation)(float), float value) {
  try {
    return {operation(value), false};
  } catch (const gfx::VertexError &) {
    return {0, true};
  }
}

std::vector<u32> edges() {
  std::vector<u32> values{0,          0x80000000, 1,          0x80000001, 0x007fffff,
                          0x807fffff, 0x00800000, 0x80800000, 0x7f7fffff, 0xff7fffff,
                          0x7f800000, 0xff800000, 0x7fc12345, 0xffc54321, 0x7f800001};
  for (float center : {0.0078125f, 1.0f, 65536.0f, 131072.0f, 16777216.0f})
    for (float sign : {1.0f, -1.0f}) {
      const u32 bits = std::bit_cast<u32>(center * sign);
      for (s32 offset = -8; offset <= 8; ++offset)
        values.push_back(bits + static_cast<u32>(offset));
    }
  for (u32 exponent = 0; exponent < 256; ++exponent)
    for (u32 mantissa : {0u, 1u, 0x12345u, 0x3fffffu, 0x7ffffeu, 0x7fffffu})
      for (u32 sign : {0u, 0x80000000u})
        values.push_back(sign | (exponent << 23) | mantissa);
  return values;
}

void differential(const std::vector<u32> &values) {
  for (u32 bits : values) {
    const float value = std::bit_cast<float>(bits);
    CHECK(invoke(previous, value) == invoke(current, value));
    ++comparisons;
  }
  u32 bits = 0x1429ac73;
  for (unsigned n = 0; n < 1048576; ++n) {
    bits += 0x9e3779b9;
    const float value = std::bit_cast<float>(bits);
    CHECK(invoke(previous, value) == invoke(current, value));
    ++comparisons;
  }
  // Independent witnesses at signed-24, modular and signed-conversion seams.
  CHECK(current(65536.0f) == -8388608);
  CHECK(current(-65536.0f) == -8388608);
  CHECK(current(131072.0f) == 0 && current(-131072.0f) == 0);
  CHECK(current(16777216.0f) == 0 && current(-16777216.0f) == 0);
  CHECK(current(-0.01171875f) == -1 && current(0.01171875f) == 1);
}

void environment(const std::vector<u32> &values) {
  for (int rounding : {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD})
    for (unsigned flush = 0; flush < 4; ++flush)
      for (bool sticky : {false, true})
        for (u32 bits : values) {
          const auto setup = [&] {
            CHECK(std::fesetenv(FE_DFL_ENV) == 0);
            CHECK(std::fesetround(rounding) == 0);
            if (sticky)
              CHECK(std::feraiseexcept(FE_DIVBYZERO | FE_INVALID) == 0);
#if defined(_M_X64) || defined(__x86_64__)
            _mm_setcsr((_mm_getcsr() & ~0x8040u) | (flush & 1 ? 0x8000u : 0u) |
                       (flush & 2 ? 0x40u : 0u));
#endif
          };
          setup();
          const auto expected = invoke(previous, std::bit_cast<float>(bits));
          const int flags = std::fetestexcept(FE_ALL_EXCEPT);
#if defined(_M_X64) || defined(__x86_64__)
          const unsigned simd = _mm_getcsr();
#endif
          setup();
          const auto actual = invoke(current, std::bit_cast<float>(bits));
          if (!(actual == expected && flags == std::fetestexcept(FE_ALL_EXCEPT)))
            std::cerr << "bits=" << std::hex << bits << std::dec << " rounding=" << rounding
                      << " flush=" << flush << " sticky=" << sticky
                      << " expected=" << expected.value << " actual=" << actual.value
                      << " flags=" << flags << "/" << std::fetestexcept(FE_ALL_EXCEPT) << '\n';
          CHECK(actual == expected && flags == std::fetestexcept(FE_ALL_EXCEPT));
          CHECK(std::fegetround() == rounding);
#if defined(_M_X64) || defined(__x86_64__)
          CHECK(simd == _mm_getcsr());
#endif
          ++environments;
        }
  CHECK(std::fesetenv(FE_DFL_ENV) == 0);
}

void shader(const std::vector<u32> &values) {
  Memory memory(Console::GameCube);
  std::array<u32, 256> bp{};
  bp[0] = 1;
  bp[0xf3] = (7 << 16) | (7 << 19);
  gfx::TevRegisters colors{}, konst{};
  std::vector<u8> tmem(1024 * 1024);
  gfx::FragmentShader fragment(memory, bp, colors, konst, tmem);
  gfx::FragmentInputs input;
  for (u32 bits : values) {
    const float value = std::bit_cast<float>(bits);
    if (!std::isfinite(value))
      continue;
    input.uv[0] = {value, -value};
    (void)fragment.shade(input);
    const auto state = fragment.texture_state();
    CHECK(state.coordinates[0] == previous(value));
    CHECK(state.coordinates[1] == previous(-value));
    ++shader_cases;
  }
}
} // namespace

int main() {
  std::fenv_t saved;
  if (std::fegetenv(&saved))
    return 1;
#if defined(_M_X64) || defined(__x86_64__)
  const unsigned saved_simd = _mm_getcsr();
#endif
  const int result = run_tests([] {
    CHECK(std::fesetenv(FE_DFL_ENV) == 0);
    const auto values = edges();
    differential(values);
    environment(values);
    shader(values);
    std::cout << "Coordinate comparisons=" << comparisons << " environments=" << environments
              << " shader state cases=" << shader_cases << '\n';
  });
  (void)std::fesetenv(&saved);
#if defined(_M_X64) || defined(__x86_64__)
  _mm_setcsr(saved_simd);
#endif
  return result;
}
