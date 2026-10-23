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
#include <utility>
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

void controller_poll_field_boundary() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Devices devices(console);
    const u64 halfline = (console == Console::Wii ? 27ull : 18ull) * 429;
    // A 17-halfline field and a one-line poll interval put the second poll
    // exactly at the beginning of the even field.
    devices.write(vi, 16, 2);
    devices.write(vi + 0x0c, 15, 4);
    devices.write(vi + 0x10, 15, 4);
    devices.write(si + 0x30, 1u << 16, 4);
    devices.write(si + 0x34, 0x08000000, 4);
    devices.write(pi + 4, 8, 4);
    PadState pad;
    pad.connected = true;
    pad.buttons = 0x100;
    devices.set_pad(0, pad);
    devices.advance(16 * halfline);
    CHECK(devices.external_interrupt_pending());
    CHECK(devices.read(si + 4, 4) == 0x01808080);
    CHECK(!devices.external_interrupt_pending());
    devices.advance(2 * halfline - 1);
    CHECK(!devices.external_interrupt_pending());
    pad.buttons = 0x200;
    devices.set_pad(0, pad);
    devices.advance(1);
    CHECK(devices.external_interrupt_pending());
    CHECK(devices.read(si + 4, 4) == 0x02808080);
    CHECK(!devices.external_interrupt_pending());
    // The boundary also rearms the first poll of the new field 15 halflines
    // later; the old two-halfline cadence must not continue across the field.
    devices.advance(15 * halfline - 1);
    CHECK(!devices.external_interrupt_pending());
    devices.advance(1);
    CHECK(devices.external_interrupt_pending());
  }
}

void controller_calibration() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    const u32 base = console == Console::Wii ? 0x0d006400 : si;
    for (unsigned port = 0; port < 4; ++port) {
      Devices devices(console);
      PadState pad;
      pad.connected = true;
      pad.stick_x = 0x12;
      pad.stick_y = 0x34;
      pad.cstick_x = 0x56;
      pad.cstick_y = 0x78;
      pad.left_trigger = 0x9a;
      pad.right_trigger = 0xbc;
      pad.analog_a = 0xde;
      pad.analog_b = 0xf0;
      devices.set_pad(port, pad);
      const auto transfer = [&](u8 command) {
        devices.write(base + 0x80, u32(command) << 24, 4);
        devices.write(base + 0x34, 0xc0010a01 | (port << 1), 4);
      };
      // Merely moving the axes does not recalibrate either origin command.
      for (u8 command : {u8(0x41), u8(0x42)}) {
        transfer(command);
        CHECK(devices.read(base + 0x80, 4) == 0x00008080);
        CHECK(devices.read(base + 0x84, 4) == 0x80800000);
        CHECK(devices.read(base + 0x88, 2) == 0);
      }
      // An origin request is consumed for every buffer command, including the
      // origin query itself, independently for each controller port.
      pad.buttons = 0x2000;
      devices.set_pad(port, pad);
      transfer(0x41);
      CHECK(devices.read(base + 0x80, 4) == 0x00001234);
      CHECK(devices.read(base + 0x84, 4) == 0x56789abc);
      CHECK(devices.read(base + 0x88, 2) == 0);
      pad.buttons = 0;
      pad.stick_x = 0x44;
      pad.left_trigger = 0;
      devices.set_pad(port, pad);
      transfer(0x42);
      CHECK(devices.read(base + 0x80, 4) == 0x00001234);
      CHECK(devices.read(base + 0x84, 4) == 0x56789abc);
      transfer(0x40);
      CHECK(devices.read(base + 0x80, 4) == 0x00804434);
      CHECK(devices.read(base + 0x84, 4) == 0x567800bc);

      pad.buttons = 0x2000;
      devices.set_pad(port, pad);
      devices.poll_controllers();
      CHECK(devices.read(base + 4 + 12 * port, 4) == 0x20804434);
      pad.buttons = 0;
      devices.set_pad(port, pad);
      transfer(0x41);
      CHECK(devices.read(base + 0x80, 4) == 0x00004434);
      CHECK(devices.read(base + 0x84, 4) == 0x567800bc);
      // A replacement controller starts with factory calibration.
      pad.connected = false;
      devices.set_pad(port, pad);
      pad.connected = true;
      devices.set_pad(port, pad);
      transfer(0x41);
      CHECK(devices.read(base + 0x80, 4) == 0x00008080);
      CHECK(devices.read(base + 0x84, 4) == 0x80800000);
    }

    Devices devices(console);
    // A zero-length field keeps automatic SI polls out of this direct-command
    // boundary test; the transfer itself is the controller's sample point.
    devices.write(vi, 0, 2);
    devices.write(vi + 0x0c, 0, 4);
    devices.write(vi + 0x10, 0, 4);
    PadState pad;
    pad.connected = true;
    pad.buttons = 0x1c01; // X + Y + Start, with an unrelated D-pad direction.
    pad.stick_x = 0x91;
    pad.left_trigger = 0x32;
    devices.set_pad(0, pad);
    const auto direct = [&] {
      devices.write(base + 0x80, 0x40000000, 4);
      devices.write(base + 0x34, 0xc0010801, 4);
      return devices.read(base + 0x80, 4);
    };
    const auto origin = [&] {
      devices.write(base + 0x80, 0x41000000, 4);
      devices.write(base + 0x34, 0xc0010a01, 4);
      return devices.read(base + 0x80, 4);
    };
    CHECK(direct() == 0x1c819180);
    devices.advance(3 * devices.ticks_per_second());
    CHECK(direct() == 0x1c819180);
    CHECK(origin() == 0x00008080);
    devices.advance(1);
    CHECK(direct() == 0x3c819180);
    CHECK(origin() == 0x00009180);
    CHECK(devices.read(base + 0x84, 4) == 0x80803200);
    // The request flag is transient; keeping the chord held rearms its timer.
    pad.stick_x = 0x71;
    devices.set_pad(0, pad);
    CHECK(direct() == 0x1c817180);
    devices.advance(devices.ticks_per_second());
    pad.buttons = 0x1800;
    devices.set_pad(0, pad);
    CHECK(direct() == 0x18807180);
    pad.buttons = 0x1c00;
    devices.set_pad(0, pad);
    CHECK(direct() == 0x1c807180);
    devices.advance(2 * devices.ticks_per_second() + 1);
    CHECK(direct() == 0x1c807180);
    CHECK(origin() == 0x00009180);
    devices.advance(devices.ticks_per_second());
    devices.poll_controllers();
    CHECK(devices.read(base + 4, 4) == 0x3c807180);
    CHECK(origin() == 0x00007180);
  }
}

