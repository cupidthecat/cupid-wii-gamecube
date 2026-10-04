// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/interpreter.hpp"
#include "test_support.hpp"
#include <array>
#include <iostream>
#include <vector>

using namespace cupid;
using namespace cupid::cpu;

namespace {
class RecordedMemory final : public Bus {
public:
  std::vector<u8> bytes = std::vector<u8>(1024 * 1024);
  std::vector<std::array<u64, 4>> records;
  bool reject_fetch = false;

  u8 read8(u32 address) override {
    if (address >= bytes.size())
      throw BusFault(address, false, "Authored native test bounds");
    return bytes[address];
  }

  void write8(u32 address, u8 value) override {
    if (address >= bytes.size())
      throw BusFault(address, true, "Authored native test bounds");
    bytes[address] = value;
  }

  bool read_instruction_words(u32 address, std::span<u32> words) override {
    records.push_back({0, address, words.size(), 0});
    if (reject_fetch)
      throw BusFault(address, false, "Authored native fetch failure", true);
    return Bus::read_instruction_words(address, words);
  }

  bool read_instruction_bytes(u32 address, std::span<u8> destination) override {
    records.push_back({1, address, destination.size(), 0});
    return Bus::read_instruction_bytes(address, destination);
  }

  u64 read_cpu_data(u32 address, unsigned size) override {
    const u64 value = Bus::read_cpu_data(address, size);
    records.push_back({2, address, size, value});
    return value;
  }

