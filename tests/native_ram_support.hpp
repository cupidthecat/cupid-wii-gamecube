// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/cpu/interpreter.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <span>
#include <vector>

using namespace cupid;
using namespace cupid::cpu;

struct MixedMemory final : Bus {
  std::vector<u8> bytes = std::vector<u8>(65536);
  std::vector<std::array<u64, 4>> records;
  bool view = true, plain = true;
  unsigned view_size = 65536;

  u8 read8(u32 address) override {
    if (address >= bytes.size())
      throw BusFault(address, false, "Native RAM read bounds");
    return bytes[address];
  }

  void write8(u32 address, u8 value) override {
    if (address >= bytes.size())
      throw BusFault(address, true, "Native RAM write bounds");
    bytes[address] = value;
  }

  u32 instruction_cache_key(u32 address) const override {
    return address;
  }

  bool stable_instruction_cache_key(u32 address, u32 &key) const override {
    key = address;
    return !(address & 3) && address < bytes.size();
  }

  bool read_instruction_words(u32 address, std::span<u32> words) override {
    records.push_back({0, address, words.size(), 0});
    return Bus::read_instruction_words(address, words);
  }

  bool read_instruction_bytes(u32 address, std::span<u8> words) override {
    records.push_back({1, address, words.size(), 0});
    return Bus::read_instruction_bytes(address, words);
  }

  std::span<u8> plain_cpu_ram(u32 address, unsigned count) override {
    if (!plain || address >= bytes.size() || count > bytes.size() - address ||
        (address & 4095) + count > 4096)
      return {};
    return std::span(bytes).subspan(address, count);
  }

  PlainCpuRamView plain_cpu_ram_view() override {
    return view && plain ? PlainCpuRamView{0, std::span(bytes).first(view_size)}
                         : PlainCpuRamView{};
  }
};

inline u32 dform(unsigned operation, unsigned destination, unsigned address, unsigned value = 0) {
  return (operation << 26) | (destination << 21) | (address << 16) | (value & 65535);
}

inline u32 xform(unsigned operation, unsigned destination, unsigned a, unsigned b,
                 bool rc = false) {
  return (31u << 26) | (destination << 21) | (a << 16) | (b << 11) | (operation << 1) | u32(rc);
}

inline State mixed_seed(unsigned salt = 0) {
  State state;
  for (unsigned n = 0; n < state.gpr.size(); ++n)
    state.gpr[n] = (0x87654321u + n * 0x9e3779b9u) ^ salt;
  for (unsigned n = 0; n < state.fpr.size(); ++n)
    state.fpr[n].bits = {0x7ff123456789abcdull ^ n, 0x8123456789abcdefull ^ n};
  for (unsigned n = 0; n < state.spr.size(); ++n)
    state.spr[n] = (0xa1234567u + n * 0x12345u) ^ salt;
  for (unsigned n = 0; n < state.sr.size(); ++n)
    state.sr[n] = (n * 0x9876543u) ^ salt;
  for (unsigned n = 528; n < 576; ++n)
    state.spr[n] = 0;
  state.spr[spr::hid0] = state.spr[spr::hid4] = state.spr[spr::mmcr0] = state.spr[spr::mmcr1] = 0;
  state.spr[spr::dec] = 0xffffffff;
  state.gpr[3] = 0x2000;
  state.gpr[7] = 0x2800;
  state.pc = 0x1000;
  state.msr = 0;
  state.cr = 0x89abcdef ^ salt;
  state.fpscr = 0x12345678 ^ salt;
  state.last_pc = 0x9000;
  state.last_opcode = 0x55667788;
  state.cycles = 0x123456789;
  state.instructions = 0x23456789;
  state.timebase = 0xfffffffffffffff0ull;
  return state;
}

inline void mixed_equal(const State &a, const State &b) {
  CHECK(a.gpr == b.gpr && a.spr == b.spr && a.sr == b.sr);
  for (unsigned n = 0; n < a.fpr.size(); ++n)
    CHECK(a.fpr[n].bits == b.fpr[n].bits);
  CHECK(a.pc == b.pc && a.msr == b.msr && a.cr == b.cr && a.fpscr == b.fpscr);
  CHECK(a.last_pc == b.last_pc && a.last_opcode == b.last_opcode);
  CHECK(a.cycles == b.cycles && a.instructions == b.instructions && a.timebase == b.timebase);
}