void controller_analog_modes() {
  constexpr std::array<u32, 8> expected{0x56789bdf, 0x579abcdf, 0x579bdef0, 0x56789abc,
                                        0x5678def0, 0x56789bdf, 0x56789bdf, 0x56789bdf};
  for (Console console : {Console::GameCube, Console::Wii}) {
    Devices devices(console);
    const u32 base = console == Console::Wii ? 0x0d006400 : si;
    PadState pad;
    pad.connected = true;
    pad.buttons = 0x101;
    pad.stick_x = 0x12;
    pad.stick_y = 0x34;
    pad.cstick_x = 0x56;
    pad.cstick_y = 0x78;
    pad.left_trigger = 0x9a;
    pad.right_trigger = 0xbc;
    pad.analog_a = 0xde;
    pad.analog_b = 0xf0;
    for (unsigned port = 0; port < 4; ++port)
      devices.set_pad(port, pad);
    for (u32 mode = 0; mode < expected.size(); ++mode) {
      for (unsigned port = 0; port < 4; ++port)
        devices.write(base + port * 12, 0x400000 | (mode << 8), 4);
      devices.write(base + 0x38, 0x80000000, 4);
      devices.poll_controllers();
      for (unsigned port = 0; port < 4; ++port) {
        CHECK(devices.read(base + 4 + port * 12, 4) == 0x01811234);
        CHECK(devices.read(base + 8 + port * 12, 4) == expected[mode]);
        devices.write(base + 0x80, 0x40000000, 4);
        devices.write(base + 0x34, 0xc0030801 | (port << 1), 4);
        CHECK(devices.read(base + 0x80, 4) == 0x01811234);
        CHECK(devices.read(base + 0x84, 4) == expected[mode]);
      }
    }
    // When channel polling is enabled, a direct write keeps the previous
    // analog mode even though its motor command is still delivered.
    devices.write(base + 0x30, 0xf0, 4);
    for (unsigned port = 0; port < 4; ++port)
      devices.write(base + port * 12, 0x400300, 4);
    devices.write(base + 0x38, 0x80000000, 4);
    devices.poll_controllers();
    for (unsigned port = 0; port < 4; ++port)
      CHECK(devices.read(base + 8 + port * 12, 4) == expected[7]);
  }
}

void video_timing_register_updates() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    const u64 fast_sample = console == Console::Wii ? 27 : 18;
    Devices devices(console);
    devices.write(vi + 4, 5, 4);
    devices.advance(7);
    CHECK(devices.event_free_cycles() == 5 * fast_sample - 8);
    devices.write(vi + 6, 3, 2);
    CHECK(devices.event_free_cycles() == 3 * fast_sample - 8);
    devices.write(vi + 4, 0xabcd, 2);
    CHECK(devices.event_free_cycles() == 3 * fast_sample - 8);
    devices.write(vi + 0x6c, 0, 2);
    CHECK(devices.event_free_cycles() == 6 * fast_sample - 8);
    devices.write(vi + 0x6c, 1, 2);
    CHECK(devices.event_free_cycles() == 3 * fast_sample - 8);
    devices.advance(3 * fast_sample - 7);
    CHECK(devices.read(vi + 0x2c, 2) == 1);
    CHECK(devices.read(vi + 0x2e, 2) == 4);
    devices.write(vi + 6, 0, 2);
    CHECK(devices.event_free_cycles() == 0);

    struct FieldTiming {
      u16 vertical, odd_before, odd_after, even_before, even_after;
      u32 odd_length, even_length, odd_output, even_output;
    };

    // Output positions account for the different postblank lengths of the fields.
    constexpr std::array<FieldTiming, 4> timings{{
        {0x21, 2, 1, 3, 4, 10, 14, 9, 23},
        {0x12, 0, 7, 4, 1, 15, 13, 8, 21},
        {0x30, 1, 9, 8, 2, 16, 16, 7, 23},
        {0x03, 1, 2, 3, 4, 12, 16, 0, 0},
    }};
    for (const auto &timing : timings) {
      Devices output(console);
      output.write(vi + 6, 1, 2);
      output.write(vi + 0x1c, 0x10000020, 4);
      output.write(vi + 0x24, 0x10000040, 4);
      output.write(vi, timing.vertical, 2);
      // Exercise each half independently so no write can conceal stale geometry.
      output.write(vi + 0x0c, timing.odd_after, 2);
      output.write(vi + 0x0e, timing.odd_before, 2);
      output.write(vi + 0x10, timing.even_after, 2);
      output.write(vi + 0x12, timing.even_before, 2);
      std::vector<std::pair<unsigned, u64>> frames;
      output.xfb_present = [&](const Xfb &frame) {
        CHECK(frame.address == (frame.field == 0 ? 0x400u : 0x800u));
        frames.emplace_back(frame.field, frame.cycles);
      };
      const u64 total = u64(timing.odd_length + timing.even_length) * fast_sample;
      for (u64 tick = 0; tick < total; ++tick)
        output.advance(1);
      CHECK(output.read(vi + 0x2c, 2) == 1);
      CHECK(output.read(vi + 0x2e, 2) == 1);
      if (timing.odd_output == 0) {
        CHECK(frames.empty());
        CHECK(output.field_count() == 0);
      } else {
        const std::vector<std::pair<unsigned, u64>> expected{{0, timing.odd_output * fast_sample},
                                                             {1, timing.even_output * fast_sample}};
        CHECK(frames == expected);
        CHECK(output.field_count() == 2);
      }
    }
  }
}

