// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/machine.hpp"
#include "test_support.hpp"
#include <array>
#include <limits>
#include <sstream>
#include <thread>

namespace {
void memory_error_interrupt(cupid::Console console) {
  cupid::runtime::Machine machine(console);
  auto &memory = machine.memory();
  auto &devices = machine.devices();
  const auto initial = memory.read32(0x0c003000);
  const auto cause = memory.read32(0x0c00301c);
  const auto address = memory.read32(0x0c003020);
  CHECK(!devices.external_interrupt_pending());
  memory.memory_access_error();
  CHECK(memory.read32(0x0c003000) == (initial | 1));
  CHECK(!devices.external_interrupt_pending());
  memory.write32(0x0c003004, 1);
  CHECK(devices.external_interrupt_pending());
  memory.memory_access_error();
  CHECK(memory.read32(0x0c003000) == (initial | 1));
  CHECK(memory.read32(0x0c00301c) == cause && memory.read32(0x0c003020) == address);
  memory.write32(0x0c003000, 1);
  CHECK(memory.read32(0x0c003000) == initial);
  CHECK(!devices.external_interrupt_pending());
}

void fault_step_limits(cupid::Console console) {
  using namespace cupid;
  runtime::Machine machine(console);
  machine.load_executable(runtime::self_test_dol());
  machine.set_limits({5, 0, 0});
  machine.step();
  CHECK(machine.cpu().state().instructions == 1);
  // A reserved nonzero opcode faults repeatedly at the program vector.
  machine.memory().write32(0x700, 1);
  machine.cpu().state().pc = 0x80000700;
  for (unsigned attempt = 1; attempt < 5; ++attempt)
    machine.step();
  CHECK(machine.cpu().state().instructions == 1);
  CHECK(machine.cpu().state().pc == 0x700);
  bool bounded = false;
  try {
    machine.step();
  } catch (const runtime::LimitReached &limit) {
    bounded = true;
    CHECK(std::string(limit.what()) == "instruction-limit");
  }
  CHECK(bounded);
  std::ostringstream state;
  machine.write_state(state, "test");
  CHECK(state.str().find("\"step_attempts\":5") != std::string::npos);
}

void fault_poll_cadence(cupid::Console console) {
  using namespace cupid;
  runtime::Machine machine(console);
  machine.load_executable(runtime::self_test_dol());
  machine.set_limits({0, 0, 0});
  machine.step();
  machine.memory().write32(0x700, 1);
  machine.cpu().state().pc = 0x80000700;
  for (unsigned attempt = 1; attempt < 4096; ++attempt)
    machine.step();
  CHECK(machine.cpu().state().instructions == 1);
  machine.set_limits({0, 0, std::numeric_limits<double>::min()});
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  bool bounded = false;
  try {
    machine.step();
  } catch (const runtime::LimitReached &limit) {
    bounded = true;
    CHECK(std::string(limit.what()) == "time-limit");
  }
  CHECK(bounded);
}

void zero_step_limits(cupid::Console console) {
  using namespace cupid;
  runtime::Machine machine(console);
  machine.load_executable(runtime::self_test_dol());
  machine.set_limits({5, 0, 0});
  machine.step();
  const auto cycles = machine.cpu().state().cycles;
  machine.cpu().state().pc = 0x80005000;
  for (unsigned attempt = 1; attempt < 5; ++attempt)
    machine.step();
  CHECK(machine.cpu().state().pc == 0x80005010);
  CHECK(machine.cpu().state().cycles == cycles);
  CHECK(machine.cpu().state().instructions == 1);
  CHECK(machine.cpu().state().spr[cpu::spr::srr0] == 0);
  CHECK_THROWS(machine.step());
  std::ostringstream state;
  machine.write_state(state, "test");
  CHECK(state.str().find("\"step_attempts\":5") != std::string::npos);
  machine.load_executable(runtime::self_test_dol());
  machine.step();
  CHECK(machine.cpu().state().instructions == 1);
  std::ostringstream reset;
  machine.write_state(reset, "test");
  CHECK(reset.str().find("\"step_attempts\":1") != std::string::npos);
}
} // namespace

int main() {
  return run_tests([] {
    using namespace cupid;
    memory_error_interrupt(Console::GameCube);
    memory_error_interrupt(Console::Wii);
    fault_step_limits(Console::GameCube);
    fault_step_limits(Console::Wii);
    fault_poll_cadence(Console::GameCube);
    fault_poll_cadence(Console::Wii);
    zero_step_limits(Console::GameCube);
    zero_step_limits(Console::Wii);
    runtime::Machine machine(Console::GameCube);
    machine.load_executable(runtime::self_test_dol());
    for (unsigned i = 0; i < 7; ++i)
      machine.step();
    CHECK(machine.cpu().state().gpr[5] == 16);
    CHECK(machine.cpu().state().gpr[7] == 16);
    CHECK(machine.memory().read32(0x5000) == 16);
    CHECK(machine.cpu().state().pc == 0x8000401c);
    machine.set_limits({8, 0, 0});
    machine.step();
    CHECK_THROWS(machine.step());
    auto malformed = runtime::self_test_dol();
    malformed[0x90] = 0xff;
    Memory memory(Console::GameCube);
    memory.write32(0x4000, 0xdeadbeef);
    CHECK_THROWS(runtime::load_dol(memory, malformed));
    CHECK(memory.read32(0x4000) == 0xdeadbeef);
    malformed = runtime::self_test_dol();
    malformed[0xe3] = 1;
    CHECK_THROWS(runtime::load_dol(memory, malformed));
    CHECK_THROWS(runtime::load_dol(memory, std::span<const u8>(malformed).first(128)));
    std::array<u8, 8> yuv{16, 128, 235, 128, 81, 90, 145, 240};
    memory.copy_in(0x1000, yuv);
    auto frame = platform::decode_xfb(memory, {0x1000, 2, 2, 4, 0, 0});
    CHECK(frame.rgba.size() == 16);
    CHECK(frame.rgba[0] == 0);
    CHECK(frame.rgba[3] == 255);
    CHECK(frame.rgba[4] == 254);
    CHECK(frame.rgba[8] == 254);
    CHECK(frame.rgba[9] == 0);
    CHECK(frame.rgba[10] == 0);
    CHECK_THROWS(platform::decode_xfb(memory, {0x1000, 3, 2, 6, 0, 0}));
    CHECK_THROWS(platform::decode_xfb(memory, {Memory::mem1_size - 4, 2, 2, 4, 0, 0}));
    std::ostringstream state;
    machine.write_state(state, "test-limit");
    CHECK(state.str().find("\"instructions\":8") != std::string::npos);
    platform::Output output({true, false, {}, {}});
    output.present(memory, {0x1000, 2, 2, 4, 0, 0});
    CHECK(output.presented_frames() == 1);
    CHECK(output.last_frame_hash() == platform::frame_hash(frame.rgba));
  });
}
