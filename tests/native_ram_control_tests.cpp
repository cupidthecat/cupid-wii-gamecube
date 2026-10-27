// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/machine.hpp"
#include "native_ram_support.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {
struct Counts {
  u64 direct = 0, guards = 0, declines = 0, decline_warm = 0;
  u64 coexistence = 0, resets = 0, runtime = 0, machine = 0, next_pc = 0, relocated_reuse = 0;
} counts;

const char *active_group = "startup";
u64 active_row = 0;
unsigned active_length = 0;

void identify(const char *group, u64 row, unsigned length) {
  active_group = group;
  active_row = row;
  active_length = length;
}

constexpr u32 branch_word(unsigned bo, unsigned bi, u32 displacement, bool absolute = false) {
  return (16u << 26) | (bo << 21) | (bi << 16) | (displacement & 0xfffc) | (absolute ? 2u : 0);
}

std::array<u64, 16> statistics(const NativeIntegerStatistics &s) {
  return {s.executed,
          s.fallback,
          s.compiled_operations,
          s.cache_hits,
          s.capacity_fallbacks,
          s.cache_entries,
          s.executable_bytes,
          s.cached_fallbacks,
          s.block_runs,
          s.block_executed,
          s.block_compilations,
          s.block_cache_hits,
          s.block_replacements,
          s.block_cache_entries,
          s.block_executable_bytes,
          s.block_compilation_failures};
}

void block_delta(const NativeIntegerStatistics &before, const NativeIntegerStatistics &after,
                 unsigned retired, bool cold) {
  CHECK(after.executed == before.executed + retired);
  CHECK(after.block_runs == before.block_runs + 1);
  CHECK(after.block_executed == before.block_executed + retired);
  CHECK(after.block_compilations == before.block_compilations + unsigned(cold));
  CHECK(after.block_cache_hits == before.block_cache_hits + unsigned(!cold));
  CHECK(after.cache_hits == before.cache_hits + (cold ? 0 : retired));
  CHECK(after.block_cache_entries == before.block_cache_entries + unsigned(cold));
  CHECK(after.block_compilation_failures == before.block_compilation_failures);
  CHECK(after.block_replacements == before.block_replacements);
  CHECK(after.block_executable_bytes > 0);
  CHECK(after.block_executable_bytes <= NativeInteger::maximum_sequence_byte_budget);
  if (before.block_executable_bytes)
    CHECK(after.block_executable_bytes == before.block_executable_bytes);
}

std::array<u32, 8> packet(unsigned length, unsigned bo, unsigned bi, bool absolute,
                          unsigned pattern, unsigned tail = 0) {
  std::array<u32, 8> words{};
  for (unsigned n = 0; n < words.size(); ++n)
    words[n] = n < length ? dform(14, 8, 8, n + 1) : tail ? 0xffffffffu : 0x48000000u;
  switch (pattern) {
  case 0:
    words[0] = dform(32, 4, 3);
    words[1] = dform(36, 4, 3, 4);
    words[2] = dform(26, 4, 4, 0xffff);
    break;
  case 1:
    words[0] = dform(32, 3, 3);
    words[1] = dform(36, 3, 3, 4);
    words[2] = dform(32, 4, 3, 4);
    break;
  case 2:
    words[0] = dform(36, 3, 3);
    words[1] = dform(32, 4, 3);
    words[2] = xform(138, 6, 4, 5, true);
    words[3] = dform(36, 6, 3, 4);
    break;
  default:
    words[0] = dform(32, 0, 0, 0x2000);
    words[1] = dform(36, 0, 0, 0x2004);
    words[2] = dform(14, 5, 5, 1);
    words[3] = xform(266, 6, 0, 5, true) | 0x400u;
    break;
  }
  words[length - 1] = branch_word(bo, bi, pattern & 1 ? 0xffc0u : 0x40u, absolute);
  return words;
}