namespace audio_dma_clock_checks {
struct Rate {
  Console console;
  u32 control, label;
  u64 period;
};

// Independent integer CPU-tick calendar for eight stereo DAC frames.
constexpr std::array rates{
    Rate{Console::GameCube, 0, 48042, 80928},
    Rate{Console::GameCube, 64, 32028, 121392},
    Rate{Console::Wii, 0, 48000, 121500},
    Rate{Console::Wii, 64, 32000, 182250},
};

struct Packet {
  std::array<s16, 16> values;
  u32 rate;
  bool operator==(const Packet &) const = default;
};

std::array<s16, 16> signal(unsigned block) {
  std::array<s16, 16> result{};
  for (unsigned frame = 0; frame != 8; ++frame) {
    result[frame * 2] = s16(100 + block * 8 + frame);
    result[frame * 2 + 1] = s16(-100 - int(block * 8 + frame));
  }
  return result;
}

struct Fixture {
  hw::Devices device;
  Rate rate;
  std::vector<Packet> packets;
  std::vector<u32> reads;

  explicit Fixture(Rate r, bool connect_memory) : device(r.console), rate(r) {
    device.write(ai, r.control, 4); // Explicit rate, without AIS streaming.
    device.audio_samples = [this](std::span<const s16> values, u32 label) {
      CHECK(values.size() == 16);
      Packet packet{{}, label};
      std::copy(values.begin(), values.end(), packet.values.begin());
      packets.push_back(packet);
    };
    if (connect_memory)
      device.dma_read = [this](u32 address, std::span<u8> bytes) {
        CHECK(address >= 0x120 && address <= 0x160 && (address - 0x120) % 32 == 0);
        CHECK(bytes.size() == 32);
        reads.push_back(address);
        const auto values = signal((address - 0x120) / 32);
        for (unsigned index = 0; index != values.size(); ++index) {
          const u16 bits = u16(values[index]);
          bytes[index * 2] = u8(bits >> 8);
          bytes[index * 2 + 1] = u8(bits);
        }
      };
  }

  void start(u16 blocks) {
    device.write(dsp + 0x30, 0x120, 4);
    device.write(dsp + 0x36, 0x8000 | blocks, 2);
  }

  void packet(unsigned index, const std::array<s16, 16> &values) const {
    CHECK(index < packets.size());
    CHECK(packets[index].values == values && packets[index].rate == rate.label);
  }
};

void enabled_controls() {
  for (const Rate rate : rates) {
    Fixture f(rate, true);
    f.start(3);
    for (unsigned block = 0; block != 3; ++block) {
      f.device.advance(rate.period);
      CHECK(f.packets.size() == block + 1);
      f.packet(block, signal(block));
    }
    CHECK(f.reads == (std::vector<u32>{0x120, 0x140, 0x160}));
    CHECK(f.device.read(dsp + 0x36, 2) == 0x8003);
    CHECK(f.device.read(dsp + 0x3a, 2) == 2);
    CHECK((f.device.read(dsp + 10, 2) & 8) != 0);
    CHECK(f.device.read(ai + 8, 4) == 0);
  }
}

void disabled_tick_calendar() {
  for (const Rate rate : rates) {
    Fixture f(rate, false); // Disabled delivery must need no DMA bus connection.
    f.device.advance(rate.period - 1);
    CHECK(f.packets.empty());
    f.device.advance(1);
    CHECK(f.packets.size() == 1);
    f.packet(0, {});
    f.device.advance(rate.period * 2);
    CHECK(f.packets.size() == 3);
    f.packet(1, {});
    f.packet(2, {});
    CHECK(f.reads.empty());
    CHECK(f.device.read(dsp + 0x36, 2) == 0);
    CHECK(f.device.read(dsp + 0x3a, 2) == 0);
    CHECK((f.device.read(dsp + 10, 2) & 8) == 0);
    CHECK(f.device.read(ai + 8, 4) == 0);

    Fixture partitioned(rate, false);
    for (unsigned block = 0; block != 3; ++block) {
      u64 left = rate.period;
      while (left) {
        const u64 safe = partitioned.device.event_free_cycles();
        const u64 piece = std::min(left, safe ? safe : 1);
        partitioned.device.advance(piece);
        left -= piece;
      }
    }
    CHECK(partitioned.packets == f.packets);
  }
}

void stop_restart_descriptor() {
  for (const Rate rate : rates) {
    Fixture f(rate, true);
    f.start(3);
    f.device.advance(rate.period);
    f.packet(0, signal(0));
    f.device.write(dsp + 10, 12, 2); // Clear the already elapsed start interrupt.
    f.device.write(dsp + 0x36, 3, 2);
    const auto blocks = f.device.read(dsp + 0x3a, 2);
    const auto control = f.device.read(dsp + 10, 2);
    f.device.advance(rate.period * 2);
    CHECK(f.packets.size() == 3);
    f.packet(1, {});
    f.packet(2, {});
    CHECK(f.reads == (std::vector<u32>{0x120}));
    CHECK(f.device.read(dsp + 0x3a, 2) == blocks);
    CHECK(f.device.read(dsp + 10, 2) == control);
    CHECK(f.device.read(dsp + 0x36, 2) == 3);
    f.start(3);
    f.device.advance(rate.period);
    f.packet(3, signal(0));
    f.device.write(dsp + 10, 12, 2);
    // Updating an enabled reload length does not replace the current prefix.
    f.device.write(dsp + 0x36, 0x8001, 2);
    f.device.advance(rate.period);
    f.packet(4, signal(1));
    CHECK((f.device.read(dsp + 10, 2) & 8) == 0);
    f.device.advance(rate.period);
    f.packet(5, signal(2));
    CHECK(f.reads == (std::vector<u32>{0x120, 0x120, 0x140, 0x160}));
    CHECK(f.device.read(dsp + 0x3a, 2) == 0);
    CHECK((f.device.read(dsp + 10, 2) & 8) != 0);
    CHECK(f.device.read(ai + 8, 4) == 0);
  }
}

void absent_callback() {
  for (const Rate rate : rates) {
    hw::Devices device(rate.console);
    device.write(ai, rate.control, 4);
    // No audio callback and no bus connection are needed for idle DAC clocks.
    device.advance(rate.period * 3);
    CHECK(device.read(dsp + 0x36, 2) == 0);
    CHECK((device.read(dsp + 10, 2) & 8) == 0);
    unsigned reads = 0;
    device.dma_read = [&](u32 address, std::span<u8> bytes) {
      CHECK(address == 0x120 + reads * 32);
      CHECK(bytes.size() == 32);
      ++reads;
      std::fill(bytes.begin(), bytes.end(), u8{0});
    };
    device.write(dsp + 0x30, 0x120, 4);
    device.write(dsp + 0x36, 0x8003, 2);
    device.advance(rate.period * 3);
    CHECK(reads == 3);
    CHECK(device.read(dsp + 0x3a, 2) == 2);
    device.write(dsp + 0x36, 3, 2);
    device.advance(rate.period * 2);
    CHECK(reads == 3);
    CHECK(device.read(dsp + 0x3a, 2) == 2);
    CHECK(device.read(ai + 8, 4) == 0);
  }
}
} // namespace audio_dma_clock_checks

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

