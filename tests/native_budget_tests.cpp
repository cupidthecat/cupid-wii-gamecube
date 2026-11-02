// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/interpreter.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

using namespace cupid;
using namespace cupid::cpu;

namespace {
u64 comparisons = 0;

class Memory final : public Bus {
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

  bool stable_instruction_cache_key(u32 address, u32 &key) const override {
    key = address & 65535;
    return !(address & 3);
  }

  u32 instruction_cache_key(u32 address) const override {
    return address & 65535;
  }
};

constexpr u32 immediate(unsigned primary, unsigned destination, unsigned source, unsigned value) {
  return (primary << 26) | (destination << 21) | (source << 16) | (value & 65535);
}

constexpr u32 extended(unsigned operation, unsigned destination, unsigned left, unsigned right) {
  return (31u << 26) | (destination << 21) | (left << 16) | (right << 11) | (operation << 1) |
         0x401u;
}

std::array<u32, 8> words(unsigned n, bool control) {
  std::array<u32, 8> result{immediate(14, 3, 3, n + 1),      extended(10, 4, 3, 4),
                            immediate(26, 4, 4, n ^ 0x8001), extended(138, 5, 3, 4),
                            extended(136, 3, 5, 3),          immediate(11, 0, 3, n ^ 0xff00),
                            immediate(24, 3, 4, n ^ 0x1234), extended(234, 5, 4, 0)};
  if (control) {
    result[2] = (16u << 26) | ((n & 31) << 21) | (((n >> 5) & 31) << 16) | (n & 1 ? 4u : 0xfffcu) |
                (n & 6 ? 1u : 0u);
    result[5] = (16u << 26) | (12u << 21) | (((n + 7) & 31) << 16) | 12u;
  }
  return result;
}

State seed(unsigned n) {
  State result;
  for (unsigned i = 0; i < result.gpr.size(); ++i)
    result.gpr[i] = (0x87654321u + i * 0x9e3779b9u) ^ n;
  for (unsigned i = 0; i < result.fpr.size(); ++i)
    result.fpr[i].bits = {0x7ff0000000000001ull ^ (u64{n} << 32) ^ i, 0x8123456789abcdefull ^ i};
  for (unsigned i = 0; i < result.spr.size(); ++i)
    result.spr[i] = (0xa1234567u + i * 0x12345u) ^ n;
  for (unsigned i = 0; i < result.sr.size(); ++i)
    result.sr[i] = 0xb2345678u ^ i;
  result.spr[spr::hid0] = result.spr[spr::mmcr0] = result.spr[spr::mmcr1] = 0;
  result.spr[spr::dec] = 0xffffffff;
  result.pc = n & 1 ? 0x1000 : 0x80001000;
  result.cr = 0xabcdef01 ^ n;
  result.fpscr = 0x76543210 ^ n;
  result.last_pc = 0xc1234564;
  result.last_opcode = 0x77665544;
  result.cycles = 0x1234567890abcdefull;
  result.instructions = 0x234567890abcdef1ull;
  result.timebase = 0x34567890abcdef12ull;
  return result;
}

void equal_state(const State &a, const State &b) {
  CHECK(a.gpr == b.gpr && a.spr == b.spr && a.sr == b.sr);
  for (unsigned i = 0; i < a.fpr.size(); ++i)
    CHECK(a.fpr[i].bits == b.fpr[i].bits);
  CHECK(a.pc == b.pc && a.msr == b.msr && a.cr == b.cr && a.fpscr == b.fpscr);
  CHECK(a.last_pc == b.last_pc && a.last_opcode == b.last_opcode);
  CHECK(a.cycles == b.cycles && a.instructions == b.instructions && a.timebase == b.timebase);
}

class Oracle {
public:
  explicit Oracle(Console console) : interpreter_(expected_, console) {
    for (unsigned n = 0; n < actual_.bytes.size(); ++n)
      actual_.bytes[n] = static_cast<u8>((n * 37u) ^ (n >> 7));
    expected_.bytes = actual_.bytes;
  }

