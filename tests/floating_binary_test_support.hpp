// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/cpu/interpreter.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace floating_binary_test {
using namespace cupid;
using namespace cupid::cpu;

inline void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

constexpr std::array<u64, 20> literals{0,
                                       0x8000000000000000ull,
                                       1,
                                       0x8000000000000001ull,
                                       0x000fffffffffffffull,
                                       0x800fffffffffffffull,
                                       0x0010000000000000ull,
                                       0x8010000000000000ull,
                                       0x3ff0000000000000ull,
                                       0xbff0000000000000ull,
                                       0x7fefffffffffffffull,
                                       0xffefffffffffffffull,
                                       0x7ff0000000000000ull,
                                       0xfff0000000000000ull,
                                       0x7ff0000000000001ull,
                                       0xfff0000000001234ull,
                                       0x7ff123456789abcdull,
                                       0xfff7654321abcde0ull,
                                       0x7ff8000000000001ull,
                                       0xfffedcba98765432ull};

struct Operation {
  unsigned primary, xo;
};

constexpr std::array<Operation, 12> operations{{{63, 40},
                                                {63, 72},
                                                {63, 136},
                                                {63, 264},
                                                {4, 40},
                                                {4, 72},
                                                {4, 136},
                                                {4, 264},
                                                {4, 528},
                                                {4, 560},
                                                {4, 592},
                                                {4, 624}}};

inline u32 opcode(Operation op, unsigned d, unsigned a, unsigned b, bool rc) {
  return (op.primary << 26) | (d << 21) | (a << 16) | (b << 11) | (op.xo << 1) | u32(rc);
}

inline u64 transform(u64 value, unsigned xo) {
  if (xo == 40)
    return value ^ 0x8000000000000000ull;
  if (xo == 136)
    return value | 0x8000000000000000ull;
  if (xo == 264)
    return value & 0x7fffffffffffffffull;
  return value;
}

inline void expected_binary(State &out, const State &before, Operation op, unsigned d, unsigned a,
                            unsigned b, bool rc) {
  if (op.xo >= 528) {
    out.fpr[d].bits = {before.fpr[a].bits[(op.xo - 528) / 64],
                       before.fpr[b].bits[((op.xo - 528) / 32) & 1]};
  } else {
    out.fpr[d].bits[0] = transform(before.fpr[b].bits[0], op.xo);
    if (op.primary == 4)
      out.fpr[d].bits[1] = transform(before.fpr[b].bits[1], op.xo);
  }
  if (rc)
    out.cr = (before.cr & ~0x0f000000u) | ((before.fpscr >> 28) << 24);
}

inline void equal(const State &a, const State &b) {
  require(a.gpr == b.gpr && a.spr == b.spr && a.sr == b.sr, "integer/SPR/SR state differs");
  for (unsigned n = 0; n < 32; ++n)
    require(a.fpr[n].bits == b.fpr[n].bits, "raw FPR lane differs");
  require(a.pc == b.pc && a.msr == b.msr && a.cr == b.cr && a.fpscr == b.fpscr,
          "PC/MSR/CR/FPSCR differs");
  require(a.last_pc == b.last_pc && a.last_opcode == b.last_opcode, "last fetch differs");
  require(a.cycles == b.cycles && a.instructions == b.instructions && a.timebase == b.timebase,
          "architectural counters differ");
}

inline u64 state_hash(const State &s) {
  u64 h = 14695981039346656037ull;
  const auto word = [&](u64 value, unsigned width) {
    for (unsigned n = 0; n < width; ++n) {
      h ^= u8(value >> (n * 8));
      h *= 1099511628211ull;
    }
  };
  for (u32 x : s.gpr)
    word(x, 4);
  for (const auto &f : s.fpr)
    for (u64 x : f.bits)
      word(x, 8);
  for (u32 x : s.spr)
    word(x, 4);
  for (u32 x : s.sr)
    word(x, 4);
  for (u32 x : {s.pc, s.msr, s.cr, s.fpscr, s.last_pc, s.last_opcode})
    word(x, 4);
  for (u64 x : {s.cycles, s.instructions, s.timebase})
    word(x, 8);
  return h;
}

class CodeBus final : public Bus {
public:
  std::array<u8, 8192> bytes{};
  std::array<u32, 8> fetches{};
  unsigned fetch_count = 0;
  bool record = false;