void expansion_select_transactions() {
  const auto image = Devices::format_memory_card(4, 0);
  for (Console console : {Console::GameCube, Console::Wii}) {
    const u32 base = console == Console::GameCube ? exi : 0x0d006800;
    for (unsigned slot = 0; slot < 2; ++slot) {
      Devices devices(console);
      devices.set_memory_card(slot, image);
      const u32 channel = base + slot * 20;
      const auto select = [&](u32 mask) { devices.write(channel, (mask << 7) | 5, 4); };
      const auto send = [&](u32 value, unsigned count) {
        devices.write(channel + 16, value, 4);
        devices.write(channel + 12, 5 | ((count - 1) << 4), 4);
      };
      const auto receive = [&] {
        devices.write(channel + 12, 1, 4);
        return devices.read(channel + 16, 4);
      };
      select(1);
      send(0xf2005000, 4);
      send(0, 1);
      send(0xaabbccdd, 4);
      select(2);
      CHECK(devices.card_image(slot)[0xa000] == 255);
      CHECK(!devices.memory_card_dirty(slot));

      select(0);
      select(1);
      send(0x83000000, 1);
      select(2);
      select(1);
      CHECK(receive() == 0xc1000000);

      select(0);
      select(1);
      send(0x83000000, 1);
      select(3);
      select(1);
      CHECK(receive() == 0xc1000000);

      select(0);
      devices.set_memory_card(slot, image);
      select(1);
      send(0xf2005000, 4);
      send(0, 1);
      send(0xaabbccdd, 4);
      select(3);
      select(2);
      CHECK(devices.card_image(slot)[0xa000] == 255);
      CHECK(!devices.memory_card_dirty(slot));
      // The single changed card-select line receives the entire positive select mask.
      select(1);
      send(0x83000000, 1);
      CHECK(receive() == 0xc1000000);

      select(0);
      select(1);
      send(0xf2005000, 4);
      send(0, 1);
      send(0xaabbccdd, 4);
      select(0);
      CHECK(devices.memory_card_dirty(slot));
      CHECK(devices.card_image(slot)[0xa000] == 0xaa);
      CHECK(devices.card_image(slot)[0xa003] == 0xdd);
    }

    Devices devices(console);
    std::vector<u8> rom(0x200000);
    for (std::size_t index = 0; index < rom.size(); ++index)
      rom[index] = u8(index & 255);
    devices.set_ipl(rom);
    const auto select = [&](u32 mask) { devices.write(base, (mask << 7) | 5, 4); };
    const auto receive = [&] {
      devices.write(base + 12, 0x31, 4);
      return devices.read(base + 16, 4);
    };
    select(2);
    devices.write(base + 16, 0x00004000, 4);
    devices.write(base + 12, 0x35, 4);
    CHECK(receive() == 0x00010203);
    select(1);
    select(2);
    CHECK(receive() == 0x04050607);
    select(3);
    select(2);
    CHECK(receive() == 0x08090a0b);
    select(0);
    select(2);
    CHECK(receive() == 0xffffffff);
  }
}