  void execute(NativeInteger &native, std::span<const u32> program, bool control, unsigned salt) {
    interpreter_.reset();
    const auto initial = seed(salt);
    interpreter_.state() = initial;
    for (unsigned n = 0; n < program.size(); ++n) {
      expected_.write32(initial.pc + n * 4, program[n]);
      actual_.write32(initial.pc + n * 4, program[n]);
    }
    const auto reads = expected_.reads, writes = expected_.writes;
    NativeControlRun expected;
    for (u32 opcode : program) {
      bool taken = false;
      if ((opcode >> 26) == 16) {
        const auto &state = interpreter_.state();
        const unsigned bo = (opcode >> 21) & 31, bi = (opcode >> 16) & 31;
        const u32 counter = state.spr[spr::ctr] - ((bo & 4) ? 0 : 1);
        taken = ((bo & 4) || ((counter != 0) != ((bo & 2) != 0))) &&
                ((bo & 16) || (((state.cr >> (31 - bi)) & 1) == ((bo >> 3) & 1)));
      }
      CHECK(interpreter_.step() == 1);
      ++expected.instructions;
      if (taken) {
        expected.taken = true;
        break;
      }
    }
    auto golden = interpreter_.state();
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
    if (control) {
      NativeControlRun result{0x1234, true};
      CHECK(native.execute_control_sequence(actual.state, program, result));
      CHECK(result.instructions == expected.instructions && result.taken == expected.taken);
    } else
      CHECK(native.execute_sequence(actual.state, program));
    equal_state(actual.state, golden);
    CHECK(std::all_of(actual.before.begin(), actual.before.end(),
                      [](u64 value) { return value == 0x123456789abcdef0ull; }));
    CHECK(std::all_of(actual.after.begin(), actual.after.end(),
                      [](u64 value) { return value == 0xfedcba9876543210ull; }));
    CHECK(actual_.bytes == expected_.bytes);
    CHECK(expected_.reads - reads == expected.instructions * 4 && expected_.writes == writes);
    ++comparisons;
  }

private:
  Memory expected_, actual_;
  Interpreter interpreter_;
};

void bounds_and_reset_tests() {
  for (std::size_t budget :
       {std::size_t{0}, std::size_t{1}, std::size_t{2047},
        NativeInteger::maximum_sequence_byte_budget + 1, std::numeric_limits<std::size_t>::max()}) {
    CHECK_THROWS(NativeInteger(2, 1, budget));
  }
  CHECK_THROWS(NativeInteger(0, 1, 2048));
  CHECK_THROWS(NativeInteger(2, 0, 2048));
  CHECK_THROWS(NativeInteger(4097, 1, 2048));
  CHECK_THROWS(NativeInteger(2, 513, 2048));
  Oracle oracle(Console::GameCube);
  for (std::size_t budget : {std::size_t{2048}, std::size_t{2049}, std::size_t{32u * 1024u * 1024u},
                             NativeInteger::maximum_sequence_byte_budget}) {
    NativeInteger native(2, 1, budget);
    CHECK(native.statistics().block_executable_bytes == 0);
    oracle.execute(native, words(7, false), false, 7);
    const auto reserved = native.statistics().block_executable_bytes;
    CHECK(reserved >= budget && reserved - budget < 65536);
    oracle.execute(native, words(7, true), true, 8);
    native.reset();
    CHECK(native.statistics().block_executable_bytes == 0 &&
          native.statistics().block_cache_entries == 0 && native.statistics().block_runs == 0);
    oracle.execute(native, words(7, true), true, 9);
    CHECK(native.statistics().block_executable_bytes == reserved &&
          native.statistics().block_compilations == 1);
    oracle.execute(native, words(7, false), false, 10);
    CHECK(native.statistics().block_compilations == 2);
  }
}

void shared_retention_tests() {
  constexpr unsigned keys = 1024;
  for (Console console : {Console::GameCube, Console::Wii}) {
    Oracle oracle(console);
    NativeInteger runtime(2, 1, NativeInteger::runtime_sequence_byte_budget), legacy(2, 256);
    for (unsigned n = 0; n < keys; ++n)
      for (bool control : {false, true}) {
        const auto program = words(n, control);
        oracle.execute(runtime, program, control, n);
        oracle.execute(legacy, program, control, n);
      }
    const auto warm = runtime.statistics(), old = legacy.statistics();
    CHECK(warm.block_compilations == 2 * keys && warm.block_cache_entries == 2 * keys &&
          warm.block_replacements == 0 &&
          warm.block_executable_bytes == NativeInteger::runtime_sequence_byte_budget);
    // The exact same emitted working set truly exhausts the legacy 512 KiB
    // arena. Its retained entry count cannot stand in for a code byte budget.
    CHECK(old.block_executable_bytes == 512 * 1024 && old.block_replacements > 0 &&
          old.block_cache_entries < 2 * keys);
    for (unsigned sweep = 0; sweep < 2; ++sweep)
      for (unsigned index = 0; index < keys; ++index) {
        const unsigned n = sweep ? keys - index - 1 : (index + 257) % keys;
        for (bool control : {true, false})
          oracle.execute(runtime, words(n, control), control, n ^ (sweep << 16));
      }
    CHECK(runtime.statistics().block_compilations == warm.block_compilations &&
          runtime.statistics().block_cache_entries == warm.block_cache_entries &&
          runtime.statistics().block_replacements == 0 &&
          runtime.statistics().block_cache_hits == warm.block_cache_hits + 4 * keys);
  }
}

void true_exhaustion_tests() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Oracle oracle(console);
    NativeInteger native(2, 256, 2048);
    for (unsigned n = 0; n < 128; ++n) {
      const bool control = (n & 1) != 0;
      oracle.execute(native, words(n, control), control, n);
      const auto statistics = native.statistics();
      CHECK(statistics.block_compilations == n + 1 && statistics.block_compilation_failures == 0);
      CHECK(statistics.block_replacements + statistics.block_cache_entries == n + 1);
    }
    const auto full = native.statistics();
    CHECK(full.block_replacements > 0 && full.block_cache_entries < full.block_compilations);
    oracle.execute(native, words(0, false), false, 1000);
    CHECK(native.statistics().block_compilations == full.block_compilations + 1);
    oracle.execute(native, words(0, false), false, 1001);
    CHECK(native.statistics().block_compilations == full.block_compilations + 1);
  }
}

