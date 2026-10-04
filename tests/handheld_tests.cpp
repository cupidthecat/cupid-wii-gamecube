// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/memory.hpp"
#include "cupid/cpu/interpreter.hpp"
#include "cupid/dsp/engine.hpp"
#include "cupid/dsp/handheld.hpp"
#include "cupid/dsp/handheld_task.hpp"
#include "cupid/hw/devices.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>

using namespace cupid;

namespace {
struct Vector {
  u32 challenge, palette, speed, length, key, authentication;
};

constexpr std::array<Vector, 4> vectors{
    {{0, 0, 0, 0, 0x6f646573, 0x9187e1cb},
     {0x12345678, 3, 2, 0x2000, 0x7d50330b, 0x96d6e2b3},
     {0xdeadbeef, 6, 0xffffffff, 0x20808, 0xb1c9db9c, 0xc890e08a},
     {0, 0xffffffff, 0x80, 0x800001f8, 0x6f646573, 0x90919a8c}}};

void calculations() {
  for (const auto &vector : vectors) {
    const auto result =
        dsp::joyboot_authentication(vector.challenge, vector.palette, vector.speed, vector.length);
    CHECK(result[0] == vector.key && result[1] == vector.authentication);
  }
}

void memory_requests(Console console) {
  Memory memory(console);
  for (const auto &vector : vectors)
    for (const u32 alias : {0u, 0x80000000u, 0xc0000000u}) {
      auto bytes = memory.range(0x10000, 128);
      std::fill(bytes.begin(), bytes.end(), u8(0xa5));
      for (unsigned index = 0; index < 4; ++index)
        memory.write8(0x10000 + index, u8(vector.challenge >> (index * 8)));
      memory.write32(0x10004, vector.palette);
      memory.write32(0x10008, vector.speed);
      memory.write32(0x1000c, vector.length);
      memory.write32(0x10010, alias | 0x10040);
      std::array<u8, 128> expected;
      std::copy(bytes.begin(), bytes.end(), expected.begin());
      for (unsigned index = 0; index < 8; ++index) {
        const u32 word = index < 4 ? vector.key : vector.authentication;
        expected[64 + index] = u8(word >> (24 - 8 * (index % 4)));
      }
      dsp::process_joyboot_request(memory, alias | 0x10000);
      CHECK(std::equal(bytes.begin(), bytes.end(), expected.begin()));
    }
  auto last = memory.range(Memory::mem1_size - 16, 16);
  std::fill(last.begin(), last.end(), u8(0xa5));
  CHECK_THROWS(dsp::process_joyboot_request(memory, Memory::mem1_size - 16));
  CHECK(std::all_of(last.begin(), last.end(), [](u8 byte) { return byte == 0xa5; }));
  memory.write32(0x10010, Memory::mem1_size - 4);
  CHECK_THROWS(dsp::process_joyboot_request(memory, 0x10000));
  CHECK(std::all_of(last.begin(), last.end(), [](u8 byte) { return byte == 0xa5; }));
}

void send(dsp::Engine &engine, u32 mail) {
  engine.write16(0, u16(mail >> 16));
  engine.write16(2, u16(mail));
}

u32 receive(dsp::Engine &engine) {
  return (u32(engine.read16(4)) << 16) | engine.read16(6);
}

void load_profile(Memory &memory, dsp::Engine &engine, u32 hash) {
  // Authored fingerprint bytes isolate protocol dispatch; they contain no firmware.
  std::array<u8, 32> fingerprint{};
  for (unsigned bit = 0; bit < 32; ++bit)
    if (hash & (1u << bit)) {
      const unsigned distance = bit * 11 % 32;
      fingerprint[distance == 0 ? 0 : 32 - distance] ^= 1;
    }
  CHECK(dsp::firmware_hash(fingerprint) == hash);
  memory.copy_in(0x2000, fingerprint);
  engine.write16(0x0a, 0x800);
  CHECK(receive(engine) == 0x8071feed);
  for (const auto pair :
       {std::array<u32, 2>{0x80f3a001, 0x2000}, {0x80f3a002, 32}, {0x80f3d001, 0}}) {
    send(engine, pair[0]);
    send(engine, pair[1]);
  }
  CHECK(engine.program_hash() == hash && receive(engine) == 0x88881111);
}

void guest_command(Console console) {
  for (const u32 hash : {0xdf059f68u, 0x4be6a5cbu}) {
    Memory memory(console);
    dsp::Engine engine(memory, console);
    hw::Devices devices(console);
    load_profile(memory, engine, hash);
    memory.mmio_handles = [&](u32 address) { return devices.handles(address); };
    memory.mmio_read = [&](u32 address, unsigned width) { return devices.read(address, width); };
    memory.mmio_write = [&](u32 address, u32 value, unsigned width) {
      devices.write(address, value, width);
    };
    devices.dsp_read = [&](u32 offset) { return engine.read16(offset); };
    devices.dsp_write = [&](u32 offset, u16 value) { engine.write16(offset, value); };
    auto result = memory.range(0x11000, 16);
    std::fill(result.begin(), result.end(), u8(0xa5));
    const auto instruction = [](u32 op, u32 target, u32 base, u32 immediate) {
      return (op << 26) | (target << 21) | (base << 16) | (immediate & 0xffff);
    };
    const std::array<u32, 15> program{
        instruction(36, 3, 8, 0),      instruction(36, 4, 8, 4),  instruction(36, 5, 8, 8),
        instruction(36, 6, 8, 12),     instruction(36, 7, 8, 16), instruction(44, 9, 10, 0),
        instruction(14, 9, 0, 0x2468), instruction(44, 9, 10, 2), instruction(14, 9, 0, 0x8001),
        instruction(44, 9, 10, 0),     instruction(14, 9, 0, 0),  instruction(44, 9, 10, 2),
        instruction(32, 3, 11, 0),     instruction(32, 4, 11, 4), 0x4e800020};
    for (unsigned index = 0; index < program.size(); ++index)
      memory.write32(0x1000 + index * 4, program[index]);
    cpu::Interpreter cpu(memory, console);
    auto &state = cpu.state();
    state.pc = 0x1000;
    state.gpr[3] = 0x78563412;
    state.gpr[4] = 3;
    state.gpr[5] = 2;
    state.gpr[6] = 0x2000;
    state.gpr[7] = 0xc0011000;
    state.gpr[8] = 0x10000;
    state.gpr[9] = 0x8c37;
    state.gpr[10] = 0x0c005000;
    state.gpr[11] = 0x11000;
    for (unsigned index = 0; index < program.size(); ++index)
      cpu.step();
    CHECK(state.pc == 0 && state.gpr[3] == 0x7d50330b && state.gpr[4] == 0x96d6e2b3);
    CHECK(receive(engine) == 0x8000007a && (engine.read16(4) & 0x8000) == 0);
    CHECK(std::all_of(result.begin() + 8, result.end(), [](u8 byte) { return byte == 0xa5; }));
    send(engine, 0x8c370000);
    CHECK((engine.read16(4) & 0x8000) == 0);
    CHECK_THROWS(send(engine, Memory::mem1_size - 16));
  }
}

void unknown_variants() {
  for (const u32 hash : {0x24b22038u, 0x42f64ac4u}) {
    Memory memory(Console::GameCube);
    dsp::Engine engine(memory, Console::GameCube);
    load_profile(memory, engine, hash);
    CHECK_THROWS(send(engine, 0x8c370000));
  }
}
} // namespace