void memory_card_dma_completions() {
  const auto image = Devices::format_memory_card(4, 0);
  for (Console console : {Console::GameCube, Console::Wii}) {
    const u32 base = console == Console::GameCube ? exi : 0x0d006800;
    for (unsigned slot = 0; slot < 2; ++slot) {
      Devices devices(console);
      devices.set_memory_card(slot, image);
      const u32 channel = base + slot * 20;
      unsigned reads = 0;
      auto expected = image;
      devices.dma_write = [&](u32 address, std::span<const u8> data) {
        CHECK(address == 0x100);
        CHECK(std::equal(data.begin(), data.end(), expected.begin()));
        ++reads;
      };
      devices.dma_read = [&](u32 address, std::span<u8> data) {
        CHECK(address == 0x100);
        for (std::size_t index = 0; index < data.size(); ++index)
          data[index] = expected[index] = u8(index * 37 + 19);
      };
      devices.write(pi + 4, 16, 4);
      devices.write(channel, 0x85, 4);
      devices.write(channel + 16, 0x52000000, 4);
      devices.write(channel + 12, 0x35, 4);
      devices.write(channel + 16, 0, 4);
      devices.write(channel + 12, 5, 4);
      devices.write(channel + 4, 0x100, 4);
      const auto dma = [&](u32 bytes) {
        devices.write(channel + 8, bytes, 4);
        devices.write(channel + 12, 3, 4);
        CHECK((devices.read(channel + 12, 4) & 1) == 0);
      };
      const auto pending = [&] {
        const bool complete = (devices.read(channel, 4) & 8) != 0;
        CHECK(devices.external_interrupt_pending() == complete);
        return complete;
      };
      const auto acknowledge = [&] {
        devices.write(channel, 0x8d, 4);
        CHECK(!pending());
      };
      const u64 ticks = devices.ticks_per_second() / 524288;
      dma(8);
      devices.advance(ticks);
      dma(2);
      CHECK(reads == 2);
      devices.advance(2 * ticks - 1);
      CHECK(!pending());
      devices.advance(1);
      CHECK(pending());
      acknowledge();
      devices.advance(5 * ticks - 1);
      CHECK(!pending());
      devices.advance(1);
      CHECK(pending());
      acknowledge();

      // A slower write retains its callback while a later read finishes first.
      const u64 write_ticks = devices.ticks_per_second() / 98432;
      devices.write(channel + 8, 4, 4);
      devices.write(channel + 12, 7, 4);
      CHECK(devices.memory_card_dirty(slot));
      CHECK(std::equal(expected.begin(), expected.end(), devices.card_image(slot).begin()));
      devices.advance(ticks);
      dma(2);
      devices.advance(2 * ticks);
      CHECK(pending());
      acknowledge();
      devices.advance(4 * write_ticks - 3 * ticks - 1);
      CHECK(!pending());
      devices.advance(1);
      CHECK(pending());
      acknowledge();

      // Each zero-byte transfer still queues a completion for the next timing update.
      dma(0);
      CHECK(!pending());
      devices.advance(0);
      CHECK(pending());
      acknowledge();

      dma(4);
      dma(4);
      devices.advance(4 * ticks);
      CHECK(pending());
      acknowledge();
      devices.advance(ticks);
      CHECK(!pending());

      // Replacing a card discards that device's outstanding completion callbacks.
      dma(8);
      devices.set_memory_card(slot, image);
      devices.advance(8 * ticks);
      CHECK(!pending());
    }
  }
}

void timed_interrupt_boundaries() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Devices devices(console);
    const u64 sample =
        devices.ticks_per_second() * (console == Console::Wii ? 2250u : 2248u) / 108000000;
    devices.write(pi + 4, 32, 4);
    devices.write(ai + 12, 1, 4);
    devices.write(ai, 7, 4);
    for (u64 tick = 0; tick < sample - 1; ++tick) {
      devices.advance(1);
      CHECK(!devices.external_interrupt_pending());
    }
    CHECK(devices.cycles() == sample - 1);
    CHECK(devices.read(ai + 8, 4) == 0);
    devices.advance(1);
    CHECK(devices.read(ai + 8, 4) == 1);
    CHECK(devices.external_interrupt_pending());
    devices.write(pi, 32, 4);
    CHECK(!devices.external_interrupt_pending());
    devices.advance(0);
    CHECK(devices.external_interrupt_pending());
    devices.write(pi, 32, 4);
    devices.advance(1);
    CHECK(devices.external_interrupt_pending());
    devices.write(ai, 15, 4);
    CHECK(!devices.external_interrupt_pending());

    devices.write(ai, 0x25, 4);
    devices.advance(91);
    devices.write(ai, 7, 4);
    devices.advance(sample - 1);
    CHECK(devices.read(ai + 8, 4) == 0);
    devices.advance(1);
    CHECK(devices.read(ai + 8, 4) == 1);
    CHECK(devices.external_interrupt_pending());

    devices.write(pi + 4, 64, 4);
    devices.write(dsp + 10, 0x44, 2);
    devices.dma_read = [](u32, std::span<u8> out) { std::fill(out.begin(), out.end(), u8(0x5a)); };
    devices.dma_write = [](u32 address, std::span<const u8> in) {
      CHECK(address == 0x10000000);
      CHECK(in.size() == 32 && in.front() == 0x5a && in.back() == 0x5a);
    };
    devices.write(dsp + 0x20, 0, 4);
    devices.write(dsp + 0x24, 0, 4);
    devices.write(dsp + 0x28, 32, 4);
    for (unsigned tick = 0; tick < 245; ++tick) {
      devices.advance(1);
      CHECK(!devices.external_interrupt_pending());
    }
    CHECK(devices.read(dsp + 10, 2) & 0x200);
    devices.advance(1);
    CHECK((devices.read(dsp + 10, 2) & 0x220) == 0x20);
    CHECK(devices.external_interrupt_pending());
    devices.write(pi, 64, 4);
    CHECK(!devices.external_interrupt_pending());
    devices.advance(0);
    CHECK(devices.external_interrupt_pending());
  }
}

