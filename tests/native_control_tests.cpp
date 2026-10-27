// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/interpreter.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <span>
#include <vector>

using namespace cupid;
using namespace cupid::cpu;

namespace {
u64 comparisons = 0, retirements = 0;

class RecordedMemory final : public Bus {
public:
  std::vector<u8> bytes = std::vector<u8>(1024 * 1024);
  u64 reads = 0, writes = 0;

  u8 read8(u32 address) override {
    ++reads;
    return bytes[address & 65535];
  }

  void write8(u32 address, u8 value) override {
    ++writes;
    bytes[address & 65535] = value;
  }
};

constexpr u32 immediate(unsigned primary, unsigned destination, unsigned source, unsigned value) {
  return (primary << 26) | (destination << 21) | (source << 16) | (value & 65535);
}

constexpr u32 extended(unsigned operation, unsigned destination, unsigned left, unsigned right,
                       bool record, bool overflow = false) {
  return (31u << 26) | (destination << 21) | (left << 16) | (right << 11) | (operation << 1) |
         (overflow ? 0x400u : 0) | static_cast<u32>(record);
}

constexpr u32 branch(unsigned bo, unsigned bi, s32 displacement, bool link = false,
                     bool absolute = false) {
  return (16u << 26) | (bo << 21) | (bi << 16) | (static_cast<u32>(displacement) & 0xfffc) |
         (absolute ? 2u : 0) | static_cast<u32>(link);
}

void equal_state(const State &a, const State &b) {
  CHECK(a.gpr == b.gpr && a.spr == b.spr && a.sr == b.sr);
  for (unsigned i = 0; i < a.fpr.size(); ++i)
    CHECK(a.fpr[i].bits == b.fpr[i].bits);
  CHECK(a.pc == b.pc && a.msr == b.msr && a.cr == b.cr && a.fpscr == b.fpscr);
  CHECK(a.last_pc == b.last_pc && a.last_opcode == b.last_opcode);
  CHECK(a.cycles == b.cycles && a.instructions == b.instructions && a.timebase == b.timebase);
}

State seed(unsigned salt) {
  State state;
  for (unsigned i = 0; i < state.gpr.size(); ++i)
    state.gpr[i] = (0x87654321u + i * 0x9e3779b9u) ^ salt;
  for (unsigned i = 0; i < state.fpr.size(); ++i)
    state.fpr[i].bits = {0x7ff0000000000001ull ^ (u64{salt} << 32) ^ i, 0x8123456789abcdefull ^ i};
  for (unsigned i = 0; i < state.spr.size(); ++i)
    state.spr[i] = (0xa1234567u + i * 0x12345u) ^ salt;
  for (unsigned i = 0; i < state.sr.size(); ++i)
    state.sr[i] = 0xb2345678u ^ i;
  state.spr[spr::hid0] = state.spr[spr::mmcr0] = state.spr[spr::mmcr1] = 0;
  state.spr[spr::dec] = 0xffffffff;
  state.pc = 0x1000;
  state.cr = 0xabcdef01 ^ salt;
  state.fpscr = 0x76543210 ^ salt;
  state.last_pc = 0xc1234564;
  state.last_opcode = 0x77665544;
  state.cycles = 0x1234567890abcdefull;
  state.instructions = 0x234567890abcdef1ull;
  state.timebase = 0x34567890abcdef12ull;
  return state;
}

class Fixture {
public:
  RecordedMemory expected_memory, actual_memory;
  Interpreter interpreter;
  NativeInteger native;

  explicit Fixture(Console console, unsigned capacity = 256)
      : interpreter(expected_memory, console), native(2, capacity) {
    for (unsigned i = 0; i < expected_memory.bytes.size(); ++i)
      expected_memory.bytes[i] = static_cast<u8>((i * 37u) ^ (i >> 7));
    actual_memory.bytes = expected_memory.bytes;
  }

