// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/command_processor.hpp"
#include "test_support.hpp"
#include <array>
#include <bit>
#include <vector>

namespace {
using namespace cupid;

void put(std::vector<u8> &bytes, u32 value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes.push_back(static_cast<u8>(value >> (24 - i * 8)));
}

void cp(gfx::CommandProcessor &graphics, u8 index, u32 value) {
  std::vector<u8> bytes{8, index};
  put(bytes, value);
  graphics.submit(bytes);
}

void bp(gfx::CommandProcessor &graphics, u8 index, u32 value) {
  std::vector<u8> bytes{0x61};
  put(bytes, (u32(index) << 24) | (value & 0xffffff));
  graphics.submit(bytes);
}

void fifo(gfx::CommandProcessor &graphics, unsigned offset, u32 value) {
  graphics.write(0x0c000000 + offset, value & 0xffff, 2);
  graphics.write(0x0c000002 + offset, value >> 16, 2);
}

void xf(gfx::CommandProcessor &graphics, u32 address, std::span<const u32> values) {
  CHECK(!values.empty() && values.size() <= 16);
  std::vector<u8> bytes{0x10};
  put(bytes, (static_cast<u32>(values.size() - 1) << 16) | address);
  for (u32 value : values)
    put(bytes, value);
  graphics.submit(bytes);
}

void xf_float(gfx::CommandProcessor &graphics, u32 address, std::initializer_list<float> values) {
  std::vector<u32> words;
  for (float value : values)
    words.push_back(std::bit_cast<u32>(value));
  xf(graphics, address, words);
}

struct Drawing {
  Memory memory{Console::GameCube};
  hw::Devices devices{Console::GameCube};
  gfx::CommandProcessor graphics{memory, devices, Console::GameCube};

  Drawing() {
    cp(graphics, 0x50, 1 << 9);
    cp(graphics, 0x70, 1 | (4 << 1));
    xf_float(graphics, 0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
    xf_float(graphics, 0x1020, {1, 0, 1, 0, 1, 0});
    xf(graphics, 0x1026, std::array<u32, 1>{1});
    xf_float(graphics, 0x101a, {8, 8, 1000, 350, 350, 1000});
    xf(graphics, 0x100c, std::array<u32, 1>{0x11223344});
    bp(graphics, 0, 1 << 4);
    bp(graphics, 0x20, (342 << 12) | 342);
    bp(graphics, 0x21, (357 << 12) | 357);
    bp(graphics, 0x59, 171 | (171 << 10));
    bp(graphics, 0x41, 24);
    bp(graphics, 0xf6, 4);
    bp(graphics, 0xf7, 14);
    bp(graphics, 0xf3, (7 << 16) | (7 << 19));
    bp(graphics, 0xc0, 0xfffa | (1 << 19));
    bp(graphics, 0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19));
  }