void disc_dma_aliases() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    for (u32 physical : {0x00001000u, 0x10001000u}) {
      if (console == Console::GameCube && physical >= 0x10000000)
        continue;
      for (u32 alias : {0u, 0x40000000u, 0x80000000u, 0xc0000000u}) {
        Memory memory(console);
        Devices devices(console);
        const u32 retained = console == Console::Wii ? physical | alias : physical;
        std::array<u8, 32> expected{};
        for (unsigned i = 0; i < expected.size(); ++i)
          expected[i] = u8(i ^ 0xa6);
        unsigned writes = 0;
        devices.dma_write = [&](u32 address, std::span<const u8> bytes) {
          CHECK(address == retained && bytes.size() == expected.size());
          memory.copy_in(address, bytes);
          ++writes;
        };
        devices.disc_read = [&](u64 offset, std::span<u8> bytes) {
          CHECK(offset == 0x200 && bytes.size() == expected.size());
          std::copy(expected.begin(), expected.end(), bytes.begin());
        };
        devices.write(di + 8, 0xa8000000, 4);
        devices.write(di + 12, 0x80, 4);
        devices.write(di + 16, 32, 4);
        devices.write(di + 20, physical | alias, 4);
        devices.write(di + 24, 32, 4);
        devices.write(di + 28, 3, 4);
        CHECK(devices.read(di + 20, 4) == retained);
        devices.advance(300 * (devices.ticks_per_second() / 1000000));
        CHECK(writes == 1);
        CHECK(devices.read(di + 20, 4) == retained + 32);
        CHECK(devices.read(di + 24, 4) == 0);
        CHECK(devices.read(di, 4) & 16);
        CHECK(std::equal(expected.begin(), expected.end(), memory.range(physical, 32).begin()));
      }
    }
  }
}

void timed_callback_failure() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Devices devices(console);
    const u64 delay = 300 * (devices.ticks_per_second() / 1000000);
    const u64 sample =
        devices.ticks_per_second() * (console == Console::Wii ? 2250u : 2248u) / 108000000;
    devices.write(ai, 3, 4);
    devices.write(ai + 12, 0xffffffff, 4);
    unsigned callbacks = 0;
    devices.dma_write = [&](u32 address, std::span<const u8> in) {
      ++callbacks;
      CHECK(address == 0x100 && in.size() == 12);
      CHECK(devices.cycles() == delay + 1);
      CHECK(devices.read(ai + 8, 4) == (delay - 1) / sample);
      CHECK(devices.read(di + 0x1c, 4) & 1);
      throw std::runtime_error("Authored DMA callback failure");
    };
    devices.write(di + 8, 0x12000000, 4);
    devices.write(di + 0x14, 0x100, 4);
    devices.write(di + 0x18, 32, 4);
    devices.write(di + 0x1c, 3, 4);
    for (u64 tick = 0; tick < delay - 1; ++tick)
      devices.advance(1);
    CHECK(callbacks == 0);
    CHECK_THROWS(devices.advance(2));
    CHECK(callbacks == 1);
    CHECK(devices.read(ai + 8, 4) == (delay - 1) / sample);
    CHECK(devices.read(di + 0x14, 4) == 0x100);
    CHECK(devices.read(di + 0x18, 4) == 32);
    CHECK(devices.read(di + 0x1c, 4) & 1);
    devices.advance(1);
    CHECK(callbacks == 1);
    const u64 cycles = devices.cycles();
    CHECK_THROWS(devices.advance(std::numeric_limits<u64>::max()));
    CHECK(devices.cycles() == cycles);
  }
}

void aram_dma_bank_boundaries() {
  for (u32 start : {0x003fffe0u, 0x00ffffe0u}) {
    Devices devices(Console::GameCube);
    std::array<u8, 64> input{}, output{};
    for (std::size_t i = 0; i < input.size(); ++i)
      input[i] = u8(37 * i + 19);
    std::vector<u32> reads, writes;
    devices.dma_read = [&](u32 address, std::span<u8> bytes) {
      CHECK(bytes.size() == 32);
      CHECK(address == 0 || address == 32);
      std::copy_n(input.begin() + address, bytes.size(), bytes.begin());
      reads.push_back(address);
    };
    devices.dma_write = [&](u32 address, std::span<const u8> bytes) {
      CHECK(bytes.size() == 32);
      CHECK(address == 0 || address == 32);
      std::copy(bytes.begin(), bytes.end(), output.begin() + address);
      writes.push_back(address);
    };
    devices.write(dsp + 0x12, 4, 2);
    // Unaligned descriptor bits are discarded before the two hardware blocks
    // are copied, including the blocks adjacent to bank boundaries.
    devices.write(dsp + 0x20, 19, 4);
    devices.write(dsp + 0x24, start | 17, 4);
    devices.write(dsp + 0x28, 79, 4);
    CHECK(reads == (std::vector<u32>{0, 32}));
    CHECK(writes.empty());
    CHECK(devices.read(dsp + 0x20, 4) == 64);
    CHECK(devices.read(dsp + 0x24, 4) == start + 64);
    std::array<u8, 32> block{};
    devices.read_aram(start, block);
    CHECK(std::equal(block.begin(), block.end(), input.begin()));
    devices.read_aram((start + 32) & 0xffffff, block);
    CHECK(std::equal(block.begin(), block.end(), input.begin() + 32));
    if (start == 0x003fffe0) {
      devices.read_aram(0x007fffe0, block);
      CHECK(std::equal(block.begin(), block.end(), input.begin()));
      devices.read_aram(0x00800000, block);
      CHECK(std::all_of(block.begin(), block.end(), [](u8 byte) { return byte == 0; }));
    }
    devices.advance(492);
    devices.write(dsp + 0x20, 0, 4);
    devices.write(dsp + 0x24, start, 4);
    devices.write(dsp + 0x28, 0x80000040, 4);
    CHECK(writes == (std::vector<u32>{0, 32}));
    CHECK(output == input);
    devices.advance(491);
    CHECK(devices.read(dsp + 10, 2) & 0x200);
    devices.advance(1);
    CHECK((devices.read(dsp + 10, 2) & 0x200) == 0);
  }
}