  NativeControlRun execute(std::span<const u32> words, const State &initial, bool control = true) {
    interpreter.reset();
    interpreter.state() = initial;
    for (unsigned i = 0; i < words.size(); ++i) {
      expected_memory.write32(initial.pc + i * 4, words[i]);
      actual_memory.write32(initial.pc + i * 4, words[i]);
    }
    const auto old_reads = expected_memory.reads, old_writes = expected_memory.writes;
    NativeControlRun expected;
    for (u32 word : words) {
      bool taken = false;
      if ((word >> 26) == 16) {
        const auto &state = interpreter.state();
        const unsigned bo = (word >> 21) & 31, bi = (word >> 16) & 31;
        const u32 counter = state.spr[spr::ctr] - ((bo & 4) ? 0 : 1);
        taken = ((bo & 4) || ((counter != 0) != ((bo & 2) != 0))) &&
                ((bo & 16) || (((state.cr >> (31 - bi)) & 1) == ((bo >> 3) & 1)));
      }
      CHECK(interpreter.step() == 1);
      ++expected.instructions;
      if (taken) {
        expected.taken = true;
        break;
      }
    }
    State golden = interpreter.state();
    // Fetch, metadata and clocks belong to the caller. Only a control body
    // owns its PC exit; an arithmetic body also retains the original PC.
    golden.last_pc = initial.last_pc;
    golden.last_opcode = initial.last_opcode;
    golden.cycles = initial.cycles;
    golden.instructions = initial.instructions;
    golden.timebase = initial.timebase;
    if (!control)
      golden.pc = initial.pc;

    struct Guarded {
      std::array<u64, 16> before;
      State state;
      std::array<u64, 16> after;
    } actual;

    actual.before.fill(0x123456789abcdef0ull);
    actual.after.fill(0xfedcba9876543210ull);
    actual.state = initial;
    NativeControlRun result{0x1234, true};
    const auto before = native.statistics();
    if (control) {
      CHECK(native.execute_control_sequence(actual.state, words, result));
      CHECK(result.instructions == expected.instructions && result.taken == expected.taken);
    } else {
      CHECK(native.execute_sequence(actual.state, words));
      result = {static_cast<u32>(words.size()), false};
    }
    equal_state(actual.state, golden);
    CHECK(std::all_of(actual.before.begin(), actual.before.end(),
                      [](u64 value) { return value == 0x123456789abcdef0ull; }));
    CHECK(std::all_of(actual.after.begin(), actual.after.end(),
                      [](u64 value) { return value == 0xfedcba9876543210ull; }));
    CHECK(expected_memory.bytes == actual_memory.bytes);
    CHECK(expected_memory.reads - old_reads == expected.instructions * 4);
    CHECK(expected_memory.writes == old_writes);
    const auto after = native.statistics();
    CHECK(after.executed - before.executed == expected.instructions &&
          after.block_executed - before.block_executed == expected.instructions &&
          after.block_runs == before.block_runs + 1 && after.cache_entries == 0);
    ++comparisons;
    retirements += expected.instructions;
    return result;
  }
};

void conditional_matrix_tests() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Fixture fixture(console);
    for (unsigned bo = 0; bo < 32; ++bo)
      for (unsigned bi = 0; bi < 32; ++bi)
        for (bool link : {false, true})
          for (bool absolute : {false, true})
            for (s32 displacement : {-32768, -4, 0, 4, 32764}) {
              const std::array<u32, 4> words{immediate(14, 3, 3, 1),
                                             branch(bo, bi, displacement, link, absolute),
                                             immediate(14, 3, 3, 2), immediate(26, 3, 4, 0x1234)};
              for (u32 counter : {0u, 1u, 2u, 0xffffffffu})
                for (bool condition : {false, true}) {
                  auto state = seed(counter ^ bi);
                  constexpr std::array<u32, 3> addresses{0x1000, 0x80001000, 0xfffffff8};
                  state.pc = addresses[(counter + bi + u32(condition)) % addresses.size()];
                  state.spr[spr::ctr] = counter;
                  state.cr = condition ? 1u << (31 - bi) : 0;
                  fixture.execute(words, state);
                }
            }
  }
}