void initialize(MixedMemory &ram, unsigned salt) {
  for (unsigned n = 0; n < ram.bytes.size(); ++n)
    ram.bytes[n] = u8((n * 37u + salt) ^ (n >> 7));
  ram.write32(0x2000, 0x2800); // A load may replace its own address register.
}

void install(MixedMemory &ram, std::span<const u32> words, u32 pc) {
  for (unsigned n = 0; n < words.size(); ++n)
    ram.write32(pc + n * 4, words[n]);
}

State initial_state(unsigned salt, unsigned bi = 0, bool condition = false, u32 ctr = 2) {
  auto state = mixed_seed(salt);
  state.spr[spr::ctr] = ctr;
  const u32 mask = u32{1} << (31 - bi);
  state.cr = (state.cr & ~mask) | (condition ? mask : 0);
  return state;
}

bool ram_control(NativeInteger &native, State &state, std::span<const u32> words,
                 const NativeRamContext &context, NativeRamControlRun &result) {
  return words.size() == 8
             ? native.execute_ram_control_sequence(state, words, context, result)
             : native.execute_short_ram_control_sequence(state, words, context, result);
}

enum class Entry { RamControl, Arithmetic, Control, Ram };

// The ordinary interpreter is the semantic oracle. Direct entries leave these
// five fetch/retirement/clock fields caller-owned. Every other State field,
// including PC for control exits and every SPR, is compared without a mask.
void direct_check(NativeInteger &native, std::span<const u32> words, State initial,
                  MixedMemory &actual, unsigned wanted, const NativeRamContext &context,
                  Entry entry = Entry::RamControl, bool unconditional_next_pc = false) {
  install(actual, words, initial.pc);
  MixedMemory expected;
  expected.bytes = actual.bytes;
  Interpreter oracle(expected);
  CHECK(oracle.execution_backend() == ExecutionBackend::Interpreter);
  oracle.state() = initial;
  for (unsigned n = 0; n < wanted; ++n)
    CHECK(oracle.step() == 1);
  auto golden = oracle.state();
  golden.last_pc = initial.last_pc;
  golden.last_opcode = initial.last_opcode;
  golden.cycles = initial.cycles;
  golden.instructions = initial.instructions;
  golden.timebase = initial.timebase;
  const bool owns_pc = entry == Entry::RamControl || entry == Entry::Control;
  if (!owns_pc)
    golden.pc = initial.pc;
  const bool taken =
      owns_pc && wanted == words.size() &&
      (golden.pc != initial.pc + static_cast<u32>(words.size()) * 4 || unconditional_next_pc);
  auto state = initial;
  NativeRamControlRun result{0x1234, true};
  if (entry == Entry::RamControl) {
    CHECK(ram_control(native, state, words, context, result));
  } else if (entry == Entry::Arithmetic) {
    CHECK(native.execute_sequence(state, words));
    result = {static_cast<u32>(words.size()), false};
  } else if (entry == Entry::Control) {
    NativeControlRun control{0x1234, true};
    CHECK(native.execute_control_sequence(state, words, control));
    result = {control.instructions, control.taken};
  } else {
    u32 retired = 0x1234;
    CHECK(native.execute_ram_sequence(state, words, context, retired));
    result = {retired, false};
  }
  CHECK(result.instructions == wanted && result.taken == taken);
  mixed_equal(state, golden);
  CHECK(actual.bytes == expected.bytes);
  if (wanted < words.size()) {
    CHECK(state.pc == initial.pc + wanted * 4);
    CHECK(state.spr[spr::ctr] == initial.spr[spr::ctr]);
    CHECK(state.spr[spr::lr] == initial.spr[spr::lr]);
  }
}

