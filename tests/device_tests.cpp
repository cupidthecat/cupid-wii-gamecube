// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/bus.hpp"
#include "cupid/core/memory.hpp"
#include "cupid/cpu/interpreter.hpp"
#include "cupid/hw/devices.hpp"
#include "cupid/hw/period.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace cupid;
using namespace cupid::hw;

namespace {
constexpr u32 pi = 0x0c003000, vi = 0x0c002000, di = 0x0c006000;
constexpr u32 si = 0x0c006400, exi = 0x0c006800, ai = 0x0c006c00, dsp = 0x0c005000;

void period_accounting() {
  const auto reference = [](u64 amount, u64 period, u64 &remainder) {
    const u64 whole = amount / period;
    const u64 tail = amount - whole * period;
    const u64 accumulated = remainder + tail;
    const u64 carry = accumulated / period;
    remainder = accumulated - carry * period;
    return whole + carry;
  };
  constexpr u64 maximum = std::numeric_limits<u64>::max();
  constexpr std::array<u64, 14> periods{1,     2,     3,     729,   4096,   10116,  15174,
                                        15187, 15444, 22781, 80928, 121392, 121500, 182250};
  constexpr std::array<u64, 14> amounts{0,           1,           2,           3,       4,
                                        31,          729,         10000,       1000000, 0xffffffff,
                                        0x100000000, maximum / 2, maximum - 1, maximum};
  for (u64 period : periods) {
    for (u64 initial : {u64(0), period - 1, period, period + 1, u64(182249)})
      for (u64 amount : amounts) {
        u64 expected = initial, observed = initial;
        const u64 count = reference(amount, period, expected);
        CHECK(hw::detail::elapsed_periods(amount, period, observed) == count);
        CHECK(observed == expected);
      }
    const std::array<u64, 7> pieces{0, 1, period - 1, 1, 2, period + 5, 10000000};
    u64 expected = period - 1, observed = expected, count = 0, total = 0;
    for (u64 piece : pieces) {
      count += hw::detail::elapsed_periods(piece, period, observed);
      reference(piece, period, expected);
      CHECK(observed == expected);
      total += piece;
    }
    u64 bulk = period - 1;
    CHECK(hw::detail::elapsed_periods(total, period, bulk) == count);
    CHECK(bulk == observed);
  }
  // Preserve phase across both longer and shorter rate/VI periods, including zero-cycle writes.
  u64 expected = 0, observed = 0, state = 73;
  for (unsigned step = 0; step < 10000; ++step) {
    state = state * 6364136223846793005ull + 1;
    const u64 period = periods[std::size_t(state % periods.size())];
    const u64 amount = step % 257 == 0 ? maximum : step % 19 == 0 ? 0 : state % 1000000;
    const u64 count = reference(amount, period, expected);
    CHECK(hw::detail::elapsed_periods(amount, period, observed) == count);
    CHECK(observed == expected);
  }
  Devices devices(Console::GameCube);
  devices.advance(1);
  CHECK_THROWS(devices.advance(maximum));
}

void processor_interface() {
  Devices devices(Console::GameCube);
  CHECK(devices.read(pi, 4) == 0x10100);
  CHECK(devices.read(pi, 2) == 1);
  CHECK(devices.read(pi + 2, 2) == 0x100);
  CHECK(!devices.external_interrupt_pending());
  devices.write(pi + 4, 0x100, 4);
  CHECK(devices.external_interrupt_pending());
  devices.write(pi, 0x100, 4);
  CHECK(!devices.external_interrupt_pending());
  CHECK(devices.read(pi, 4) == 0x10000);
  devices.set_interrupt(0x800, true);
  devices.write(pi + 4, 0x800, 4);
  CHECK(devices.external_interrupt_pending());
  devices.set_interrupt(0x800, false);
  CHECK(!devices.external_interrupt_pending());
  devices.write(pi + 0x0c, 0xffffffff, 4);
  CHECK(devices.read(pi + 0x0c, 4) == 0x03ffffe0);
  CHECK(devices.read(pi + 0x2c, 4) == 0x246500b1);
  CHECK_THROWS(devices.write(pi, 1, 2));
  CHECK_THROWS(devices.write(pi + 0x2c, 1, 4));
  CHECK_THROWS(devices.read(pi + 0x18, 4));
  CHECK_THROWS(devices.read(pi + 1, 2));
  try {
    devices.read(pi + 1, 2);
    CHECK(false);
  } catch (const BusFault &fault) {
    CHECK(!fault.unsupported_reason);
  }
  bool reset = false;
  devices.fifo_reset = [&] { reset = true; };
  devices.write(pi + 0x18, 1, 4);
  CHECK(reset);
  Devices wii(Console::Wii);
  wii.write(pi + 0x0c, 0xffffffff, 4);
  CHECK(wii.read(pi + 0x0c, 4) == 0x1fffffe0);
  CHECK_THROWS(wii.read(0x0d00302c, 4));
  CHECK_THROWS(wii.read(0x0d000000, 4));
  wii.ipc_read = [](u32 address) { return address; };
  CHECK(wii.read(0x0d000004, 4) == 0x0d000004);
}

void memory_interface() {
  Devices devices(Console::GameCube);
  constexpr u32 base = 0x0c004000;
  CHECK(devices.read(base, 4) == 0);
  devices.write(base, 0x12345678, 4);
  CHECK(devices.read(base, 2) == 0x1234);
  CHECK(devices.read(base + 2, 2) == 0x5678);
  devices.write(base + 0x1c, 0x001f, 2);
  devices.write(base + 0x1e, 0xffff, 2);
  CHECK(devices.read(base + 0x1c, 4) == 0x001fffff);
  devices.write(base + 0x32, 0xabcd, 2);
  devices.write(base + 0x34, 0xef01, 2);
  devices.advance(10000);
  CHECK(devices.read(base + 0x32, 2) == 0xabcd);
  CHECK(devices.read(base + 0x34, 2) == 0xef01);
  CHECK_THROWS(devices.read(base + 0x12, 2));
  CHECK_THROWS(devices.read(base, 1));
}

void video_interface() {
  Devices devices(Console::GameCube);
  const u64 halfline_cycles = 18 * 429;
  devices.write(vi + 0x30, 0x10020001, 4);
  devices.write(vi + 0x34, 0, 4);
  devices.write(pi + 4, 0x100, 4);
  devices.advance(halfline_cycles - 1);
  CHECK(!devices.external_interrupt_pending());
  devices.advance(halfline_cycles + 1);
  CHECK(devices.read(vi + 0x2c, 2) == 2);
  CHECK(devices.read(vi + 0x30, 4) == 0x90020001);
  CHECK(devices.external_interrupt_pending());
  devices.write(vi + 0x30, 0x1002, 2);
  CHECK(!devices.external_interrupt_pending());
  devices.write(vi + 0x30, 0x9002, 2);
  CHECK(devices.external_interrupt_pending());
  devices.write(vi + 2, 3, 2);
  CHECK(devices.read(vi + 2, 2) == 1);
  CHECK(devices.read(vi + 0x30, 4) == 0);
  CHECK(!devices.external_interrupt_pending());
  devices.write(vi + 0x1c, 0x10000123, 4);
  devices.write(vi + 0x24, 0x00000234, 4);
  CHECK(devices.xfb().address == 0x2460);
  CHECK(devices.xfb(1).address == 0x4680);
  devices.write(vi + 0x1c, 0x9000, 2);
  CHECK(devices.xfb().address == 0x123);
  CHECK(devices.xfb(1).address == 0x234);
  CHECK(devices.read(vi, 1) == 0);
  CHECK(devices.read(vi + 1, 1) == 6);
  CHECK_THROWS(devices.write(vi, 1, 1));

  Devices output(Console::GameCube);
  output.write(vi, 16, 2);
  output.write(vi + 0x0c, 0x10001, 4);
  output.write(vi + 0x10, 0x10001, 4);
  output.write(vi + 0x1c, 0x10000020, 4);
  output.write(vi + 0x24, 0x10000040, 4);
  unsigned presented = 0;
  output.xfb_present = [&](const Xfb &frame) {
    CHECK(frame.address == 0x400);
    CHECK(frame.width == 640);
    CHECK(frame.height == 1);
    CHECK(frame.stride_bytes == 1280);
    CHECK(frame.field == 0);
    ++presented;
  };
  output.advance(2 * halfline_cycles);
  CHECK(presented == 0);
  output.advance(halfline_cycles);
  CHECK(presented == 1);
  CHECK(output.field_count() == 1);
}

void video_aspect_ratio() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Devices devices(console);
    CHECK(devices.video_aspect_ratio() == 4.0f / 3.0f);
    CHECK(devices.xfb().aspect_ratio == 4.0f / 3.0f);
    const auto configure = [&](u32 active_lines, u32 half_width, u32 blank_start, u32 blank_end,
                               u32 pre_blank, u32 post_blank, u16 control) {
      devices.write(vi, (active_lines << 4) | 6, 2);
      devices.write(vi + 2, control, 2);
      devices.write(vi + 4, half_width, 4);
      devices.write(vi + 8, (blank_start << 17) | (blank_end << 7), 4);
      devices.write(vi + 0x10, (post_blank << 16) | pre_blank, 4);
    };
    configure(240, 429, 373, 162, 24, 3, 1);
    const float ntsc = (640.0f / 858.0f) * (63.555555f / 52.655555f) /
                       ((480.0f / 525.0f) * (525.0f / 486.0f)) * (4.0f / 3.0f);
    CHECK(std::abs(devices.video_aspect_ratio() - ntsc) < 0.000001f);
    CHECK(devices.xfb().aspect_ratio == devices.video_aspect_ratio());
    const float interlaced = devices.video_aspect_ratio();
    devices.write(vi + 2, 5, 2);
    CHECK(devices.video_aspect_ratio() == interlaced);
    devices.write(vi + 0x0c, 0x03ff03ff, 4);
    CHECK(devices.video_aspect_ratio() == interlaced);
    devices.write(vi + 0x6c, 0, 2);
    CHECK(std::abs(devices.video_aspect_ratio() - interlaced) < 0.000001f);
    configure(288, 432, 373, 165, 28, 3, 0x101);
    const float pal = (640.0f / 864.0f) * (64.0f / 52.0f) /
                      ((576.0f / 625.0f) * (625.0f / 576.0f)) * (4.0f / 3.0f);
    CHECK(std::abs(devices.video_aspect_ratio() - pal) < 0.000001f);
    configure(228, 429, 373, 162, 48, 3, 1);
    const float cropped = (640.0f / 858.0f) * (63.555555f / 52.655555f) /
                          ((456.0f / 525.0f) * (525.0f / 486.0f)) * (4.0f / 3.0f);
    CHECK(std::abs(devices.video_aspect_ratio() - cropped) < 0.000001f);
    configure(0, 429, 373, 162, 48, 3, 1);
    CHECK(devices.video_aspect_ratio() == 4.0f / 3.0f);
    configure(228, 0, 373, 162, 48, 3, 1);
    CHECK(devices.video_aspect_ratio() == 4.0f / 3.0f);
    configure(228, 100, 100, 200, 48, 3, 1);
    CHECK(devices.video_aspect_ratio() == 4.0f / 3.0f);
  }
}