void flag_and_multiple_exit_tests() {
  constexpr std::array<u32, 7> prefixes{immediate(28, 3, 3, 0xffff),
                                        immediate(13, 3, 3, 0xffff),
                                        extended(266, 3, 3, 4, true, true),
                                        extended(824, 3, 3, 31, true),
                                        (21u << 26) | (3u << 21) | (3u << 16) | (31u << 11) |
                                            (31u << 1) | 1u,
                                        immediate(11, 0, 3, 0),
                                        extended(138, 3, 3, 3, true, true)};
  for (Console console : {Console::GameCube, Console::Wii}) {
    Fixture fixture(console);
    for (u32 prefix : prefixes)
      for (u32 value : {0u, 1u, 0xffffffffu, 0x80000000u, 0x7fffffffu})
        for (unsigned flags = 0; flags < 8; ++flags)
          for (unsigned bi : {0u, 1u, 2u, 3u, 31u})
            for (bool link : {false, true}) {
              auto state = seed(flags);
              state.gpr[3] = value;
              state.gpr[4] = ~value;
              state.spr[spr::xer] = (flags << 29) | 0x1234ffff;
              const std::array<u32, 8> words{prefix,
                                             branch(12, bi, 4, link),
                                             immediate(13, 3, 3, 0xffff),
                                             branch(4, bi, -4, link),
                                             immediate(28, 3, 3, 0xff),
                                             branch(16, bi, 16, link),
                                             immediate(14, 3, 3, 7),
                                             branch(18, bi, 4, link)};
              fixture.execute(words, state);
            }
    auto state = seed(0);
    state.cr = 0x20000000;
    const std::array<u32, 2> fallthrough{immediate(14, 3, 3, 1), branch(12, 2, 4, true)};
    CHECK(fixture.execute(fallthrough, state).taken);
  }
}

void rejection_tests() {
  NativeInteger native;
  const auto initial = seed(0);
  const std::array<std::vector<u32>, 8> rejected{{{},
                                                  {branch(20, 0, 4)},
                                                  std::vector<u32>(9, branch(20, 0, 4)),
                                                  {immediate(14, 3, 3, 1), immediate(24, 3, 4, 2)},
                                                  {0, branch(20, 0, 4)},
                                                  {immediate(32, 3, 4, 0), branch(20, 0, 4)},
                                                  {immediate(7, 3, 4, 2), branch(20, 0, 4)},
                                                  {0xfc000090, branch(20, 0, 4)}}};
  for (const auto &words : rejected) {
    State state = initial;
    NativeControlRun result{0x1234, true};
    CHECK(!native.execute_control_sequence(state, words, result));
    equal_state(state, initial);
    CHECK(result.instructions == 0x1234 && result.taken);
    CHECK(native.statistics().executed == 0 && native.statistics().block_compilations == 0 &&
          native.statistics().block_executable_bytes == 0 &&
          native.statistics().block_compilation_failures == 0);
  }
}

void typed_cache_and_exhaustion_tests() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Fixture fixture(console);
    const auto apply = [&](unsigned n, bool control) {
      std::array<u32, 4> words{immediate(14, 3, 3, n + 1), branch(12, n & 31, 4, n & 1),
                               immediate(14, 3, 3, 7), immediate(26, 3, 4, n ^ 0x1234)};
      if (!control)
        words[1] = immediate(24, 3, 4, n);
      fixture.execute(words, seed(n), control);
    };
    for (unsigned n = 0; n < 512; ++n) {
      apply(n, false);
      apply(n, true);
    }
    const auto warm = fixture.native.statistics();
    CHECK(warm.block_compilations == 1024 && warm.block_cache_entries == 1024 &&
          warm.block_replacements == 0 && warm.block_executable_bytes == 512 * 1024);
    for (unsigned n = 0; n < 512; ++n) {
      apply((n * 257) & 511, true);
      apply((n * 257) & 511, false);
    }
    CHECK(fixture.native.statistics().block_compilations == warm.block_compilations);
    CHECK(fixture.native.statistics().block_cache_hits == warm.block_cache_hits + 1024);
    fixture.native.reset();
    CHECK(fixture.native.statistics().block_cache_entries == 0 &&
          fixture.native.statistics().block_executable_bytes == 0);
    apply(0, true);
    apply(0, false);
    CHECK(fixture.native.statistics().block_compilations == 2);
    Fixture small(console, 1);
    for (unsigned n = 0; n < 128; ++n) {
      const std::array<u32, 4> words{immediate(14, 3, 3, n + 1), branch(12, n & 31, 4),
                                     immediate(14, 3, 3, 7), immediate(26, 3, 4, n ^ 0x1234)};
      small.execute(words, seed(n));
      const auto before = small.native.statistics();
      small.execute(words, seed(~n));
      CHECK(small.native.statistics().block_compilations == before.block_compilations);
      CHECK(before.block_cache_entries + before.block_replacements == before.block_compilations);
      CHECK(before.block_executable_bytes <= 65536);
    }
    CHECK(small.native.statistics().block_replacements > 0 &&
          small.native.statistics().block_compilation_failures == 0);
    const auto previous = small.native.statistics();
    const std::array<u32, 4> first{immediate(14, 3, 3, 1), branch(12, 0, 4), immediate(14, 3, 3, 7),
                                   immediate(26, 3, 4, 0x1234)};
    small.execute(first, seed(0));
    CHECK(small.native.statistics().block_compilations == previous.block_compilations + 1);
  }
}