void direct_matrix() {
  for (unsigned length : {6u, 7u, 8u})
    for (unsigned bo = 0; bo < 32; ++bo)
      for (unsigned bi : {0u, 31u})
        for (u32 ctr : {0u, 1u, 2u, 0xffffffffu})
          for (bool condition : {false, true})
            for (bool absolute : {false, true})
              for (unsigned pattern = 0; pattern < 4; ++pattern) {
                NativeInteger native(8, 8, 65536);
                for (unsigned phase = 0; phase < 2; ++phase) {
                  identify("direct", counts.direct, length);
                  auto words = packet(length, bo, bi, absolute, pattern, phase);
                  auto state = initial_state(unsigned(counts.direct), bi, condition, ctr);
                  state.pc += phase * 64;
                  MixedMemory ram;
                  initialize(ram, unsigned(counts.direct));
                  NativeRamContext context{ram.bytes.data(), nullptr, 0,
                                           unsigned(ram.bytes.size())};
                  const auto before = native.statistics();
                  direct_check(native, std::span(words).first(length), state, ram, length, context);
                  block_delta(before, native.statistics(), length, phase == 0);
                  ++counts.direct;
                }
              }
}

void narrow_matrix() {
  u64 checks = 0;
  NativeInteger native(8, 8, 262144);
  for (unsigned length : {6u, 7u, 8u})
    for (unsigned operation : {34u, 38u, 40u, 42u, 44u})
      for (unsigned position = 0; position + 1 < length; ++position)
        for (unsigned address : {0x2000u, 0x2001u, 0x2002u, 0xfffeu, 0xffffu, 0x10000u})
          for (bool condition : {false, true}) {
            const unsigned width = operation == 34 || operation == 38 ? 1 : 2;
            std::array<u32, 8> words;
            for (unsigned n = 0; n < length; ++n)
              words[n] = dform(14, 5, 5, n + 1);
            words[position] = dform(operation, 4, 3);
            words[length - 1] = branch_word(12, 0, 64);
            auto state = initial_state(unsigned(checks), 0, condition);
            state.gpr[3] = address;
            MixedMemory ram;
            initialize(ram, unsigned(checks));
            NativeRamContext context{ram.bytes.data(), nullptr, 0, unsigned(ram.bytes.size())};
            const bool accepted = address <= ram.bytes.size() - width && !(address & (width - 1));
            identify("narrow-transfer", checks, length);
            direct_check(native, std::span(words).first(length), state, ram,
                         accepted ? length : position, context);
            ++checks;
          }
  CHECK(checks == 1080);
  std::cout << "Narrow RAM/control comparisons: " << checks << '\n';
}

void indexed_matrix() {
  unsigned checked = 0;
  NativeInteger native(8, 8, 262144);
  for (unsigned length : {6u, 7u, 8u})
    for (unsigned operation : {23u, 87u, 151u, 215u, 279u, 343u, 407u})
      for (unsigned position = 0; position + 1 < length; ++position)
        for (unsigned address : {0x2000u, 0x2001u, 0x2002u, 0xfffeu, 0xffffu, 0x10000u})
          for (bool condition : {false, true}) {
            std::array<u32, 8> words;
            for (unsigned n = 0; n < length; ++n)
              words[n] = dform(14, 5, 5, n + 1);
            words[position] = xform(operation, 4, 0, 3);
            words[length - 1] = branch_word(12, 0, 64);
            const unsigned width = NativeInteger::ram_access_bytes(words[position]);
            auto state = initial_state(checked, 0, condition);
            state.gpr[3] = address;
            MixedMemory ram;
            initialize(ram, checked);
            NativeRamContext context{ram.bytes.data(), nullptr, 0, unsigned(ram.bytes.size())};
            const bool accepted = address <= ram.bytes.size() - width && !(address & (width - 1));
            identify("indexed-transfer", checked, length);
            direct_check(native, std::span(words).first(length), state, ram,
                         accepted ? length : position, context);
            ++checked;
          }
  CHECK(checked == 1512);
  std::cout << "Indexed RAM/control comparisons: " << checked << '\n';
}

