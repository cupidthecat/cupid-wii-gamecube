// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in arithmetic comparison using the pinned behavioral reference helpers.
#include "Common/FPURoundMode.h"
#include "Core/PowerPC/Interpreter/Interpreter_FPUtils.h"
#include "cupid/core/memory.hpp"
#include "cupid/cpu/interpreter.hpp"
#include <array>
#include <iostream>

// Only the actual arithmetic and host rounding helpers are used by this driver.
CPUInfo::CPUInfo() {
}

CPUInfo cpu_info;
PowerPC::InstructionCache::~InstructionCache() = default;

namespace {
double arithmetic(PowerPC::PowerPCState &state, unsigned operation, bool single, double a, double b,
                  double c) {
  switch (operation) {
  case 18:
    return NI_div(state, a, b).value;
  case 20:
    return NI_sub(state, a, b).value;
  case 21:
    return NI_add(state, a, b).value;
  case 25:
    return NI_mul(state, a, single ? Force25Bit(c) : c).value;
  case 28:
  case 30:
    return single ? NI_msub<true>(state, a, c, b).value : NI_msub<false>(state, a, c, b).value;
  case 29:
  case 31:
    return single ? NI_madd<true>(state, a, c, b).value : NI_madd<false>(state, a, c, b).value;
  default:
    throw std::logic_error("Invalid comparison operation");
  }
}
} // namespace

int main() {
  cpu_info.bFlushToZero = true;
  PowerPC::PowerPCState reference{};
  cupid::Memory memory(cupid::Console::GameCube);
  cupid::cpu::Interpreter cpu(memory);
  auto &state = cpu.state();
  state.spr[1008] = 0; // Freshly authored opcodes must be fetched from memory each time.
  u64 compared = 0;
  const auto compare = [&](u64 a_bits, u64 b_bits, u64 c_bits) {
    const double a = std::bit_cast<double>(a_bits), b = std::bit_cast<double>(b_bits),
                 c = std::bit_cast<double>(c_bits);
    for (unsigned mode = 0; mode < 4; ++mode) {
      for (const bool ni : {false, true}) {
        for (const bool single : {false, true}) {
          for (const unsigned operation : {18u, 20u, 21u, 25u, 28u, 29u, 30u, 31u}) {
            const u32 fpscr = mode | (ni ? 4u : 0u);
            reference.fpscr.Hex = fpscr;
            Common::FPU::SetSIMDMode(static_cast<Common::FPU::RoundMode>(mode), ni);
            const double intermediate = arithmetic(reference, operation, single, a, b, c);
            double expected = single
                                  ? static_cast<double>(ForceSingle(reference.fpscr, intermediate))
                                  : ForceDouble(reference.fpscr, intermediate);
            if (operation >= 30 && !std::isnan(expected))
              expected = -expected;
            const u64 expected_bits = std::bit_cast<u64>(expected);
            Common::FPU::LoadDefaultSIMDState();

            state.pc = 0x1000;
            state.msr = cupid::cpu::msr::fp;
            state.fpscr = fpscr;
            state.fpr[2].bits[0] = a_bits;
            state.fpr[3].bits[0] = b_bits;
            state.fpr[4].bits[0] = c_bits;
            state.fpr[1].bits[1] = 0x3ff8000000000000ULL;
            const u32 opcode = ((single ? 59u : 63u) << 26) | (1u << 21) | (2u << 16) | (3u << 11) |
                               (4u << 6) | (operation << 1);
            memory.write32(0x1000, opcode);
            cpu.step();
            ++compared;
            if (state.fpr[1].bits[0] != expected_bits ||
                state.fpr[1].bits[1] != (single ? expected_bits : 0x3ff8000000000000ULL)) {
              std::cerr << std::hex << "arithmetic divergence op=" << operation
                        << " single=" << single << " mode=" << mode << " NI=" << ni
                        << " a=" << a_bits << " b=" << b_bits << " c=" << c_bits
                        << " observed=" << state.fpr[1].bits[0] << " expected=" << expected_bits
                        << '\n';
              return false;
            }
          }
        }
      }
    }
    return true;
  };

  // Exhaustive combinations include subnormal inputs, cancellation, signed zeros,
  // both normal thresholds, infinities and NaN payloads.
  constexpr std::array<u64, 20> edges{0,
                                      0x8000000000000000ULL,
                                      1,
                                      0x8000000000000001ULL,
                                      0x000fffffffffffffULL,
                                      0x0010000000000000ULL,
                                      0x8010000000000000ULL,
                                      0x0010000000000001ULL,
                                      0x001ffffffffffffeULL,
                                      0x380fffffffffffffULL,
                                      0x3810000000000000ULL,
                                      0x3fefffffffffffffULL,
                                      0x3ff0000000000000ULL,
                                      0xbff0000000000000ULL,
                                      0x3fffffffffffffffULL,
                                      0x7fe0000000000000ULL,
                                      0x7fefffffffffffffULL,
                                      0x7ff0000000000000ULL,
                                      0x7ff8000000001234ULL,
                                      0xfff0000000005678ULL};
  for (const u64 a : edges)
    for (const u64 b : edges)
      for (const u64 c : edges)
        if (!compare(a, b, c))
          return 1;

  for (u64 offset = 0; offset < 256; ++offset) {
    for (const u64 sign : {u64{0}, u64{1} << 63}) {
      if (!compare(sign | (0x0010000000000000ULL + offset), offset,
                   0x3fefffffffffffffULL - offset) ||
          !compare(sign | (0x001ffffffffffffeULL - offset), 0x3fffffffffffffffULL - offset,
                   0x3ff0000000000000ULL))
        return 1;
    }
  }

  u64 random = 0x4fb4926c58a12fe3ULL;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 7;
    random ^= random << 17;
    return random;
  };
  for (unsigned n = 0; n < 10000; ++n) {
    const u64 a = next(), b = next(), c = next();
    if (!compare(a, b, c))
      return 1;
  }
  std::cout << "Passed " << compared
            << " scalar arithmetic results and destination lanes across four rounding modes, "
               "with NI off and on\n";
}