  std::vector<u8> triangle(float z = -0.5f) {
    std::vector<u8> bytes{0x90, 0, 3};
    for (auto p : {std::array<float, 2>{-1, -1}, {-0.5f, -1}, {-1, -0.5f}}) {
      put(bytes, std::bit_cast<u32>(p[0]));
      put(bytes, std::bit_cast<u32>(p[1]));
      put(bytes, std::bit_cast<u32>(z));
    }
    return bytes;
  }
};

void primitive_pipeline() {
  Drawing f;
  CHECK(f.graphics.color(0, 0) == 0xff);
  auto bytes = f.triangle();
  const auto commands = f.graphics.command_count();
  f.graphics.submit(std::span<const u8>(bytes).first(bytes.size() - 1));
  CHECK(f.graphics.command_count() == commands);
  CHECK(f.graphics.fragment_count() == 0);
  f.graphics.submit(std::span<const u8>(bytes).last(1));
  CHECK(f.graphics.fragment_count() == 6);
  CHECK(f.graphics.color(0, 0) == 0x112233ff);
  CHECK(f.graphics.color(2, 0) == 0x112233ff);
  CHECK(f.graphics.color(3, 0) == 0xff);
  bp(f.graphics, 0x43, 1);
  // Changing pixel format reinterprets the same 24 physical EFB bits.
  CHECK(f.graphics.color(0, 0) == 0x104920cf);
  f.graphics.submit(bytes);
  CHECK(f.graphics.color(0, 0) == 0x10203045);
}

void depth_and_alpha() {
  Drawing f;
  bp(f.graphics, 0x40, 16);
  bp(f.graphics, 0x49, 0);
  bp(f.graphics, 0x4a, (3 << 10) | 3);
  bp(f.graphics, 0x51, 1000);
  bp(f.graphics, 0x52, 1 << 11);
  bp(f.graphics, 0x40, 1 | (1 << 1) | 16);
  f.graphics.submit(f.triangle());
  CHECK(f.graphics.depth(0, 0) == 500);
  const auto fragments = f.graphics.fragment_count();
  xf(f.graphics, 0x100c, std::array<u32, 1>{0xff000080});
  f.graphics.submit(f.triangle(-0.25f));
  CHECK(f.graphics.fragment_count() == fragments);
  CHECK(f.graphics.color(0, 0) == 0x112233ff);
  // A rejected late alpha test leaves depth intact; an early depth test updates it.
  bp(f.graphics, 0xf3, 0);
  f.graphics.submit(f.triangle(-0.75f));
  CHECK(f.graphics.depth(0, 0) == 500);
  bp(f.graphics, 0x43, 64);
  f.graphics.submit(f.triangle(-0.75f));
  CHECK(f.graphics.depth(0, 0) == 250);
  bp(f.graphics, 0x40, 0);
  bp(f.graphics, 0x41, 0);
  bp(f.graphics, 0x51, 123);
  bp(f.graphics, 0x50, 0xffffff);
  bp(f.graphics, 0x52, 1 << 11);
  CHECK(f.graphics.depth(0, 0) == 250);
  CHECK(f.graphics.color(0, 0) == 0x112233ff);
}

void blending_and_uploads() {
  Drawing f;
  bp(f.graphics, 0x43, 1);
  f.graphics.submit(f.triangle());
  xf(f.graphics, 0x100c, std::array<u32, 1>{0x80402080});
  bp(f.graphics, 0x41, 1 | 24 | (4 << 8) | (5 << 5));
  f.graphics.submit(f.triangle());
  CHECK(f.graphics.color(0, 0) == 0x49302461);
  bp(f.graphics, 0x41, 2 | 24 | (6 << 12));
  f.graphics.submit(f.triangle());
  CHECK(f.graphics.color(0, 0) == 0xcb7104e3);
  bp(f.graphics, 0x42, 256 | 17);
  bp(f.graphics, 0x41, 16);
  f.graphics.submit(f.triangle());
  CHECK(f.graphics.color(0, 0) == 0xcb710410);
  // A real TLUT transfer supplies the palette used by the rasterized C4 texture.
  f.memory.write8(0x2000, 0x10);
  f.memory.write16(0x3002, 0xf800);
  bp(f.graphics, 0x64, 0x04003000 >> 5);
  bp(f.graphics, 0x65, 1 << 10);
  bp(f.graphics, 0x88, 8 << 20);
  bp(f.graphics, 0x94, 0x2000 >> 5);
  bp(f.graphics, 0x98, 1 << 10);
  bp(f.graphics, 0, 1 | (1 << 4));
  xf(f.graphics, 0x103f, std::array<u32, 1>{1});
  xf(f.graphics, 0x1040, std::array<u32, 1>{2 << 4});
  bp(f.graphics, 0x28, 64);
  bp(f.graphics, 0xc0, 0xfff8 | (1 << 19));
  bp(f.graphics, 0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19));
  bp(f.graphics, 0x42, 0);
  bp(f.graphics, 0x41, 24);
  f.graphics.submit(f.triangle());
  CHECK(f.graphics.color(0, 0) == 0xff0000ff);
  // Manual RGBA8 preloads split AR and GB into their selected TMEM banks.
  f.memory.write16(0x4000, 0x1234);
  f.memory.write16(0x4020, 0x5678);
  bp(f.graphics, 0x60, 0x4000 >> 5);
  bp(f.graphics, 0x61, 0);
  bp(f.graphics, 0x62, 0x10000 >> 5);
  bp(f.graphics, 0x63, (3 << 15) | 1);
  bp(f.graphics, 0x88, 6 << 20);
  bp(f.graphics, 0x8c, 1 << 21);
  bp(f.graphics, 0x90, 0x10000 >> 5);
  bp(f.graphics, 0x43, 0);
  f.graphics.submit(f.triangle());
  CHECK(f.graphics.color(0, 0) == 0x345678ff);
  bp(f.graphics, 0x63, 0);
}