void disc_interface() {
  Devices devices(Console::GameCube);
  std::array<u8, 128> memory{};
  devices.dma_write = [&](u32 address, std::span<const u8> data) {
    CHECK(address + data.size() <= memory.size());
    std::copy(data.begin(), data.end(), memory.begin() + address);
  };
  u64 offset = 0;
  devices.disc_read = [&](u64 position, std::span<u8> data) {
    offset = position;
    CHECK(data.size() == 32);
    for (unsigned i = 0; i < data.size(); ++i)
      data[i] = u8(i + 1);
  };
  devices.write(pi + 4, 4, 4);
  devices.write(di, 8, 4);
  devices.write(di + 8, 0xa8000000, 4);
  devices.write(di + 12, 7, 4);
  devices.write(di + 16, 32, 4);
  devices.write(di + 20, 0xf0000023, 4);
  CHECK(devices.read(di + 20, 4) == 32);
  devices.write(di + 24, 63, 4);
  CHECK(devices.read(di + 24, 4) == 32);
  devices.write(di + 28, 3, 4);
  CHECK(offset == 28);
  CHECK(memory[32] == 0);
  CHECK(devices.read(di + 28, 4) == 3);
  devices.advance(145799);
  CHECK(!devices.external_interrupt_pending());
  devices.advance(1);
  CHECK(devices.external_interrupt_pending());
  CHECK(devices.read(di + 28, 4) == 2);
  CHECK(devices.read(di + 20, 4) == 64);
  CHECK(devices.read(di + 24, 4) == 0);
  CHECK(memory[32] == 1 && memory[63] == 32);
  devices.write(di, 0x18, 4);
  CHECK(!devices.external_interrupt_pending());
  CHECK(devices.read(di, 4) == 8);

  devices.disc_read = [](u64, std::span<u8>) { throw std::out_of_range("Disc extent"); };
  devices.write(di, 2, 4);
  devices.write(di + 20, 32, 4);
  devices.write(di + 24, 32, 4);
  devices.write(di + 28, 3, 4);
  devices.advance(145800);
  CHECK(devices.read(di, 4) == 6);
  CHECK(devices.read(di + 20, 4) == 32);
  CHECK(devices.read(di + 24, 4) == 32);
  devices.write(di + 8, 0xe0000000, 4);
  devices.write(di + 28, 1, 4);
  CHECK(devices.read(di + 32, 4) == 0x52100);
  devices.advance(145800);
  devices.write(di + 28, 1, 4);
  CHECK(devices.read(di + 32, 4) == 0);
  devices.advance(145800);
  CHECK_THROWS(devices.write(di, 1, 4));
  CHECK_THROWS(devices.write(di + 36, 0, 4));
}

