// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/memory.hpp"
#include "native_ram_support.hpp"
#include <cfenv>
#include <cstring>
#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif
#include <iostream>
#include <limits>

namespace {
u64 direct_cases = 0, runtime_cases = 0, rejected = 0, fenv_cases = 0, context_cases = 0;
u64 activation_assertions = 0, native_activations = 0;

void direct(NativeInteger &native, std::span<const u32> words, State initial, unsigned wanted,
            bool translated = false, unsigned view_size = 65536, u32 bad_entry = 0) {
  MixedMemory expected, actual;
  for (unsigned n = 0; n < actual.bytes.size(); ++n)
    actual.bytes[n] = u8((n * 37) ^ (n >> 7));
  expected.bytes = actual.bytes;
  for (unsigned n = 0; n < words.size(); ++n) {
    expected.write32(initial.pc + n * 4, words[n]);
    actual.write32(initial.pc + n * 4, words[n]);
  }
  std::array<u32, 32768> bat{};
  if (translated) {
    initial.msr |= msr::dr;
    initial.spr[536] = 0x80000003;
    initial.spr[537] = 2;
    bat[0x4000] = 1;
    bat[0x4001] = bad_entry;
  }
  Interpreter oracle(expected);
  oracle.state() = initial;
  for (unsigned n = 0; n < wanted; ++n)
    CHECK(oracle.step() == 1);
  auto golden = oracle.state();
  golden.pc = initial.pc;
  golden.last_pc = initial.last_pc;
  golden.last_opcode = initial.last_opcode;
  golden.cycles = initial.cycles;
  golden.instructions = initial.instructions;
  golden.timebase = initial.timebase;
  auto state = initial;
  NativeRamContext context{actual.bytes.data(), translated ? bat.data() : nullptr, 0, view_size};
  u32 retired = 0x1234;
  CHECK(native.execute_ram_sequence(state, words, context, retired));
  if (retired != wanted) {
    std::cerr << "direct mismatch: case=" << direct_cases << " wanted=" << wanted
              << " retired=" << retired << " translated=" << translated << " r3=" << std::hex
              << initial.gpr[3] << " r7=" << initial.gpr[7] << " bad=" << bad_entry << " words";
    for (auto word : words)
      std::cerr << ' ' << word;
    std::cerr << std::dec << '\n';
  }
  CHECK(retired == wanted);
  mixed_equal(state, golden);
  CHECK(actual.bytes == expected.bytes);
  ++direct_cases;
}

void semantics() {
  NativeInteger native(2, 1, 128 * 1024);
  for (unsigned count = 2; count <= 8; ++count)
    for (unsigned memory_position = 0; memory_position < count; ++memory_position)
      for (unsigned reg : {0u, 3u, 7u, 31u})
        for (unsigned operation : {32u, 36u})
          for (bool translated : {false, true}) {
            auto initial = mixed_seed(memory_position + reg);
            initial.gpr[3] = translated ? 0x80002000 : 0x2000;
            std::array<u32, 8> words;
            for (unsigned n = 0; n < count; ++n)
              words[n] = n & 1 ? xform(138, 6, 6, 5, true) : dform(14, 5, 5, n + 1);
            words[memory_position] = dform(operation, reg, 3, 4);
            direct(native, std::span(words).first(count), initial, count, translated);
          }
  for (unsigned offset : {0u, 4u, 0x7ffcu, 0x8000u, 0xfffcu}) {
    const std::array<u32, 4> words{dform(32, 4, 3, offset), dform(26, 4, 4, 0xffff),
                                   dform(36, 4, 3, offset), dform(32, 3, 3, offset)};
    auto initial = mixed_seed(offset);
    initial.gpr[3] = offset & 0x8000 ? 0x9000 : 0x2000;
    direct(native, words, initial, 4);
  }
  // Later memory addresses depend on earlier emitted arithmetic and load data.
  const std::array<u32, 4> chain{dform(14, 3, 3, 4), dform(36, 3, 7, 0), dform(32, 4, 7, 0),
                                 dform(32, 5, 4, 0)};
  direct(native, chain, mixed_seed(), 4);
  const std::array<u32, 2> zero_base{dform(32, 4, 0, 0x2000), dform(36, 4, 0, 0x2004)};
  direct(native, zero_base, mixed_seed(), 2);
  auto wrapped = mixed_seed();
  wrapped.gpr[3] = 0xfffffffc;
  const std::array<u32, 2> wrap{dform(32, 4, 3, 8), dform(36, 4, 0, 0x2000)};
  direct(native, wrap, wrapped, 2);
}

void guards() {
  NativeInteger native(2, 1, 32768);
  for (unsigned count = 2; count <= 8; ++count)
    for (unsigned position = 0; position < count; ++position)
      for (unsigned operation : {32u, 36u})
        for (unsigned kind = 0; kind < 5; ++kind) {
          auto initial = mixed_seed(kind);
          std::array<u32, 8> words;
          for (unsigned n = 0; n < count; ++n)
            words[n] = n == 0 && position ? dform(36, 6, 7, 0) : dform(14, 5, 5, n + 1);
          words[position] = dform(operation, 4, 3, 0);
          initial.gpr[3] = kind == 0 ? 0x2001 : kind == 1 ? 0x10000 : 0x80020000;
          if (kind < 2)
            direct(native, std::span(words).first(count), initial, position);
          else {
            initial.gpr[7] = 0x80002800;
            direct(native, std::span(words).first(count), initial, position, true, 65536,
                   kind == 3   ? 5
                   : kind == 4 ? 4
                               : 0);
          }
        }
  for (u32 word : {0x48000004u, 0x4182fff8u, dform(33, 4, 3), dform(48, 4, 3), xform(23, 4, 3, 5),
                   dform(7, 4, 3), 0u}) {
    const std::array<u32, 2> words{dform(32, 4, 3), word};
    auto initial = mixed_seed();
    const auto before = initial;
    MixedMemory ram;
    const auto bytes = ram.bytes;
    NativeRamContext context{ram.bytes.data(), nullptr, 0, unsigned(ram.bytes.size())};
    u32 result = 0x1234;
    CHECK(!native.execute_ram_sequence(initial, words, context, result));
    CHECK(result == 0x1234 && ram.bytes == bytes);
    mixed_equal(initial, before);
    ++rejected;
  }
  MixedMemory ram;
  std::array<u32, 32768> bat{};
  const std::array<u32, 2> words{dform(32, 4, 3), dform(36, 4, 7)};
  for (unsigned kind = 0; kind < 9; ++kind) {
    auto state = mixed_seed(kind);
    NativeRamContext context{ram.bytes.data(), nullptr, 0, unsigned(ram.bytes.size())};
    if (kind == 0)
      context.data = nullptr;
    if (kind >= 1 && kind <= 3)
      context.bytes = kind == 3 ? 3 : kind - 1;
    if (kind == 4) {
      context.physical_base = 0xfffffff0;
      context.bytes = 32;
    }
    if (kind == 5) {
      context.physical_base = 2;
      context.bytes = 0xffffffff;
    }
    if (kind == 6)
      state.msr |= msr::le;
    if (kind == 7)
      state.msr |= msr::dr;
    if (kind == 8)
      context.bat_entries = bat.data();
    const auto before = state;
    const auto before_ram = ram.bytes;
    const auto stats = native.statistics();
    u32 retired = 0x1234;
    CHECK(!native.execute_ram_sequence(state, words, context, retired));
    CHECK(retired == 0x1234 && ram.bytes == before_ram);
    mixed_equal(state, before);
    const auto after = native.statistics();
    CHECK(after.block_runs == stats.block_runs &&
          after.block_compilations == stats.block_compilations &&
          after.block_cache_entries == stats.block_cache_entries &&
          after.block_executable_bytes == stats.block_executable_bytes &&
          after.executed == stats.executed && after.fallback == stats.fallback);
    ++rejected;
  }
}

void arithmetic_context_survival() {
  NativeInteger native(2, 1, 128 * 1024);
  constexpr std::array<std::array<unsigned, 3>, 7> aliases{
      {{3, 4, 5}, {3, 3, 5}, {3, 4, 3}, {3, 4, 4}, {3, 3, 3}, {0, 0, 31}, {31, 0, 31}}};
  const auto check = [&](u32 opcode) {
    if (!NativeInteger::can_group(opcode))
      return;
    for (bool translated : {false, true}) {
      auto initial = mixed_seed(opcode);
      initial.gpr[7] = translated ? 0x80002800 : 0x2800;
      const std::array<u32, 8> words{dform(32, 6, 7), opcode, opcode, opcode,
                                     opcode,          opcode, opcode, dform(36, 6, 7, 4)};
      direct(native, words, initial, 8, translated);
    }
  };
  for (const auto &alias : aliases) {
    for (unsigned primary :
         {8u, 10u, 11u, 12u, 13u, 14u, 15u, 20u, 21u, 23u, 24u, 25u, 26u, 27u, 28u, 29u})
      for (unsigned immediate : {0u, 1u, 31u, 0x7fffu, 0x8000u, 0xffffu})
        check(dform(primary, alias[0], alias[1], immediate));
    for (unsigned operation :
         {0u,   24u,  26u,  28u, 32u, 60u, 124u, 284u, 316u, 412u, 444u, 476u, 536u, 792u,
          824u, 922u, 954u, 8u,  10u, 40u, 104u, 136u, 138u, 200u, 202u, 232u, 234u, 266u})
      for (bool record : {false, true})
        for (bool overflow : {false, true})
          check(xform(operation, alias[0], alias[1], alias[2], record) | (overflow ? 0x400u : 0));
  }
}

void context_and_hints() {
  NativeInteger native(2, 1, 512 * 1024);
  std::array<u8, 16> left{}, right{};
  for (unsigned n = 0; n < 8192; ++n) {
    auto &ram = n & 1 ? left : right;
    const u32 value = n * 0x9e3779b9u;
    for (unsigned byte = 0; byte < 4; ++byte)
      ram[byte] = u8(value >> (24 - byte * 8));
    auto expected_ram = ram;
    for (unsigned byte = 0; byte < 4; ++byte)
      expected_ram[4 + byte] = ram[byte];
    auto state = mixed_seed(n);
    state.pc = 0x80004000 + n * 4;
    state.gpr[3] = 0x2000;
    auto golden = state;
    golden.gpr[4] = value;
    const std::array<u32, 3> words{dform(32, 4, 3), dform(36, 4, 3, 4),
                                   dform(14, 5, 5, n & 1 ? 7 : 11)};
    const unsigned count = n & 2 ? 2 : 3;
    if (count == 3)
      golden.gpr[5] += n & 1 ? 7 : 11;
    NativeRamContext context{ram.data(), nullptr, 0x2000, unsigned(ram.size())};
    u32 result = 0;
    CHECK(native.execute_ram_sequence(state, std::span(words).first(count), context, result));
    CHECK(result == count && ram == expected_ram);
    mixed_equal(state, golden);
    ++context_cases;
  }
  CHECK(native.statistics().block_compilations == 3);
  std::array<u32, 32768> first_bat{}, second_bat{};
  first_bat[0x4000] = 1;
  second_bat[0x4000] = 0x20001;
  const std::array<u32, 2> words{dform(32, 4, 3), dform(36, 4, 3, 4)};
  for (unsigned n = 0; n < 4; ++n) {
    auto &ram = n & 1 ? left : right;
    auto state = mixed_seed(n);
    state.msr = msr::dr;
    state.gpr[3] = 0x80002000;
    state.spr[536] = 0x80000003;
    state.spr[537] = n & 1 ? 0x20002 : 2;
    auto golden = state;
    golden.gpr[4] = (u32(ram[0]) << 24) | (u32(ram[1]) << 16) | (u32(ram[2]) << 8) | ram[3];
    const NativeRamContext context{ram.data(), n & 1 ? second_bat.data() : first_bat.data(),
                                   n & 1 ? 0x22000u : 0x2000u, n & 2 ? 4u : 16u};
    const auto original_ram = ram;
    u32 result = 0;
    CHECK(native.execute_ram_sequence(state, words, context, result));
    CHECK(result == (n & 2 ? 1u : 2u));
    mixed_equal(state, golden);
    if (n & 2)
      CHECK(ram == original_ram);
    ++context_cases;
  }
}

struct RuntimePair {
  MixedMemory expected, actual;
  Interpreter oracle, native;
  State initial;