void uploads() {
  Memory memory(Console::GameCube);
  hw::Devices devices(Console::GameCube);
  gfx::CommandProcessor graphics(memory, devices, Console::GameCube);
  const std::array<u8, 6> command{8, 0x53, 0x12, 0x34, 0x56, 0x78};
  graphics.submit(std::span<const u8>(command).first(5));
  CHECK(graphics.cp_state(0x50) == 0);
  graphics.submit(std::span<const u8>(command).last(1));
  CHECK(graphics.cp_state(0x50) == 0x12345678);
  cp(graphics, 0x79, 0xabc);
  CHECK(graphics.cp_state(0x71) == 0xabc);
  cp(graphics, 0xa1, 0xffffffff);
  CHECK(graphics.cp_state(0xa1) == 0x03ffffff);
  cp(graphics, 0xb1, 0xabcdef12);
  CHECK(graphics.cp_state(0xb1) == 0x12);
  bp(graphics, 0x30, 0xabcdef);
  bp(graphics, 0xfe, 0xff);
  bp(graphics, 0x30, 0x123456);
  CHECK(graphics.bp_state(0x30) == 0xabcd56);
  CHECK(graphics.bp_state(0xfe) == 0xffffff);
  bp(graphics, 0x30, 0x123456);
  CHECK(graphics.bp_state(0x30) == 0x123456);
  std::vector<u8> transform{0x10};
  put(transform, 0x00020004);
  put(transform, 0x11223344);
  put(transform, 0x55667788);
  put(transform, 0x99aabbcc);
  graphics.submit(std::span<const u8>(transform).first(10));
  CHECK(graphics.xf_state(4) == 0);
  graphics.submit(std::span<const u8>(transform).subspan(10));
  CHECK(graphics.xf_state(6) == 0x99aabbcc);
  cp(graphics, 0xac, 0x2000);
  cp(graphics, 0xbc, 12);
  memory.write32(0x2018, 0xdeadbeef);
  memory.write32(0x201c, 0x01020304);
  memory.write32(0x2020, 0x87654321);
  std::vector<u8> indexed{0x20};
  put(indexed, 0x00022003);
  graphics.submit(indexed);
  CHECK(graphics.xf_state(3) == 0xdeadbeef);
  CHECK(graphics.xf_state(5) == 0x87654321);
  std::vector<u8> list{8, 0x30};
  put(list, 0x12345678);
  list.resize(32);
  memory.copy_in(0x3000, list);
  std::vector<u8> call{0x40};
  put(call, 0x3007);
  put(call, 63);
  graphics.submit(call);
  CHECK(graphics.cp_state(0x30) == 0x12345678);
  CHECK_THROWS(graphics.write(0x0c000020, 0, 1));
  CHECK_THROWS(graphics.read(0x0c001001, 2));
  const std::array<u8, 3> primitive{0x90, 0, 3};
  const auto commands = graphics.command_count();
  graphics.submit(primitive);
  CHECK(graphics.command_count() == commands);
}

void display_list_aliases(Console console) {
  Memory memory(console);
  hw::Devices devices(console);
  for (u32 physical : {0x00f863a0u, Memory::mem1_size - 32, 0x10020000u}) {
    if (console == Console::GameCube && physical >= 0x10000000)
      continue;
    for (u32 alias : {0u, 0x40000000u, 0x80000000u, 0xc0000000u}) {
      gfx::CommandProcessor graphics(memory, devices, console);
      std::vector<u8> list{0x61};
      put(list, 0x22123456);
      list.resize(32);
      memory.copy_in(physical, list);
      std::vector<u8> call{0x40};
      put(call, (physical | alias) + 7);
      put(call, 63);
      graphics.submit(std::span<const u8>(call).first(6));
      CHECK(graphics.command_count() == 0 && graphics.bp_state(0x22) == 0);
      graphics.submit(std::span<const u8>(call).subspan(6));
      CHECK(graphics.bp_state(0x22) == 0x123456 && graphics.command_count() == 29);
      std::vector<u8> after(32);
      memory.copy_out(physical, after);
      CHECK(after == list);
    }
  }
  for (u32 address : {0x8c008000u, 0xe0000000u, 0x81800000u, 0xd4000000u}) {
    gfx::CommandProcessor graphics(memory, devices, console);
    std::vector<u8> call{0x40};
    put(call, address);
    put(call, 32);
    CHECK_THROWS(graphics.submit(call));
    CHECK(graphics.command_count() == 0 && graphics.bp_state(0x22) == 0);
  }
  gfx::CommandProcessor graphics(memory, devices, console);
  std::vector<u8> oversized{0x40};
  put(oversized, 0x80000000 | (Memory::mem1_size - 32));
  put(oversized, 64);
  CHECK_THROWS(graphics.submit(oversized));
  CHECK(graphics.command_count() == 0);
}