void serial_interface() {
  Devices devices(Console::GameCube);
  PadState pad;
  pad.connected = true;
  pad.buttons = 0x100;
  pad.stick_x = 0x12;
  pad.stick_y = 0x34;
  pad.cstick_x = 0x56;
  pad.cstick_y = 0x78;
  pad.left_trigger = 0x9a;
  pad.right_trigger = 0xbc;
  devices.set_pad(0, pad);
  devices.write(pi + 4, 8, 4);
  devices.write(si + 0x34, 0x08000000, 4);
  devices.poll_controllers();
  CHECK(devices.external_interrupt_pending());
  CHECK(devices.read(si + 0x34, 4) & 0x10000000);
  CHECK(devices.read(si + 4, 4) == 0x01801234);
  CHECK(!devices.external_interrupt_pending());
  CHECK(devices.read(si + 8, 4) == 0x56789abc);
  devices.write(si + 0x80, 0, 4);
  devices.write(si + 0x34, 0x40010301, 4);
  CHECK(devices.read(si + 0x80, 4) == 0x09000000);
  CHECK(devices.external_interrupt_pending());
  CHECK((devices.read(si + 0x34, 4) & 1) == 0);
  devices.write(si + 0x34, 0xc0000000, 4);
  CHECK(!devices.external_interrupt_pending());
  devices.write(si + 0x80, 0x4100, 2);
  devices.write(si + 0x34, 0x40010a01, 4);
  CHECK(devices.read(si + 0x80, 4) == 0x00008080);
  CHECK(devices.read(si + 0x84, 4) == 0x80800000);
  devices.write(si + 0x34, 0xc0010303, 4);
  CHECK(devices.read(si + 0x34, 4) & 0x20000000);
  CHECK(devices.read(si + 0x38, 4) & 0x80000);
  CHECK_THROWS(devices.set_pad(4, pad));
}

