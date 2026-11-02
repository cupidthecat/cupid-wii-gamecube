// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/memory.hpp"
#include "cupid/cpu/data_cache.hpp"
#include "cupid/cpu/interpreter.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>

using namespace cupid;

namespace {
u32 indexed(unsigned operation, unsigned reg = 4, unsigned address = 3) {
  return (31u << 26) | (reg << 21) | (address << 11) | (operation << 1);
}

void cache_storage(Console console) {
  Memory memory(console);
  cpu::DataCache cache;
  std::array<u8, 8> first{1, 2, 3, 4, 5, 6, 7, 8}, result{};
  for (unsigned offset = 0; offset < 32; ++offset) {
    cache.reset();
    std::fill(memory.range(0x8000, 64).begin(), memory.range(0x8000, 64).end(), 0x5a);
    cache.write(memory, 0x8000 + offset, first, false);
    cache.read(memory, 0x8000 + offset, result, true);
    CHECK(result == first);
    CHECK(memory.read8(0x8000 + offset) == 0x5a);
    cache.store(memory, 0x8000);
    cache.store(memory, 0x8020);
    CHECK(std::equal(first.begin(), first.end(), memory.range(0x8000 + offset, 8).begin()));
    memory.write8(0x8000 + offset, 0xab);
    cache.read(memory, 0x8000 + offset, result, false);
    CHECK(result == first);
    cache.invalidate(memory, 0x8000);
    cache.invalidate(memory, 0x8020);
    cache.read(memory, 0x8000 + offset, result, false);
    CHECK(result[0] == 0xab);
  }
  cache.reset();
  memory.write8(0x4000, 0x33);
  cache.write(memory, 0x4000, first, true);
  CHECK(memory.read8(0x4000) == 1);
  memory.write8(0x4000, 0x55);
  cache.read(memory, 0x4000, result, true);
  CHECK(result[0] == 0x55);
  cache.read(memory, 0x4000, result, false);
  cache.write(memory, 0x4000, first, true);
  CHECK(memory.read8(0x4000) == 0x55);
  cache.flush(memory, 0x80004000);
  CHECK(memory.read8(0x4000) == 1);
  memory.write8(0x4000, 0x66);
  cache.read(memory, 0x4000, result, false);
  CHECK(result[0] == 0x66);
  cache.reset();
  for (unsigned tag = 0; tag < 8; ++tag) {
    memory.write8(0x10000 + tag * 4096, 0);
    cache.write(memory, 0x10000 + tag * 4096, first, false);
  }
  CHECK(memory.read8(0x10000) == 0);
  cache.read(memory, 0x18000, result, false);
  CHECK(memory.read8(0x10000) == 1);
  cache.flush_all(memory);
  for (unsigned tag = 0; tag < 8; ++tag)
    CHECK(memory.read8(0x10000 + tag * 4096) == 1);
  // Failed fills retain the selected way's bytes. Locked cold failures have
  // no defined read result and must not silently manufacture a value.
  cache.reset();
  cache.write(memory, 0x8000, first, false);
  cache.reset();
  cache.read(memory, 0x01808000, result, false);
  CHECK(result == first);
  cache.store(memory, 0x01808000);
  cache.reset();
  CHECK_THROWS(cache.read(memory, 0x01808000, result, true));
  CHECK(memory.instruction_backing_read_errors() == 0);
}

void uncached_subword_visibility(Console console) {
  for (unsigned width : {1u, 2u, 4u})
    for (unsigned offset = 0; offset < 8; ++offset) {
      if (width == 4 && (offset & 3) == 0)
        continue;
      Memory memory(console);
      unsigned interrupts = 0;
      memory.memory_error = [&] { ++interrupts; };
      cpu::Interpreter processor(memory, console);
      processor.set_data_cache_enabled(true);
      auto &state = processor.state();
      state.pc = 0x1000;
      state.msr = cpu::msr::dr;
      state.spr[536] = 0x40000002;
      state.spr[537] = 0x42;
      state.gpr[3] = 0x40009000 + offset;
      state.gpr[4] = 0x12345678;
      std::fill(memory.range(0x9000, 32).begin(), memory.range(0x9000, 32).end(), 0x5a);
      memory.write32(0x1000, indexed(width == 1 ? 215 : width == 2 ? 407 : 151));
      processor.step();
      CHECK(interrupts == 1 && state.pc == 0x1004);
      CHECK(std::all_of(memory.range(0x9000, 32).begin(), memory.range(0x9000, 32).end(),
                        [](u8 byte) { return byte == 0x5a; }));
      processor.set_data_cache_enabled(false);
      CHECK(memory.read32(0x9000) != 0x5a5a5a5a);
      CHECK(memory.read32(0x9000) == memory.read32(0x9004));
    }
}

void cpu_visibility(Console console, cpu::ExecutionBackend backend) {
  Memory memory(console);
  cpu::Interpreter cpu(memory, console);
  cpu.set_execution_backend(backend);
  cpu.set_data_cache_enabled(true);
  auto &state = cpu.state();
  const auto step = [&](u32 opcode) {
    state.pc = 0x1000;
    memory.write32(0x1000, opcode);
    cpu.step();
  };
  state.spr[cpu::spr::hid0] = 0x4000;
  state.gpr[3] = 0x8000;
  state.gpr[4] = 0x12345678;
  step(indexed(151));
  CHECK(memory.read32(0x8000) == 0);
  const auto instructions = state.instructions, cycles = state.cycles;
  const auto pc = state.pc;
  CHECK(cpu.read_data_word(0x8000) == 0x12345678);
  CHECK(state.instructions == instructions && state.cycles == cycles && state.pc == pc);
  step(indexed(23, 5));
  CHECK(state.gpr[5] == 0x12345678);
  step(indexed(54));
  CHECK(memory.read32(0x8000) == 0x12345678);
  memory.write32(0x8000, 0xaabbccdd);
  step(indexed(23, 5));
  CHECK(state.gpr[5] == 0x12345678);
  step(indexed(470));
  step(indexed(23, 5));
  CHECK(state.gpr[5] == 0xaabbccdd);
  step(indexed(1014));
  CHECK(memory.read32(0x8000) == 0xaabbccdd);
  step(indexed(23, 5));
  CHECK(state.gpr[5] == 0);
  step(indexed(86));
  CHECK(memory.read32(0x8000) == 0);
  state.gpr[4] = 0x87654321;
  step(indexed(151));
  cpu.set_data_cache_enabled(false);
  CHECK(memory.read32(0x8000) == 0x87654321);
  CHECK(!cpu.data_cache_enabled());
  cpu.set_data_cache_enabled(true);
  state.spr[cpu::spr::hid0] |= 0x1000;
  state.gpr[3] = 0xa000;
  step(indexed(151));
  CHECK(memory.read32(0xa000) == 0x87654321);
  // Native RAM groups cannot bypass cache visibility.
  CHECK(!cpu.integer_run_possible());
  CHECK(cpu.try_integer_run(32, 32).instructions == 0);
  state.spr[cpu::spr::hid0] &= ~0x1000u;
  state.gpr[3] = 0x8000;
  step(indexed(23, 5));
  state.msr = cpu::msr::dr;
  state.spr[536] = 0x40000002;
  state.spr[537] = 0x42;
  state.gpr[3] = 0x40008000;
  state.gpr[4] = 0x11223344;
  step(indexed(151));
  CHECK(memory.read32(0x8000) == 0x11223344);
  step(indexed(23, 5));
  CHECK(state.gpr[5] == 0x11223344);
  state.msr = 0;
  state.gpr[3] = 0x8000;
  step(indexed(23, 5));
  CHECK(state.gpr[5] == 0x87654321);
  state.msr = cpu::msr::dr;
  state.spr[536] = 0;
  state.gpr[3] = 0x40008017;
  state.sr[4] = 0x80000123;
  step(indexed(86));
  CHECK(state.pc == 0x1004);
  state.sr[4] = 0x123;
  state.spr[cpu::spr::dar] = 0x13579bdf;
  step(indexed(470));
  CHECK(state.pc == 0x1004 && state.spr[cpu::spr::dar] == 0x13579bdf);
  step(indexed(54));
  CHECK(state.pc == 0x300 && state.spr[cpu::spr::dar] == 0x40008000);
}
} // namespace

int main() {
  return run_tests([] {
    for (Console console : {Console::GameCube, Console::Wii}) {
      cache_storage(console);
      uncached_subword_visibility(console);
      cpu_visibility(console, cpu::ExecutionBackend::Interpreter);
      if (cpu::NativeInteger::supported())
        cpu_visibility(console, cpu::ExecutionBackend::NativeInteger);
    }
  });
}
