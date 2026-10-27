// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in helper differential check. Compile against the pinned behavioral reference.
#include "Common/FloatUtils.h"
#include "cupid/cpu/floating.hpp"
#include <bit>
#include <iostream>

int main() {
  unsigned long long checks = 0;
  const auto compare = [&](cupid::u64 bits) {
    const double input = std::bit_cast<double>(bits);
    const cupid::u64 reciprocal = std::bit_cast<cupid::u64>(cupid::cpu::reciprocal_estimate(input));
    const cupid::u64 expected_reciprocal =
        std::bit_cast<cupid::u64>(Common::ApproximateReciprocal(input));
    const cupid::u64 root =
        std::bit_cast<cupid::u64>(cupid::cpu::reciprocal_square_root_estimate(input));
    const cupid::u64 expected_root =
        std::bit_cast<cupid::u64>(Common::ApproximateReciprocalSquareRoot(input));
    checks += 2;
    if (reciprocal != expected_reciprocal || root != expected_root) {
      std::cerr << std::hex << "First helper divergence input=" << bits
                << " reciprocal=" << reciprocal << " expected=" << expected_reciprocal
                << " square_root=" << root << " expected=" << expected_root << '\n';
      return false;
    }
    return true;
  };
  for (cupid::u64 bits :
       {0ull, 1ull, 0x000fffffffffffffull, 0x0010000000000000ull, 0x37efffffffffffffull,
        0x37f0000000000000ull, 0x47cfffffffffffffull, 0x47d0000000000000ull, 0x7fefffffffffffffull,
        0x7ff0000000000000ull, 0x7ff0000000000001ull, 0x7ff8000000000000ull, 0x8000000000000000ull,
        0x8000000000000001ull, 0xfff0000000000000ull, 0xfff123456789abcdull})
    if (!compare(bits))
      return 1;
  for (unsigned exponent : {1u, 894u, 895u, 1022u, 1023u, 1024u, 1148u, 1149u, 2046u})
    for (unsigned mantissa = 0; mantissa < 32768; ++mantissa) {
      const cupid::u64 input =
          (static_cast<cupid::u64>(exponent) << 52) | (static_cast<cupid::u64>(mantissa) << 37);
      if (!compare(input) || !compare(input | 0x1fffffffffull) ||
          !compare(input | 0x8000000000000000ull))
        return 1;
    }
  cupid::u64 random = 0x91c84291d53e729bull;
  for (unsigned n = 0; n < 100000; ++n) {
    random ^= random << 13;
    random ^= random >> 7;
    random ^= random << 17;
    if (!compare(random))
      return 1;
  }
  std::cout << "Passed " << checks << " helper comparisons against pinned behavioral reference\n";
}
