// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in single FMA comparison using the pinned behavioral reference headers.
// Test-only host detection and unused cache destruction avoid unrelated runtime setup.
#include "Core/PowerPC/Interpreter/Interpreter_FPUtils.h"
#include "cupid/cpu/floating.hpp"
#include <cfenv>
#include <iostream>

CPUInfo::CPUInfo() {
}

CPUInfo cpu_info;
PowerPC::InstructionCache::~InstructionCache() = default;

int main() {
  PowerPC::PowerPCState state{};
  unsigned long long count = 0;
  unsigned long long random = 0x9328310298bc3827ull;
  const auto compare = [&](double a, double c, double b) {
    for (bool sub : {false, true}) {
      const double expected =
          sub ? NI_msub<true>(state, a, c, b).value : NI_madd<true>(state, a, c, b).value;
      const double actual = cupid::cpu::single_fused_intermediate(a, Force25Bit(c), b, sub);
      const auto e = std::bit_cast<u32>(ForceSingle(state.fpscr, expected));
      const auto v = std::bit_cast<u32>(ForceSingle(state.fpscr, actual));
      ++count;
      if (e != v) {
        std::cerr << std::hex << "single FMA divergence a=" << std::bit_cast<u64>(a)
                  << " c=" << std::bit_cast<u64>(c) << " b=" << std::bit_cast<u64>(b)
                  << " sub=" << sub << " actual=" << v << " expected=" << e << '\n';
        return false;
      }
    }
    return true;
  };
  for (int mode : {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD}) {
    std::fesetround(mode);
    for (unsigned n = 0; n < 100000; ++n) {
      const auto next = [&] {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        return random;
      };
      double a = std::bit_cast<double>(next()), c = std::bit_cast<double>(next()),
             b = std::bit_cast<double>(next());
      if (std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && !compare(a, c, b))
        return 1;
      a = static_cast<double>(std::bit_cast<float>(static_cast<u32>(next())));
      c = static_cast<double>(std::bit_cast<float>(static_cast<u32>(next())));
      b = static_cast<double>(std::bit_cast<float>(static_cast<u32>(next())));
      if (std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && !compare(a, c, b))
        return 1;
    }
  }
  std::cout << "Passed " << count << " single FMA comparisons across four rounding modes\n";
}