void audio_and_aram() {
  Devices devices(Console::GameCube);
  devices.write(pi + 4, 32, 4);
  devices.write(ai + 12, 3, 4);
  devices.write(ai, 7, 4);
  devices.advance(10116 * 2 + 10115);
  CHECK(devices.read(ai + 8, 4) == 2);
  CHECK(!devices.external_interrupt_pending());
  devices.advance(1);
  CHECK(devices.read(ai + 8, 4) == 3);
  CHECK(devices.external_interrupt_pending());
  devices.write(ai, 15, 4);
  CHECK(!devices.external_interrupt_pending());
  devices.write(ai, 0x27, 4);
  CHECK(devices.read(ai + 8, 4) == 0);
  CHECK((devices.read(ai, 4) & 0x20) == 0);
  devices.write(ai + 8, 0xfffffffe, 4);
  devices.write(ai + 12, 0, 4);
  devices.advance(10116 * 2);
  CHECK(devices.read(ai + 8, 4) == 0);
  CHECK(devices.external_interrupt_pending());

  std::array<u8, 256> memory{};
  for (unsigned i = 0; i < 32; ++i)
    memory[i] = u8(i);
  devices.dma_read = [&](u32 address, std::span<u8> out) {
    CHECK(address + out.size() <= memory.size());
    std::copy_n(memory.begin() + address, out.size(), out.begin());
  };
  devices.dma_write = [&](u32 address, std::span<const u8> in) {
    CHECK(address + in.size() <= memory.size());
    std::copy(in.begin(), in.end(), memory.begin() + address);
  };
  devices.write(pi + 4, 64, 4);
  devices.write(dsp + 10, 0xf044, 2);
  CHECK((devices.read(dsp + 10, 2) & 0xf000) == 0xf000);
  devices.write(dsp + 10, 0x44, 2);
  CHECK((devices.read(dsp + 10, 2) & 0xf000) == 0);
  devices.write(dsp + 0x20, 0, 4);
  devices.write(dsp + 0x24, 0, 4);
  devices.write(dsp + 0x28, 32, 4);
  CHECK(devices.read(dsp + 10, 2) & 0x200);
  devices.advance(245);
  CHECK(!devices.external_interrupt_pending());
  devices.advance(1);
  CHECK(devices.external_interrupt_pending());
  CHECK((devices.read(dsp + 10, 2) & 0x200) == 0);
  devices.write(dsp + 10, 0x64, 2);
  devices.write(dsp + 0x20, 64, 4);
  devices.write(dsp + 0x24, 0, 4);
  devices.write(dsp + 0x28, 0x80000020, 4);
  CHECK(std::equal(memory.begin(), memory.begin() + 32, memory.begin() + 64));
  std::array<u8, 32> copied_aram{};
  devices.read_aram(0, copied_aram);
  CHECK(std::equal(copied_aram.begin(), copied_aram.end(), memory.begin()));
  CHECK_THROWS(devices.read_aram(0x1000000, copied_aram));
  devices.advance(246);
  CHECK(devices.read(dsp + 0x20, 4) == 96);
  CHECK(devices.read(dsp + 0x28, 4) == 0x80000000);
  CHECK_THROWS(devices.read(dsp, 2));
  try {
    devices.read(dsp, 2);
    CHECK(false);
  } catch (const BusFault &fault) {
    CHECK(fault.unsupported_reason);
  }
  CHECK_THROWS(devices.write(dsp + 10, 0, 2));

  unsigned packets = 0;
  devices.audio_samples = [&](std::span<const s16> samples, u32 rate) {
    CHECK(samples.size() == 16);
    CHECK(samples[0] == 1 && samples[1] == 0x0203);
    CHECK(rate == 48042);
    ++packets;
  };
  devices.write(dsp + 0x30, 0, 4);
  devices.write(dsp + 0x36, 0x8001, 2);
  devices.advance(80928);
  CHECK(packets == 1);
  CHECK(devices.read(dsp + 0x3a, 2) == 0);
}

