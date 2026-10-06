// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/interpreter.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <vector>

using namespace cupid;
using namespace cupid::cpu;

namespace {
class RecordedMemory final : public Bus {
public:
  std::vector<u8> bytes = std::vector<u8>(1024 * 1024);
  std::vector<std::array<u64, 4>> records;
  bool stable = true;
  bool plain = false;
  u32 blocked = 0xffffffff;

  std::span<u8> plain_cpu_ram(u32 address, unsigned size) override {
    address &= 0x3fffffff;
    if (!plain || (size != 1 && size != 2 && size != 4 && size != 8) ||
        (address & 4095) + size > 4096 || address >= bytes.size() ||
        size > bytes.size() - address ||
        (address < blocked + u64{4096} && address + size > blocked))
      return {};
    return std::span(bytes).subspan(address, size);
  }

  u8 read8(u32 address) override {
    address &= 0x3fffffff;
    if (address >= bytes.size())
      throw BusFault(address, false, "Integer run test bounds");
    return bytes[address];
  }

  void write8(u32 address, u8 value) override {
    address &= 0x3fffffff;
    if (address >= bytes.size())
      throw BusFault(address, true, "Integer run test bounds");
    bytes[address] = value;
  }

  u32 instruction_cache_key(u32 physical) const override {
    return physical & 0x3fffffff;
  }

  bool stable_instruction_cache_key(u32 physical, u32 &key) const override {
    if (!stable || (physical & 3) || (physical & 0x3fffffff) >= bytes.size())
      return false;
    key = instruction_cache_key(physical);
    return true;
  }

  bool read_instruction_words(u32 address, std::span<u32> words) override {
    records.push_back({0, address, words.size(), 0});
    return Bus::read_instruction_words(address, words);
  }

  bool read_instruction_bytes(u32 address, std::span<u8> destination) override {
    records.push_back({1, address, destination.size(), 0});
    return Bus::read_instruction_bytes(address, destination);
  }

  u64 read_cpu_data(u32 address, unsigned size) override {
    const u64 value = Bus::read_cpu_data(address, size);
    if (plain_cpu_ram(address, size).empty())
      records.push_back({2, address, size, value});
    return value;
  }

  void write_cpu_data(u32 address, u32 value, unsigned size) override {
    if (plain_cpu_ram(address, size).empty())
      records.push_back({3, address, size, value});
    Bus::write_cpu_data(address, value, size);
  }
};

void equal_state(const State &a, const State &b) {
  CHECK(a.gpr == b.gpr && a.spr == b.spr && a.sr == b.sr);
  for (unsigned n = 0; n < a.fpr.size(); ++n)
    CHECK(a.fpr[n].bits == b.fpr[n].bits);
  CHECK(a.pc == b.pc && a.msr == b.msr && a.cr == b.cr && a.fpscr == b.fpscr);
  CHECK(a.last_pc == b.last_pc && a.last_opcode == b.last_opcode);
  CHECK(a.cycles == b.cycles && a.instructions == b.instructions && a.timebase == b.timebase);
}

u32 immediate(unsigned primary, unsigned destination, unsigned source, unsigned value) {
  return (primary << 26) | (destination << 21) | (source << 16) | value;
}

u32 extended(unsigned operation, unsigned destination, unsigned left, unsigned right, bool record,
             bool overflow = false) {
  return (31u << 26) | (destination << 21) | (left << 16) | (right << 11) | (operation << 1) |
         (overflow ? 0x400u : 0) | static_cast<u32>(record);
}

u64 comparisons = 0, grouped_instructions = 0;

struct Pair {
  RecordedMemory first_memory, run_memory;
  Interpreter first, run;

  explicit Pair(Console console, ExecutionBackend backend)
      : first(first_memory, console), run(run_memory, console) {
    run.set_execution_backend(backend);
    reset();
  }

  void reset() {
    first.reset();
    run.reset();
    first.configure_boot();
    run.configure_boot();
    first.state().spr[spr::hid0] = run.state().spr[spr::hid0] = 0x8000;
    first.state().spr[spr::dec] = run.state().spr[spr::dec] = 0xffffffff;
    first_memory.records.clear();
    run_memory.records.clear();
    first_memory.stable = run_memory.stable = true;
    first_memory.plain = run_memory.plain = false;
    first_memory.blocked = run_memory.blocked = 0xffffffff;
  }

  void install(u32 physical, u32 word) {
    first_memory.write32(physical, word);
    run_memory.write32(physical, word);
  }

  void line(u32 physical = 0x1000) {
    for (unsigned n = 0; n < 8; ++n)
      install(physical + n * 4, immediate(14, 3, 3, n + 1));
  }

  void pc(u32 value = 0x80001000) {
    first.state().pc = run.state().pc = value;
  }

  void verify() {
    equal_state(first.state(), run.state());
    CHECK(first.last_step_fetched_instruction() == run.last_step_fetched_instruction());
    CHECK(first_memory.records == run_memory.records);
    if (run_memory.plain)
      CHECK(first_memory.bytes == run_memory.bytes);
    ++comparisons;
  }

  void step() {
    CHECK(first.step() == run.step());
    verify();
  }