void taken_to_next_pc() {
  for (unsigned length : {6u, 7u, 8u}) {
    NativeInteger native(8, 8, 65536);
    auto words = packet(length, 20, 0, false, 0);
    words[length - 1] = branch_word(20, 0, 4);
    for (unsigned phase = 0; phase < 2; ++phase) {
      MixedMemory ram;
      identify("taken-to-next-PC", counts.next_pc, length);
      initialize(ram, phase);
      const auto state = initial_state(phase);
      NativeRamContext context{ram.bytes.data(), nullptr, 0, unsigned(ram.bytes.size())};
      const auto before = native.statistics();
      // BO20 ignores CR and CTR: relative BD4 is taken even though the ordinary
      // step has the same PC as a fallthrough. Compare its full State as usual,
      // and require the separately observable public result to report taken.
      direct_check(native, std::span(words).first(length), state, ram, length, context,
                   Entry::RamControl, true);
      block_delta(before, native.statistics(), length, phase == 0);
      ++counts.next_pc;
    }
  }
}

void relocated_cache_reuse() {
  for (unsigned length : {6u, 7u, 8u})
    for (unsigned bo : {12u, 16u, 20u})
      for (bool condition : {false, true}) {
        NativeInteger native(8, 8, 65536);
        std::vector<u32> bat(32768);
        bat[0x4000] = 1;
        const auto words = packet(length, bo, 31, false, 0);
        for (unsigned phase = 0; phase < 2; ++phase) {
          identify("relocated-cache-reuse", counts.relocated_reuse, length);
          MixedMemory ram;
          initialize(ram, unsigned(counts.relocated_reuse));
          auto state = initial_state(unsigned(counts.relocated_reuse), 31, condition);
          if (phase) {
            state.msr = msr::dr;
            state.gpr[3] = 0x80002000;
            state.spr[536] = 0x80000003;
            state.spr[537] = 2;
            state.pc += 64;
          }
          NativeRamContext context{ram.bytes.data(), phase ? bat.data() : nullptr, 0,
                                   unsigned(ram.bytes.size())};
          const auto before = native.statistics();
          direct_check(native, std::span(words).first(length), state, ram, length, context);
          block_delta(before, native.statistics(), length, phase == 0);
          ++counts.relocated_reuse;
        }
      }
}

void guard_matrix() {
  for (unsigned length : {6u, 7u, 8u})
    for (unsigned position : {0u, 2u, length - 2})
      for (unsigned operation : {32u, 36u})
        for (unsigned kind = 0; kind < 6; ++kind) {
          NativeInteger native(8, 8, 65536);
          std::vector<u32> bat(32768);
          bat[0x4000] = 1;
          bat[0x4001] = kind == 3 ? 0 : kind == 4 ? 5 : 0x20001;
          std::array<u32, 8> words{};
          for (unsigned n = 0; n < length; ++n)
            words[n] = dform(14, 8, 8, n + 1);
          if (position)
            words[0] = dform(36, 6, 7);
          words[position] = dform(operation, 4, 3);
          words[length - 1] = branch_word(16, 0, 64);
          for (unsigned phase = 0; phase < 2; ++phase) {
            identify("guard", counts.guards, length);
            auto state = initial_state(unsigned(counts.guards));
            state.gpr[3] = kind == 0 ? 0x2001 : kind == 1 ? 0xffc : kind == 2 ? 0xfffc : 0x80020000;
            if (kind >= 3) {
              state.msr = msr::dr;
              state.gpr[7] = 0x80002800;
              state.spr[536] = 0x80000003;
              state.spr[537] = 2;
            }
            MixedMemory ram;
            initialize(ram, unsigned(counts.guards));
            const u32 base = kind == 1 ? 0x1000 : 0;
            const unsigned bytes = kind == 2 ? 0xfffe : unsigned(ram.bytes.size()) - base;
            NativeRamContext context{ram.bytes.data() + base, kind >= 3 ? bat.data() : nullptr,
                                     base, bytes};
            const auto before = native.statistics();
            direct_check(native, std::span(words).first(length), state, ram, position, context);
            block_delta(before, native.statistics(), position, phase == 0);
            ++counts.guards;
          }
        }
}