void disconnected_aram_live_descriptor() {
  Memory memory(Console::GameCube);
  Devices devices(Console::GameCube);
  memory.mmio_handles = [&](u32 address) { return devices.handles(address); };
  memory.mmio_read = [&](u32 address, unsigned width) { return devices.read(address, width); };
  memory.mmio_write = [&](u32 address, u32 value, unsigned width) {
    devices.write(address, value, width);
  };
  devices.dma_read = [&](u32 address, std::span<u8> out) { memory.copy_out(address, out); };
  unsigned memory_writes = 0;
  devices.dma_write = [&](u32 address, std::span<const u8> in) {
    memory.copy_in(address, in);
    ++memory_writes;
  };
  std::array<u8, 32> original{};
  for (unsigned i = 0; i < original.size(); ++i)
    original[i] = u8(i ^ 0x6b);
  memory.copy_in(0x00427600, original);
  // The captured halfword stores programmed this descriptor immediately before
  // the guest's expansion-port probe at instruction 407,051,717.
  devices.write(dsp + 0x20, 0x8042, 2);
  devices.write(dsp + 0x22, 0x7600, 2);
  devices.write(dsp + 0x24, 0x0100, 2);
  devices.write(dsp + 0x26, 0, 2);
  devices.write(dsp + 0x28, 0, 2);
  cpu::Interpreter interpreter(memory, Console::GameCube);
  interpreter.configure_boot();
  interpreter.state().pc = 0x803531f4;
  interpreter.state().gpr[0] = 32;
  interpreter.state().gpr[29] = 0xcc005000;
  memory.write32(0x003531f4, 0xb41d002a);
  interpreter.step();
  CHECK(interpreter.state().pc == 0x803531f8);
  CHECK(interpreter.state().gpr[29] == 0xcc00502a);
  CHECK(std::equal(original.begin(), original.end(), memory.range(0x00427600, 32).begin()));
  CHECK(memory_writes == 0);
  CHECK(devices.read(dsp + 0x20, 4) == 0x00427620);
  CHECK(devices.read(dsp + 0x24, 4) == 0x01000020);
  CHECK(devices.read(dsp + 0x28, 4) == 0);
  CHECK(devices.read(dsp + 10, 2) & 0x200);
  devices.advance(245);
  CHECK(devices.read(dsp + 10, 2) & 0x200);
  devices.advance(1);
  CHECK((devices.read(dsp + 10, 2) & 0x220) == 0x20);
}

void disconnected_aram_port() {
  Devices devices(Console::GameCube);
  std::array<u8, 256> memory{};
  for (unsigned i = 0; i < memory.size(); ++i)
    memory[i] = u8(i ^ 0xa5);
  unsigned memory_writes = 0;
  devices.dma_read = [&](u32 address, std::span<u8> out) {
    CHECK(u64(address) + out.size() <= memory.size());
    std::copy_n(memory.begin() + address, out.size(), out.begin());
  };
  devices.dma_write = [&](u32 address, std::span<const u8> in) {
    CHECK(u64(address) + in.size() <= memory.size());
    std::copy(in.begin(), in.end(), memory.begin() + address);
    ++memory_writes;
  };
  devices.write(dsp + 0x20, 0, 4);
  devices.write(dsp + 0x24, 0, 4);
  devices.write(dsp + 0x28, 32, 4);
  devices.advance(246);
  const auto original = memory;
  for (u32 address : {0x01000000u, 0x03000000u, 0x41000000u}) {
    for (u32 direction : {0u, 0x80000000u}) {
      devices.write(dsp + 0x20, 128, 4);
      devices.write(dsp + 0x24, address, 4);
      devices.write(dsp + 0x28, direction | 32, 4);
      CHECK(memory == original);
      CHECK(memory_writes == 0);
      CHECK(devices.read(dsp + 0x20, 4) == 160);
      CHECK(devices.read(dsp + 0x24, 4) == ((address & 0x3ffffff) + 32));
      CHECK(devices.read(dsp + 0x28, 4) == direction);
      CHECK(devices.read(dsp + 10, 2) & 0x200);
      devices.advance(245);
      CHECK(devices.read(dsp + 10, 2) & 0x200);
      devices.advance(1);
      CHECK((devices.read(dsp + 10, 2) & 0x220) == 0x20);
      std::array<u8, 32> built_in{};
      devices.read_aram(0, built_in);
      CHECK(std::equal(built_in.begin(), built_in.end(), original.begin()));
    }
  }
}