namespace {
std::array<u8, 32> authored_fingerprint(u32 hash) {
  std::array<u8, 32> bytes{};
  for (unsigned bit = 0; bit < 32; ++bit)
    if (hash & (1u << bit)) {
      const unsigned phase = bit * 11 % 32;
      bytes[phase == 0 ? 0 : 32 - phase] ^= 1;
    }
  CHECK(dsp::firmware_hash(bytes) == hash);
  return bytes;
}

void load_task(Memory &memory, dsp::Engine &engine) {
  constexpr u32 hash = 0xdd7e72d5;
  std::array<u8, 32> fingerprint{};
  for (unsigned bit = 0; bit < 32; ++bit)
    if (hash & (1u << bit)) {
      const unsigned phase = bit * 11 % 32;
      fingerprint[phase == 0 ? 0 : 32 - phase] ^= 1;
    }
  CHECK(dsp::firmware_hash(fingerprint) == hash);
  memory.copy_in(0x2000, fingerprint);
  engine.write16(0x0a, 0x800);
  CHECK(receive(engine) == 0x8071feed);
  for (const auto fields :
       {std::array<u32, 2>{0x80f3a001, 0x2000}, {0x80f3a002, 32}, {0x80f3d001, 0}}) {
    send(engine, fields[0]);
    send(engine, fields[1]);
  }
  CHECK(engine.program_hash() == hash && receive(engine) == 0xdcd10000);
}

void standalone_guest(Console console) {
  Memory memory(console);
  dsp::Engine engine(memory, console);
  hw::Devices devices(console);
  unsigned interrupts = 0;
  engine.interrupt = [&] { ++interrupts; };
  load_task(memory, engine);
  memory.mmio_handles = [&](u32 address) { return devices.handles(address); };
  memory.mmio_read = [&](u32 address, unsigned width) { return devices.read(address, width); };
  memory.mmio_write = [&](u32 address, u32 value, unsigned width) {
    devices.write(address, value, width);
  };
  devices.dsp_read = [&](u32 offset) { return engine.read16(offset); };
  devices.dsp_write = [&](u32 offset, u16 value) { engine.write16(offset, value); };
  auto output = memory.range(0x11000, 16);
  std::fill(output.begin(), output.end(), u8(0xa5));
  const auto insn = [](u32 op, u32 target, u32 base, u32 immediate) {
    return (op << 26) | (target << 21) | (base << 16) | (immediate & 0xffff);
  };
  const std::array<u32, 15> program{insn(36, 3, 8, 0),  insn(36, 4, 8, 4),  insn(36, 5, 8, 8),
                                    insn(36, 6, 8, 12), insn(36, 7, 8, 16), insn(44, 9, 10, 0),
                                    insn(14, 9, 0, 0),  insn(44, 9, 10, 2), insn(14, 9, 0, 0xc001),
                                    insn(44, 9, 10, 0), insn(14, 9, 0, 0),  insn(44, 9, 10, 2),
                                    insn(32, 3, 11, 0), insn(32, 4, 11, 4), 0x4e800020};
  for (unsigned index = 0; index < program.size(); ++index)
    memory.write32(0x1000 + index * 4, program[index]);
  cpu::Interpreter cpu(memory, console);
  auto &state = cpu.state();
  state.pc = 0x1000;
  state.gpr[3] = 0x78563412;
  state.gpr[4] = 3;
  state.gpr[5] = 2;
  state.gpr[6] = 0x2000;
  state.gpr[7] = 0x80011000;
  state.gpr[8] = 0x10000;
  state.gpr[9] = 0xabba;
  state.gpr[10] = 0x0c005000;
  state.gpr[11] = 0x11000;
  for (unsigned index = 0; index < program.size(); ++index)
    cpu.step();
  CHECK(state.pc == 0 && state.gpr[3] == 0x7d50330b && state.gpr[4] == 0x96d6e2b3);
  CHECK(std::all_of(output.begin() + 8, output.end(), [](u8 byte) { return byte == 0xa5; }));
  const u64 period = console == Console::Wii ? 729000 : 486000;
  CHECK(interrupts == 0);
  engine.advance(period - 1);
  CHECK(interrupts == 0);
  engine.advance(1);
  CHECK(interrupts == 1);
  engine.advance(period);
  CHECK(interrupts == 2);
  CHECK(receive(engine) == 0xdcd10003);
  engine.advance(period);
  CHECK(interrupts == 2);
  send(engine, 0x80000002);
  CHECK(engine.program_hash() == 0xdd7e72d5);
  send(engine, 0xcdd10002);
  CHECK(engine.program_hash() == 0 && receive(engine) == 0x8071feed);
}

void checked_task(Console console) {
  Memory memory(console);
  dsp::MailQueue queue;
  unsigned interrupts = 0;
  dsp::HandheldTask task(
      memory, [&](u32 value) { queue.push(value); }, [&] { return queue.pending(); },
      [&] { ++interrupts; });
  task.initialize();
  CHECK(queue.read_high() == 0xdcd1 && queue.read_low() == 0);
  task.update();
  CHECK(interrupts == 0);
  CHECK(!task.handle_mail(0xabba0000));
  auto guard = memory.range(Memory::mem1_size - 16, 16);
  std::fill(guard.begin(), guard.end(), u8(0xa5));
  CHECK_THROWS(task.handle_mail(Memory::mem1_size - 16));
  CHECK(!queue.pending() &&
        std::all_of(guard.begin(), guard.end(), [](u8 value) { return value == 0xa5; }));
  memory.write32(0x10010, 0x11000);
  CHECK(!task.handle_mail(0xf0010000));
  CHECK(queue.read_high() == 0xdcd1 && queue.read_low() == 3);
  CHECK(!task.handle_mail(0xcdd10001));
  const std::array<u32, 10> descriptor{0, 0, 0, 0x80020000, 32, 0x3456, 0x1234, 0, 0, 0};
  memory.copy_in(0x20000, std::array<u8, 32>{});
  std::optional<dsp::HandheldTask::Transition> action;
  for (unsigned index = 0; index < descriptor.size(); ++index) {
    action = task.handle_mail(descriptor[index]);
    CHECK(bool(action) == (index + 1 == descriptor.size()));
  }
  CHECK(action && action->hash == 0 && action->preserve && action->boot.source == 0x80020000 &&
        action->boot.length == 32 && action->boot.instruction_address == 0x3456 &&
        action->boot.entry == 0x1234);
  CHECK(!queue.pending());
  task.update();
  CHECK(interrupts == 0);
  CHECK(!task.handle_mail(0xcdd10001));
  auto invalid = descriptor;
  invalid[1] = 1;
  for (unsigned index = 0; index < 9; ++index)
    CHECK(!task.handle_mail(invalid[index]));
  CHECK_THROWS(task.handle_mail(invalid.back()));
}

void audio_resume(Console console) {
  Memory memory(console);
  dsp::Engine engine(memory, console);
  constexpr u32 audio_hash = 0x56d36052;
  memory.copy_in(0x2000, authored_fingerprint(audio_hash));
  memory.copy_in(0x2100, authored_fingerprint(0xdd7e72d5));
  engine.write16(0x0a, 0x800);
  CHECK(receive(engine) == 0x8071feed);
  for (const auto fields :
       {std::array<u32, 2>{0x80f3a001, 0x2000}, {0x80f3a002, 32}, {0x80f3d001, 0}}) {
    send(engine, fields[0]);
    send(engine, fields[1]);
  }
  CHECK(engine.program_hash() == audio_hash && receive(engine) == 0xdcd10000);
  CHECK(receive(engine) == 0xf3551111);
  send(engine, 0xcdd10001);
  for (u32 value : std::array<u32, 10>{0, 0, 0, 0x2100, 32, 0, 0, 0, 0, 0})
    send(engine, value);
  CHECK(engine.program_hash() == 0xdd7e72d5 && receive(engine) == 0xdcd10000);
  memory.write32(0x10010, 0x11000);
  send(engine, 0xabba0000);
  send(engine, 0x80010000);
  CHECK(receive(engine) == 0xdcd10003 && memory.read32(0x11000) == 0x6f646573);
  send(engine, 0xcdd10001);
  for (u32 value : std::array<u32, 10>{0, 0, 0, 0x2000, 32, 0, 0, 0, 0, 0})
    send(engine, value);
  CHECK(engine.program_hash() == audio_hash && !(engine.read16(4) & 0x8000));
  engine.advance(console == Console::Wii ? 729000 : 486000);
  CHECK(receive(engine) == 0xdcd10001 && !(engine.read16(4) & 0x8000));
}

void standalone_resume(Console console) {
  Memory memory(console);
  dsp::Engine engine(memory, console);
  unsigned interrupts = 0;
  engine.interrupt = [&] { ++interrupts; };
  load_task(memory, engine);
  memory.write32(0x10010, 0x11000);
  send(engine, 0xabba0000);
  send(engine, 0x10000);
  CHECK(receive(engine) == 0xdcd10003);
  memory.copy_in(0x3000, authored_fingerprint(0x56d36052));
  send(engine, 0xcdd10001);
  for (u32 value : std::array<u32, 10>{0, 0, 0, 0x3000, 32, 0, 0, 0, 0, 0})
    send(engine, value);
  CHECK(engine.program_hash() == 0x56d36052 && receive(engine) == 0xdcd10000);
  CHECK(receive(engine) == 0xf3551111);
  send(engine, 0xcdd10001);
  for (u32 value : std::array<u32, 10>{0, 0, 0, 0x2000, 32, 0, 0, 0, 0, 0})
    send(engine, value);
  CHECK(engine.program_hash() == 0xdd7e72d5 && !(engine.read16(4) & 0x8000));
  const unsigned previous_interrupts = interrupts;
  engine.advance(console == Console::Wii ? 729000 : 486000);
  CHECK(!(engine.read16(4) & 0x8000) && interrupts == previous_interrupts);
  auto destination = memory.range(0x11000, 8);
  std::fill(destination.begin(), destination.end(), u8(0xa5));
  send(engine, 0xabba0000);
  send(engine, 0x10000);
  CHECK(!(engine.read16(4) & 0x8000));
  CHECK(
      std::all_of(destination.begin(), destination.end(), [](u8 value) { return value == 0xa5; }));
  send(engine, 0xcdd10002);
  CHECK(engine.program_hash() == 0 && receive(engine) == 0x8071feed);
}
} // namespace

int main() {
  return run_tests([] {
    calculations();
    memory_requests(Console::GameCube);
    memory_requests(Console::Wii);
    guest_command(Console::GameCube);
    guest_command(Console::Wii);
    unknown_variants();
    standalone_guest(Console::GameCube);
    standalone_guest(Console::Wii);
    checked_task(Console::GameCube);
    checked_task(Console::Wii);
    audio_resume(Console::GameCube);
    audio_resume(Console::Wii);
    standalone_resume(Console::GameCube);
    standalone_resume(Console::Wii);
  });
}