void event_free_boundaries() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Devices devices(console);
    const u64 halfline = (console == Console::Wii ? 27ull : 18ull) * 429;
    CHECK(devices.event_free_cycles() == halfline - 1);
    devices.advance(17);
    CHECK(devices.event_free_cycles() == halfline - 18);
    CHECK(devices.read(vi + 0x2e, 2) == 1 + 429 * 17 / halfline);
    CHECK(devices.event_free_cycles() == halfline - 18);
    devices.write(vi + 4, 1, 4);
    // A period write recomputes the deadline from the already elapsed phase.
    CHECK(devices.event_free_cycles() == (console == Console::Wii ? 9u : 0u));
    devices.advance(console == Console::Wii ? 10 : 1);
    CHECK(devices.read(vi + 0x2c, 2) == 1);
    devices.write(vi + 4, 429, 4);

    const u64 sample =
        devices.ticks_per_second() * (console == Console::Wii ? 2250u : 2248u) / 108000000;
    devices.write(pi + 4, 32, 4);
    devices.write(ai + 12, 1, 4);
    devices.write(ai, 7, 4);
    CHECK(devices.event_free_cycles() == std::min(sample - 1, halfline - 1));
    // Use event-free pieces so a different periodic update may precede the AI sample.
    u64 elapsed = 0;
    while (elapsed < sample) {
      const u64 safe = devices.event_free_cycles();
      const u64 piece = std::min(sample - elapsed, safe ? safe : 1);
      devices.advance(piece);
      elapsed += piece;
    }
    CHECK(devices.interrupt_causes(32) == 32);
    devices.write(pi, 32, 4);
    CHECK(devices.interrupt_causes(32) == 0);
    CHECK(devices.event_free_cycles() == 0);
    devices.advance(0);
    CHECK(devices.interrupt_causes(32) == 32);
    CHECK(devices.event_free_cycles() > 0);
    devices.write(ai, 14, 4);
    CHECK(devices.interrupt_causes(32) == 0);
  }
}