void ring() {
  Memory memory(Console::GameCube);
  hw::Devices devices(Console::GameCube);
  gfx::CommandProcessor graphics(memory, devices, Console::GameCube);
  devices.write(0x0c00300c, 0x1000, 4);
  devices.write(0x0c003010, 0x1040, 4);
  devices.write(0x0c003014, 0x1020, 4);
  fifo(graphics, 0x20, 0x1000);
  fifo(graphics, 0x24, 0x1040);
  fifo(graphics, 0x34, 0x1020);
  fifo(graphics, 0x38, 0x1020);
  graphics.write(0x0c000002, 0x11, 2);
  for (unsigned i = 0; i < 31; ++i)
    graphics.gather(0);
  CHECK(devices.read(0x0c003014, 4) == 0x1020);
  CHECK(graphics.command_count() == 0);
  graphics.gather(0);
  CHECK(devices.read(0x0c003014, 4) == 0x1040);
  CHECK(graphics.command_count() == 32);
  CHECK(graphics.read(0x0c000030, 2) == 0);
  for (unsigned i = 0; i < 32; ++i)
    graphics.gather(0);
  CHECK(devices.read(0x0c003014, 4) == 0x1000);
  CHECK(graphics.command_count() == 64);
  CHECK((graphics.read(0x0c000000, 2) & 12) == 12);
  graphics.reset_fifo();
  CHECK(graphics.read(0x0c000002, 2) == 0x10);
  CHECK(graphics.read(0x0c00002c, 2) == 0);
}

void interrupts() {
  Memory memory(Console::GameCube);
  hw::Devices devices(Console::GameCube);
  gfx::CommandProcessor graphics(memory, devices, Console::GameCube);
  devices.write(0x0c003004, 0x600, 4);
  graphics.write(0x0c00100a, 3, 2);
  bp(graphics, 0x48, 0x1234);
  bp(graphics, 0x47, 0xabcd);
  bp(graphics, 0x45, 2);
  graphics.advance(499);
  CHECK(!devices.external_interrupt_pending());
  CHECK(graphics.read(0x0c00100e, 2) == 0);
  graphics.advance(1);
  CHECK(devices.external_interrupt_pending());
  CHECK(graphics.read(0x0c00100e, 2) == 0xabcd);
  CHECK((devices.read(0x0c003000, 4) & 0x600) == 0x600);
  graphics.write(0x0c00100a, 15, 2);
  CHECK(!devices.external_interrupt_pending());
  bp(graphics, 0x47, 0x3333);
  graphics.advance(500);
  CHECK(!devices.external_interrupt_pending());
  CHECK(graphics.read(0x0c00100e, 2) == 0x3333);
}