void interpreter_runtime_budget_tests() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Memory memory;
    Interpreter interpreter(memory, console);
    interpreter.set_execution_backend(ExecutionBackend::NativeInteger);
    const auto program = words(0, false);
    for (unsigned n = 0; n < program.size(); ++n)
      memory.write32(0x1000 + n * 4, program[n]);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
      interpreter.reset();
      interpreter.state() = seed(attempt);
      interpreter.state().pc = 0x1000;
      interpreter.state().spr[spr::hid0] = 0x8000;
      CHECK(interpreter.native_statistics().block_executable_bytes == 0);
      CHECK(interpreter.step() == 1);
      interpreter.state().pc = 0x1000;
      CHECK(interpreter.try_integer_run(8, 8).instructions == 8);
      CHECK(interpreter.native_statistics().block_executable_bytes ==
            NativeInteger::runtime_sequence_byte_budget);
    }
  }
}
} // namespace

int main() {
  if (!NativeInteger::supported()) {
    std::cout << "SKIP: native byte budget checks require x86-64\n";
    return 77;
  }
  return run_tests([] {
    bounds_and_reset_tests();
    shared_retention_tests();
    true_exhaustion_tests();
    interpreter_runtime_budget_tests();
    std::cout << comparisons << " complete-State/RAM byte-budget comparisons passed\n";
  });
}