std::array<u32, 4> pc_program(unsigned key) {
  return {immediate(14, 3, 3, key + 1), branch(12, key & 31, key & 1 ? 4 : -4, key & 2),
          immediate(14, 4, 3, key ^ 0x1234), immediate(26, 4, 4, key ^ 0x8001)};
}

void pc_reuse_tests() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Fixture fixture(console);
    for (unsigned pass = 0; pass < 4; ++pass)
      for (unsigned n = 0; n < 128; ++n) {
        const unsigned key = pass & 1 ? 127 - n : n;
        auto state = seed(key ^ (pass << 16));
        state.pc = 0x1000 + key * 32;
        fixture.execute(pc_program(key), state);
      }
    CHECK(fixture.native.statistics().block_compilations == 128);
    // These PCs collide in the dispatch table. Exact fetched contents still
    // select the body, and collisions must leave backing entries available.
    for (unsigned pass = 0; pass < 4; ++pass)
      for (unsigned n = 0; n < 128; ++n) {
        auto state = seed(n ^ (pass << 16));
        state.pc = 0x1000 + (n & 3) * 0x4000;
        fixture.execute(pc_program(n), state);
      }
    CHECK(fixture.native.statistics().block_compilations == 128 &&
          fixture.native.statistics().block_replacements == 0);
    // A shorter event quota changes the admitted word count at the same PC.
    // Untaken branches expose the differing suffixes to the stepped oracle.
    for (unsigned n = 0; n < 128; ++n) {
      auto state = seed(n);
      state.pc = 0x2000;
      const auto program = pc_program(n);
      fixture.execute(std::span(program).first(2), state);
      fixture.execute(std::span(program).first(3), state);
      fixture.execute(program, state);
    }
    CHECK(fixture.native.statistics().block_compilations == 384 &&
          fixture.native.statistics().block_replacements == 0);
    fixture.native.reset();
    CHECK(fixture.native.statistics().block_compilations == 0);
    for (unsigned n = 0; n < 128; ++n) {
      auto state = seed(n);
      state.pc = 0x2000;
      fixture.execute(pc_program(n), state);
    }
    CHECK(fixture.native.statistics().block_compilations == 128);
  }
}

void pc_shared_exhaustion_tests() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Fixture fixture(console, 1);
    for (unsigned n = 0; n < 512; ++n) {
      auto state = seed(n);
      state.pc = 0x1000 + (n & 15) * 32;
      fixture.execute(pc_program(n), state);
      const std::array<u32, 4> arithmetic{
          immediate(14, 3, 3, n + 1), immediate(26, 3, 3, n ^ 0x8001),
          immediate(24, 3, 4, n ^ 0x1234), immediate(14, 4, 3, n ^ 0xff00)};
      fixture.execute(arithmetic, state, false);
      fixture.execute(pc_program(n), state);
    }
    CHECK(fixture.native.statistics().block_replacements > 0 &&
          fixture.native.statistics().block_compilation_failures == 0);
  }
}
} // namespace

int main() {
  if (!NativeInteger::supported()) {
    std::cout << "SKIP: native control execution requires x86-64\n";
    return 77;
  }
  return run_tests([] {
    conditional_matrix_tests();
    flag_and_multiple_exit_tests();
    rejection_tests();
    typed_cache_and_exhaustion_tests();
    pc_reuse_tests();
    pc_shared_exhaustion_tests();
    std::cout << comparisons << " complete-State/RAM control comparisons, " << retirements
              << " stepped oracle retirements passed\n";
  });
}