void declines() {
  for (unsigned length : {6u, 7u, 8u})
    for (unsigned kind = 0; kind < 16; ++kind) {
      NativeInteger native(8, 8, 65536);
      identify("decline", counts.declines, length);
      auto words = packet(length, 12, 0, false, 0);
      auto state = initial_state(kind);
      MixedMemory ram;
      initialize(ram, kind);
      NativeRamContext context{ram.bytes.data(), nullptr, 0, unsigned(ram.bytes.size())};
      direct_check(native, std::span(words).first(length), state, ram, length, context);
      ++counts.decline_warm;
      std::vector<u32> bat(32768);
      switch (kind) {
      case 0:
        words[length - 1] |= 1;
        break;
      case 1:
        words[2] = branch_word(20, 0, 64);
        break;
      case 2:
        words[length - 1] = 0x48000040;
        break;
      case 3:
        for (unsigned n = 0; n + 1 < length; ++n)
          words[n] = dform(14, 8, 8, n + 1);
        break;
      case 4:
        words[2] = dform(7, 4, 3, 1);
        break;
      case 5:
        words[2] = dform(33, 4, 3);
        break;
      case 6:
        words[2] = xform(55, 4, 3, 5);
        break;
      case 7:
        words[2] = 0;
        break;
      case 8:
        context.data = nullptr;
        break;
      case 9:
        context.bytes = 3;
        break;
      case 10:
        context.physical_base = 0xfffffff0;
        context.bytes = 32;
        break;
      case 11:
        state.msr |= msr::le;
        break;
      case 12:
        state.msr |= msr::dr;
        break;
      case 13:
        context.bat_entries = bat.data();
        break;
      default:
        break;
      }
      const auto original = state;
      const auto bytes = ram.bytes;
      const auto before = statistics(native.statistics());
      NativeRamControlRun result{0x1234, true};
      auto selected = std::span(words).first(kind == 15 ? length - 1 : length);
      bool accepted = false;
      if (kind == 14)
        accepted = length == 8
                       ? native.execute_short_ram_control_sequence(state, selected, context, result)
                       : native.execute_ram_control_sequence(state, selected, context, result);
      else
        accepted = ram_control(native, state, selected, context, result);
      CHECK(!accepted && result.instructions == 0x1234 && result.taken);
      mixed_equal(state, original);
      CHECK(ram.bytes == bytes && statistics(native.statistics()) == before);
      ++counts.declines;
    }
}

void cache_coexistence() {
  NativeInteger native(8, 8, 65536);
  std::array<MixedMemory, 2> backing;
  const std::array<unsigned, 12> order{6, 7, 8, 0, 1, 2, 6, 8, 7, 2, 1, 0};
  for (unsigned round = 0; round < 2; ++round) {
    for (unsigned index = 0; index < order.size(); ++index) {
      const unsigned kind = order[index];
      auto &ram = backing[(index + round) & 1];
      initialize(ram, unsigned(counts.coexistence));
      auto words = packet(kind >= 6 ? kind : 6, 20, 0, false, 0, index & 1);
      const unsigned length = kind >= 6 ? kind : 2;
      identify("coexistence", counts.coexistence, length);
      Entry entry = Entry::RamControl;
      if (kind == 0) {
        words[0] = dform(14, 5, 5, 1);
        words[1] = dform(26, 6, 6, 0xffff);
        entry = Entry::Arithmetic;
      }
      if (kind == 1) {
        words[0] = dform(14, 5, 5, 1);
        words[1] = branch_word(20, 0, 64);
        entry = Entry::Control;
      }
      if (kind == 2) {
        words[0] = dform(32, 4, 3);
        words[1] = dform(36, 4, 3, 4);
        entry = Entry::Ram;
      }
      auto state = initial_state(unsigned(counts.coexistence));
      state.pc += index * 64;
      NativeRamContext context{ram.bytes.data(), nullptr, 0, unsigned(ram.bytes.size())};
      const auto before = native.statistics();
      direct_check(native, std::span(words).first(length), state, ram, length, context, entry);
      block_delta(before, native.statistics(), length, index < 6);
      CHECK(native.statistics().block_cache_entries == std::min(index + 1, 6u));
      ++counts.coexistence;
    }
    native.reset();
    CHECK((statistics(native.statistics()) == std::array<u64, 16>{}));
    ++counts.resets;
  }
}