  explicit RuntimePair(Console console) : oracle(expected, console), native(actual, console) {
    native.set_execution_backend(ExecutionBackend::NativeInteger);
    initial = mixed_seed();
    initial.spr[spr::hid0] = 0x8000;
  }

  void prepare(std::span<const u32> words, unsigned offset) {
    oracle.reset();
    native.reset();
    for (unsigned n = 0; n < 16; ++n)
      expected.write32(0x1000 + n * 4, 0x60000000);
    for (unsigned n = 0; n < words.size(); ++n)
      expected.write32(0x1000 + offset * 4 + n * 4, words[n]);
    actual.bytes = expected.bytes;
    oracle.state() = native.state() = initial;
    CHECK(oracle.step() == native.step());
    oracle.state().pc = native.state().pc = 0x1020;
    CHECK(oracle.step() == native.step());
    oracle.state().pc = native.state().pc = 0x1000 + offset * 4;
    CHECK(oracle.step() == native.step());
    oracle.state().pc = native.state().pc = 0x1000;
    CHECK(oracle.step() == native.step());
    oracle.state() = native.state() = initial;
    oracle.state().pc = native.state().pc = 0x1000 + offset * 4;
    actual.bytes = expected.bytes;
    expected.records.clear();
    actual.records.clear();
  }