  u8 read8(u32 address) override {
    require(address < bytes.size(), "instruction read out of range");
    if (record) {
      require(fetch_count < fetches.size(), "unexpected extra bus read");
      fetches[fetch_count++] = address;
    }
    return bytes[address];
  }

  void write8(u32 address, u8 value) override {
    require(!record, "guest data store during register operation");
    require(address < bytes.size(), "host code write out of range");
    bytes[address] = value;
  }
};

#if defined(_M_X64) || defined(__x86_64__)
#ifdef _MSC_VER
extern "C" unsigned fp_x87_control();
extern "C" unsigned fp_x87_status();
extern "C" void fp_x87_set_control(unsigned);
extern "C" void fp_x87_set_flags(unsigned);
#else
inline unsigned fp_x87_control() {
  unsigned short value;
  asm volatile("fnstcw %0" : "=m"(value));
  return value;
}

inline unsigned fp_x87_status() {
  unsigned short value;
  asm volatile("fnstsw %0" : "=am"(value));
  return value;
}

inline void fp_x87_set_control(unsigned value) {
  const auto v = static_cast<unsigned short>(value);
  asm volatile("fldcw %0" ::"m"(v));
}

inline void fp_x87_set_flags(unsigned flags) {
  alignas(16) std::array<u8, 32> env{};
  asm volatile("fnstenv %0" : "=m"(env));
  env[4] = u8((env[4] & ~0x3fu) | (flags & 0x3f));
  asm volatile("fldenv %0" ::"m"(env));
}
#endif
struct HostSnapshot {
  unsigned control, status, simd;
  int rounding, flags;

  static HostSnapshot capture() {
    return {fp_x87_control(), fp_x87_status(), _mm_getcsr(), std::fegetround(),
            std::fetestexcept(FE_ALL_EXCEPT)};
  }

  bool operator==(const HostSnapshot &) const = default;
};

class HostEnvironment {
public:
  HostEnvironment() {
    require(std::fegetenv(&saved) == 0, "host environment save failed");
    simd = _mm_getcsr();
  }

  ~HostEnvironment() {
    std::fesetenv(&saved);
    _mm_setcsr(simd);
  }

private:
  std::fenv_t saved{};
  unsigned simd = 0;
};

inline void host_controls(unsigned x87, unsigned simd, unsigned flush, unsigned status) {
  require(std::fesetenv(FE_DFL_ENV) == 0, "host default environment failed");
  constexpr unsigned x87_modes[] = {0, 0xc00, 0x800, 0x400};
  constexpr unsigned simd_modes[] = {0, 0x6000, 0x4000, 0x2000};
  // Zero-flag cases also exercise unmasked controls. Sticky cases retain masks so
  // the fixture itself cannot fault before reaching the actual handler.
  const bool unmasked = status == 1;
  const unsigned flags = status == 2 ? 0x05u : status == 3 ? 0x22u : 0u;
  fp_x87_set_control((fp_x87_control() & ~0xc3fu) | x87_modes[x87] | (unmasked ? 0u : 0x3fu));
  fp_x87_set_flags(flags);
  _mm_setcsr(simd_modes[simd] | flush | (unmasked ? 0u : 0x1f80u) | flags);
}
#endif

inline State patterned(Console console) {
  State s;
  for (unsigned n = 0; n < 32; ++n) {
    s.gpr[n] = 0x12345678u + n * 0x1020304u;
    s.fpr[n].bits = {literals[n % 20], literals[(n + 7) % 20]};
  }
  for (unsigned n = 0; n < 1024; ++n)
    s.spr[n] = 0x3a970000u ^ n * 0x10203u;
  for (unsigned n = 0; n < 16; ++n)
    s.sr[n] = 0x1234ab00u + n;
  s.spr[spr::pvr] = console == Console::Wii ? 0x00087102 : 0x00083214;
  s.spr[spr::hid0] = 0;
  s.spr[spr::hid2] = 0xa0000000;
  s.spr[spr::dec] = 0xffffffff;
  s.spr[spr::mmcr0] = s.spr[spr::mmcr1] = 0;
  s.pc = 0x1000;
  s.msr = msr::fp | msr::ri | 0x900;
  s.cr = 0x6a59c3e7;
  s.fpscr = 0xd40b5af0;
  return s;
}
} // namespace floating_binary_test