struct RuntimePair {
  MixedMemory expected, actual;
  Interpreter oracle{expected}, native{actual};
  State initial;

  RuntimePair(std::span<const u32> words, unsigned offset, unsigned salt,
              Console console = Console::GameCube)
      : oracle(expected, console), native(actual, console) {
    native.set_execution_backend(ExecutionBackend::NativeInteger);
    initial = initial_state(salt);
    initial.spr[spr::hid0] = 0x8000;
    initial.pc = 0x1040 + offset * 4;
    initialize(expected, salt);
    for (unsigned n = 0; n < 32; ++n)
      expected.write32(0x1000 + n * 4, 0x60000000);
    install(expected, words, initial.pc);
    actual.bytes = expected.bytes;
    for (auto *cpu : {&oracle, &native}) {
      cpu->state() = initial;
      for (u32 pc : {0x1000u, 0x1020u, 0x1060u, 0x1040u}) {
        cpu->state().pc = pc;
        CHECK(cpu->step() == 1);
      }
    }
    actual.bytes = expected.bytes;
    oracle.state() = native.state() = initial;
    CHECK(oracle.execution_backend() == ExecutionBackend::Interpreter);
  }
};

void runtime_matrix() {
  for (unsigned length : {6u, 7u, 8u})
    for (unsigned offset : {0u, 6u})
      for (unsigned bo : {0u, 4u, 12u, 16u, 20u, 31u})
        for (bool absolute : {false, true})
          for (bool condition : {false, true}) {
            auto words = packet(length, bo, 0, absolute, 0);
            for (unsigned n = length; n < words.size(); ++n)
              words[n] = dform(14, 9, 9, 7);
            RuntimePair pair(words, offset, unsigned(counts.runtime));
            pair.initial = initial_state(unsigned(counts.runtime), 0, condition);
            pair.initial.spr[spr::hid0] = 0x8000;
            pair.initial.pc = 0x1040 + offset * 4;
            for (unsigned phase = 0; phase < 2; ++phase) {
              identify("try-integer-run", counts.runtime, length);
              pair.oracle.state() = pair.native.state() = pair.initial;
              const auto before = pair.native.native_statistics();
              const auto result = pair.native.try_integer_run(8, 8);
              CHECK(result.instructions == length && result.cycles == length);
              for (unsigned n = 0; n < length; ++n)
                CHECK(pair.oracle.step() == 1);
              mixed_equal(pair.oracle.state(), pair.native.state());
              CHECK(pair.expected.bytes == pair.actual.bytes);
              CHECK(pair.expected.records == pair.actual.records);
              block_delta(before, pair.native.native_statistics(), length, phase == 0);
              ++counts.runtime;
            }
          }
}