  IntegerRun attempt(unsigned quota, unsigned cycles) {
    const auto result = native.try_integer_run(quota, cycles);
    for (unsigned n = 0; n < result.instructions; ++n)
      CHECK(oracle.step() == 1);
    mixed_equal(oracle.state(), native.state());
    CHECK(expected.bytes == actual.bytes && expected.records == actual.records);
    ++runtime_cases;
    return result;
  }
};

void activation(RuntimePair &pair, NativeIntegerStatistics before, unsigned runs,
                unsigned compilations) {
  const auto after = pair.native.native_statistics();
  CHECK(after.block_runs == before.block_runs + runs);
  CHECK(after.block_compilations == before.block_compilations + compilations);
  CHECK(after.block_executed == before.block_executed + runs * 8u);
  if (runs && !compilations)
    CHECK(after.block_cache_hits == before.block_cache_hits + runs);
  ++activation_assertions;
  native_activations += runs;
}

void bat_runtime() {
  const std::array<u32, 8> words{dform(32, 4, 3),    dform(14, 4, 4, 3),        dform(36, 4, 7),
                                 dform(32, 5, 7),    xform(138, 6, 5, 6, true), dform(36, 6, 7, 4),
                                 dform(26, 8, 8, 1), dform(14, 9, 9, 1)};
  for (auto console : {Console::GameCube, Console::Wii}) {
    RuntimePair pair(console);
    pair.initial.msr = msr::dr;
    pair.initial.spr[536] = 0x80000003;
    pair.initial.spr[537] = 2;
    pair.initial.gpr[3] = 0x80002000;
    pair.initial.gpr[7] = 0x80002800;
    pair.prepare(words, 0); // Ordinary loads establish the actual interpreter BAT map.
    const auto attempt = [&](unsigned wanted, unsigned runs, unsigned compilations) {
      pair.oracle.state().pc = pair.native.state().pc = 0x1000;
      const auto before = pair.native.native_statistics();
      CHECK(pair.attempt(8, 8).instructions == wanted);
      activation(pair, before, runs, compilations);
    };
    attempt(8, 1, 1);
    attempt(8, 1, 0);
    for (auto *cpu : {&pair.oracle, &pair.native})
      cpu->state().spr[537] = 0x20002;
    attempt(0, 0, 0); // Stale lower-BAT key must not rebuild during admission.
    for (auto *cpu : {&pair.oracle, &pair.native})
      cpu->state().spr[537] = 2;
    attempt(8, 1, 0);
    for (auto *cpu : {&pair.oracle, &pair.native})
      cpu->state().gpr[3] = 0x80020000;
    attempt(0, 0, 0); // Valid map, missing entry.
    for (auto *cpu : {&pair.oracle, &pair.native})
      cpu->state().gpr[3] = 0x80002000;
    attempt(8, 1, 0);
    for (auto *cpu : {&pair.oracle, &pair.native})
      cpu->state().spr[spr::hid4] = 0x02000000;
    if (console == Console::Wii) {
      attempt(0, 0, 0); // Cached four-bank table cannot stand in for eight banks.
      CHECK(pair.oracle.step() == pair.native.step());
      mixed_equal(pair.oracle.state(), pair.native.state());
      attempt(8, 1, 0);
      for (auto *cpu : {&pair.oracle, &pair.native}) {
        cpu->state().spr[568] = 0x90000003;
        cpu->state().spr[569] = 2;
      }
      attempt(0, 0, 0); // Stale extended register key.
      for (auto *cpu : {&pair.oracle, &pair.native}) {
        cpu->state().spr[568] = 0;
        cpu->state().spr[569] = 0;
      }
      attempt(8, 1, 0);
    } else {
      attempt(8, 1, 0); // The extended-bank switch is ignored on this console.
    }
    pair.actual.view = false;
    attempt(8, 0, 0); // Capability denial retains the old complete scalar packet.
    pair.actual.view = true;
    attempt(8, 1, 0);
    for (auto *cpu : {&pair.oracle, &pair.native}) {
      cpu->state().msr = 0;
      cpu->state().gpr[3] = 0x2000;
      cpu->state().gpr[7] = 0x2800;
    }
    attempt(8, 1, 0); // Fresh nontranslated context despite a populated BAT map.

    RuntimePair missing(console);
    missing.prepare(words, 0); // Cache fills with relocation disabled; no data BAT map exists.
    for (auto *cpu : {&missing.oracle, &missing.native}) {
      cpu->state().msr = msr::dr;
      cpu->state().spr[536] = 0x80000003;
      cpu->state().spr[537] = 2;
      cpu->state().gpr[3] = 0x80002000;
      cpu->state().gpr[7] = 0x80002800;
    }
    auto before = missing.native.native_statistics();
    CHECK(missing.attempt(8, 8).instructions == 0);
    activation(missing, before, 0, 0);
    CHECK(missing.oracle.step() == missing.native.step());
    mixed_equal(missing.oracle.state(), missing.native.state());
    missing.oracle.state().pc = missing.native.state().pc = 0x1000;
    before = missing.native.native_statistics();
    CHECK(missing.attempt(8, 8).instructions == 8);
    activation(missing, before, 1, 1);
  }
}

void runtime() {
  for (auto console : {Console::GameCube, Console::Wii}) {
    RuntimePair pair(console);
    for (unsigned offset = 0; offset < 8; ++offset)
      for (unsigned quota = 0; quota <= 10; ++quota)
        for (unsigned cycles = 0; cycles <= 10; ++cycles) {
          const std::array<u32, 8> words{
              dform(32, 4, 3),           dform(14, 4, 4, 3), dform(36, 4, 7),    dform(32, 5, 7),
              xform(138, 6, 5, 6, true), dform(36, 6, 7, 4), dform(14, 3, 3, 4), dform(32, 5, 3)};
          pair.prepare(words, offset);
          const auto wanted = std::min({quota, cycles, 8u});
          const auto before = pair.native.native_statistics();
          CHECK(pair.attempt(quota, cycles).instructions == (wanted >= 2 ? wanted : 0));
          activation(pair, before, wanted == 8 ? 1 : 0, wanted == 8 ? 1 : 0);
        }
    // A register prefix changes the preflight-valid address to excluded backing.
    const std::array<u32, 3> partial{dform(14, 3, 3, 0x2000), dform(32, 4, 3), dform(36, 4, 7)};
    pair.actual.view_size = 0x3000;
    pair.prepare(partial, 0);
    CHECK(pair.attempt(3, 3).instructions == 3);
    CHECK(pair.native.native_statistics().block_runs == 0);
    const std::array<u32, 8> full_partial{
        partial[0],         partial[1],         partial[2],         dform(14, 5, 5, 1),
        dform(26, 6, 6, 1), dform(14, 8, 8, 1), dform(26, 9, 9, 1), dform(14, 10, 10, 1)};
    pair.prepare(full_partial, 0);
    CHECK(pair.attempt(8, 8).instructions == 1);
    CHECK(pair.native.native_statistics().block_runs == 1);
    CHECK(pair.attempt(7, 7).instructions == 7);
    pair.actual.view_size = 65536;
    // Branch in the second line must retain complete polling-loop folding.
    const std::array<u32, 3> polling{dform(32, 5, 3), dform(11, 0, 5), 0x4182fff8};
    pair.prepare(polling, 7);
    pair.expected.write32(0x2000, 0);
    pair.actual.write32(0x2000, 0);
    CHECK(pair.attempt(30, 30).instructions == 30);
    CHECK(pair.native.native_statistics().block_runs == 0);
    // A native store changes instruction backing, while cached fetched words
    // remain authoritative until an ordinary explicit invalidate executes.
    pair.initial.gpr[3] = 0x1000;
    pair.initial.gpr[6] = dform(14, 5, 5, 99);
    const std::array<u32, 8> selfmod{dform(36, 6, 3, 4),   dform(32, 4, 3, 4),  dform(14, 5, 5, 1),
                                     dform(36, 4, 7),      dform(14, 8, 8, 1),  dform(26, 9, 9, 1),
                                     dform(14, 10, 10, 1), dform(26, 11, 11, 1)};
    for (unsigned count : {4u, 8u}) {
      pair.prepare(std::span(selfmod).first(count), 0);
      CHECK(pair.attempt(count, count).instructions == count);
      CHECK(pair.native.native_statistics().block_runs == (count == 8 ? 1u : 0u));
      CHECK(pair.native.state().gpr[4] == pair.initial.gpr[6]);
      for (auto *ram : {&pair.expected, &pair.actual})
        ram->write32(0x1080, xform(982, 0, 0, 3));
      pair.oracle.state().pc = pair.native.state().pc = 0x1080;
      CHECK(pair.oracle.step() == pair.native.step());
      pair.oracle.state().pc = pair.native.state().pc = 0x1004;
      CHECK(pair.oracle.step() == pair.native.step());
      mixed_equal(pair.oracle.state(), pair.native.state());
      CHECK(pair.expected.bytes == pair.actual.bytes &&
            pair.expected.records == pair.actual.records);
    }
    // Actual Memory grants exactly canonical allocated MEM1, not mirror ranges.
    cupid::Memory real(console);
    const auto view = real.plain_cpu_ram_view();
    CHECK(view.physical_base == 0 && view.bytes.size() == cupid::Memory::mem1_allocated_size);
  }
}

void environment() {
#if defined(_M_X64) || defined(__x86_64__)
  const auto old_simd = _mm_getcsr();
  std::fenv_t old_environment;
  CHECK(std::fegetenv(&old_environment) == 0);
  constexpr int modes[]{FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
  constexpr unsigned simd_modes[]{0u, 0x6000u, 0x4000u, 0x2000u};
  for (unsigned mode = 0; mode < 4; ++mode)
    for (unsigned denormal = 0; denormal < 4; ++denormal)
      for (bool split : {false, true}) {
        NativeInteger native;
        MixedMemory ram;
        const std::array<u32, 4> words{dform(32, 4, 3), xform(138, 5, 4, 5, true), dform(36, 5, 7),
                                       dform(32, 6, 7)};
        for (unsigned pass = 0; pass < 3; ++pass) {
          CHECK(std::fesetenv(FE_DFL_ENV) == 0 && std::fesetround(modes[mode]) == 0);
          CHECK(std::feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0);
          _mm_setcsr((_mm_getcsr() & ~0xe040u) | simd_modes[(mode + unsigned(split)) & 3] |
                     (denormal & 1 ? 0x8000u : 0) | (denormal & 2 ? 0x40u : 0));
          const auto before_simd = _mm_getcsr();
          alignas(16) std::array<u8, 512> before_fx{}, after_fx{};
          _fxsave64(before_fx.data());
          std::fenv_t before{}, after{};
          CHECK(std::fegetenv(&before) == 0);
          auto state = mixed_seed(pass);
          if (pass == 2)
            state.gpr[7] = 0x10000;
          NativeRamContext context{ram.bytes.data(), nullptr, 0, unsigned(ram.bytes.size())};
          u32 retired = 0;
          CHECK(native.execute_ram_sequence(state, words, context, retired));
          CHECK(retired == (pass == 2 ? 2u : 4u));
          CHECK(std::fegetenv(&after) == 0 && before_simd == _mm_getcsr());
          _fxsave64(after_fx.data());
          CHECK(before_fx[0] == after_fx[0] && before_fx[1] == after_fx[1] &&
                (before_fx[2] & 0x3f) == (after_fx[2] & 0x3f));
          CHECK(std::memcmp(&before, &after, sizeof(before)) == 0);
          ++fenv_cases;
        }
      }
  CHECK(std::fesetenv(&old_environment) == 0);
  _mm_setcsr(old_simd);
#endif
}
} // namespace

int main() {
  if (!NativeInteger::supported()) {
    std::cout << "SKIP: native RAM execution requires x86-64\n";
    return 77;
  }
  return run_tests([] {
    semantics();
    guards();
    arithmetic_context_survival();
    context_and_hints();
    runtime();
    bat_runtime();
    environment();
    std::cout << "{\"direct_state_ram\":" << direct_cases
              << ",\"runtime_state_ram_bus\":" << runtime_cases << ",\"rejected\":" << rejected
              << ",\"FENV\":" << fenv_cases << ",\"dynamic_context_exact_PC\":" << context_cases
              << ",\"activation_assertions\":" << activation_assertions
              << ",\"native_activations\":" << native_activations << "}\n";
  });
}