void wii_aram_dma() {
  Devices devices(Console::Wii);
  std::array<u8, 32> main{}, extended{};
  for (unsigned i = 0; i < main.size(); ++i)
    main[i] = u8(3 * i);
  devices.dma_read = [&](u32 address, std::span<u8> bytes) {
    CHECK(bytes.size() == 32);
    CHECK(address == 0x01000000 || address == 0x10000000);
    const auto &source = address == 0x01000000 ? main : extended;
    std::copy(source.begin(), source.end(), bytes.begin());
  };
  devices.dma_write = [&](u32 address, std::span<const u8> bytes) {
    CHECK(bytes.size() == 32);
    CHECK(address == 0x01000000 || address == 0x10000000);
    auto &destination = address == 0x01000000 ? main : extended;
    std::copy(bytes.begin(), bytes.end(), destination.begin());
  };
  devices.write(pi + 4, 64, 4);
  devices.write(dsp + 10, 0x44, 2);
  devices.write(dsp + 0x12, 0x43, 2);
  devices.write(dsp + 0x20, 0x01000000, 4);
  devices.write(dsp + 0x24, 0, 4);
  devices.write(dsp + 0x28, 32, 4);
  CHECK(main == extended);
  CHECK(devices.read(dsp + 0x20, 4) == 0x01000020);
  CHECK(devices.read(dsp + 0x24, 4) == 32);
  CHECK(devices.read(dsp + 0x28, 4) == 0);
  CHECK(devices.read(dsp + 10, 2) & 0x200);
  devices.advance(245);
  CHECK(!devices.external_interrupt_pending());
  devices.advance(1);
  CHECK((devices.read(dsp + 10, 2) & 0x200) == 0);
  CHECK(devices.external_interrupt_pending());
  devices.write(dsp + 10, 0x64, 2);
  std::fill(main.begin(), main.end(), u8(0));
  devices.write(dsp + 0x20, 0x81000000, 4);
  devices.write(dsp + 0x24, 0x84000000, 4);
  devices.write(dsp + 0x28, 0x80000020, 4);
  CHECK(main == extended);
  CHECK(devices.read(dsp + 0x20, 4) == 0x01000020);
  CHECK(devices.read(dsp + 0x24, 4) == 32);
  devices.advance(246);
}