void narrow_runtime_matrix() {
  unsigned checked = 0;
  for (auto console : {Console::GameCube, Console::Wii})
    for (unsigned length : {6u, 7u, 8u})
      for (unsigned operation : {34u, 38u, 40u, 42u, 44u})
        for (unsigned offset : {0u, 6u})
          for (bool condition : {false, true}) {
            std::array<u32, 8> words;
            for (unsigned n = 0; n < words.size(); ++n)
              words[n] = dform(14, 8, 8, n + 1);
            const unsigned displacement = operation == 34 || operation == 38 ? 1 : 2;
            words[0] = dform(operation, 4, 3, displacement);
            words[length - 1] = branch_word(12, 0, 64);
            RuntimePair pair(words, offset, checked, console);
            pair.initial.cr = (pair.initial.cr & 0x7fffffff) | (condition ? 0x80000000u : 0);
            for (unsigned phase = 0; phase < 2; ++phase) {
              pair.oracle.state() = pair.native.state() = pair.initial;
              const auto before = pair.native.native_statistics();
              const auto result = pair.native.try_integer_run(8, 8);
              CHECK(result.instructions == length && result.cycles == length);
              for (unsigned n = 0; n < length; ++n)
                CHECK(pair.oracle.step() == 1);
              mixed_equal(pair.oracle.state(), pair.native.state());
              CHECK(pair.expected.bytes == pair.actual.bytes);
              CHECK(pair.expected.records == pair.actual.records);
              block_delta(before, pair.native.native_statistics(), length, phase == 0);
              ++checked;
            }
          }
  CHECK(checked == 240);
  std::cout << "Narrow RAM/control runtime comparisons: " << checked << '\n';
}

void indexed_runtime_matrix() {
  unsigned checked = 0;
  for (auto console : {Console::GameCube, Console::Wii})
    for (unsigned length : {6u, 7u, 8u})
      for (unsigned operation : {23u, 87u, 151u, 215u, 279u, 343u, 407u})
        for (unsigned offset : {0u, 6u})
          for (bool condition : {false, true}) {
            std::array<u32, 8> words;
            for (unsigned n = 0; n < words.size(); ++n)
              words[n] = dform(14, 8, 8, n + 1);
            words[0] = xform(operation, 4, 3, 7);
            words[length - 1] = branch_word(12, 0, 64);
            RuntimePair pair(words, offset, checked, console);
            pair.initial.cr = (pair.initial.cr & 0x7fffffff) | (condition ? 0x80000000u : 0);
            for (unsigned phase = 0; phase < 2; ++phase) {
              pair.oracle.state() = pair.native.state() = pair.initial;
              const auto before = pair.native.native_statistics();
              const auto result = pair.native.try_integer_run(8, 8);
              CHECK(result.instructions == length && result.cycles == length);
              for (unsigned n = 0; n < length; ++n)
                CHECK(pair.oracle.step() == 1);
              mixed_equal(pair.oracle.state(), pair.native.state());
              CHECK(pair.expected.bytes == pair.actual.bytes);
              CHECK(pair.expected.records == pair.actual.records);
              block_delta(before, pair.native.native_statistics(), length, phase == 0);
              ++checked;
            }
          }
  CHECK(checked == 336);
  std::cout << "Indexed RAM/control runtime comparisons: " << checked << '\n';
}

void same_memory(Memory &a, Memory &b, Console console) {
  const auto av = a.plain_cpu_ram_view(), bv = b.plain_cpu_ram_view();
  CHECK(av.physical_base == bv.physical_base && av.bytes.size() == bv.bytes.size());
  CHECK(std::equal(av.bytes.begin(), av.bytes.end(), bv.bytes.begin(), bv.bytes.end()));
  const auto cache_a = a.range(0xe0000000, Memory::cache_size);
  const auto cache_b = b.range(0xe0000000, Memory::cache_size);
  CHECK(std::equal(cache_a.begin(), cache_a.end(), cache_b.begin(), cache_b.end()));
  if (console == Console::Wii) {
    const auto a2 = a.range(0x10000000, Memory::mem2_size);
    const auto b2 = b.range(0x10000000, Memory::mem2_size);
    CHECK(std::equal(a2.begin(), a2.end(), b2.begin(), b2.end()));
  }
  CHECK(a.page_table_write_errors() == b.page_table_write_errors());
  CHECK(a.instruction_backing_read_errors() == b.instruction_backing_read_errors());
}

