// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/dsp/engine.hpp"
#include "cupid/dsp/instruction_engine.hpp"
#include "cupid/runtime/machine.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <sstream>
#include <vector>

using namespace cupid;

namespace {
struct Firmware {
  std::vector<u8> program = std::vector<u8>(8192), coefficients = std::vector<u8>(4096);

  Firmware(std::initializer_list<u16> code) {
    for (unsigned i = 0; i < 4096; ++i)
      put(i, 0x21);
    unsigned offset = 0;
    for (u16 word : code)
      put(offset++, word);
  }

  void put(unsigned offset, u16 word) {
    program.at(offset * 2) = u8(word >> 8);
    program.at(offset * 2 + 1) = u8(word);
  }
};

void instruction_registers_and_loops() {
  dsp::InstructionCore core;
  core.write_register(16, 0x80);
  CHECK(core.read_register(16) == 0xff80);
  core.write_register(18, 0xffff);
  core.write_register(19, 0xffff);
  core.write_register(22, 0xffff);
  CHECK(core.read_register(18) == 255 && core.read_register(19) == 0xfeff);
  CHECK(core.read_register(22) == 255);
  core.write_register(12, 1);
  core.write_register(12, 2);
  core.write_register(12, 3);
  CHECK(core.read_register(12) == 3 && core.read_register(12) == 2);
  CHECK(core.read_register(12) == 1 && core.read_register(12) == 0);
  auto &state = core.state();
  state = {};
  state.control = 0;
  state.registers[19] = 0x4000;
  core.instruction_ram()[0] = 0x0e80;
  core.step();
  CHECK(state.registers[16] == 0xffff && state.registers[30] == 0xff80);
  CHECK(state.registers[28] == 0);
  state = {};
  state.control = 0;
  state.registers[16] = 0xff80;
  core.instruction_ram()[0] = 0x7c00;
  core.step();
  CHECK((state.registers[19] & 0xbf) == 0xba);
  // Unsigned 16-bit operands must not overflow a signed host intermediate.
  state = {};
  state.control = 0;
  state.registers[19] = 0x8000;
  state.registers[24] = state.registers[25] = 0xffff;
  core.instruction_ram()[0] = 0xa000;
  core.step();
  CHECK(state.registers[20] == 2 && state.registers[21] == 0xfffc);
  CHECK(state.registers[22] == 1 && state.registers[23] == 0);
  const std::array<u16, 9> nested{0x1102, 7, 0x1103, 5, 0x0008, 0x0009, 0x000a, 0x000b, 0x21};
  state = {};
  state.control = 0;
  std::fill_n(state.registers.begin() + 8, 4, u16(0xffff));
  std::copy(nested.begin(), nested.end(), core.instruction_ram().begin());
  core.code_changed();
  core.run(100);
  CHECK(state.control & 4);
  CHECK(state.registers[0] == 6 && state.registers[1] == 6);
  CHECK(state.registers[2] == 2 && state.registers[3] == 2);
  CHECK(state.registers[12] == 0 && state.registers[14] == 0 && state.registers[15] == 0);
  state.pc = 0x1234;
  state.registers[19] = 0xa00;
  core.external_interrupt();
  CHECK(core.check_exceptions());
  CHECK(state.pc == 14 && state.registers[12] == 0x1234 && state.registers[13] == 0xa00);
  CHECK((state.registers[19] & 0x800) == 0);
}

void accelerator_boundaries() {
  dsp::SampleAccelerator accelerator;
  std::array<u8, 4> bytes{0x12, 0x34, 0x56, 0x78};
  std::vector<unsigned> exceptions;
  accelerator.memory_read = [&](u32 address) { return bytes[address & 3]; };
  accelerator.memory_write = [&](u32 address, u8 value) { bytes[address & 3] = value; };
  accelerator.exception = [&](unsigned code) { exceptions.push_back(code); };
  accelerator.write(0xd1, 0x0a);
  accelerator.write(0xde, 2048);
  accelerator.write(0xd7, 1);
  accelerator.write(0xd9, 1);
  std::array<s16, 16> coefficients{};
  CHECK(accelerator.read_sample(coefficients) == 0x5678);
  CHECK(accelerator.state().stopped && accelerator.state().current == 0);
  CHECK(exceptions == std::vector<unsigned>{5});
  CHECK(accelerator.read_sample(coefficients) == 0 && exceptions.size() == 1);
  accelerator.write(0xdc, 0);
  CHECK(accelerator.read_sample(coefficients) == 0x1234);
  accelerator.write(0xd8, 0x8000);
  accelerator.write(0xd9, 1);
  accelerator.write_raw(0xabcd);
  CHECK(bytes[2] == 0xab && bytes[3] == 0xcd);
  CHECK(accelerator.state().current == 0x80000002 && exceptions.back() == 4);
  accelerator.write(0xd4, 0xffff);
  accelerator.write(0xd6, 0xffff);
  accelerator.write(0xd8, 0xffff);
  accelerator.write(0xda, 0xffff);
  CHECK(accelerator.state().start == 0x3fff0000);
  CHECK(accelerator.state().end == 0x3fff0001);
  CHECK(accelerator.state().current == 0xbfff0002);
  CHECK(accelerator.state().predictor == 127);
}

void hardware_mail_and_control(Console console) {
  Memory memory(console);
  Firmware firmware{0x16fc, 0x1234, 0x16fd, 0x5678, 0x16fb, 1, 0x21};
  dsp::InstructionEngine engine(memory, console, firmware.program, firmware.coefficients);
  unsigned interrupts = 0;
  engine.interrupt = [&] { ++interrupts; };
  CHECK(engine.read16(4) == 0);
  engine.write16(0x0a, 0x800);
  CHECK(engine.read16(4) == 0x9234);
  CHECK(engine.read16(6) == 0x5678);
  CHECK(engine.read16(4) == 0x1234);
  CHECK(interrupts == 1 && engine.core().state().instructions == 4);
  engine.write16(0, 0xabcd);
  engine.write16(2, 0x9876);
  CHECK(engine.core().read_data(0xfffe) == 0xabcd);
  CHECK(engine.core().read_data(0xffff) == 0x9876);
  CHECK(engine.read16(0) == 0x2bcd);
  CHECK_THROWS(engine.write16(4, 0));
  CHECK_THROWS(engine.read16(8));
  u64 timebase = 500;
  engine.guest_timebase = [&] { return timebase; };
  const auto init = memory.range(0x01000000, 0x1000);
  std::fill(init.begin(), init.end(), 0);
  init[1] = 0x21;
  engine.write16(0x0a, 0);
  CHECK(engine.core().state().pc == 0 && (engine.read16(0x0a) & 0x400));
  timebase = 629;
  CHECK(engine.read16(0x0a) & 0x400);
  timebase = 630;
  CHECK((engine.read16(0x0a) & 0x400) == 0);
  auto &state = engine.core().state();
  state.registers[0] = 0xabcd;
  state.registers[8] = 0;
  engine.write16(0x0a, 0x801);
  CHECK(state.pc == 0x8000 && state.registers[0] == 0xabcd && state.registers[8] == 0xffff);
  state.pc = 0x8004;
  state.registers[19] = 0xa00;
  const u64 instructions = state.instructions;
  engine.write16(0x0a, 0x802);
  CHECK(state.pc == 14 && state.registers[12] == 0x8004 && state.registers[13] == 0xa00);
  CHECK(state.instructions == instructions && !(state.control & 2));
}

void hardware_dma(Console console) {
  Memory memory(console);
  Firmware firmware{0x21};
  dsp::InstructionEngine engine(memory, console, firmware.program, firmware.coefficients);
  auto &core = engine.core();
  const auto write = [&](u8 reg, u16 value) { core.write_data(u16(0xff00 | reg), value); };
  const std::array<u8, 7> input{1, 2, 3, 4, 5, 6, 7};
  memory.copy_in(0x1000, input);
  write(0xcf, 0x1000);
  write(0xcd, 1);
  write(0xc9, 0);
  write(0xcb, 5);
  CHECK(core.data_ram()[1] == 0x0102 && core.data_ram()[2] == 0x0304);
  CHECK(core.data_ram()[3] == 0);
  CHECK(core.read_data(0xffcb) == 0 && core.read_data(0xffc9) == 0);
  core.data_ram()[1] = 0xabcd;
  core.data_ram()[2] = 0xef01;
  write(0xc9, 1);
  write(0xcb, 5);
  CHECK(memory.read32(0x1000) == 0xabcdef01 && memory.read8(0x1004) == 5);
  write(0xef, 1);
  write(0xc9, 2);
  write(0xcb, 4);
  CHECK(core.instruction_ram()[1] == 0x21);
  write(0xef, 0);
  memory.write32(0x1000, 0x00080021);
  write(0xcd, 0x8000);
  write(0xcb, 4);
  CHECK(core.instruction_ram()[0] == 8 && core.instruction_ram()[1] == 0x21);
  CHECK(engine.program_hash() == dsp::firmware_hash(memory.range(0x1000, 4)));
  memory.write32(0x1000, 0x5a5a5a5a);
  write(0xc9, 3);
  write(0xcb, 4);
  CHECK(memory.read32(0x1000) == 0x5a5a5a5a);
  write(0xce, 0x200);
  write(0xcf, 0);
  write(0xc9, 0);
  write(0xcb, 4);
  CHECK(core.data_ram()[1] == 0xabcd && core.data_ram()[2] == 0xef01);
  write(0xc9, 2);
  CHECK_THROWS(write(0xcb, 4));
}

void slice_boundaries(Console console) {
  Memory memory(console);
  Firmware firmware{0x0008, 0x029f, 0x8000};
  dsp::InstructionEngine engine(memory, console, firmware.program, firmware.coefficients);
  engine.write16(0x0a, 0x800);
  CHECK(engine.event_free_cycles() == 12599);
  for (unsigned i = 0; i < 175; ++i)
    engine.read16(4);
  CHECK(engine.slice_budget() == 72);
  CHECK(engine.core().state().registers[0] == 1044);
  engine.advance(12599);
  CHECK(engine.event_free_cycles() == 0 && engine.core().state().registers[0] == 1044);
  engine.advance(3);
  CHECK(engine.core().state().registers[0] == 1050);
  CHECK(engine.slice_budget() == 12598 && engine.event_free_cycles() == 12597);
  engine.advance(12598);
  CHECK(engine.core().state().registers[0] == 2100);
  CHECK(engine.slice_budget() == 12604);
}

void runtime_instruction_dsp(Console console) {
  runtime::Machine machine(console);
  Firmware firmware{0x16fc, 0x1234, 0x16fd, 0x5678, 0x16fb, 1, 0x21};
  machine.install_dsp_roms(firmware.program, firmware.coefficients);
  std::ostringstream before;
  machine.write_state(before, "fixture");
  CHECK(before.str().find("\"dsp_backend\":\"instruction\"") != std::string::npos);
  CHECK(before.str().find("\"dsp_instructions\":0") != std::string::npos);

  machine.load_executable(runtime::self_test_dol());
  auto &state = machine.cpu().state();
  state.pc = 0x4000;
  state.msr = 0;
  state.spr[cpu::spr::hid0] = 0;
  const std::array<u32, 5> program{0x3c600c00, 0x38800900, 0xb083500a, 0xa0a35004, 0xa0c35006};
  for (unsigned i = 0; i < program.size(); ++i)
    machine.memory().write32(0x4000 + i * 4, program[i]);
  for (unsigned i = 0; i < program.size(); ++i)
    machine.step();
  CHECK(state.gpr[5] == 0x9234 && state.gpr[6] == 0x5678);
  CHECK(machine.devices().interrupt_causes(64) == 64);
  CHECK_THROWS(machine.install_dsp_roms(firmware.program, firmware.coefficients));
}
} // namespace

int main() {
  return run_tests([] {
    instruction_registers_and_loops();
    accelerator_boundaries();
    for (Console console : {Console::GameCube, Console::Wii}) {
      hardware_mail_and_control(console);
      hardware_dma(console);
      slice_boundaries(console);
      runtime_instruction_dsp(console);
    }
  });
}