  void write_cpu_data(u32 address, u32 value, unsigned size) override {
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

State seed(unsigned pattern, bool summary) {
  constexpr std::array<u32, 8> values{0,          1,          0xffffffff, 0x80000000,
                                      0x7fffffff, 0xaaaaaaaa, 0x55555555, 0x12345678};
  State state;
  for (unsigned n = 0; n < state.gpr.size(); ++n)
    state.gpr[n] = values[(n + pattern) & 7];
  for (unsigned n = 0; n < state.fpr.size(); ++n)
    state.fpr[n].bits = {0x7ff123456789abcdull ^ n, 0x8123456789abcdefull ^ n};
  for (unsigned n = 0; n < state.spr.size(); ++n)
    state.spr[n] = 0x12345678u + n * 0x12345u;
  for (unsigned n = 0; n < state.sr.size(); ++n)
    state.sr[n] = n * 0x9876543u;
  state.spr[spr::xer] = (state.spr[spr::xer] & ~0x80000000u) | (summary ? 0x80000000u : 0);
  state.spr[spr::hid0] = 0;
  state.spr[spr::dec] = 0xffffffff;
  state.spr[spr::mmcr0] = state.spr[spr::mmcr1] = 0;
  state.pc = 0x1000;
  state.msr = 0;
  state.cr = 0x89abcdef;
  state.fpscr = 0x12345678;
  state.last_pc = 0x9000;
  state.last_opcode = 0x55667788;
  state.cycles = 0x123456789;
  state.instructions = 0x23456789;
  state.timebase = 0xfffffffffffffff0ull;
  return state;
}

struct Pair {
  RecordedMemory earlier_memory, native_memory;
  Interpreter earlier, native;
  u64 comparisons = 0;

  explicit Pair(Console console)
      : earlier(earlier_memory, console), native(native_memory, console) {
    native.set_execution_backend(ExecutionBackend::NativeInteger);
  }

  void reset() {
    earlier.reset();
    native.reset();
    CHECK(native.execution_backend() == ExecutionBackend::NativeInteger);
    CHECK(native.native_statistics().cache_entries == 0);
  }

  void step(bool compiled) {
    earlier_memory.records.clear();
    native_memory.records.clear();
    const auto before = native.native_statistics();
    CHECK(earlier.step() == native.step());
    equal_state(earlier.state(), native.state());
    CHECK(earlier.last_step_fetched_instruction() == native.last_step_fetched_instruction());
    CHECK(earlier_memory.records == native_memory.records);
    if (compiled)
      CHECK(native.native_statistics().executed == before.executed + 1);
    ++comparisons;
  }

  void operation(u32 opcode, const State &state) {
    // Each semantic case is independent. Clear the host compilation cache before
    // its bound so every supported case is required to execute emitted code.
    if (native.native_statistics().cache_entries >= 512)
      reset();
    earlier.state() = native.state() = state;
    earlier_memory.write32(0x1000, opcode);
    native_memory.write32(0x1000, opcode);
    step(true);
  }
};

u32 immediate(unsigned primary, unsigned source, unsigned address, unsigned value) {
  return (primary << 26) | (source << 21) | (address << 16) | value;
}

void arithmetic_and_rotate_tests() {
  u64 total = 0;
  for (Console console : {Console::GameCube, Console::Wii}) {
    Pair pair(console);
    const std::array<State, 4> states{seed(0, false), seed(3, false), seed(2, true), seed(5, true)};
    for (unsigned primary : {14u, 15u, 24u, 25u, 26u, 27u, 28u, 29u})
      for (unsigned source = 0; source < 32; ++source)
        for (unsigned address = 0; address < 32; ++address)
          for (unsigned value : {0u, 1u, 0x7fffu, 0x8000u, 0xffffu, 0x1234u})
            for (const State &state : states)
              pair.operation(immediate(primary, source, address, value), state);
    constexpr std::array<std::array<unsigned, 3>, 5> aliases{
        {{3, 4, 5}, {3, 3, 5}, {3, 4, 4}, {3, 4, 3}, {3, 3, 3}}};
    for (unsigned begin = 0; begin < 32; ++begin)
      for (unsigned end = 0; end < 32; ++end)
        for (unsigned rotation = 0; rotation < 32; ++rotation)
          for (unsigned primary : {20u, 21u, 23u})
            for (bool record : {false, true})
              for (unsigned alias = 0; alias < aliases.size(); ++alias) {
                if (alias && rotation != 0 && rotation != 1 && rotation != 16 && rotation != 31)
                  continue;
                const auto [source, destination, count] = aliases[alias];
                for (const State &initial : states) {
                  State state = initial;
                  if (primary == 23)
                    state.gpr[count] = rotation + 0xffffffc0u;
                  const u32 opcode = (primary << 26) | (source << 21) | (destination << 16) |
                                     ((primary == 23 ? count : rotation) << 11) | (begin << 6) |
                                     (end << 1) | static_cast<u32>(record);
                  pair.operation(opcode, state);
                }
              }
    CHECK(pair.earlier_memory.bytes == pair.native_memory.bytes);
    total += pair.comparisons;
  }
  std::cout << total << " complete-state emitted integer comparisons passed\n";
}

void fetch_and_fallback_tests() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Pair pair(console);
    const auto install = [&](u32 physical, u32 opcode) {
      pair.earlier_memory.write32(physical, opcode);
      pair.native_memory.write32(physical, opcode);
    };
    const auto set_pc = [&](u32 pc) { pair.earlier.state().pc = pair.native.state().pc = pc; };
    pair.earlier.configure_boot();
    pair.native.configure_boot();
    pair.earlier.state().spr[spr::hid0] = pair.native.state().spr[spr::hid0] = 0x8000;
    install(0x1000, immediate(14, 3, 0, 7));
    set_pc(0x80001000);
    pair.step(true);
    install(0x1000, immediate(14, 3, 0, 11));
    set_pc(0x80001000);
    pair.step(true);
    CHECK(pair.native.state().gpr[3] == 7);
    // A real guest invalidation exposes the changed fetched word to compilation.
    install(0x2000, (31u << 26) | (4 << 11) | (982 << 1));
    pair.earlier.state().gpr[4] = pair.native.state().gpr[4] = 0x80001000;
    set_pc(0x80002000);
    pair.step(false);
    set_pc(0x80001000);
    pair.step(true);
    CHECK(pair.native.state().gpr[3] == 11);
    pair.reset();
    install(0x1001, immediate(15, 31, 0, 0x8000));
    pair.earlier.state().spr[spr::hid0] = pair.native.state().spr[spr::hid0] = 0x8000;
    set_pc(0x1001);
    pair.step(true);
    CHECK(pair.native.state().gpr[31] == 0x80000000);
    pair.reset();
    install(0x1000, 0);
    set_pc(0x1000);
    auto before = pair.native.native_statistics();
    pair.step(false);
    CHECK(pair.native.native_statistics().executed == before.executed);
    CHECK(pair.native.state().pc == 0x1004 && pair.native.state().cycles == 0);
    install(0x1000, immediate(14, 3, 0, 17));
    set_pc(0x1000);
    pair.earlier.state().msr = pair.native.state().msr = msr::ee;
    pair.earlier.set_external_interrupt(true);
    pair.native.set_external_interrupt(true);
    pair.step(false);
    CHECK(pair.native.state().pc == 0x500 && pair.native.native_statistics().executed == 0);
    pair.earlier.set_external_interrupt(false);
    pair.native.set_external_interrupt(false);
    for (u32 opcode : {1u << 26, 63u << 26, immediate(32, 3, 4, 0), 0x48000004u}) {
      pair.reset();
      install(0x1000, opcode);
      set_pc(0x1000);
      pair.earlier.state().gpr[4] = pair.native.state().gpr[4] = 0x2000;
      pair.step(false);
      CHECK(pair.native.native_statistics().executed == 0);
      CHECK(pair.native.native_statistics().fallback == 1);
    }
    // Native selection preserves the unsupported host-operation diagnostic.
    pair.reset();
    install(0x1000, immediate(14, 3, 0, 5));
    set_pc(0x1000);
    pair.earlier_memory.reject_fetch = pair.native_memory.reject_fetch = true;
    for (Interpreter *cpu : {&pair.earlier, &pair.native}) {
      bool failed = false;
      try {
        cpu->step();
      } catch (const UnsupportedInstruction &error) {
        failed = error.pc == 0x1000;
      }
      CHECK(failed && !cpu->last_step_fetched_instruction());
    }
    equal_state(pair.earlier.state(), pair.native.state());
    CHECK(pair.native.native_statistics().compiled_operations == 0);
    pair.earlier_memory.reject_fetch = pair.native_memory.reject_fetch = false;
    pair.native.set_execution_backend(ExecutionBackend::Interpreter);
    set_pc(0x1000);
    pair.step(false);
    CHECK(pair.native.native_statistics().executed == 0);
    pair.native.set_execution_backend(ExecutionBackend::NativeInteger);
    set_pc(0x1000);
    pair.step(true);
    CHECK(pair.native.native_statistics().executed == 1);
    CHECK(pair.earlier_memory.bytes == pair.native_memory.bytes);
  }
}

void bounded_cache_tests() {
  NativeInteger native(2);
  State state = seed(0, false);
  CHECK(native.execute(state, immediate(14, 3, 0, 7)));
  CHECK(native.execute(state, immediate(24, 3, 4, 0x8000)));
  CHECK(native.statistics().cache_entries == 2 && native.statistics().compiled_operations == 2);
  const State full = state;
  CHECK(!native.execute(state, immediate(14, 3, 0, 9)));
  equal_state(full, state);
  CHECK(native.statistics().fallback == 1 && native.statistics().capacity_fallbacks == 1);
  CHECK(native.execute(state, immediate(14, 3, 0, 7)));
  CHECK(native.statistics().cache_hits == 1 && state.gpr[3] == 7);
  const State before = state;
  CHECK(!native.execute(state, 0x48000004));
  equal_state(before, state);
  CHECK(native.statistics().fallback == 2 && native.statistics().capacity_fallbacks == 1);
  CHECK(native.statistics().executable_bytes >= 4096 &&
        native.statistics().executable_bytes <= 65536);
  native.reset();
  CHECK(native.statistics().executed == 0 && native.statistics().cache_entries == 0);
  CHECK(native.statistics().executable_bytes == 0);
  CHECK(native.execute(state, immediate(14, 3, 0, 9)) && state.gpr[3] == 9);
  for (unsigned capacity : {0u, 4097u}) {
    bool rejected = false;
    try {
      NativeInteger invalid(capacity);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    CHECK(rejected);
  }
}

void clock_monitor_and_fault_tests() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Pair pair(console);
    State initial = seed(2, true);
    initial.spr[spr::dec] = 3;
    initial.spr[spr::mmcr0] = (1 << 6) | 11 | (1 << 15);
    initial.spr[spr::pmc1] = 0x7ffffffe;
    initial.spr[spr::pmc2] = 0xffffffff;
    pair.earlier.state() = pair.native.state() = initial;
    for (unsigned n = 0; n < 96; ++n) {
      auto &a = pair.earlier.state();
      auto &b = pair.native.state();
      a.pc = b.pc = 0x1000;
      a.msr = b.msr = n % 5 == 0 ? msr::ee : 0;
      u32 opcode = n % 3 == 0   ? immediate(14, 3, 3, 1)
                   : n % 3 == 1 ? immediate(28, 3, 4, 0xffff)
                                : (31u << 26) | (3 << 21) | (5 << 16) | (6 << 11) | (459 << 1);
      if (n % 7 == 0 || n % 11 == 0) {
        const unsigned index = n % 7 == 0 ? spr::dec : 284;
        opcode = (31u << 26) | (3 << 21) | ((index & 31) << 16) | ((index >> 5) << 11) | (467 << 1);
        a.gpr[3] = b.gpr[3] = n % 7 == 0 ? n & 15 : 0xfffffff0u + n;
      }
      pair.earlier_memory.write32(0x1000, opcode);
      pair.native_memory.write32(0x1000, opcode);
      pair.step(false);
    }
    CHECK(pair.native.native_statistics().executed != 0);
    CHECK(pair.native.native_statistics().fallback != 0);
    pair.reset();
    pair.earlier.state().pc = pair.native.state().pc = 0x80340000;
    pair.earlier.state().msr = pair.native.state().msr = msr::ir;
    pair.step(false);
    CHECK(pair.native.state().pc == 0x400);
    CHECK(pair.native.native_statistics().compiled_operations == 0);
    pair.reset();
    pair.earlier.state().pc = pair.native.state().pc = 0x1000;
    pair.earlier.state().msr = pair.native.state().msr = msr::le;
    for (Interpreter *cpu : {&pair.earlier, &pair.native}) {
      bool rejected = false;
      try {
        cpu->step();
      } catch (const UnsupportedInstruction &) {
        rejected = true;
      }
      CHECK(rejected && !cpu->last_step_fetched_instruction());
    }
    equal_state(pair.earlier.state(), pair.native.state());
    CHECK(pair.native.native_statistics().executed == 0);
  }
}
} // namespace

int main() {
  if (!NativeInteger::supported()) {
    std::cout << "SKIP: native integer execution requires x86-64\n";
    return 77;
  }
  return run_tests([] {
    arithmetic_and_rotate_tests();
    fetch_and_fallback_tests();
    bounded_cache_tests();
    clock_monitor_and_fault_tests();
  });
}