  IntegerRun attempt(u32 instructions, u32 cycles, unsigned expected,
                     unsigned expected_cycles = 0) {
    if (!expected_cycles)
      expected_cycles = expected;
    if (!run_memory.plain && run.execution_backend() == ExecutionBackend::NativeInteger &&
        expected < 4 && expected_cycles == expected)
      expected = expected_cycles = 0;
    const auto before = run.state();
    const auto stats = run.native_statistics();
    const auto record_count = run_memory.records.size();
    const auto fetched = run.last_step_fetched_instruction();
    const auto result = run.try_integer_run(instructions, cycles);
    if (result.instructions != expected || result.cycles != expected_cycles)
      std::cerr << "Integer run mismatch at " << std::hex << before.pc << std::dec << ": quota "
                << instructions << "/" << cycles << ", expected " << expected << ", got "
                << result.instructions << "/" << result.cycles << ", MSR " << std::hex << before.msr
                << ", HID4 " << before.spr[spr::hid4] << std::dec << '\n';
    CHECK(result.instructions == expected && result.cycles == expected_cycles);
    u32 stepped_cycles = 0;
    for (unsigned n = 0; n < expected; ++n)
      stepped_cycles += first.step();
    CHECK(stepped_cycles == expected_cycles);
    if (!expected) {
      equal_state(before, run.state());
      CHECK(run_memory.records.size() == record_count);
      CHECK(run.last_step_fetched_instruction() == fetched);
      const auto after = run.native_statistics();
      CHECK(stats.executed == after.executed && stats.fallback == after.fallback &&
            stats.cache_hits == after.cache_hits &&
            stats.compiled_operations == after.compiled_operations);
      CHECK(stats.block_runs == after.block_runs &&
            stats.block_compilations == after.block_compilations &&
            stats.block_cache_entries == after.block_cache_entries &&
            stats.block_executable_bytes == after.block_executable_bytes);
    }
    verify();
    grouped_instructions += expected;
    return result;
  }
};

template <typename Function> void each_backend(Function function) {
  for (Console console : {Console::GameCube, Console::Wii})
    for (ExecutionBackend backend :
         {ExecutionBackend::Interpreter, ExecutionBackend::NativeInteger}) {
      if (backend == ExecutionBackend::NativeInteger && !NativeInteger::supported())
        continue;
      Pair pair(console, backend);
      function(pair);
      CHECK(pair.first_memory.bytes == pair.run_memory.bytes);
    }
}

void quota_and_clock_tests() {
  each_backend([](Pair &pair) {
    for (unsigned count = 0; count <= 12; ++count)
      for (unsigned cycles = 0; cycles <= 12; ++cycles)
        for (unsigned offset = 0; offset < 8; ++offset) {
          pair.reset();
          pair.line();
          pair.pc();
          pair.step();
          pair.pc(0x80001000 + offset * 4);
          const auto limit = std::min({count, cycles, 8u - offset});
          pair.attempt(count, cycles, limit >= 2 ? limit : 0);
          pair.pc();
          pair.step(); // observes hidden TB/DEC remainders after an aggregate tick
        }
    pair.reset();
    pair.line();
    pair.pc();
    pair.step();
    pair.first.state().instructions = pair.run.state().instructions =
        std::numeric_limits<u64>::max() - 3;
    pair.attempt(8, 8, 0);
    pair.first.state().instructions = pair.run.state().instructions = 0;
    pair.first.state().timebase = pair.run.state().timebase = std::numeric_limits<u64>::max();
    pair.attempt(7, 7, 7);
    pair.pc();
    for (unsigned n = 0; n < 20; ++n) {
      pair.step();
      pair.pc();
    }
    CHECK(pair.run.state().timebase == pair.first.state().timebase);
  });
}

void semantic_sequence_tests() {
  each_backend([](Pair &pair) {
    u32 random = 0x912438ab;
    const auto next = [&] {
      random ^= random << 13;
      random ^= random >> 17;
      random ^= random << 5;
      return random;
    };
    constexpr std::array<unsigned, 28> operations{0,   32,  8,   10,  40,  104, 136, 138, 200, 202,
                                                  232, 234, 266, 24,  26,  28,  60,  124, 284, 316,
                                                  412, 444, 476, 536, 792, 824, 922, 954};
    for (unsigned sample = 0; sample < 1024; ++sample) {
      pair.reset();
      for (unsigned n = 0; n < 32; ++n) {
        pair.first.state().gpr[n] = pair.run.state().gpr[n] = next();
        pair.first.state().fpr[n].bits = pair.run.state().fpr[n].bits = {next(), next()};
      }
      pair.first.state().cr = pair.run.state().cr = next();
      pair.first.state().fpscr = pair.run.state().fpscr = next();
      pair.first.state().spr[spr::xer] = pair.run.state().spr[spr::xer] = next();
      for (unsigned n = 0; n < 8; ++n) {
        const auto value = next();
        u32 word = 0;
        if (sample & 1) {
          word = extended(operations[value % operations.size()], (value >> 5) & 31,
                          (value >> 10) & 31, (value >> 15) & 31, value & 1, value & 2);
          // OE is reserved for the logical/shift encodings; retain only supported words.
          if (!NativeInteger::can_execute(word))
            word &= ~0x400u;
        } else {
          constexpr std::array<unsigned, 15> primary{8,  10, 11, 12, 13, 14, 15, 20,
                                                     21, 23, 24, 25, 26, 28, 29};
          word = immediate(primary[value % primary.size()], (value >> 5) & 31, (value >> 10) & 31,
                           value & 0xffff);
        }
        pair.install(0x1000 + n * 4, word);
      }
      pair.pc();
      pair.step();
      pair.pc();
      pair.attempt(8, 8, 8);
      pair.pc();
      pair.step();
      if (pair.run.execution_backend() == ExecutionBackend::NativeInteger)
        CHECK(pair.run.native_statistics().block_executed == 8);
    }
  });
}

void prefix_and_cache_tests() {
  each_backend([](Pair &pair) {
    constexpr std::array<u32, 11> stops{0,          0x48000004, 0x44000002, 0x80640000,
                                        0x90640000, 0xfc200090, 0x7c0004ac, 0x7c000124,
                                        0x7c0007ac, 0x1c630001, 0x7c6329d6};
    for (u32 stop : stops)
      for (unsigned prefix = 0; prefix < 8; ++prefix) {
        pair.reset();
        pair.line();
        pair.install(0x1000 + prefix * 4, stop);
        // Warm from a guaranteed integer at the end while leaving the prefix unexecuted.
        pair.install(0x1020, immediate(14, 3, 3, 1));
        pair.pc(0x8000101c);
        if (prefix == 7)
          continue;
        pair.step();
        pair.pc();
        pair.attempt(8, 8, prefix >= 2 ? prefix : 0);
      }
    pair.reset();
    pair.line();
    pair.pc();
    pair.attempt(8, 8, 0); // cold fetch remains an ordinary attempted step
    pair.step();
    pair.install(0x1004, immediate(14, 3, 3, 999));
    pair.attempt(7, 7, 7); // resident guest words survive a backing rewrite
    CHECK(pair.run.state().gpr[3] == 36);
    pair.install(0x2000, (31u << 26) | (4 << 11) | (982 << 1)); // icbi indexed by r4
    pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80001000;
    pair.pc(0x80002000);
    pair.step();
    pair.pc();
    pair.attempt(8, 8, 0);
    pair.step();
    pair.attempt(7, 7, 7);
    CHECK(pair.run.state().gpr[3] == 1069);
    // Touch all nine conflicting lines to compare replacement state after grouping.
    for (unsigned n = 0; n < 9; ++n) {
      pair.install(0x1000 + n * 4096, immediate(14, 3, 3, n));
      pair.pc(0x80001000 + n * 4096);
      pair.step();
    }
    pair.pc();
    pair.step();
  });
}

void rejection_and_interrupt_tests() {
  each_backend([](Pair &pair) {
    for (unsigned kind = 0; kind < 15; ++kind) {
      pair.reset();
      pair.line();
      pair.pc();
      pair.step();
      auto &first = pair.first.state();
      auto &run = pair.run.state();
      switch (kind) {
      case 0:
        first.spr[spr::hid0] = run.spr[spr::hid0] = 0;
        break;
      case 1:
        first.spr[spr::hid0] = run.spr[spr::hid0] = 0xa000;
        break;
      case 2:
        first.msr = run.msr = msr::le;
        break;
      case 3:
        pair.pc(0x80001005);
        break;
      case 4:
        pair.first_memory.stable = pair.run_memory.stable = false;
        break;
      case 5:
        pair.pc(0x80002000);
        break;
      case 6:
        first.spr[528] = run.spr[528] ^= 4;
        break;
      case 7:
        first.spr[529] = run.spr[529] ^= 0x20000;
        break;
      case 8:
        first.spr[spr::mmcr0] = run.spr[spr::mmcr0] = 1 << 6;
        break;
      case 9:
        first.spr[spr::mmcr1] = run.spr[spr::mmcr1] = 1 << 27;
        break;
      case 10:
        first.msr = run.msr |= msr::ee;
        pair.first.set_external_interrupt(true);
        pair.run.set_external_interrupt(true);
        break;
      case 11:
        first.spr[spr::dec] = run.spr[spr::dec] = 0;
        break;
      case 12:
        first.spr[528] = run.spr[528] = 0;
        pair.step(); // hidden mapping is current but the next PC has no BAT hit
        pair.pc();
        first.msr = run.msr = msr::ir;
        break;
      case 13:
        first.spr[spr::hid4] = run.spr[spr::hid4] ^= 0x02000000;
        if (pair.run.state().spr[spr::pvr] != 0x00087102)
          continue;
        break;
      case 14:
        first.spr[spr::mmcr0] = run.spr[spr::mmcr0] = (1 << 6) | (1 << 15);
        first.spr[spr::pmc1] = run.spr[spr::pmc1] = 0x7fffffff;
        pair.step();
        first.spr[spr::mmcr0] = run.spr[spr::mmcr0] = 0;
        first.msr = run.msr |= msr::ee;
        break;
      }
      pair.attempt(8, 8, 0);
    }
    // Every DEC phase is followed through its exact transition and interrupt.
    for (unsigned phase = 0; phase < 12; ++phase) {
      pair.reset();
      pair.line();
      pair.first.state().msr = pair.run.state().msr |= msr::ee;
      pair.first.state().spr[spr::dec] = pair.run.state().spr[spr::dec] = 1;
      pair.pc();
      pair.step();
      for (unsigned n = 0; n < phase; ++n) {
        pair.pc();
        pair.step();
      }
      pair.pc();
      const unsigned available = phase < 11 ? 10 - phase : 0;
      const unsigned count = std::min(8u, available);
      pair.attempt(8, 8, count >= 2 ? count : 0);
      for (unsigned n = 0; n < 16; ++n) {
        pair.pc();
        pair.step();
      }
    }
  });
}

void bounded_native_sequence_tests() {
  if (!NativeInteger::supported())
    return;
  NativeInteger native(2, 1), separate(1024);
  State state, baseline;
  state.spr[spr::xer] = baseline.spr[spr::xer] = 0xa1234567;
  for (unsigned sample = 0; sample < 100; ++sample) {
    const std::array<u32, 3> words{immediate(14, 3, 3, sample), extended(138, 4, 3, 3, true, true),
                                   extended(824, 5, 4, sample & 31, true)};
    for (u32 word : words)
      CHECK(separate.execute(baseline, word));
    CHECK(native.execute_sequence(state, words));
    equal_state(state, baseline);
    CHECK(native.statistics().block_cache_entries == 1);
    CHECK(native.statistics().block_executable_bytes <= 65536);
    CHECK(native.statistics().block_compilations == sample + 1);
  }
  CHECK(native.statistics().block_replacements == 99);
  const auto unchanged = state;
  constexpr std::array<u32, 9> too_many{};
  constexpr std::array<u32, 2> illegal{0x48000004, 0x38630001};
  CHECK(!native.execute_sequence(state, {}));
  CHECK(!native.execute_sequence(state, too_many));
  CHECK(!native.execute_sequence(state, illegal));
  equal_state(state, unchanged);
  native.reset();
  CHECK(native.statistics().block_runs == 0 && native.statistics().block_cache_entries == 0 &&
        native.statistics().block_executable_bytes == 0);
  for (unsigned capacity : {0u, 513u}) {
    bool rejected = false;
    try {
      NativeInteger invalid(2, capacity);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    CHECK(rejected);
  }

  NativeInteger cached(1, 4), expected(1024);
  State cached_state, expected_state;
  cached_state.gpr[3] = expected_state.gpr[3] = 0x80000000;
  cached_state.spr[spr::xer] = expected_state.spr[spr::xer] = 0xa1234567;
  const std::array<u32, 8> repeated{immediate(14, 3, 3, 1),
                                    extended(138, 4, 3, 3, true, true),
                                    extended(234, 5, 4, 0, true, true),
                                    extended(232, 6, 5, 0, true, true),
                                    extended(824, 7, 6, 17, true),
                                    immediate(26, 7, 7, 0x1234),
                                    (23u << 26) | (7 << 21) | (8 << 16) | (3 << 11) | (19 << 6) |
                                        (7 << 1) | 1,
                                    immediate(11, 0, 8, 0xff00)};
  for (unsigned iteration = 0; iteration < 1000; ++iteration) {
    const auto words = std::span(repeated).first(iteration & 1 ? 2 : 8);
    for (u32 word : words)
      CHECK(expected.execute(expected_state, word));
    CHECK(cached.execute_sequence(cached_state, words));
    equal_state(cached_state, expected_state);
  }
  CHECK(cached.statistics().block_compilations == 2 &&
        cached.statistics().block_cache_hits == 998 &&
        cached.statistics().block_cache_entries == 2);
  CHECK(cached.statistics().cache_entries == 0); // block entries have their own capacity

  NativeInteger leaf(2);
  State leaf_state, leaf_expected;
  const u32 word = extended(138, 3, 3, 4, true, true);
  CHECK(!leaf.execute_cached(leaf_state, word));
  CHECK(leaf.statistics().executed == 0 && leaf.statistics().fallback == 0 &&
        leaf.statistics().compiled_operations == 0);
  CHECK(leaf.execute(leaf_state, word));
  CHECK(expected.execute(leaf_expected, word));
  for (unsigned n = 0; n < 32; ++n) {
    leaf_state.gpr[3] = leaf_expected.gpr[3] = n * 0x80000001;
    leaf_state.gpr[4] = leaf_expected.gpr[4] = ~n;
    CHECK(leaf.execute_cached(leaf_state, word));
    CHECK(expected.execute(leaf_expected, word));
    equal_state(leaf_state, leaf_expected);
  }
  const auto missing_before = leaf_state;
  const auto missing_statistics = leaf.statistics();
  CHECK(!leaf.execute_cached(leaf_state, 0x48000004));
  CHECK(!leaf.execute_cached(leaf_state, immediate(14, 3, 3, 1)));
  equal_state(leaf_state, missing_before);
  CHECK(leaf.statistics().executed == missing_statistics.executed &&
        leaf.statistics().fallback == missing_statistics.fallback &&
        leaf.statistics().compiled_operations == missing_statistics.compiled_operations);
  leaf.reset();
  CHECK(!leaf.execute_cached(leaf_state, word));
}

void aliased_sequence_tests() {
  constexpr std::array<unsigned, 26> operations{8,   10,  40,  104, 136, 138, 200, 202, 232,
                                                234, 266, 24,  26,  28,  60,  124, 284, 316,
                                                412, 444, 476, 536, 792, 824, 922, 954};
  constexpr std::array<std::array<unsigned, 3>, 9> aliases{{{3, 4, 5},
                                                            {3, 3, 5},
                                                            {3, 4, 3},
                                                            {3, 4, 4},
                                                            {3, 3, 3},
                                                            {0, 0, 31},
                                                            {31, 0, 31},
                                                            {31, 31, 0},
                                                            {30, 31, 30}}};
  constexpr std::array<u32, 8> values{0,          1,          0xffffffff, 0x80000000,
                                      0x7fffffff, 0xaaaaaaaa, 0x55555555, 0x12345678};
  each_backend([&](Pair &pair) {
    for (unsigned operation : operations)
      for (const auto &[destination, left, right] : aliases)
        for (unsigned sample = 0; sample < values.size(); ++sample) {
          pair.reset();
          const bool arithmetic = operation == 8 || operation == 10 || operation == 40 ||
                                  operation == 104 || operation == 136 || operation == 138 ||
                                  operation == 200 || operation == 202 || operation == 232 ||
                                  operation == 234 || operation == 266;
          const std::array<u32, 8> words{
              immediate(14, destination, 0, values[sample] & 0xffff),
              extended(operation, destination, left, right, sample & 1, arithmetic && (sample & 2)),
              extended(sample & 1 ? 0 : 32, (sample & 7) << 2, destination, right, false),
              (20u << 26) | (destination << 21) | (left << 16) | (sample << 11) | (19 << 6) |
                  (7 << 1) | (sample & 1),
              immediate(13, destination, left, values[(sample + 1) & 7] & 0xffff),
              extended(sample & 1 ? 232 : 234, destination, destination, 0, true, true),
              extended(444, destination, left, destination, true),
              immediate(11, 7 << 2, left, values[(sample + 3) & 7] & 0xffff)};
          for (unsigned n = 0; n < words.size(); ++n)
            pair.install(0x1000 + n * 4, words[n]);
          pair.pc(0x8000101c);
          pair.step();
          for (unsigned n = 0; n < 32; ++n)
            pair.first.state().gpr[n] = pair.run.state().gpr[n] = values[(sample + n) & 7];
          pair.first.state().spr[spr::xer] = pair.run.state().spr[spr::xer] =
              (sample << 29) | 0x1234567;
          pair.first.state().cr = pair.run.state().cr = 0x89abcdef;
          pair.pc();
          pair.attempt(8, 8, 8);
          pair.pc();
          pair.attempt(8, 8, 8); // the compiled body retains no registers between calls
          pair.step();
        }
  });
}

void cache_flush_run_tests() {
  each_backend([](Pair &pair) {
    const auto install = [&](unsigned offset, u32 register_word, u32 branch_word) {
      pair.line();
      pair.install(0x1000 + offset * 4, (31u << 26) | (3 << 16) | (4 << 11) | (86 << 1));
      pair.install(0x1004 + offset * 4, register_word);
      pair.install(0x1008 + offset * 4, branch_word);
      pair.pc(0x80001000 + offset * 4);
      pair.step(); // ordinary DCBF establishes the resident line
      pair.pc(0x80001000 + offset * 4);
    };
    const auto branch = [](unsigned bo, unsigned bi, bool link, bool absolute, u32 displacement) {
      return (16u << 26) | (bo << 21) | (bi << 16) | (displacement & 0xfffc) |
             (absolute ? 2u : 0u) | static_cast<u32>(link);
    };
    for (unsigned offset = 0; offset < 6; ++offset)
      for (unsigned bo = 0; bo < 32; ++bo)
        for (unsigned bi : {0u, 2u, 17u, 31u})
          for (u32 counter : {0u, 1u, 2u, 0xffffffffu})
            for (bool link : {false, true}) {
              pair.reset();
              install(offset, immediate(14, 3, 3, 32), branch(bo, bi, link, bo & 1, 0xfff8));
              pair.first.state().spr[spr::ctr] = pair.run.state().spr[spr::ctr] = counter;
              pair.first.state().spr[spr::lr] = pair.run.state().spr[spr::lr] = 0x80002003;
              pair.first.state().cr = pair.run.state().cr = counter & 1 ? 0xffffffff : 0;
              pair.attempt(3, 7, 3, 7);
              pair.pc();
              pair.step(); // follows hidden tick and decrementer phases
            }
    for (unsigned instructions = 0; instructions < 10; ++instructions)
      for (unsigned cycles = 0; cycles < 12; ++cycles) {
        pair.reset();
        install(0, immediate(14, 3, 3, 32), branch(16, 0, false, false, 0xfff8));
        pair.attempt(instructions, cycles, instructions >= 3 && cycles >= 7 ? 3 : 0,
                     instructions >= 3 && cycles >= 7 ? 7 : 0);
      }
    for (unsigned phase = 0; phase < 12; ++phase) {
      pair.reset();
      install(0, immediate(14, 3, 3, 32), branch(16, 0, false, false, 0xfff8));
      pair.first.state().spr[spr::dec] = pair.run.state().spr[spr::dec] = 1;
      pair.first.state().msr = pair.run.state().msr |= msr::ee;
      for (unsigned n = 0; n < phase; ++n) {
        pair.pc(0x80001004);
        pair.step();
      }
      pair.pc();
      pair.attempt(3, 7, phase < 5 ? 3 : 0, phase < 5 ? 7 : 0);
      for (unsigned n = 0; n < 16; ++n) {
        pair.pc();
        pair.step();
      }
    }
    for (u32 word : {immediate(7, 3, 3, 1), immediate(32, 3, 3, 0), 0x44000002u, 0xfc200090u}) {
      pair.reset();
      install(0, word, branch(16, 0, false, false, 0xfff8));
      pair.attempt(8, 8, 0);
    }
    pair.reset();
    install(0, immediate(13, 3, 3, 0xffff), branch(12, 2, true, false, 8));
    pair.first.state().gpr[3] = pair.run.state().gpr[3] = 1;
    pair.attempt(3, 7, 3, 7); // CR produced by the middle operation controls the branch
    CHECK(pair.run.state().pc == 0x80001010);
    pair.pc();
    pair.first.state().spr[spr::mmcr0] = pair.run.state().spr[spr::mmcr0] = 1 << 6;
    pair.attempt(3, 7, 0);
    pair.first.state().spr[spr::mmcr0] = pair.run.state().spr[spr::mmcr0] = 0;
    pair.first.state().msr = pair.run.state().msr |= msr::ee;
    pair.first.set_external_interrupt(true);
    pair.run.set_external_interrupt(true);
    pair.attempt(3, 7, 0);
    pair.step();
    CHECK(pair.first_memory.bytes == pair.run_memory.bytes);
  });
}

void scalar_run_tests() {
  each_backend([](Pair &pair) {
    const auto prepare = [&](unsigned offset, u32 memory, u32 middle, u32 branch) {
      pair.first_memory.plain = pair.run_memory.plain = true;
      pair.line();
      pair.line(0x1020);
      const u32 start = 0x1000 + offset * 4;
      pair.install(start, memory);
      pair.install(start + 4, middle);
      pair.install(start + 8, branch);
      pair.install(start + 12, 0); // the legacy fixture has exactly three instructions
      const auto registers = pair.first.state().gpr;
      pair.pc(0x80000000 + start);
      pair.step(); // ordinary transfer establishes the data BAT map
      pair.pc(0x80000008 + start);
      pair.step(); // both instruction lines become resident at offsets six/seven
      pair.first.state().gpr = pair.run.state().gpr = registers;
      pair.pc(0x80000000 + start);
    };
    for (unsigned primary = 32; primary <= 45; ++primary)
      for (unsigned offset = 0; offset < 8; ++offset)
        for (unsigned alignment = 0; alignment < 8; ++alignment)
          for (const auto &[destination, address] :
               std::array<std::array<unsigned, 2>, 5>{{{3, 4}, {4, 4}, {0, 4}, {3, 0}, {31, 31}}}) {
            pair.reset();
            pair.first.state().gpr[0] = pair.run.state().gpr[0] = 0x80002000;
            pair.first.state().gpr[address] = pair.run.state().gpr[address] = 0x80002000;
            const auto opcode = immediate(primary, destination, address, alignment);
            if (!(primary & 1) && !address) {
              pair.first.state().msr = pair.run.state().msr &= ~msr::dr;
              pair.install(alignment & ~3u, 0x89abcdef);
              pair.install((alignment & ~3u) + 4, 0x76543210);
            } else {
              pair.install(0x2000, 0x89abcdef);
              pair.install(0x2004, 0x76543210);
              pair.install(0x2008, 0xfedcba98);
            }
            prepare(offset, opcode, immediate(11, 0, destination, 0), 0x4bfffff8);
            pair.attempt(3, 3, 3);
            pair.step();
          }

    // A load-produced pointer can force a later transfer out of plain RAM.
    for (unsigned offset : {0u, 6u, 7u}) {
      pair.reset();
      pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80002000;
      pair.install(0x2000, 0x80003000);
      pair.install(0x3000, 0xdeadbeef);
      prepare(offset, immediate(32, 3, 4, 0), immediate(32, 5, 3, 0), 0x4bfffff8);
      pair.first_memory.blocked = pair.run_memory.blocked = 0x3000;
      pair.attempt(3, 3, 1);
      pair.step();
      CHECK(pair.run.state().gpr[5] == 0xdeadbeef);
      CHECK(pair.run_memory.records.back()[1] == 0x3000);
    }
    // The same prefix must retire before an unmapped second transfer faults.
    pair.reset();
    pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80002000;
    pair.install(0x2000, 0x50000000);
    prepare(7, immediate(32, 3, 4, 0), immediate(32, 5, 3, 0), 0x4bfffff8);
    pair.attempt(3, 3, 1);
    pair.step();
    CHECK(pair.run.state().spr[spr::dar] == 0x50000000);

    for (unsigned offset = 0; offset < 8; ++offset)
      for (unsigned bo = 0; bo < 32; ++bo)
        for (u32 counter : {0u, 1u, 2u, 0xffffffffu})
          for (bool link : {false, true}) {
            pair.reset();
            pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80002000;
            pair.install(0x2000, counter);
            const u32 branch = (16u << 26) | (bo << 21) | (2 << 16) | 0xfff8 | u32(link);
            prepare(offset, immediate(32, 3, 4, 0), immediate(11, 0, 3, 0), branch);
            pair.first.state().spr[spr::ctr] = pair.run.state().spr[spr::ctr] = counter;
            pair.first.state().spr[spr::lr] = pair.run.state().spr[spr::lr] = 0x80002003;
            pair.attempt(3, 3, 3);
            pair.pc();
            pair.step();
          }
    for (unsigned operation : {16u, 528u})
      for (unsigned bo : {4u, 12u, 20u, 28u})
        for (bool link : {false, true}) {
          pair.reset();
          pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80002000;
          pair.install(0x2000, 0);
          prepare(6, immediate(32, 3, 4, 0), immediate(11, 0, 3, 0),
                  (19u << 26) | (bo << 21) | (2 << 16) | (operation << 1) | u32(link));
          pair.first.state().spr[spr::lr] = pair.run.state().spr[spr::lr] = 0x80002003;
          pair.first.state().spr[spr::ctr] = pair.run.state().spr[spr::ctr] = 0x80003003;
          pair.attempt(3, 3, 3);
        }
    for (unsigned offset : {0u, 6u, 7u})
      for (unsigned instructions = 0; instructions < 9; ++instructions)
        for (unsigned cycles = 0; cycles < 9; ++cycles) {
          pair.reset();
          pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80002000;
          pair.install(0x2000, 1);
          prepare(offset, immediate(32, 3, 4, 0), immediate(11, 0, 3, 0), 0x4182fff8);
          const unsigned count = std::min({instructions, cycles, 3u});
          pair.attempt(instructions, cycles, count >= 2 ? count : 0);
        }
    for (unsigned phase = 0; phase < 12; ++phase) {
      pair.reset();
      pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80002000;
      prepare(6, immediate(32, 3, 4, 0), immediate(11, 0, 3, 0), 0x4182fff8);
      pair.first.state().spr[spr::dec] = pair.run.state().spr[spr::dec] = 1;
      pair.first.state().msr = pair.run.state().msr |= msr::ee;
      for (unsigned n = 0; n < phase; ++n) {
        pair.pc(0x8000101c);
        pair.step();
      }
      pair.pc(0x80001018);
      const unsigned available = phase < 11 ? std::min(3u, 11 - phase) : 0;
      pair.attempt(3, 3, available >= 2 ? available : 0);
      for (unsigned n = 0; n < 16; ++n) {
        pair.pc();
        pair.step();
      }
    }
    for (unsigned rejection = 0; rejection < 8; ++rejection) {
      pair.reset();
      pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80002000;
      prepare(6, immediate(32, 3, 4, 0), immediate(11, 0, 3, 0), 0x4182fff8);
      switch (rejection) {
      case 0:
        pair.first_memory.blocked = pair.run_memory.blocked = 0x2000;
        break;
      case 1:
        pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80002fff;
        break;
      case 2:
        pair.first.state().spr[537] = pair.run.state().spr[537] |= 0x20;
        break;
      case 3:
        pair.first.state().spr[536] = pair.run.state().spr[536] = 0;
        break;
      case 4:
        pair.first.state().spr[spr::mmcr0] = pair.run.state().spr[spr::mmcr0] = 1;
        break;
      case 5:
        pair.first.state().msr = pair.run.state().msr |= msr::ee;
        pair.first.set_external_interrupt(true);
        pair.run.set_external_interrupt(true);
        break;
      case 6:
        pair.first_memory.stable = pair.run_memory.stable = false;
        break;
      case 7:
        pair.run.state().instructions = pair.first.state().instructions =
            std::numeric_limits<u64>::max() - 1;
        break;
      }
      pair.attempt(8, 8, 0);
    }

    pair.reset();
    pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80002000;
    pair.first.state().spr[537] = pair.run.state().spr[537] |= 0x20;
    prepare(6, immediate(32, 3, 4, 0), immediate(11, 0, 3, 0), 0x4182fff8);
    pair.attempt(3, 3, 0); // a current write-through BAT is still declined

    pair.reset();
    pair.first_memory.plain = pair.run_memory.plain = true;
    pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80002000;
    pair.line();
    pair.install(0x1000, immediate(32, 3, 4, 0));
    pair.install(0x1004, immediate(11, 0, 3, 0));
    pair.install(0x1008, 0x4182fff8);
    pair.pc(0x80001008);
    pair.step(); // resident code alone cannot establish the data BAT map
    pair.pc();
    pair.attempt(3, 3, 0);
    pair.step();
    pair.pc();
    pair.attempt(3, 3, 3);

    // Stores into instruction backing retain the old resident instructions
    // until explicit invalidation, including the following line's branch.
    pair.reset();
    pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80002000;
    pair.first.state().gpr[5] = pair.run.state().gpr[5] = immediate(14, 3, 3, 1);
    prepare(6, immediate(36, 5, 4, 0), immediate(11, 0, 3, 0), 0x4182fff8);
    pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80001020;
    pair.attempt(3, 3, 3);
    pair.install(0x1100, extended(982, 0, 0, 4, false)); // ICBI of the modified line
    pair.pc(0x80001100);
    pair.step();
    pair.pc(0x80001020);
    pair.step();
    CHECK(pair.run.state().last_opcode == immediate(14, 3, 3, 1));

    // Stable RAM polling can repeat whole iterations only inside the caller's
    // quotas. The actual branch result determines whether repetition is safe.
    for (unsigned offset : {0u, 6u, 7u})
      for (unsigned primary : {32u, 34u, 40u, 42u})
        for (unsigned compare_primary : {10u, 11u})
          for (unsigned bo = 0; bo < 32; ++bo)
            if (bo & 4)
              for (u32 value : {0u, 1u, 0xffffffffu, 0x80000000u}) {
                pair.reset();
                pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80002000;
                pair.install(0x2000, value);
                prepare(offset, immediate(primary, 3, 4, 0), immediate(compare_primary, 0, 3, 0),
                        (16u << 26) | (bo << 21) | (2 << 16) | 0xfff8 | (bo & 1));
                const u32 start = pair.run.state().pc;
                for (unsigned n = 0; n < 3; ++n)
                  pair.step();
                const bool taken = pair.run.state().pc == start;
                for (unsigned quota :
                     {2u, 3u, 4u, 5u, 6u, 7u, 31u, 32u, 33u, 4095u, 4096u, 4097u}) {
                  pair.pc(start);
                  pair.attempt(quota, quota + 1, quota == 2 ? 2 : taken ? quota / 3 * 3 : 3);
                }
              }
    // A load replacing its own base cannot repeat: the next iteration can
    // observe another address, even when its first branch is taken.
    for (unsigned primary : {32u, 33u, 34u, 35u, 40u, 41u, 42u, 43u}) {
      pair.reset();
      pair.first.state().gpr[3] = pair.run.state().gpr[3] = 0x80002000;
      pair.install(0x2000, 0);
      prepare(6, immediate(primary, 3, 3, 0), immediate(11, 0, 3, 0), 0x4182fff8);
      pair.attempt(4096, 4096, 3);
    }
    for (unsigned phase = 0; phase < 12; ++phase) {
      pair.reset();
      pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x80002000;
      pair.install(0x2000, 0);
      prepare(6, immediate(32, 3, 4, 0), immediate(11, 0, 3, 0), 0x4182fff8);
      pair.first.state().spr[spr::dec] = pair.run.state().spr[spr::dec] = 2;
      for (unsigned n = 0; n < phase; ++n) {
        pair.pc(0x8000101c);
        pair.step();
      }
      pair.pc(0x80001018);
      pair.attempt(4096, 4096, (23 - phase) / 3 * 3);
      for (unsigned n = 0; n < 16; ++n) {
        pair.pc();
        pair.step();
      }
    }
    // Thrashing both sets after a grouped cross-line hit must evict exactly
    // the same resident words as ordinary fetching.
    pair.reset();
    pair.first.state().gpr[4] = pair.run.state().gpr[4] = 0x800f0000;
    pair.install(0xf0000, 0);
    prepare(6, immediate(32, 3, 4, 0), immediate(11, 0, 3, 0), 0x4182fff8);
    for (unsigned n = 1; n < 8; ++n) {
      pair.line(0x1000 + n * 4096);
      pair.line(0x1020 + n * 4096);
      pair.pc(0x80001000 + n * 4096);
      pair.step();
      pair.pc(0x80001020 + n * 4096);
      pair.step();
    }
    pair.pc(0x80001020);
    pair.step();
    pair.pc(0x80001018);
    const auto statistics_before = pair.run.native_statistics();
    pair.attempt(4096, 4096, 4095);
    if (pair.run.execution_backend() == ExecutionBackend::NativeInteger) {
      const auto statistics_after = pair.run.native_statistics();
      const u64 emitted = statistics_after.executed - statistics_before.executed;
      CHECK(emitted <= 1); // only the first comparison can invoke emitted code
      CHECK(emitted + statistics_after.fallback - statistics_before.fallback == 4095);
      CHECK(statistics_after.compiled_operations == statistics_before.compiled_operations);
    }
    for (unsigned n = 1; n <= 10; ++n) {
      pair.line(0x1000 + n * 4096);
      pair.line(0x1020 + n * 4096);
      pair.pc(0x80001000 + n * 4096);
      pair.step();
      pair.pc(0x80001020 + n * 4096);
      pair.step();
    }
    for (unsigned n = 0; n <= 10; ++n) {
      pair.pc(0x80001000 + n * 4096);
      pair.step();
      pair.pc(0x80001020 + n * 4096);
      pair.step();
    }
  });
}

void program(Pair &pair, unsigned offset, std::span<const u32> words) {
  pair.first_memory.plain = pair.run_memory.plain = true;
  pair.line(0x1000);
  pair.line(0x1020);
  const u32 start = 0x1000 + offset * 4;
  for (unsigned n = 0; n < words.size(); ++n)
    pair.install(start + n * 4, words[n]);
  const auto registers = pair.first.state().gpr;
  const u32 condition = pair.first.state().cr, counter = pair.first.state().spr[spr::ctr];
  const u32 link = pair.first.state().spr[spr::lr], status = pair.first.state().spr[spr::xer];
  for (unsigned n : {0u, static_cast<unsigned>(words.size() - 1), 0u}) {
    pair.pc(0x80000000 + start + n * 4);
    pair.step();
  }
  pair.first.state().gpr = pair.run.state().gpr = registers;
  pair.first.state().cr = pair.run.state().cr = condition;
  pair.first.state().spr[spr::ctr] = pair.run.state().spr[spr::ctr] = counter;
  pair.first.state().spr[spr::lr] = pair.run.state().spr[spr::lr] = link;
  pair.first.state().spr[spr::xer] = pair.run.state().spr[spr::xer] = status;
  pair.pc(0x80000000 + start);
}

bool taken(unsigned bo, unsigned bi, u32 ctr, u32 cr) {
  if (!(bo & 4))
    --ctr;
  return ((bo & 4) || ((ctr != 0) != ((bo & 2) != 0))) &&
         ((bo & 16) || (((cr >> (31 - bi)) & 1) == ((bo >> 3) & 1)));
}

void byte_copy_tests() {
  each_backend([](Pair &pair) {
    for (unsigned offset = 0; offset < 8; ++offset)
      for (unsigned bo = 0; bo < 32; ++bo)
        for (u32 ctr : {0u, 1u, 2u, 0xffffffffu})
          for (bool link : {false, true})
            for (bool end : {false, true})
              for (unsigned quota = 0; quota <= 9; ++quota) {
                pair.reset();
                auto &first = pair.first.state();
                first.gpr[6] = 0x80002000;
                first.gpr[31] = 0x80003000;
                first.gpr[30] = 0x80003000 + (end ? 1 : 3);
                first.gpr[4] = 3;
                first.spr[spr::ctr] = ctr;
                first.spr[spr::lr] = 0x12345678;
                pair.run.state() = first;
                pair.first_memory.bytes[0x2000] = pair.run_memory.bytes[0x2000] = 0xab;
                const std::array<u32, 8> words{0x88060000,
                                               0x981f0000,
                                               0x3bff0001,
                                               0x7c1ff040,
                                               (16u << 26) | (bo << 21) | (2u << 16) | 16u |
                                                   u32(link),
                                               0x3484ffff,
                                               0x38c60001,
                                               0x4082ffe4};
                program(pair, offset, words);
                const u32 compared_cr = end ? 2u << 28 : 8u << 28;
                const bool exit = taken(bo, 2, ctr, compared_cr);
                const unsigned maximum = !exit ? (offset >= 1 && offset <= 3 ? 8 - offset : 8) : 5;
                const unsigned count = std::min(quota, maximum);
                const auto before = pair.run.native_statistics();
                pair.attempt(quota, quota + 1, count >= 2 ? count : 0);
                const auto after = pair.run.native_statistics();
                if (pair.run.execution_backend() == ExecutionBackend::NativeInteger)
                  CHECK(after.executed - before.executed + after.fallback - before.fallback ==
                        (count >= 2 ? count : 0));
                if (count >= 2)
                  CHECK(pair.run_memory.bytes[0x3000] == 0xab);
                pair.pc();
                pair.step();
              }
  });
}

void later_memory_tests() {
  each_backend([](Pair &pair) {
    for (unsigned offset : {0u, 1u, 6u, 7u})
      for (unsigned primary = 32; primary <= 45; ++primary)
        for (unsigned address : {0u, 3u, 4u, 5u})
          for (unsigned destination : {0u, 3u, 4u, 5u})
            for (bool blocked : {false, true}) {
              pair.reset();
              pair.first.state().gpr.fill(0x80002000);
              pair.first.state().gpr[6] = 0x80003000;
              pair.first.state().gpr[7] = 7;
              pair.run.state() = pair.first.state();
              pair.install(0x2000, 0x80002000);
              pair.install(0x3000, 0x12345678);
              const std::array<u32, 8> words{immediate(32, 3, 4, 0),
                                             immediate(11, 0, 7, 0),
                                             0x41820018,
                                             immediate(primary, destination, address, 0),
                                             immediate(36, 3, 6, 0),
                                             immediate(32, 7, 6, 0),
                                             immediate(11, 0, 7, 0),
                                             0x4bffffe4};
              program(pair, offset, words);
              if (blocked)
                pair.first_memory.blocked = pair.run_memory.blocked = 0x2000;
              unsigned count = offset == 1 ? 7 : 8;
              if (blocked)
                count = 0;
              else if (!(primary & 1) && address == 0)
                count = 3; // unmapped effective zero is declined after the branch
              pair.attempt(8, 8, count);
              pair.step();
            }
  });
}

void executed_poll_prefix_tests() {
  each_backend([](Pair &pair) {
    for (unsigned offset : {0u, 6u, 7u})
      for (u32 value : {0u, 1u})
        for (unsigned quota : {2u, 3u, 4u, 7u, 8u, 9u, 4095u, 4096u, 4097u}) {
          pair.reset();
          pair.first.state().gpr[4] = 0x80002000;
          pair.run.state() = pair.first.state();
          pair.install(0x2000, value);
          const std::array<u32, 8> words{
              immediate(32, 3, 4, 0),   immediate(11, 0, 3, 0),  0x4182fff8,
              immediate(14, 7, 7, 1),   immediate(14, 8, 8, 1),  immediate(14, 9, 9, 1),
              immediate(14, 10, 10, 1), immediate(14, 11, 11, 1)};
          program(pair, offset, words);
          const unsigned count = quota == 2 ? 2 : value == 0 ? quota / 3 * 3 : std::min(quota, 8u);
          const auto before = pair.run.native_statistics();
          pair.attempt(quota, quota, count);
          const auto after = pair.run.native_statistics();
          if (pair.run.execution_backend() == ExecutionBackend::NativeInteger) {
            CHECK(after.executed - before.executed + after.fallback - before.fallback == count);
            CHECK(after.compiled_operations == before.compiled_operations);
          }
        }
  });
}

void later_decline_tests() {
  each_backend([](Pair &p) {
    for (unsigned offset : {0u, 6u, 7u})
      for (unsigned cause = 0; cause < 4; ++cause)
        for (unsigned primary : {32u, 33u, 36u, 37u, 40u, 41u, 44u, 45u}) {
          p.reset();
          p.first.state().gpr[4] = 0x80002000;
          p.first.state().gpr[7] = 1;
          p.run.state() = p.first.state();
          u32 address = cause == 0   ? 0x80003000
                        : cause == 1 ? 0x80002fff
                        : cause == 2 ? 0x50000000
                                     : 0x80023000;
          p.install(0x2000, address);
          const std::array<u32, 8> words{immediate(32, 3, 4, 0),
                                         immediate(11, 0, 7, 0),
                                         0x41820018,
                                         immediate(primary, 5, 3, 0),
                                         immediate(14, 8, 8, 1),
                                         immediate(14, 9, 9, 1),
                                         immediate(14, 10, 10, 1),
                                         0x4bffffe4};
          program(p, offset, words);
          if (cause == 0)
            p.first_memory.blocked = p.run_memory.blocked = 0x3000;
          if (cause == 3) {
            p.first.state().spr[538] = p.run.state().spr[538] = 0x80020003;
            p.first.state().spr[539] = p.run.state().spr[539] = 0x20002;
          }
          p.attempt(8, 8, cause == 3 ? 0 : 3);
          p.step();
          if (cause == 2)
            CHECK(p.run.state().spr[spr::dar] == address);
          if (cause == 0)
            CHECK(p.run_memory.records.back()[1] == 0x3000);
          // A changed data BAT must be rejected before a register or RAM prefix.
          if (cause == 3) {
            p.pc(0x80000000 + 0x1000 + offset * 4);
            p.step();
            p.pc(0x80000000 + 0x1000 + offset * 4);
            p.attempt(8, 8, 8);
          }
        }
  });
}

void sequential_target_tests() {
  each_backend([](Pair &p) {
    for (unsigned offset : {0u, 6u, 7u})
      for (bool take : {false, true})
        for (bool link : {false, true}) {
          p.reset();
          p.first.state().gpr[4] = 0x80002000;
          p.run.state() = p.first.state();
          p.install(0x2000, take ? 0 : 1);
          const std::array<u32, 8> words{immediate(32, 3, 4, 0),   immediate(11, 0, 3, 0),
                                         0x41820004 | u32(link),   immediate(14, 7, 7, 1),
                                         immediate(14, 8, 8, 1),   immediate(14, 9, 9, 1),
                                         immediate(14, 10, 10, 1), 0x4bffffe4};
          program(p, offset, words);
          p.attempt(8, 8, !take ? 8 : 3);
          p.step();
        }
  });
}

void native_hit_tests() {
  each_backend([](Pair &p) {
    for (unsigned offset : {0u, 6u, 7u}) {
      p.reset();
      p.first.state().gpr[4] = 0x80002000;
      p.first.state().gpr[6] = 0x80003000;
      p.first.state().gpr[7] = 1;
      p.run.state() = p.first.state();
      p.install(0x2000, 0x12345678);
      const std::array<u32, 8> words{immediate(32, 3, 4, 0),
                                     immediate(11, 0, 7, 0),
                                     0x41820018,
                                     immediate(14, 8, 8, 1),
                                     immediate(36, 3, 6, 0),
                                     immediate(32, 9, 6, 0),
                                     immediate(11, 0, 9, 0),
                                     0x4bffffe4};
      program(p, offset, words);
      for (unsigned n : {1u, 3u, 6u}) {
        p.pc(0x80001000 + offset * 4 + n * 4);
        p.step();
      }
      p.pc(0x80001000 + offset * 4);
      const auto before = p.run.native_statistics();
      p.attempt(8, 8, 8);
      const auto after = p.run.native_statistics();
      if (p.run.execution_backend() == ExecutionBackend::NativeInteger) {
        CHECK(after.executed - before.executed == 3);
        CHECK(after.fallback - before.fallback == 5);
        CHECK(after.compiled_operations == before.compiled_operations);
      }
    }
  });
}

void code_alias_tests() {
  each_backend([](Pair &p) {
    for (unsigned offset : {0u, 6u, 7u}) {
      p.reset();
      p.first.state().gpr[4] = 0x80002000;
      p.first.state().gpr[6] = 0x80001000 + offset * 4 + 16;
      p.first.state().gpr[7] = 1;
      p.run.state() = p.first.state();
      p.install(0x2000, immediate(14, 8, 8, 7));
      const std::array<u32, 8> words{immediate(32, 3, 4, 0),
                                     immediate(11, 0, 7, 0),
                                     0x41820018,
                                     immediate(36, 3, 6, 0),
                                     immediate(14, 8, 8, 1),
                                     immediate(14, 9, 9, 1),
                                     immediate(14, 10, 10, 1),
                                     0x4bffffe4};
      program(p, offset, words);
      p.attempt(8, 8, 8);
      CHECK(p.run.state().gpr[8] == 1);
      p.install(0x1100, extended(982, 0, 0, 6, false));
      p.pc(0x80001100);
      p.step();
      p.pc(p.run.state().gpr[6]);
      p.step();
      CHECK(p.run.state().gpr[8] == 8);
      for (unsigned n = 1; n <= 10; ++n) {
        p.line(0x1000 + n * 4096);
        p.line(0x1020 + n * 4096);
        p.pc(0x80001000 + n * 4096);
        p.step();
        p.pc(0x80001020 + n * 4096);
        p.step();
      }
      for (unsigned n = 0; n <= 10; ++n) {
        p.pc(0x80001000 + n * 4096);
        p.step();
        p.pc(0x80001020 + n * 4096);
        p.step();
      }
    }
  });
}

void false_hint_tests() {
  each_backend([](Pair &p) {
    for (unsigned offset = 0; offset < 8; ++offset) {
      p.reset();
      p.first_memory.plain = p.run_memory.plain = true;
      p.first.state().gpr[4] = p.run.state().gpr[4] = 0x80002000;
      const std::array<u32, 8> words{immediate(32, 3, 4, 0),
                                     0x4182001c,
                                     0,
                                     immediate(32, 3, 4, 0),
                                     0,
                                     immediate(32, 3, 4, 0),
                                     immediate(14, 3, 3, 1),
                                     immediate(32, 3, 4, 0)};
      for (unsigned n = 0; n < 8; ++n)
        p.install(0x1000 + n * 4, words[n]);
      p.pc();
      p.step();
      p.pc(0x80001000 + offset * 4);
      const bool expected = offset == 0 || offset >= 5;
      CHECK(p.first.integer_run_possible() == expected && p.run.integer_run_possible() == expected);
      if (offset >= 1 && offset <= 3)
        p.attempt(8, 8, 0);
    }
  });
}

void cache_flush_loop_program(Pair &pair, u32 counter, u32 middle = 0x38630020,
                              u32 branch = 0x4200fff8, unsigned offset = 3) {
  pair.first_memory.plain = pair.run_memory.plain = true;
  pair.line();
  pair.install(0x1000 + offset * 4, 0x7c0018ac);
  pair.install(0x1004 + offset * 4, middle);
  pair.install(0x1008 + offset * 4, branch);
  pair.first.state().spr[spr::ctr] = pair.run.state().spr[spr::ctr] = counter;
  pair.pc(0x80001000 + offset * 4);
  pair.step();
  pair.pc(0x80001000 + offset * 4);
}

void cache_flush_loop_quota_tests() {
  struct Quota {
    u32 instructions, cycles;
    unsigned retired, elapsed;
  };

  // Each loop is three instructions and seven cycles. The first row also
  // makes a short single-iteration implementation fail this fixture.
  constexpr std::array<Quota, 21> quotas{{{4096, 4096, 1755, 4095},
                                          {0, 4096, 0, 0},
                                          {1, 4096, 0, 0},
                                          {2, 4096, 0, 0},
                                          {3, 6, 0, 0},
                                          {3, 7, 3, 7},
                                          {3, 8, 3, 7},
                                          {4, 7, 3, 7},
                                          {5, 14, 3, 7},
                                          {6, 13, 3, 7},
                                          {6, 14, 6, 14},
                                          {7, 14, 6, 14},
                                          {8, 21, 6, 14},
                                          {9, 20, 6, 14},
                                          {9, 21, 9, 21},
                                          {10, 21, 9, 21},
                                          {4095, 4095, 1755, 4095},
                                          {4097, 4097, 1755, 4095},
                                          {3, 0xffffffff, 3, 7},
                                          {0xffffffff, 7, 3, 7},
                                          {0xffffffff, 4096, 1755, 4095}}};
  each_backend([&](Pair &pair) {
    for (unsigned offset : {0u, 3u, 5u})
      for (const auto &quota : quotas) {
        pair.reset();
        cache_flush_loop_program(pair, 1000, 0x38630020, 0x4200fff8, offset);
        const auto before = pair.run.native_statistics();
        pair.attempt(quota.instructions, quota.cycles, quota.retired, quota.elapsed);
        if (quota.retired && pair.run.execution_backend() == ExecutionBackend::NativeInteger) {
          const auto after = pair.run.native_statistics();
          CHECK(after.executed - before.executed == 1);
          CHECK(after.fallback - before.fallback == quota.retired - 1);
          CHECK(after.compiled_operations - before.compiled_operations == 1);
        }
        pair.pc(0x80001000 + offset * 4);
        pair.step();
      }
    for (unsigned offset : {6u, 7u}) {
      pair.reset();
      cache_flush_loop_program(pair, 1000, 0x38630020, 0x4200fff8, offset);
      pair.attempt(4096, 4096, 0);
      pair.step();
    }
  });
}

void cache_flush_loop_counter_tests() {
  struct Counter {
    u32 initial, final;
    unsigned retired, elapsed;
    bool terminal;
  };

  constexpr std::array<Counter, 7> counters{{{1, 0, 3, 7, true},
                                             {2, 0, 6, 14, true},
                                             {3, 0, 9, 21, true},
                                             {7, 0, 21, 49, true},
                                             {1000, 415, 1755, 4095, false},
                                             {0, 0xfffffdb7, 1755, 4095, false},
                                             {0xffffffff, 0xfffffdb6, 1755, 4095, false}}};
  each_backend([&](Pair &pair) {
    for (u32 branch : {0x4200fff8u, 0x423ffff9u})
      for (const auto &counter : counters) {
        pair.reset();
        pair.first.state().spr[spr::lr] = pair.run.state().spr[spr::lr] = 0x12345678;
        cache_flush_loop_program(pair, counter.initial, 0x38630020, branch);
        pair.attempt(4096, 4096, counter.retired, counter.elapsed);
        CHECK(pair.run.state().spr[spr::ctr] == counter.final);
        CHECK(pair.run.state().pc == (counter.terminal ? 0x80001018u : 0x8000100cu));
        CHECK(pair.run.state().spr[spr::lr] ==
              ((branch & 1) && counter.initial != 1 ? 0x80001018u : 0x12345678u));
        pair.pc(0x8000100c);
        pair.step();
      }
  });
}

void cache_flush_loop_integer_tests() {
  struct Integer {
    unsigned destination, base;
    u16 immediate;
    u32 before, after;
    unsigned retired, elapsed;
  };

  constexpr std::array<Integer, 11> integers{{{3, 3, 32, 0xfffffff0, 0x70, 12, 28},
                                              {3, 3, 0xffff, 1, 0xfffffffd, 12, 28},
                                              {3, 3, 0x8000, 0x7fffffff, 0x7ffdffff, 12, 28},
                                              {3, 3, 0x7fff, 0x80000000, 0x8001fffc, 12, 28},
                                              {31, 31, 0x8000, 0, 0xfffe0000, 12, 28},
                                              {4, 4, 0, 0x87654321, 0x87654321, 12, 28},
                                              {1, 1, 1, 0xffffffff, 3, 12, 28},
                                              {0, 0, 32, 0xffffffff, 32, 3, 7},
                                              {3, 0, 32, 0xdeadbeef, 32, 3, 7},
                                              {3, 4, 32, 5, 37, 3, 7},
                                              {4, 3, 0xffff, 0, 0xffffffff, 3, 7}}};
  each_backend([&](Pair &pair) {
    for (const auto &integer : integers) {
      pair.reset();
      pair.first.state().gpr.fill(integer.before);
      pair.run.state() = pair.first.state();
      cache_flush_loop_program(pair, 4,
                               immediate(14, integer.destination, integer.base, integer.immediate));
      pair.attempt(4096, 4096, integer.retired, integer.elapsed);
      CHECK(pair.run.state().gpr[integer.destination] == integer.after);
      pair.pc(0x8000100c);
      pair.step();
    }
    for (u32 middle :
         {immediate(15, 3, 3, 1), immediate(13, 3, 3, 0xffff), extended(266, 3, 3, 4, false)}) {
      pair.reset();
      cache_flush_loop_program(pair, 4, middle);
      pair.attempt(4096, 4096, 3, 7);
      pair.pc(0x8000100c);
      pair.step();
    }
    for (unsigned operation : {54u, 470u, 1014u}) {
      pair.reset();
      pair.first_memory.plain = pair.run_memory.plain = true;
      pair.line();
      pair.install(0x1000, extended(operation, 0, 0, 0, false));
      pair.install(0x1004, 0x38630020);
      pair.install(0x1008, 0x4200fff8);
      pair.pc(0x80001004);
      pair.step();
      pair.pc();
      pair.attempt(4096, 4096, 0);
    }
  });
}

void cache_flush_loop_branch_tests() {
  constexpr std::array<std::array<unsigned, 2>, 12> options{{{0, 3},
                                                             {2, 3},
                                                             {4, 3},
                                                             {8, 3},
                                                             {12, 3},
                                                             {14, 3},
                                                             {16, 9},
                                                             {17, 9},
                                                             {18, 3},
                                                             {20, 3},
                                                             {24, 3},
                                                             {31, 3}}};
  each_backend([&](Pair &pair) {
    for (const auto &option : options)
      for (unsigned bit : {0u, 31u})
        for (u32 cr : {0u, 0xffffffffu}) {
          pair.reset();
          pair.first.state().cr = pair.run.state().cr = cr;
          const u32 branch = (16u << 26) | (option[0] << 21) | (bit << 16) | 0xfff9;
          cache_flush_loop_program(pair, 7, 0x38630020, branch);
          pair.attempt(9, 21, option[1], option[1] / 3 * 7);
          pair.pc(0x8000100c);
          pair.step();
        }
    for (u32 branch : {0x4200fffau, 0x42000000u, 0x4200fffcu, 0x42000004u}) {
      pair.reset();
      cache_flush_loop_program(pair, 7, 0x38630020, branch);
      pair.attempt(9, 21, 3, 7);
      pair.pc(0x8000100c);
      pair.step();
    }
  });
}

void cache_flush_loop_decrementer_tests() {
  // Positive DEC values1,2,3 leave these literal whole-loop instruction
  // counts before the transition, for each of its twelve remainder phases.
  constexpr std::array<std::array<unsigned, 12>, 3> retired{
      {{3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0},
       {9, 9, 9, 6, 6, 6, 6, 6, 6, 6, 3, 3},
       {15, 12, 12, 12, 12, 12, 12, 12, 9, 9, 9, 9}}};
  each_backend([&](Pair &pair) {
    for (unsigned phase = 0; phase < 12; ++phase)
      for (unsigned dec = 1; dec <= 3; ++dec)
        for (bool enabled : {false, true}) {
          pair.reset();
          cache_flush_loop_program(pair, 1000);
          pair.first.state().spr[spr::dec] = pair.run.state().spr[spr::dec] = 10;
          for (unsigned pad = 0; pad < phase; ++pad) {
            pair.pc(0x80001010);
            pair.step();
          }
          pair.first.state().spr[spr::dec] = pair.run.state().spr[spr::dec] = dec;
          if (enabled)
            pair.first.state().msr = pair.run.state().msr |= msr::ee;
          pair.pc(0x8000100c);
          const unsigned count = retired[dec - 1][phase];
          pair.attempt(4096, 4096, count, count / 3 * 7);
          for (unsigned follow = 0; follow < 8; ++follow) {
            pair.pc(0x8000100c);
            pair.step();
          }
        }
  });
}

void cache_flush_loop_guard_tests() {
  each_backend([](Pair &pair) {
    for (unsigned kind = 0; kind < 15; ++kind) {
      pair.reset();
      cache_flush_loop_program(pair, 1000);
      auto &first = pair.first.state();
      auto &run = pair.run.state();
      switch (kind) {
      case 0:
        pair.first_memory.stable = pair.run_memory.stable = false;
        break;
      case 1:
        first.spr[spr::hid0] = run.spr[spr::hid0] = 0;
        break;
      case 2:
        first.spr[spr::hid0] = run.spr[spr::hid0] = 0xa000;
        break;
      case 3:
        first.spr[spr::mmcr0] = run.spr[spr::mmcr0] = 0x80000000;
        break;
      case 4:
        first.spr[spr::mmcr1] = run.spr[spr::mmcr1] = 1;
        break;
      case 5:
        first.msr = run.msr |= msr::ee;
        pair.first.set_external_interrupt(true);
        pair.run.set_external_interrupt(true);
        break;
      case 6:
        first.msr = run.msr |= msr::le;
        break;
      case 7:
        first.spr[528] = run.spr[528] ^= 4;
        break;
      case 8:
        first.spr[529] = run.spr[529] ^= 0x20000;
        break;
      case 9:
        first.instructions = run.instructions = std::numeric_limits<u64>::max() - 7;
        break;
      case 10:
        first.spr[spr::dec] = run.spr[spr::dec] = 0;
        break;
      case 11:
        first.spr[528] = run.spr[528] = 0;
        pair.step(); // establish an up-to-date map without an instruction BAT hit
        first.msr = run.msr = msr::ir;
        break;
      case 12:
        if (run.spr[spr::pvr] != 0x00087102)
          continue;
        first.spr[spr::hid4] = run.spr[spr::hid4] ^= 0x02000000;
        break;
      case 13:
        first.spr[spr::mmcr0] = run.spr[spr::mmcr0] = (1 << 6) | (1 << 15);
        first.spr[spr::pmc1] = run.spr[spr::pmc1] = 0x7fffffff;
        pair.step();
        first.spr[spr::mmcr0] = run.spr[spr::mmcr0] = 0;
        first.msr = run.msr |= msr::ee;
        break;
      case 14:
        first.spr[spr::dec] = run.spr[spr::dec] = 0;
        pair.step();
        first.msr = run.msr |= msr::ee;
        break;
      }
      pair.pc(0x8000100c);
      pair.attempt(4096, 4096, 0);
    }
  });
}

void cache_flush_loop_cache_tests() {
  each_backend([](Pair &pair) {
    for (unsigned offset : {3u, 5u}) {
      pair.reset();
      cache_flush_loop_program(pair, 7, 0x38630020, 0x4200fff8, offset);
      pair.install(0x1004 + offset * 4, 0x38630007);
      pair.attempt(4096, 4096, 21, 49);
      CHECK(pair.run.state().gpr[3] == 224); // resident code still contains ADDI32
      pair.install(0x1100, extended(982, 0, 0, 6, false));
      pair.first.state().gpr[6] = pair.run.state().gpr[6] = 0x80001000 + offset * 4;
      pair.pc(0x80001100);
      pair.step();
      pair.pc(0x80001000 + offset * 4);
      pair.attempt(4096, 4096, 0);
      pair.step();
      pair.first.state().spr[spr::ctr] = pair.run.state().spr[spr::ctr] = 3;
      pair.pc(0x80001000 + offset * 4);
      pair.attempt(4096, 4096, 9, 21);
      CHECK(pair.run.state().gpr[3] == 245);
      for (unsigned way = 1; way <= 10; ++way) {
        pair.line(0x1000 + way * 4096);
        pair.pc(0x80001000 + way * 4096);
        pair.step();
      }
      for (unsigned way = 0; way <= 10; ++way) {
        pair.pc(0x80001000 + way * 4096);
        pair.step();
      }
    }
    pair.reset();
    pair.first.state().gpr[3] = pair.run.state().gpr[3] = 0xcc008000;
    cache_flush_loop_program(pair, 3);
    pair.first_memory.plain = pair.run_memory.plain = false;
    pair.first.state().spr[536] = pair.run.state().spr[536] = 0;
    pair.first.state().spr[537] = pair.run.state().spr[537] = 0;
    pair.attempt(4096, 4096, 9, 21);
    CHECK(pair.first_memory.bytes == pair.run_memory.bytes);
  });
}

void cache_flush_loop_wrap_tests() {
  struct Headroom {
    u64 capacity;
    u32 quota;
    unsigned retired, elapsed;
  };

  constexpr std::array<Headroom, 8> headroom{{{0, 3, 0, 0},
                                              {3, 3, 3, 7},
                                              {3, 8, 0, 0},
                                              {7, 4096, 0, 0},
                                              {8, 4096, 6, 14},
                                              {9, 4096, 9, 21},
                                              {11, 4096, 9, 21},
                                              {32, 4096, 30, 70}}};
  each_backend([&](Pair &pair) {
    for (const auto &limit : headroom) {
      pair.reset();
      cache_flush_loop_program(pair, 1000);
      pair.first.state().instructions = pair.run.state().instructions =
          std::numeric_limits<u64>::max() - limit.capacity;
      pair.first.state().cycles = pair.run.state().cycles = std::numeric_limits<u64>::max() - 5;
      pair.first.state().timebase = pair.run.state().timebase = std::numeric_limits<u64>::max() - 1;
      pair.attempt(limit.quota, 4096, limit.retired, limit.elapsed);
      pair.pc(0x8000100c);
      pair.step();
    }
    struct PcWrap {
      u32 counter, quota, final_counter;
      unsigned retired, elapsed;
      u32 pc, lr;
    };
    constexpr std::array<PcWrap, 5> wraps{{{0, 4096, 0xfffffdb7, 1755, 4095, 0xfffffff4, 0},
                                           {1, 4096, 0, 3, 7, 0, 0x12345678},
                                           {2, 4096, 0, 6, 14, 0, 0},
                                           {7, 4096, 0, 21, 49, 0, 0},
                                           {7, 6, 5, 6, 14, 0xfffffff4, 0}}};
    for (const auto &wrap : wraps) {
      pair.reset();
      pair.first_memory.plain = pair.run_memory.plain = true;
      pair.first.state().spr[528] = pair.run.state().spr[528] = 0xfffe0003;
      pair.first.state().spr[529] = pair.run.state().spr[529] = 2;
      pair.install(0x1fff4, 0x7c0018ac);
      pair.install(0x1fff8, 0x3863ffe0);
      pair.install(0x1fffc, 0x423ffff9);
      pair.first.state().gpr[3] = pair.run.state().gpr[3] = 3;
      pair.first.state().spr[spr::ctr] = pair.run.state().spr[spr::ctr] = wrap.counter;
      pair.first.state().spr[spr::lr] = pair.run.state().spr[spr::lr] = 0x12345678;
      pair.pc(0xfffffff4);
      pair.step();
      pair.pc(0xfffffff4);
      pair.attempt(wrap.quota, 4096, wrap.retired, wrap.elapsed);
      CHECK(pair.run.state().pc == wrap.pc && pair.run.state().spr[spr::lr] == wrap.lr);
      CHECK(pair.run.state().spr[spr::ctr] == wrap.final_counter);
      pair.pc(0xfffffff4);
      pair.step();
    }
  });
}

void cache_flush_loop_tick_limit_tests() {
  // This independent large-quota State oracle complements bounded ordinary
  // differential execution. It avoids stepping billions of instructions.
  each_backend([](Pair &pair) {
    for (unsigned phase = 0; phase < 12; ++phase) {
      pair.reset();
      cache_flush_loop_program(pair, 0, 0x38630000, 0x423ffff9, 0);
      pair.install(0x1100, immediate(14, 20, 20, 0));
      pair.pc(0x80001100);
      pair.step();
      for (unsigned pad = 0; pad < (phase + 12 - 11) % 12; ++pad) {
        pair.pc(0x80001100);
        pair.step();
      }
      pair.pc();
      pair.step();
      pair.pc();
      pair.first.state().instructions = pair.run.state().instructions = 0;
      pair.first.state().cycles = pair.run.state().cycles = std::numeric_limits<u64>::max() - 3;
      pair.first.state().timebase = pair.run.state().timebase =
          std::numeric_limits<u64>::max() - 17;
      State expected = pair.run.state();
      expected.spr[spr::ctr] = 0xdb6db6de;
      expected.spr[spr::lr] = 0x8000100c;
      expected.last_pc = 0x80001008;
      expected.last_opcode = 0x423ffff9;
      expected.instructions = 1840700262;
      expected.cycles = 4294967274;
      expected.timebase = 357913921 + (phase >= 2 ? 1u : 0u);
      const auto before = pair.run.native_statistics();
      const auto result = pair.run.try_integer_run(0xffffffff, 0xffffffff);
      CHECK(result.instructions == 1840700262 && result.cycles == 4294967278);
      equal_state(expected, pair.run.state());
      CHECK(pair.first_memory.bytes == pair.run_memory.bytes);
      CHECK(pair.first_memory.records == pair.run_memory.records);
      if (pair.run.execution_backend() == ExecutionBackend::NativeInteger) {
        const auto after = pair.run.native_statistics();
        CHECK(after.executed - before.executed == 1);
        CHECK(after.fallback - before.fallback == 1840700261);
      }
      // Ten ordinary cycles establish the oracle's new hidden remainder,
      // independently of its restored literal architectural State above.
      for (unsigned pad = 0; pad < 5; ++pad) {
        pair.first.state().pc = 0x80001100;
        pair.first.step();
      }
      pair.first.state().pc = 0x80001000;
      pair.first.step();
      pair.first.state() = expected;
      pair.verify();
      pair.pc();
      pair.step();
    }
  });
}

void cache_flush_loop_tests() {
  std::cout << comparisons << " existing complete state and ordered bus comparisons; "
            << grouped_instructions << " existing grouped instructions passed\n";
  cache_flush_loop_quota_tests();
  cache_flush_loop_counter_tests();
  cache_flush_loop_integer_tests();
  cache_flush_loop_branch_tests();
  cache_flush_loop_decrementer_tests();
  cache_flush_loop_guard_tests();
  cache_flush_loop_cache_tests();
  cache_flush_loop_wrap_tests();
  cache_flush_loop_tick_limit_tests();
}

void register_branch_program(Pair &pair, unsigned offset, std::span<const u32> words) {
  pair.first_memory.plain = pair.run_memory.plain = true;
  pair.line();
  pair.line(0x1020);
  for (unsigned n = 0; n < words.size(); ++n)
    pair.install(0x1000 + offset * 4 + n * 4, words[n]);
  const auto saved = pair.first.state();
  pair.run.state() = saved;
  if (offset + words.size() > 8) {
    pair.pc(0x80001020);
    pair.step();
  }
  pair.pc(0x80001000 + offset * 4);
  pair.step();
  pair.first.state() = pair.run.state() = saved;
  pair.pc(0x80001000 + offset * 4);
}

void register_branch_condition_tests() {
  each_backend([](Pair &pair) {
    for (unsigned offset : {0u, 5u, 7u})
      for (unsigned bo = 0; bo < 32; ++bo)
        for (unsigned bi = 0; bi < 32; ++bi)
          for (u32 counter : {0u, 1u, 2u, 0xffffffffu})
            for (bool condition : {false, true})
              for (bool link : {false, true}) {
                pair.reset();
                const u32 start = 0x80001000 + offset * 4;
                pair.first.state().spr[spr::ctr] = counter;
                pair.first.state().spr[spr::lr] = 0x12345678;
                pair.first.state().cr = condition ? 1u << (31 - bi) : 0;
                const u32 branch = (16u << 26) | (bo << 21) | (bi << 16) | 16u | u32(link);
                const std::array<u32, 8> words{immediate(14, 3, 3, 1), branch,
                                               immediate(14, 4, 4, 1), immediate(14, 5, 5, 1),
                                               immediate(14, 6, 6, 1), immediate(14, 7, 7, 1),
                                               immediate(14, 8, 8, 1), immediate(14, 9, 9, 1)};
                register_branch_program(pair, offset, words);
                const u32 final_counter = bo & 4 ? counter : counter - 1;
                const bool take = ((bo & 4) || ((final_counter != 0) != ((bo & 2) != 0))) &&
                                  ((bo & 16) || (condition == ((bo & 8) != 0)));
                const unsigned count = take ? 2 : 8;
                pair.attempt(8, 8, count);
                CHECK(pair.run.state().pc == (take ? start + 20 : start + 32));
                CHECK(pair.run.state().spr[spr::ctr] == final_counter);
                CHECK(pair.run.state().spr[spr::lr] == (take && link ? start + 8 : 0x12345678));
                CHECK(pair.run.state().gpr[3] == 1 && pair.run.state().gpr[4] == !take);
              }
  });
}

void register_branch_boundary_tests() {
  each_backend([](Pair &pair) {
    for (unsigned offset : {0u, 6u, 7u})
      for (unsigned instructions = 0; instructions <= 10; ++instructions)
        for (unsigned cycles = 0; cycles <= 10; ++cycles)
          for (bool take : {false, true}) {
            pair.reset();
            pair.first.state().cr = take ? 2u << 28 : 0;
            const std::array<u32, 8> words{immediate(14, 3, 3, 1), 0x41820004,
                                           immediate(14, 4, 4, 1), immediate(14, 5, 5, 1),
                                           immediate(14, 6, 6, 1), immediate(14, 7, 7, 1),
                                           immediate(14, 8, 8, 1), immediate(14, 9, 9, 1)};
            register_branch_program(pair, offset, words);
            const unsigned limit = std::min({instructions, cycles, 8u});
            const unsigned count = limit < 2 ? 0 : take ? 2 : limit;
            pair.attempt(instructions, cycles, count);
            if (count)
              CHECK(pair.run.state().pc == 0x80001000 + offset * 4 + (take ? 8u : count * 4));
            pair.pc(0x80001000 + offset * 4);
            pair.step();
          }
    for (unsigned phase = 0; phase < 12; ++phase)
      for (bool enabled : {false, true}) {
        pair.reset();
        const std::array<u32, 8> words{immediate(14, 3, 3, 1), 0x41820010,
                                       immediate(14, 4, 4, 1), immediate(14, 5, 5, 1),
                                       immediate(14, 6, 6, 1), immediate(14, 7, 7, 1),
                                       immediate(14, 8, 8, 1), immediate(14, 9, 9, 1)};
        register_branch_program(pair, 0, words);
        pair.first.state().spr[spr::dec] = pair.run.state().spr[spr::dec] = 10;
        for (unsigned n = 0; n < phase; ++n) {
          pair.pc(0x8000100c);
          pair.step();
        }
        pair.first.state().spr[spr::dec] = pair.run.state().spr[spr::dec] = 1;
        if (enabled)
          pair.first.state().msr = pair.run.state().msr |= msr::ee;
        pair.pc();
        const unsigned available = 11 - phase;
        pair.attempt(8, 8, available >= 2 ? std::min(available, 8u) : 0);
        for (unsigned n = 0; n < 16; ++n) {
          pair.pc();
          pair.step();
        }
      }
  });
}

void register_branch_visibility_tests() {
  each_backend([](Pair &pair) {
    const std::array<u32, 8> words{0x2c070000,
                                   0x4082003c,
                                   immediate(14, 4, 4, 1),
                                   immediate(14, 5, 5, 1),
                                   immediate(14, 6, 6, 1),
                                   immediate(14, 7, 7, 1),
                                   immediate(14, 8, 8, 1),
                                   0};
    for (unsigned offset : {0u, 5u, 7u}) {
      pair.reset();
      pair.first.state().gpr[7] = 1;
      register_branch_program(pair, offset, words);
      pair.install(0x1000 + offset * 4, 0x2c070001);
      pair.attempt(8, 8, 2); // resident CMPWI still compares against zero
      pair.install(0x1100, extended(982, 0, 0, 6, false));
      pair.first.state().gpr[6] = pair.run.state().gpr[6] = 0x80001000 + offset * 4;
      pair.pc(0x80001100);
      pair.step();
      pair.pc(0x80001000 + offset * 4);
      pair.attempt(8, 8, 0);
      pair.step();
      pair.pc(0x80001000 + offset * 4);
      pair.attempt(8, 8, 7);
      CHECK(pair.run.state().cr >> 28 == 2);
      for (unsigned way = 1; way <= 10; ++way) {
        pair.line(0x1000 + way * 4096);
        pair.pc(0x80001000 + way * 4096);
        pair.step();
      }
      for (unsigned way = 0; way <= 10; ++way) {
        pair.pc(0x80001000 + way * 4096);
        pair.step();
      }
    }
  });
}

void register_branch_guard_tests() {
  each_backend([](Pair &pair) {
    for (unsigned kind = 0; kind < 12; ++kind) {
      pair.reset();
      const std::array<u32, 3> words{0x2c070000, 0x4082003c, 0};
      register_branch_program(pair, 0, words);
      auto &first = pair.first.state();
      auto &run = pair.run.state();
      switch (kind) {
      case 0:
        first.spr[spr::hid0] = run.spr[spr::hid0] = 0;
        break;
      case 1:
        first.spr[spr::hid0] = run.spr[spr::hid0] = 0xa000;
        break;
      case 2:
        first.msr = run.msr |= msr::le;
        break;
      case 3:
        first.spr[spr::mmcr0] = run.spr[spr::mmcr0] = 1;
        break;
      case 4:
        first.spr[spr::mmcr1] = run.spr[spr::mmcr1] = 1;
        break;
      case 5:
        first.msr = run.msr |= msr::ee;
        pair.first.set_external_interrupt(true);
        pair.run.set_external_interrupt(true);
        break;
      case 6:
        first.spr[528] = run.spr[528] ^= 4;
        break;
      case 7:
        first.spr[529] = run.spr[529] ^= 0x20000;
        break;
      case 8:
        pair.first_memory.stable = pair.run_memory.stable = false;
        break;
      case 9:
        first.instructions = run.instructions = std::numeric_limits<u64>::max() - 7;
        break;
      case 10:
        first.spr[spr::dec] = run.spr[spr::dec] = 0;
        pair.step();
        first.msr = run.msr |= msr::ee;
        break;
      case 11:
        first.spr[spr::mmcr0] = run.spr[spr::mmcr0] = (1 << 6) | (1 << 15);
        first.spr[spr::pmc1] = run.spr[spr::pmc1] = 0x7fffffff;
        pair.step();
        first.spr[spr::mmcr0] = run.spr[spr::mmcr0] = 0;
        first.msr = run.msr |= msr::ee;
        break;
      }
      pair.pc();
      pair.attempt(8, 8, 0);
    }
    for (u32 prefix : {0x2c070000u, 0x55000631u, 0x38e7ffffu})
      for (bool take : {false, true})
        for (bool absolute : {false, true})
          for (bool link : {false, true}) {
            pair.reset();
            pair.first.state().spr[spr::lr] = 0x12345678;
            pair.first.state().gpr[7] = take ? 0 : 1;
            pair.first.state().gpr[8] = take ? 0 : 0x80;
            pair.first.state().cr = take ? 2u << 28 : 0;
            const u32 branch = 0x41820010 | (absolute ? 2u : 0u) | u32(link);
            const std::array<u32, 3> words{prefix, branch, 0};
            register_branch_program(pair, 0, words);
            const auto before = pair.run.native_statistics();
            pair.attempt(8, 8, 2);
            const bool taken = (pair.run.state().cr & (2u << 28)) != 0;
            CHECK(pair.run.state().pc == (taken ? absolute ? 16u : 0x80001014u : 0x80001008u));
            CHECK(pair.run.state().spr[spr::lr] == (taken && link ? 0x80001008u : 0x12345678u));
            if (pair.run.execution_backend() == ExecutionBackend::NativeInteger) {
              const auto after = pair.run.native_statistics();
              CHECK(after.executed - before.executed == 1);
              CHECK(after.fallback - before.fallback == 1);
              CHECK(after.compiled_operations == before.compiled_operations);
              CHECK(after.block_runs == before.block_runs);
            }
          }
  });
}

void register_branch_address_tests() {
  each_backend([](Pair &pair) {
    for (bool take : {false, true})
      for (bool link : {false, true}) {
        pair.reset();
        pair.first_memory.plain = pair.run_memory.plain = true;
        pair.first.state().spr[528] = pair.run.state().spr[528] = 0xfffe0003;
        pair.first.state().spr[529] = pair.run.state().spr[529] = 2;
        pair.install(0x1fff8, 0x2c070000);
        pair.install(0x1fffc, 0x41820004 | u32(link));
        pair.first.state().gpr[7] = pair.run.state().gpr[7] = !take;
        pair.first.state().spr[spr::lr] = pair.run.state().spr[spr::lr] = 0x12345678;
        pair.pc(0xfffffff8);
        pair.step();
        pair.first.state().cycles = pair.run.state().cycles = std::numeric_limits<u64>::max() - 1;
        pair.first.state().timebase = pair.run.state().timebase =
            std::numeric_limits<u64>::max() - 1;
        pair.first.state().instructions = pair.run.state().instructions =
            std::numeric_limits<u64>::max() - 2;
        pair.pc(0xfffffff8);
        pair.attempt(8, 8, 0); // requested headroom is checked before admission
        pair.attempt(2, 2, 2);
        CHECK(pair.run.state().pc == 0 && pair.run.state().last_pc == 0xfffffffc);
        CHECK(pair.run.state().spr[spr::lr] == (take && link ? 0u : 0x12345678u));
        CHECK(pair.run.state().cycles == 0);
        CHECK(pair.run.state().instructions == std::numeric_limits<u64>::max());
        for (unsigned n = 0; n < 16; ++n) {
          pair.pc(0xfffffff8);
          pair.step();
        }
      }
    pair.reset();
    pair.first_memory.plain = pair.run_memory.plain = true;
    pair.install(0x1fffc, 0x2c070000);
    pair.install(0x20000, 0x4082003c);
    pair.pc(0x80020000);
    pair.step();
    pair.pc(0x8001fffc);
    pair.step();
    pair.pc(0x8001fffc);
    pair.attempt(8, 8, 0); // no group crosses a 128 KiB mapping block
    pair.step();
    pair.step();

    pair.reset();
    pair.first_memory.plain = pair.run_memory.plain = true;
    pair.line();
    pair.line(0x1020);
    pair.install(0x101c, 0x2c070000);
    pair.install(0x1020, 0x4082003c);
    pair.first.state().gpr[7] = pair.run.state().gpr[7] = 1;
    pair.pc(0x8000101c);
    pair.step();
    pair.pc(0x8000101c);
    pair.attempt(2, 2, 0); // admission never fills the next line
    pair.step();
    pair.step();
    pair.pc(0x8000101c);
    pair.attempt(2, 2, 2); // recent branch can return to its preceding prefix

    for (u32 alias : {0x1000u, 0x80001000u, 0xc0001000u}) {
      pair.reset();
      pair.first.state().gpr[7] = 1;
      const std::array<u32, 3> words{0x2c070000, 0x4082003c, 0};
      register_branch_program(pair, 0, words);
      pair.first.state().msr = pair.run.state().msr &= ~msr::ir;
      pair.pc(alias);
      pair.attempt(2, 2, 2);
      CHECK(pair.run.state().pc == alias + 64);
    }
  });
}

void register_branch_run_tests() {
  register_branch_condition_tests();
  register_branch_boundary_tests();
  register_branch_visibility_tests();
  register_branch_guard_tests();
  register_branch_address_tests();
}

} // namespace

int main() {
  return run_tests([] {
    false_hint_tests();
    quota_and_clock_tests();
    semantic_sequence_tests();
    prefix_and_cache_tests();
    rejection_and_interrupt_tests();
    bounded_native_sequence_tests();
    aliased_sequence_tests();
    cache_flush_run_tests();
    scalar_run_tests();
    byte_copy_tests();
    later_memory_tests();
    executed_poll_prefix_tests();
    later_decline_tests();
    sequential_target_tests();
    native_hit_tests();
    code_alias_tests();
    cache_flush_loop_tests();
    register_branch_run_tests();
    std::cout << comparisons << " complete state and ordered bus comparisons; "
              << grouped_instructions << " grouped instructions passed\n";
  });
}