void machine_matrix() {
  for (auto console : {Console::GameCube, Console::Wii})
    for (unsigned length : {6u, 7u, 8u})
      for (unsigned offset : {0u, 6u})
        for (bool condition : {false, true}) {
          identify("machine", counts.machine, length);
          auto expected = std::make_unique<runtime::Machine>(console);
          auto actual = std::make_unique<runtime::Machine>(console);
          expected->cpu().set_execution_backend(ExecutionBackend::Interpreter);
          actual->cpu().set_execution_backend(ExecutionBackend::NativeInteger);
          auto words = packet(length, 12, 0, false, 0);
          for (unsigned n = length; n < words.size(); ++n)
            words[n] = dform(14, 9, 9, 7);
          auto state = initial_state(unsigned(counts.machine), 0, condition);
          state.pc = 0x1040 + offset * 4;
          state.spr[spr::hid0] = 0x8000;
          for (auto *machine : {expected.get(), actual.get()}) {
            machine->devices().set_rtc_seconds(0);
            for (unsigned n = 0; n < 32; ++n)
              machine->memory().write32(0x1000 + n * 4, 0x60000000);
            for (unsigned n = 0; n < words.size(); ++n)
              machine->memory().write32(state.pc + n * 4, words[n]);
            machine->memory().write32(0x2000, 0x2800);
            machine->cpu().state() = state;
            for (u32 pc : {0x1000u, 0x1020u, 0x1060u, 0x1040u}) {
              machine->cpu().state().pc = pc;
              CHECK(machine->cpu().step() == 1);
            }
            machine->cpu().state() = state;
            machine->set_limits({length, 0, 0});
            CHECK(machine->devices().event_free_cycles() >= length);
            CHECK(machine->graphics().event_free_cycles() >= length);
          }
          const auto before = actual->cpu().native_statistics();
          bool limited = false;
          try {
            actual->run();
          } catch (const runtime::LimitReached &error) {
            CHECK(std::string(error.what()) == "instruction-limit");
            limited = true;
          }
          CHECK(limited);
          for (unsigned n = 0; n < length; ++n)
            expected->step();
          mixed_equal(actual->cpu().state(), expected->cpu().state());
          same_memory(actual->memory(), expected->memory(), console);
          CHECK(actual->devices().cycles() == expected->devices().cycles());
          CHECK(actual->devices().field_count() == expected->devices().field_count());
          CHECK(actual->devices().interrupt_causes(0xffffffff) ==
                expected->devices().interrupt_causes(0xffffffff));
          block_delta(before, actual->cpu().native_statistics(), length, true);
          ++counts.machine;
        }
}
} // namespace

int main() {
  if (!NativeInteger::supported()) {
    std::cout << "SKIP: native RAM/control execution requires x86-64\n";
    return 77;
  }
  return run_tests([] {
    try {
      direct_matrix();
      narrow_matrix();
      indexed_matrix();
      taken_to_next_pc();
      relocated_cache_reuse();
      guard_matrix();
      declines();
      cache_coexistence();
      runtime_matrix();
      narrow_runtime_matrix();
      indexed_runtime_matrix();
      machine_matrix();
      CHECK(counts.direct == 24576 && counts.guards == 216);
      CHECK(counts.declines == 48 && counts.decline_warm == 48);
      CHECK(counts.coexistence == 24 && counts.resets == 2);
      CHECK(counts.runtime == 288 && counts.machine == 24 && counts.next_pc == 6 &&
            counts.relocated_reuse == 36);
      std::cout << "{\"direct\":" << counts.direct << ",\"guards\":" << counts.guards
                << ",\"declines\":" << counts.declines
                << ",\"decline_warm\":" << counts.decline_warm
                << ",\"coexistence\":" << counts.coexistence << ",\"resets\":" << counts.resets
                << ",\"runtime\":" << counts.runtime << ",\"machine\":" << counts.machine
                << ",\"taken_to_next_pc\":" << counts.next_pc
                << ",\"relocated_cache_reuse\":" << counts.relocated_reuse << "}\n";
    } catch (...) {
      std::cerr << "native_ram_control group=" << active_group << " row=" << active_row
                << " length=" << active_length << '\n';
      throw;
    }
  });
}
