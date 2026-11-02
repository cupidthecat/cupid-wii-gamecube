// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/machine.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>

using namespace cupid;

namespace {
constexpr unsigned write_pipe_register = 921;
constexpr u32 graphics_port = 0x0c008000;

u32 special(unsigned operation, unsigned reg) {
  return (31u << 26) | (reg << 21) | ((write_pipe_register & 31) << 16) |
         ((write_pipe_register >> 5) << 11) | (operation << 1);
}

void configure(runtime::Machine &machine) {
  auto &graphics = machine.graphics();
  auto &devices = machine.devices();
  graphics.reset_fifo();
  devices.write(0x0c00300c, 0x1000, 4);
  devices.write(0x0c003010, 0x1040, 4);
  devices.write(0x0c003014, 0x1000, 4);
  const auto fifo = [&](unsigned offset, u32 value) {
    graphics.write(0x0c000000 + offset, value & 0xffff, 2);
    graphics.write(0x0c000002 + offset, value >> 16, 2);
  };
  fifo(0x20, 0x1000);
  fifo(0x24, 0x1040);
  fifo(0x34, 0x1000);
  fifo(0x38, 0x1000);
  fifo(0x28, 96);
  fifo(0x2c, 32);
  graphics.submit(std::array<u8, 5>{0x61, 0x55, 0, 0, 0});
  graphics.write(0x0c000002, 0x11, 2);
  machine.cpu().reset();
  machine.cpu().state().spr[cpu::spr::dec] = 0xffffffff;
  machine.cpu().state().pc = 0x4000;
  CHECK(graphics.read(0x0c000030, 2) == 0);
}

void gather_register_checks(Console console, cpu::ExecutionBackend backend) {
  runtime::Machine machine(console);
  machine.load_executable(runtime::self_test_dol());
  machine.set_limits({1000000, 0, 0});
  machine.cpu().set_execution_backend(backend);
  auto &memory = machine.memory();
  auto &graphics = machine.graphics();
  auto &state = machine.cpu().state();
  for (unsigned partial = 1; partial < 32; ++partial) {
    configure(machine);
    CHECK(machine.cpu().execution_backend() == backend);
    const u64 commands = graphics.command_count();
    // The first full burst leaves a decoded BP command awaiting its four
    // payload bytes. A WPAR write must not discard this independent prefix.
    for (unsigned i = 0; i < 31; ++i)
      memory.write8(graphics_port, 0);
    memory.write8(graphics_port, 0x61);
    CHECK(graphics.command_count() == commands + 31);
    for (unsigned i = 0; i < partial; ++i)
      memory.write8(graphics_port, 0xff);
    memory.write32(0x4000, special(467, 3));
    state.gpr[3] = graphics_port;
    machine.step();
    if (backend == cpu::ExecutionBackend::NativeInteger) {
      const auto stats = machine.cpu().native_statistics();
      CHECK(stats.executed + stats.fallback == 1);
    }
    CHECK(state.pc == 0x4004 && state.spr[write_pipe_register] == graphics_port);
    CHECK(machine.devices().read(0x0c003014, 4) == 0x1020);
    CHECK(graphics.read(0x0c000028, 2) == 96 && graphics.read(0x0c00002c, 2) == 32);
    CHECK(graphics.command_count() == commands + 31);
    std::array<u8, 32> next{};
    next[0] = 0x55;
    next[2] = 0x6c;
    next[3] = 0x0d;
    for (u8 value : next)
      memory.write8(graphics_port, value);
    CHECK(machine.devices().read(0x0c003014, 4) == 0x1040);
    CHECK(graphics.command_count() == commands + 60);
    CHECK(graphics.read(0x0c001010, 2) == 13 && graphics.read(0x0c001012, 2) == 27);
    const auto written = memory.range(0x1020, 32);
    CHECK(std::equal(next.begin(), next.end(), written.begin()));
    CHECK(memory.read8(0x101f) == 0x61);
  }
  for (unsigned partial = 0; partial < 32; ++partial) {
    configure(machine);
    CHECK(machine.cpu().execution_backend() == backend);
    const u64 commands = graphics.command_count();
    for (unsigned i = 0; i < partial; ++i)
      memory.write8(graphics_port, 0);
    state.spr[write_pipe_register] = graphics_port | 1;
    memory.write32(0x4000, special(339, 4));
    machine.step();
    if (backend == cpu::ExecutionBackend::NativeInteger) {
      const auto stats = machine.cpu().native_statistics();
      CHECK(stats.executed + stats.fallback == 1);
    }
    CHECK(state.gpr[4] == graphics_port && state.spr[write_pipe_register] == graphics_port);
    // Reading the compatibility BNE value cannot flush or reset the CPU prefix.
    for (unsigned i = partial; i < 32; ++i)
      memory.write8(graphics_port, 0);
    CHECK(machine.devices().read(0x0c003014, 4) == 0x1020);
    CHECK(graphics.command_count() == commands + 32);
  }
}
} // namespace

int main() {
  return run_tests([] {
    for (auto backend :
         {cpu::ExecutionBackend::Interpreter, cpu::ExecutionBackend::NativeInteger}) {
      if (backend == cpu::ExecutionBackend::NativeInteger && !cpu::NativeInteger::supported())
        continue;
      gather_register_checks(Console::GameCube, backend);
      gather_register_checks(Console::Wii, backend);
    }
  });
}
