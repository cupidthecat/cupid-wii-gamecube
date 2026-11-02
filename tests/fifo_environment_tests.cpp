// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/memory.hpp"
#include "cupid/cpu/interpreter.hpp"
#include "cupid/gfx/command_processor.hpp"
#include "test_support.hpp"
#include <array>
#include <bit>
#include <cfenv>
#include <string>
#include <vector>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

using namespace cupid;

namespace {
void word(std::vector<u8> &bytes, u32 value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes.push_back(u8(value >> (24 - i * 8)));
}

void cp(gfx::CommandProcessor &graphics, u8 address, u32 value) {
  std::vector<u8> bytes{8, address};
  word(bytes, value);
  graphics.submit(bytes);
}

void bp(gfx::CommandProcessor &graphics, u8 address, u32 value) {
  std::vector<u8> bytes{0x61};
  word(bytes, (u32(address) << 24) | value);
  graphics.submit(bytes);
}

void xf(gfx::CommandProcessor &graphics, u32 address, std::initializer_list<float> values) {
  std::vector<u8> bytes{0x10};
  word(bytes, (u32(values.size() - 1) << 16) | address);
  for (float value : values)
    word(bytes, std::bit_cast<u32>(value));
  graphics.submit(bytes);
}

void xf_word(gfx::CommandProcessor &graphics, u32 address, u32 value) {
  std::vector<u8> bytes{0x10};
  word(bytes, address);
  word(bytes, value);
  graphics.submit(bytes);
}

void fifo(gfx::CommandProcessor &graphics, unsigned address, u32 value) {
  graphics.write(0x0c000000 + address, value & 65535, 2);
  graphics.write(0x0c000002 + address, value >> 16, 2);
}

struct Environment {
  int rounding, flags;
  unsigned simd;
  bool operator==(const Environment &) const = default;
};

unsigned simd_environment() {
#if defined(_M_X64) || defined(__x86_64__)
  return _mm_getcsr();
#else
  return 0;
#endif
}

void enable_flush_modes() {
#if defined(_M_X64) || defined(__x86_64__)
  _mm_setcsr(_mm_getcsr() | 0x8040u);
#endif
}

Environment environment() {
  return {std::fegetround(), std::fetestexcept(FE_ALL_EXCEPT), simd_environment()};
}

void preset_environment() {
  CHECK(std::fesetenv(FE_DFL_ENV) == 0);
  CHECK(std::fesetround(FE_DOWNWARD) == 0);
  CHECK(std::feraiseexcept(FE_DIVBYZERO | FE_INEXACT) == 0);
  enable_flush_modes();
}

void error_restoration(Console console, unsigned mode) {
  CHECK(std::fesetenv(FE_DFL_ENV) == 0);
  Memory memory(console);
  hw::Devices devices(console);
  gfx::CommandProcessor graphics(memory, devices, console);
  cpu::Interpreter interpreter(memory, console);
  devices.write(0x0c00300c, 0x1000, 4);
  devices.write(0x0c003010, 0x1fe0, 4);
  devices.write(0x0c003014, 0x1000, 4);
  fifo(graphics, 0x20, 0x1000);
  fifo(graphics, 0x24, 0x1fe0);
  fifo(graphics, 0x34, 0x1000);
  fifo(graphics, 0x38, 0x1000);
  graphics.write(0x0c000002, 0x11, 2);
  bool executing = false, caught_in_callback = false;
  memory.fifo_write = [&](u8 value) {
    const auto before = environment();
    try {
      graphics.gather(value);
    } catch (...) {
      if (executing) {
        caught_in_callback = true;
        CHECK(environment() == before);
      }
      throw;
    }
    if (executing)
      CHECK(environment() == before);
  };
  for (unsigned i = 0; i < 60; ++i)
    memory.write8(0x0c008000, i == 32 ? 0x7f : 0);
  auto &state = interpreter.state();
  state.pc = 0x8000;
  state.msr = cpu::msr::fp;
  state.spr[cpu::spr::hid2] = 0xa0000000;
  state.gpr[3] = 0x0c008000;
  state.fpr[1].bits = {0, 0};
  state.fpscr = mode;
  memory.write32(state.pc, (60u << 26) | (1u << 21) | (3u << 16) | 0x8000);
  preset_environment();
  const auto before = environment();
  executing = true;
  bool caught = false;
  try {
    interpreter.step();
  } catch (const std::runtime_error &error) {
    caught = std::string(error.what()).find("Unsupported GX command 0x7f") != std::string::npos;
  }
  CHECK(caught && caught_in_callback);
  CHECK(environment() == before && state.fpscr == mode);
  CHECK(std::fesetenv(FE_DFL_ENV) == 0);
}

void submission_restoration(Console console) {
  CHECK(std::fesetenv(FE_DFL_ENV) == 0);
  Memory memory(console);
  hw::Devices devices(console);
  gfx::CommandProcessor graphics(memory, devices, console);
  devices.write(0x0c00300c, 0x1000, 4);
  devices.write(0x0c003010, 0x1fe0, 4);
  devices.write(0x0c003014, 0x1000, 4);
  fifo(graphics, 0x20, 0x1000);
  fifo(graphics, 0x24, 0x1fe0);
  fifo(graphics, 0x34, 0x1000);
  fifo(graphics, 0x38, 0x1000);
  graphics.write(0x0c000002, 0x10, 2);
  preset_environment();
  const auto before = environment();
  // Empty and disabled-FIFO paths leave the caller untouched.
  graphics.submit({});
  CHECK(environment() == before);
  for (unsigned i = 0; i < 32; ++i)
    graphics.gather(0);
  CHECK(environment() == before && graphics.command_count() == 0);
  const std::array<u8, 1> nop{0};
  graphics.submit(nop);
  CHECK(environment() == before);
  bool caught = false;
  try {
    graphics.submit(std::array<u8, 1>{0x7f});
  } catch (const BusFault &error) {
    caught = error.unsupported_reason;
  }
  CHECK(caught && environment() == before);
  CHECK(std::fesetenv(FE_DFL_ENV) == 0);
}

struct Result {
  std::vector<int> callback_rounding;
  std::vector<u32> depths, colors;
  std::vector<u8> stream;
  u64 fragments = 0, instructions = 0;
  u32 pc = 0, fpscr = 0;
  int before = 0, after = 0;
};

Result run(Console console, unsigned mode, bool ordinary_store) {
  CHECK(std::fesetround(FE_TONEAREST) == 0);
  Memory memory(console);
  hw::Devices devices(console);
  gfx::CommandProcessor graphics(memory, devices, console);
  cpu::Interpreter interpreter(memory, console);
  cp(graphics, 0x50, 1 << 9);
  cp(graphics, 0x70, 1 | (4 << 1));
  xf(graphics, 0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
  xf(graphics, 0x1020, {1, 0, 1, 0, 1, 0});
  xf_word(graphics, 0x1026, 1);
  xf(graphics, 0x101a, {8, 8, 1000, 350, 350, 1000});
  xf_word(graphics, 0x100c, 0x11223344);
  bp(graphics, 0, 1 << 4);
  bp(graphics, 0x20, (342 << 12) | 342);
  bp(graphics, 0x21, (357 << 12) | 357);
  bp(graphics, 0x59, 171 | (171 << 10));
  bp(graphics, 0x40, 1 | (7 << 1) | 16);
  bp(graphics, 0x41, 24);
  bp(graphics, 0xf6, 4);
  bp(graphics, 0xf7, 14);
  bp(graphics, 0xf3, (7 << 16) | (7 << 19));
  bp(graphics, 0xc0, 0xfffa | (1 << 19));
  bp(graphics, 0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19));
  devices.write(0x0c00300c, 0x1000, 4);
  devices.write(0x0c003010, 0x1fe0, 4);
  devices.write(0x0c003014, 0x1000, 4);
  fifo(graphics, 0x20, 0x1000);
  fifo(graphics, 0x24, 0x1fe0);
  fifo(graphics, 0x34, 0x1000);
  fifo(graphics, 0x38, 0x1000);
  graphics.write(0x0c000002, 0x11, 2);
  Result result;
  bool executing = false;
  memory.fifo_write = [&](u8 byte) {
    if (executing)
      result.callback_rounding.push_back(std::fegetround());
    result.stream.push_back(byte);
    const auto before = environment();
    graphics.gather(byte);
    if (executing)
      CHECK(environment() == before);
  };
  // Complete the second gather burst with a guest store of four zero NOP bytes.
  // Vertex depth slopes cross integer boundaries if CPU rounding leaks into raster work.
  std::vector<u8> stream{0x90, 0, 3};
  constexpr std::array<std::array<u32, 3>, 3> vertices{{{0xbf800000, 0xbf800000, 0xbf000000},
                                                        {0xbf000000, 0xbf800000, 0xbf666666},
                                                        {0xbf800000, 0xbf000000, 0xbf666666}}};
  for (const auto &vertex : vertices)
    for (u32 value : vertex)
      word(stream, value);
  stream.resize(64);
  for (unsigned i = 0; i < 60; ++i)
    memory.write8(0x0c008000, stream[i]);
  CHECK(graphics.fragment_count() == 0);
  auto &state = interpreter.state();
  state.pc = 0x8000;
  state.msr = cpu::msr::fp;
  state.spr[cpu::spr::hid2] = 0xa0000000;
  state.gpr[3] = 0x0c008000;
  state.gpr[4] = 0;
  state.fpr[1].bits = {0, 0};
  state.fpscr = mode;
  // Both opcodes write identical bytes; only the paired store temporarily uses FPSCR.RN.
  const u32 opcode = ordinary_store ? ((36u << 26) | (4u << 21) | (3u << 16))
                                    : ((60u << 26) | (1u << 21) | (3u << 16) | 0x8000);
  memory.write32(state.pc, opcode);
  CHECK(std::fesetenv(FE_DFL_ENV) == 0);
  CHECK(std::feraiseexcept(FE_DIVBYZERO) == 0);
  enable_flush_modes();
  const auto caller_environment = environment();
  result.before = std::fegetround();
  executing = true;
  interpreter.step();
  executing = false;
  result.after = std::fegetround();
  CHECK(environment() == caller_environment);
  result.fragments = graphics.fragment_count();
  result.instructions = state.instructions;
  result.pc = state.pc;
  result.fpscr = state.fpscr;
  for (unsigned y = 0; y < 4; ++y)
    for (unsigned x = 0; x < 4; ++x) {
      result.depths.push_back(graphics.depth(x, y));
      result.colors.push_back(graphics.color(x, y));
    }
  CHECK(result.stream == stream && result.instructions == 1 && result.pc == 0x8004);
  CHECK(result.fpscr == mode);
  CHECK(result.callback_rounding.size() == 4 && result.before == FE_TONEAREST &&
        result.after == FE_TONEAREST);
  constexpr std::array<int, 4> host_modes{FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
  for (int observed : result.callback_rounding)
    CHECK(observed == (ordinary_store ? FE_TONEAREST : host_modes[mode]));
  return result;
}

} // namespace

int main() {
  return run_tests([] {
    for (Console console : {Console::GameCube, Console::Wii}) {
      submission_restoration(console);
      for (unsigned mode = 0; mode < 4; ++mode)
        error_restoration(console, mode);
      const auto baseline = run(console, 0, false);
      CHECK(baseline.fragments == 6);
      const auto nearest_control = run(console, 0, true);
      CHECK(nearest_control.stream == baseline.stream &&
            nearest_control.depths == baseline.depths &&
            nearest_control.colors == baseline.colors &&
            nearest_control.fragments == baseline.fragments);
      CHECK(baseline.depths[0] == 401 && baseline.depths[1] == 301 && baseline.depths[2] == 201);
      for (unsigned mode : {1u, 2u, 3u}) {
        const auto current = run(console, mode, false);
        CHECK(current.stream == baseline.stream);
        CHECK(current.fragments == baseline.fragments && current.colors == baseline.colors);
        CHECK(current.depths == baseline.depths);
        const auto control = run(console, mode, true);
        CHECK(control.stream == baseline.stream && control.fragments == baseline.fragments);
        CHECK(control.depths == baseline.depths && control.colors == baseline.colors);
      }
    }
    CHECK(std::fesetenv(FE_DFL_ENV) == 0);
  });
}
