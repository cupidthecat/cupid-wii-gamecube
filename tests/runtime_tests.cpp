// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/machine.hpp"
#include "test_support.hpp"
#include <array>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <thread>

namespace {
struct TraceFile {
  std::filesystem::path path;

  TraceFile() {
    std::random_device random;
    path = std::filesystem::temp_directory_path() /
           ("cupid-trace-" + std::to_string(random()) + "-" + std::to_string(random()) + ".jsonl");
    CHECK(!std::filesystem::exists(path));
  }

  ~TraceFile() {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }
};

void trace_fetch_status(cupid::Console console) {
  using namespace cupid;
  TraceFile temporary;
  u32 initial_opcode = 0;
  constexpr u32 unaligned_load = 0x7c602028; // lwarx r3,r0,r4
  {
    runtime::Machine machine(console);
    machine.load_executable(runtime::self_test_dol());
    machine.set_trace(temporary.path, 8);
    auto &state = machine.cpu().state();
    initial_opcode = machine.memory().read32(0x4000);
    machine.memory().write32(0x5004, 1);
    machine.step();
    CHECK(state.instructions == 1);
    state.pc = 0x80005000;
    machine.step();
    CHECK(state.last_opcode == 0 && state.pc == 0x80005004);
    machine.step();
    CHECK(state.last_opcode == 1 && state.pc == 0x700);

    // An interrupt at the last fetched PC still has no new instruction word.
    state.pc = state.last_pc;
    state.msr = cpu::msr::ee;
    machine.memory().write32(0x0c003004, 1);
    machine.memory().memory_access_error();
    machine.step();
    CHECK(state.pc == 0x500 && state.last_opcode == 1);
    machine.memory().write32(0x0c003000, 1);
    state.pc = 0x80006001;
    machine.step();
    CHECK(state.pc == 0x80006005 && state.last_opcode == 0);
    CHECK(machine.cpu().last_step_fetched_instruction());

    // Data alignment faults occur after obtaining and decoding the instruction.
    machine.memory().write32(0x7000, unaligned_load);
    state.pc = 0x80007000;
    state.gpr[4] = 1;
    machine.step();
    CHECK(state.pc == 0x600 && state.last_opcode == unaligned_load);
    CHECK(state.spr[cpu::spr::dar] == 1);
    CHECK(machine.cpu().last_step_fetched_instruction());

    state.pc = 0x40005000;
    state.msr = cpu::msr::ir;
    state.sr[4] = 1u << 28;
    const auto cycles = state.cycles;
    machine.step();
    CHECK(state.pc == 0x400 && state.last_opcode == unaligned_load && state.cycles == cycles);
    CHECK(state.spr[cpu::spr::dar] == 1);
    CHECK(state.instructions == 1);

    machine.load_executable(runtime::self_test_dol());
    CHECK(!machine.cpu().last_step_fetched_instruction());
    machine.step();
    CHECK(machine.cpu().state().instructions == 1);
  }

  std::ifstream trace(temporary.path);
  CHECK(trace);
  constexpr std::array<bool, 8> fetched{true, true, true, false, true, true, false, true};
  const std::array<u32, 8> opcodes{initial_opcode, 0, 1, 1, 0, unaligned_load, unaligned_load,
                                   initial_opcode};
  constexpr std::array<u32, 8> before{0x80004000, 0x80005000, 0x80005004, 0x80005004,
                                      0x80006001, 0x80007000, 0x40005000, 0x80004000};
  constexpr std::array<u32, 8> after{0x80004004, 0x80005004, 0x700, 0x500,
                                     0x80006005, 0x600,      0x400, 0x80004004};
  constexpr std::array<u32, 8> data_addresses{0, 0, 0, 0, 0, 1, 1, 0};
  std::string record;
  for (std::size_t index = 0; index < fetched.size(); ++index) {
    CHECK(std::getline(trace, record));
    CHECK(record.find(std::string("\"opcode_fetched\":") + (fetched[index] ? "true" : "false")) !=
          std::string::npos);
    CHECK(record.find("\"opcode\":" + std::to_string(opcodes[index]) + ",") != std::string::npos);
    CHECK(record.find("\"pc_before\":" + std::to_string(before[index]) + ",") != std::string::npos);
    CHECK(record.find("\"pc_after\":" + std::to_string(after[index]) + ",") != std::string::npos);
    CHECK(record.find("\"dar\":" + std::to_string(data_addresses[index]) + ",") !=
          std::string::npos);
    CHECK(record.find("\"attempt\":" + std::to_string(index == 7 ? 1 : index + 1) + ",") !=
          std::string::npos);
    CHECK(record.find("\"step\":1,") != std::string::npos);
  }
  CHECK(!std::getline(trace, record));
  CHECK(!trace.bad());
}

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
    trace_fetch_status(Console::GameCube);
    trace_fetch_status(Console::Wii);
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