void expansion_interface() {
  {
    Devices missing(Console::GameCube);
    missing.write(exi, 0x100, 4);
    missing.write(exi + 16, 0x12340 << 6, 4);
    missing.write(exi + 12, 0x35, 4);
    try {
      missing.write(exi + 12, 1, 4);
      CHECK(false);
    } catch (const BusFault &fault) {
      CHECK(fault.unsupported_reason);
      CHECK(std::string(fault.what()).find("requested byte address 0x12340") != std::string::npos);
    }
  }
  Devices devices(Console::GameCube);
  devices.set_rtc_seconds(0x12345678);
  devices.write(pi + 4, 16, 4);
  devices.write(exi, 0x104, 4);
  devices.write(exi + 16, 0x20000000, 4);
  devices.write(exi + 12, 0x35, 4);
  CHECK(devices.external_interrupt_pending());
  CHECK((devices.read(exi + 12, 4) & 1) == 0);
  devices.write(exi, 0x10c, 4);
  CHECK(!devices.external_interrupt_pending());
  devices.write(exi + 12, 0x31, 4);
  CHECK(devices.read(exi + 16, 4) == 0x12345678);
  devices.write(exi, 0, 4);
  devices.write(exi, 0x100, 4);
  devices.write(exi + 16, 0xa0000100, 4);
  devices.write(exi + 12, 0x35, 4);
  devices.write(exi + 16, 0xaabbccdd, 4);
  devices.write(exi + 12, 0x35, 4);
  CHECK(devices.sram()[4] == 0xaa && devices.sram()[7] == 0xdd);
  std::vector<u8> rom(0x200000);
  rom[0x200] = 0x5a;
  rom[0x201] = 0xa5;
  devices.set_ipl(rom);
  devices.write(exi, 0, 4);
  devices.write(exi, 0x100, 4);
  devices.write(exi + 16, 0x8000, 4);
  devices.write(exi + 12, 0x35, 4);
  devices.write(exi + 12, 0x11, 4);
  CHECK(devices.read(exi + 16, 4) == 0x5aa50000);
  CHECK_THROWS(devices.set_ipl(std::span<const u8>(rom).first(4)));
  CHECK_THROWS(devices.set_sram(std::span<const u8>(rom).first(4)));
  CHECK_THROWS(devices.write(exi + 12, 0xd, 4));
  auto command = [&](u32 address, bool writing) {
    devices.write(exi, 0, 4);
    devices.write(exi, 0x100, 4);
    devices.write(exi + 16, (address << 6) | (writing ? 0x80000000u : 0), 4);
    devices.write(exi + 12, 0x35, 4);
  };
  auto send = [&](u32 bytes, unsigned length) {
    devices.write(exi + 16, bytes, 4);
    devices.write(exi + 12, 5 | ((length - 1) << 4), 4);
  };
  std::string text;
  devices.guest_text = [&](std::string_view line) { text += line; };
  command(0x800400, true);
  send(0x4100420d, 4);
  CHECK(text == "AB\r");
  command(0x800400, false);
  devices.write(exi + 12, 0x31, 4);
  CHECK(devices.read(exi + 16, 4) == 0);
  command(0xc00000, false);
  devices.write(exi + 16, 0xf2f30000, 4);
  devices.write(exi + 12, 0x19, 4);
  CHECK(devices.read(exi + 16, 4) == 0xf2f30000);
  devices.write(exi + 12, 0x11, 4);
  CHECK(devices.read(exi + 16, 4) == 0);
  command(0xc00004, true);
  send(0x430d0000, 2);
  CHECK(text == "AB\rC\r");
  command(0x840020, true);
  send(0x03000000, 1);
  command(0x840020, false);
  devices.write(exi + 12, 0x31, 4);
  CHECK(devices.read(exi + 16, 4) == 0x03030303);
  command(0xa00000, false);
  try {
    devices.write(exi + 12, 1, 4);
    CHECK(false);
  } catch (const BusFault &fault) {
    CHECK(fault.unsupported_reason);
    CHECK(std::string(fault.what()).find("0xa00000") != std::string::npos);
  }

  Devices wii(Console::Wii);
  std::array<u8, 68> original_sram{};
  for (unsigned i = 4; i < original_sram.size(); ++i)
    original_sram[i] = u8(i);
  wii.set_sram(original_sram);
  std::array<u8, 64> transferred{};
  wii.dma_write = [&](u32 address, std::span<const u8> data) {
    CHECK(address == 0x00650820);
    CHECK(data.size() == transferred.size());
    std::copy(data.begin(), data.end(), transferred.begin());
  };
  wii.write(0x0d006800, 0x100, 4);
  wii.write(0x0d006810, 0x20000000, 4);
  wii.write(0x0d00680c, 0x35, 4);
  wii.write(0x0d006804, 0x80650820, 4);
  wii.write(0x0d006808, 0x40, 4);
  wii.write(0x0d00680c, 3, 4);
  CHECK(wii.read(0x0d006804, 4) == 0x80650820);
  CHECK(std::equal(transferred.begin(), transferred.end(), original_sram.begin()));
  wii.dma_write = [&](u32 address, std::span<const u8> data) {
    CHECK(address == 0x10000023);
    CHECK(data.size() == 3);
  };
  wii.write(0x0d006804, 0xd0000023, 4);
  wii.write(0x0d006808, 3, 4);
  wii.write(0x0d00680c, 3, 4);
}

void ipl_font_slots() {
  Devices devices(Console::GameCube);
  const std::array<u8, 4> jis{1, 2, 3, 4}, western{5, 6, 7, 8};
  devices.set_ipl_fonts(jis, western);
  CHECK_THROWS(devices.set_ipl_fonts({}, western));
  CHECK_THROWS(devices.set_ipl_fonts(jis, {}));
  CHECK_THROWS(devices.set_ipl_fonts(std::vector<u8>(0x4d001), western));
  CHECK_THROWS(devices.set_ipl_fonts(jis, std::vector<u8>(0x3101)));
  std::array<u8, 4> output{};
  devices.dma_write = [&](u32 address, std::span<const u8> bytes) {
    CHECK(address == 0x100 && bytes.size() == output.size());
    std::copy(bytes.begin(), bytes.end(), output.begin());
  };
  const auto read = [&](u32 address) {
    devices.write(exi, 0, 4);
    devices.write(exi, 0x100, 4);
    devices.write(exi + 16, address << 6, 4);
    devices.write(exi + 12, 0x35, 4);
    devices.write(exi + 4, 0x100, 4);
    devices.write(exi + 8, 4, 4);
    devices.write(exi + 12, 3, 4);
  };
  read(0x1aff00);
  CHECK(output == jis);
  read(0x1fcf00);
  CHECK(output == western);
  CHECK_THROWS(read(0x100));
  CHECK_THROWS(read(0x1aff04));
  std::vector<u8> rom(0x200000, u8(0xaa));
  devices.set_ipl(rom);
  read(0x100);
  CHECK((output == std::array<u8, 4>({0xaa, 0xaa, 0xaa, 0xaa})));
  read(0x1aff00);
  CHECK((output == std::array<u8, 4>({0xaa, 0xaa, 0xaa, 0xaa})));
}