void event_free_partitions() {
  struct Event {
    u32 kind;
    u64 cycles;
    u32 address;
    std::vector<u8> bytes;
    bool operator==(const Event &) const = default;
  };

  struct Fixture {
    Devices devices;
    std::array<u8, 8192> ram{};
    std::vector<Event> events;

    explicit Fixture(Console console) : devices(console) {
      for (std::size_t i = 0; i < ram.size(); ++i)
        ram[i] = u8(i * 37 + 9);
      devices.dma_read = [this](u32 address, std::span<u8> bytes) {
        const u32 offset = address & 8191;
        CHECK(bytes.size() <= ram.size() - offset);
        std::copy_n(ram.begin() + offset, bytes.size(), bytes.begin());
        events.push_back({1, devices.cycles(), address, {bytes.begin(), bytes.end()}});
      };
      devices.dma_write = [this](u32 address, std::span<const u8> bytes) {
        const u32 offset = address & 8191;
        CHECK(bytes.size() <= ram.size() - offset);
        std::copy(bytes.begin(), bytes.end(), ram.begin() + offset);
        events.push_back({2, devices.cycles(), address, {bytes.begin(), bytes.end()}});
      };
      devices.disc_read = [this](u64 address, std::span<u8> bytes) {
        for (std::size_t i = 0; i < bytes.size(); ++i)
          bytes[i] = u8((address + i) * 13);
        events.push_back({3, devices.cycles(), u32(address), {bytes.begin(), bytes.end()}});
      };
      devices.audio_samples = [this](std::span<const s16> samples, u32 rate) {
        std::vector<u8> bytes;
        for (s16 sample : samples) {
          bytes.push_back(u8(u16(sample) >> 8));
          bytes.push_back(u8(sample));
        }
        events.push_back({4, devices.cycles(), rate, std::move(bytes)});
      };
      devices.disc_audio_samples = [this](std::span<const s16> samples, u32 rate, u8 left,
                                          u8 right) {
        std::vector<u8> bytes{left, right};
        for (s16 sample : samples) {
          bytes.push_back(u8(u16(sample) >> 8));
          bytes.push_back(u8(sample));
        }
        events.push_back({5, devices.cycles(), rate, std::move(bytes)});
      };
      devices.xfb_present = [this](const Xfb &frame) {
        events.push_back(
            {6,
             frame.cycles,
             frame.address,
             {u8(frame.field), u8(frame.width), u8(frame.height), u8(frame.stride_bytes)}});
      };
      devices.write(pi + 4, 0x17c, 4);
      devices.write(vi, 32, 2);
      devices.write(vi + 0x0c, 0x100002, 4);
      devices.write(vi + 0x10, 0x100002, 4);
      devices.write(vi + 0x1c, 0x10000010, 4);
      devices.write(vi + 0x24, 0x10000020, 4);
      devices.write(si + 0x30, 0x20080, 4);
      devices.write(si + 0x34, 0x08000000, 4);
      PadState pad;
      pad.connected = true;
      pad.buttons = 0x123;
      devices.set_pad(0, pad);
      devices.write(ai + 12, 4, 4);
      devices.write(ai, 7, 4);
      devices.write(dsp + 0x0a, 0x154, 2);
      devices.write(dsp + 0x30, 0x400, 4);
      devices.write(dsp + 0x36, 0x8003, 2);
      devices.write(dsp + 0x20, 0x800, 4);
      devices.write(dsp + 0x24, 0x100, 4);
      devices.write(dsp + 0x28, 96, 4);
      devices.write(di, 0x2a, 4);
      devices.write(di + 8, 0xa8000000, 4);
      devices.write(di + 12, 0x80, 4);
      devices.write(di + 16, 64, 4);
      devices.write(di + 20, 0xc00, 4);
      devices.write(di + 24, 64, 4);
      devices.write(di + 28, 3, 4);
      devices.prepare_disc_boot(true, 10);
      CHECK(!devices.disc_immediate(0xe1000000, 0x800, 4096).failed);
      devices.set_memory_card(0, Devices::format_memory_card(4, 3));
      devices.write(exi, 0x8d, 4);
      const auto send = [this](u32 value, unsigned count) {
        devices.write(exi + 16, value, 4);
        devices.write(exi + 12, 5 | ((count - 1) << 4), 4);
      };
      send(0x81010000, 2);
      devices.write(exi, 13, 4);
      devices.write(exi, 0x8d, 4);
      send(0xf2000100, 4);
      send(0, 1);
      send(0xabcdef01, 4);
      devices.write(exi, 13, 4);
      devices.write(exi, 0x8d, 4);
      send(0x52000100, 4);
      send(0, 1);
      devices.write(exi + 4, 0x1000, 4);
      devices.write(exi + 8, 7, 4);
      devices.write(exi + 12, 3, 4);
    }

    std::vector<u32> snapshot() {
      std::vector<u32> result;
      for (u32 offset : {0u, 4u, 0xcu, 0x10u, 0x14u, 0x1cu, 0x24u, 0x28u, 0x30u})
        result.push_back(devices.read(pi + offset, 4));
      for (u32 offset = 0; offset < 0x28; offset += 4)
        result.push_back(devices.read(di + offset, 4));
      for (u32 offset = 0; offset < 0x40; offset += 4)
        result.push_back(devices.read(si + offset, 4));
      for (u32 offset = 0; offset < 0x3c; offset += 4)
        result.push_back(devices.read(exi + offset, 4));
      for (u32 offset = 0; offset < 0x10; offset += 4)
        result.push_back(devices.read(ai + offset, 4));
      for (u32 offset = 0; offset <= 0x74; offset += 2)
        result.push_back(devices.read(vi + offset, 2));
      for (u32 offset : {0xau, 0x12u, 0x16u, 0x1au, 0x20u, 0x22u, 0x24u, 0x26u, 0x28u, 0x2au, 0x30u,
                         0x32u, 0x36u, 0x3au})
        result.push_back(devices.read(dsp + offset, 2));
      result.push_back(u32(devices.field_count()));
      result.push_back(devices.rtc_seconds());
      result.push_back(devices.memory_card_dirty(0));
      return result;
    }
  };

  for (Console console : {Console::GameCube, Console::Wii}) {
    Fixture stepped(console), grouped(console);
    CHECK(stepped.snapshot() == grouped.snapshot());
    unsigned boundaries = 0, grouped_calls = 0;
    for (u64 target : {u64(99), u64(245), u64(246), u64(4999), u64(5000), u64(60001), u64(145799),
                       u64(145800), u64(218700), u64(500003), u64(1000000), u64(3000000)}) {
      while (grouped.devices.cycles() < target) {
        const u64 budget = grouped.devices.event_free_cycles();
        const u64 piece = std::min(target - grouped.devices.cycles(), budget ? budget : 1);
        const u64 first = piece / 2;
        if (first) {
          grouped.devices.advance(first);
          for (u64 cycle = 0; cycle < first; ++cycle)
            stepped.devices.advance(1);
          // MMIO reads materialize elapsed cycles without moving the next
          // completion or dispatching callbacks inside a quiet interval.
          CHECK(stepped.snapshot() == grouped.snapshot());
          CHECK(grouped.devices.event_free_cycles() == budget - first);
          CHECK(stepped.events == grouped.events);
        }
        const u32 causes = grouped.devices.interrupt_causes(0xffffffff);
        grouped.devices.advance(piece - first);
        ++grouped_calls;
        boundaries += !budget;
        if (budget)
          CHECK(grouped.devices.interrupt_causes(0xffffffff) == causes);
        for (u64 cycle = first; cycle < piece; ++cycle)
          stepped.devices.advance(1);
        CHECK(stepped.events == grouped.events);
        CHECK(stepped.devices.interrupt_causes(0xffffffff) ==
              grouped.devices.interrupt_causes(0xffffffff));
      }
      CHECK(stepped.snapshot() == grouped.snapshot());
      CHECK(stepped.ram == grouped.ram);
      CHECK(std::equal(stepped.devices.sram().begin(), stepped.devices.sram().end(),
                       grouped.devices.sram().begin()));
      CHECK(std::equal(stepped.devices.card_image(0).begin(), stepped.devices.card_image(0).end(),
                       grouped.devices.card_image(0).begin()));
      if (console == Console::GameCube) {
        std::array<u8, 128> first{}, second{};
        stepped.devices.read_aram(0x100, first);
        grouped.devices.read_aram(0x100, second);
        CHECK(first == second);
      }
      // PI acknowledge and period changes invalidate a previously queried budget.
      for (Fixture *fixture : {&stepped, &grouped}) {
        (void)fixture->devices.event_free_cycles();
        fixture->devices.write(pi, 0x17c, 4);
        fixture->devices.write(ai + 12, u32(target / 37), 4);
        fixture->devices.write(vi + 4, target & 1 ? 427 : 429, 4);
      }
      CHECK(stepped.snapshot() == grouped.snapshot());
    }
    CHECK(boundaries > 100);
    CHECK(grouped_calls < 3000);
    for (u32 kind : {1u, 2u, 3u, 4u, 5u, 6u})
      CHECK(std::any_of(grouped.events.begin(), grouped.events.end(),
                        [kind](const Event &event) { return event.kind == kind; }));
  }
}

} // namespace

int main() {
  return run_tests([] {
    period_accounting();
    timed_interrupt_boundaries();
    timed_callback_failure();
    event_free_boundaries();
    event_free_partitions();
    processor_interface();
    memory_interface();
    video_interface();
    video_aspect_ratio();
    video_timing_register_updates();
    disc_interface();
    disc_dma_aliases();
    serial_interface();
    controller_calibration();
    controller_analog_modes();
    controller_poll_field_boundary();
    audio_dma_clock_checks::enabled_controls();
    audio_dma_clock_checks::disabled_tick_calendar();
    audio_dma_clock_checks::stop_restart_descriptor();
    audio_dma_clock_checks::absent_callback();
    audio_and_aram();
    disconnected_aram_live_descriptor();
    aram_dma_bank_boundaries();
    disconnected_aram_port();
    wii_aram_dma();
    expansion_interface();
    ipl_font_slots();
    memory_cards();
    expansion_select_transactions();
    memory_card_dma_completions();
  });
}