void interrupt_reassertion(Console console) {
  Memory memory(console);
  hw::Devices devices(console);
  gfx::CommandProcessor graphics(memory, devices, console);
  devices.write(0x0c003004, 0xe00, 4);
  devices.set_interrupt(0xe20, true);
  graphics.advance(0);
  CHECK((devices.read(0x0c003000, 4) & 0xe20) == 0x20);

  fifo(graphics, 0x2c, 32);
  graphics.write(0x0c000002, 9, 2);
  CHECK((devices.read(0x0c003000, 4) & 0xe20) == 0x820);
  devices.write(0x0c003000, 0x800, 4);
  CHECK(!devices.external_interrupt_pending());
  graphics.advance(0);
  CHECK(devices.external_interrupt_pending());

  graphics.write(0x0c00100a, 3, 2);
  bp(graphics, 0x48, 0x4321);
  bp(graphics, 0x45, 2);
  for (unsigned i = 0; i < 499; ++i)
    graphics.advance(1);
  CHECK((devices.read(0x0c003000, 4) & 0xe00) == 0x800);
  graphics.advance(1);
  CHECK((devices.read(0x0c003000, 4) & 0xe20) == 0xe20);
  CHECK(graphics.read(0x0c00100e, 2) == 0x4321);
  devices.write(0x0c003000, 0xe00, 4);
  graphics.advance(1);
  CHECK((devices.read(0x0c003000, 4) & 0xe20) == 0xe20);
  graphics.write(0x0c00100a, 15, 2);
  graphics.write(0x0c000002, 0, 2);
  devices.set_interrupt(0xe00, true);
  graphics.advance(0);
  CHECK((devices.read(0x0c003000, 4) & 0xe20) == 0x20);

  // A disabled FIFO resumes as soon as the read-enable register is written.
  fifo(graphics, 0x20, 0x1000);
  fifo(graphics, 0x24, 0x1040);
  fifo(graphics, 0x38, 0x1000);
  fifo(graphics, 0x30, 32);
  const auto commands = graphics.command_count();
  graphics.advance(1000);
  CHECK(graphics.command_count() == commands);
  graphics.write(0x0c000002, 1, 2);
  CHECK(graphics.command_count() == commands + 32);
  CHECK(graphics.read(0x0c000030, 2) == 0);
}

void copies() {
  Memory memory(Console::GameCube);
  hw::Devices devices(Console::GameCube);
  gfx::CommandProcessor graphics(memory, devices, Console::GameCube);
  bp(graphics, 0x40, 16);
  bp(graphics, 0x41, 24);
  bp(graphics, 0x49, 0);
  bp(graphics, 0x4a, (1 << 10) | 1);
  bp(graphics, 0x4f, 0xffff);
  bp(graphics, 0x50, 0);
  bp(graphics, 0x51, 0xabcdef);
  bp(graphics, 0x52, 1 << 11);
  CHECK(graphics.color(0, 0) == 0xff0000ff);
  CHECK(graphics.depth(1, 1) == 0xabcdef);
  CHECK(graphics.color(2, 0) == 0xff);
  CHECK_THROWS(graphics.color(640, 0));
  bp(graphics, 0x4b, 0x2000 >> 5);
  bp(graphics, 0x4d, 1);
  bp(graphics, 0x4e, 256);
  bp(graphics, 0x53, (63 << 18) | (1 << 12));
  bp(graphics, 0x54, 0);
  // Staging readback rounds to sixteen pixels; use a fully encoded row.
  bp(graphics, 0x4a, (1 << 10) | 15);
  bp(graphics, 0x52, (1 << 14) | 3);
  CHECK(memory.read32(0x2000) == 0x525a52f0);
  CHECK(memory.read32(0x2020) == 0x525a52f0);
  bp(graphics, 0x43, 1);
  bp(graphics, 0x4d, 0);
  bp(graphics, 0x4f, 0x7812);
  bp(graphics, 0x50, 0x3456);
  bp(graphics, 0x52, 1 << 11);
  CHECK(graphics.color(0, 0) == 0x10345579);
  bp(graphics, 0x64, 1);
  // A narrower source with the same stride would expose unwritten staging bytes.
  bp(graphics, 0x4a, (1 << 10) | 1);
  bp(graphics, 0x4d, 1);
  memory.write32(0x2000, 0xdeadbeef);
  CHECK_THROWS(bp(graphics, 0x52, (1 << 14) | 3));
  CHECK(memory.read32(0x2000) == 0xdeadbeef);
  CHECK(graphics.color(0, 0) == 0x10345579);
}
} // namespace

int main() {
  return run_tests([] {
    uploads();
    display_list_aliases(Console::GameCube);
    display_list_aliases(Console::Wii);
    ring();
    interrupts();
    interrupt_reassertion(Console::GameCube);
    interrupt_reassertion(Console::Wii);
    copies();
    primitive_pipeline();
    depth_and_alpha();
    blending_and_uploads();
  });
}