void memory_cards() {
  const std::array<u8, 12> flash_id{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  auto image = Devices::format_memory_card(4, 0x11223344, flash_id);
  CHECK(image.size() == 524288);
  CHECK(image[34] == 0 && image[35] == 4);
  CHECK(image[0x3ffe] == 0 && image[0x3fff] == 0);
  CHECK(image[0x3ffc] == 0xf0 && image[0x3ffd] == 3);
  CHECK(image[0x6006] == 0 && image[0x6007] == 59);
  CHECK(image[0x6008] == 0 && image[0x6009] == 4);
  CHECK(std::all_of(image.begin() + 0xa000, image.end(), [](u8 byte) { return byte == 255; }));
  u16 sum = 0, inverse = 0;
  for (unsigned offset = 0; offset < 0x1fc; offset += 2) {
    const u16 value = u16((u16(image[offset]) << 8) | image[offset + 1]);
    sum = u16(sum + value);
    inverse = u16(inverse + (value ^ 0xffff));
  }
  if (sum == 65535)
    sum = 0;
  if (inverse == 65535)
    inverse = 0;
  CHECK(image[0x1fc] == (sum >> 8) && image[0x1fd] == u8(sum));
  CHECK(image[0x1fe] == (inverse >> 8) && image[0x1ff] == u8(inverse));
  CHECK_THROWS(Devices::format_memory_card(3, 0));

  Devices devices(Console::GameCube);
  devices.set_memory_card(0, image);
  CHECK(devices.read(exi, 4) & 0x1000);
  CHECK(std::equal(flash_id.begin(), flash_id.end(), devices.sram().begin() + 24));
  CHECK(devices.sram()[62] == (78 ^ 255));
  CHECK(!devices.memory_card_dirty(0));
  CHECK(devices.card_image(1).empty());
  devices.write(pi + 4, 16, 4);
  auto select = [&] { devices.write(exi, 0x85, 4); };
  auto deselect = [&] { devices.write(exi, 5, 4); };
  auto send = [&](u32 value, unsigned count) {
    devices.write(exi + 16, value, 4);
    devices.write(exi + 12, 5 | ((count - 1) << 4), 4);
  };
  auto receive = [&](unsigned count) {
    devices.write(exi + 12, 1 | ((count - 1) << 4), 4);
    return devices.read(exi + 16, 4);
  };
  select();
  send(0, 1);
  CHECK(receive(1) == 0x80000000);
  CHECK(receive(4) == 4);
  CHECK(!devices.external_interrupt_pending());
  deselect();
  select();
  send(0x81010000, 2);
  deselect();
  select();
  send(0xf2000100, 4);
  send(0, 1);
  send(0xaabbccdd, 4);
  CHECK(devices.card_image(0)[512] == 255);
  deselect();
  CHECK(devices.card_image(0)[512] == 0xaa && devices.card_image(0)[515] == 0xdd);
  CHECK(devices.memory_card_dirty(0));
  devices.advance(4999);
  CHECK(!devices.external_interrupt_pending());
  devices.advance(1);
  CHECK(devices.external_interrupt_pending());
  select();
  send(0x89000000, 1);
  deselect();
  devices.write(exi, 7, 4);
  CHECK(!devices.external_interrupt_pending());

  std::array<u8, 32> memory{};
  devices.dma_write = [&](u32 address, std::span<const u8> in) {
    CHECK(address == 0 && in.size() == 4);
    std::copy(in.begin(), in.end(), memory.begin());
  };
  select();
  send(0x52000100, 4);
  send(0, 1);
  devices.write(exi + 4, 0, 4);
  devices.write(exi + 8, 4, 4);
  devices.write(exi + 12, 3, 4);
  CHECK(memory[0] == 0xaa && memory[3] == 0xdd);
  CHECK((devices.read(exi, 4) & 8) == 0);
  devices.advance(4 * (486000000 / 524288) - 1);
  CHECK((devices.read(exi, 4) & 8) == 0);
  devices.advance(1);
  CHECK(devices.read(exi, 4) & 8);
  deselect();

  devices.write(exi, 0xd, 4);
  select();
  send(0xf1000000, 3);
  deselect();
  CHECK(devices.card_image(0)[512] == 255);
  devices.advance(5000);
  CHECK(devices.external_interrupt_pending());
  devices.mark_memory_card_saved(0);
  CHECK(!devices.memory_card_dirty(0));
  devices.set_memory_card(0, {});
  CHECK((devices.read(exi, 4) & 0x1000) == 0);
  CHECK_THROWS(devices.set_memory_card(2, image));
  CHECK_THROWS(devices.set_memory_card(0, std::span<const u8>(image).first(100)));
}
} // namespace

int main() {
  return run_tests([] {
    period_accounting();
    processor_interface();
    memory_interface();
    video_interface();
    video_aspect_ratio();
    disc_interface();
    serial_interface();
    audio_and_aram();
    disconnected_aram_live_descriptor();
    disconnected_aram_port();
    wii_aram_dma();
    expansion_interface();
    ipl_font_slots();
    memory_cards();
  });
}
