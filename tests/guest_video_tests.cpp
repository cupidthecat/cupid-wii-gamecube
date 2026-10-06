// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/machine.hpp"
#include "test_support.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <sstream>
#include <vector>

namespace {
using namespace cupid;

void append(std::vector<u8> &bytes, u32 value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes.push_back(static_cast<u8>(value >> (24 - 8 * i)));
}

void cp(std::vector<u8> &bytes, u8 address, u32 value) {
  bytes.push_back(8);
  bytes.push_back(address);
  append(bytes, value);
}

void bp(std::vector<u8> &bytes, u8 address, u32 value) {
  bytes.push_back(0x61);
  append(bytes, (u32(address) << 24) | (value & 0xffffff));
}

void xf(std::vector<u8> &bytes, u32 address, std::initializer_list<u32> values) {
  CHECK(values.size() > 0 && values.size() <= 16);
  bytes.push_back(0x10);
  append(bytes, (static_cast<u32>(values.size() - 1) << 16) | address);
  for (const auto value : values)
    append(bytes, value);
}

void floats(std::vector<u8> &bytes, u32 address, std::initializer_list<float> values) {
  CHECK(values.size() > 0 && values.size() <= 16);
  bytes.push_back(0x10);
  append(bytes, (static_cast<u32>(values.size() - 1) << 16) | address);
  for (const auto value : values)
    append(bytes, std::bit_cast<u32>(value));
}

std::vector<u8> graphics_commands() {
  std::vector<u8> bytes;
  cp(bytes, 0x50, 1 << 9);       // Direct position.
  cp(bytes, 0x70, 1 | (4 << 1)); // XYZ binary32.
  floats(bytes, 0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
  floats(bytes, 0x1020, {1, 0, 1, 0, 1, 0});
  xf(bytes, 0x1026, {1}); // Orthographic projection.
  xf(bytes, 0x1009, {1}); // One color channel.
  floats(bytes, 0x101a, {8, 8, 1000, 350, 350, 1000});
  bp(bytes, 0, 1 << 4);
  bp(bytes, 0x20, (342 << 12) | 342);
  bp(bytes, 0x21, (357 << 12) | 357);
  bp(bytes, 0x59, 171 | (171 << 10));
  bp(bytes, 0x41, 24); // Color and alpha writes.
  bp(bytes, 0xf6, 4);
  bp(bytes, 0xf7, 14);
  bp(bytes, 0xf3, (7 << 16) | (7 << 19));
  bp(bytes, 0xc0, 0xfffa | (1 << 19)); // Raster RGB passthrough.
  bp(bytes, 0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19));

  const std::array<u32, 4> colors{0xff0000ff, 0x00ff00ff, 0x0000ffff, 0xffffffff};
  for (unsigned quadrant = 0; quadrant < colors.size(); ++quadrant) {
    xf(bytes, 0x100c, {colors[quadrant]});
    const float x = quadrant & 1 ? 0.0f : -1.0f;
    const float y = quadrant & 2 ? 0.0f : -1.0f;
    bytes.insert(bytes.end(), {0x80, 0, 4});
    for (const auto &position : std::array<std::array<float, 2>, 4>{
             std::array<float, 2>{x, y}, {x + 1, y}, {x + 1, y + 1}, {x, y + 1}}) {
      append(bytes, std::bit_cast<u32>(position[0]));
      append(bytes, std::bit_cast<u32>(position[1]));
      append(bytes, std::bit_cast<u32>(-0.5f));
    }
  }

  bp(bytes, 0x49, 0);
  bp(bytes, 0x4a, (15 << 10) | 15);
  bp(bytes, 0x4b, 0x40000 >> 5);
  bp(bytes, 0x4d, 8);                     // Four 64-byte RGBA8 tiles per block row.
  bp(bytes, 0x52, (12 << 3) | (1 << 11)); // Copy texture, then clear the EFB region.

  // Restore the image by sampling the copy. The source EFB was cleared, so this draw must
  // traverse texgen, tiled RAM decoding and the texture input to TEV.
  cp(bytes, 0x60, 1); // Direct texture coordinate zero.
  cp(bytes, 0x70, 1 | (4 << 1) | (1 << 21) | (4 << 22));
  xf(bytes, 0x103f, {1});
  xf(bytes, 0x1040, {5 << 7}); // Tex0 through the existing identity matrix.
  bp(bytes, 0, 1 | (1 << 4));
  bp(bytes, 0x30, 15);
  bp(bytes, 0x31, 15);
  bp(bytes, 0x88, (6 << 20) | (15 << 10) | 15);
  bp(bytes, 0x94, 0x40000 >> 5);
  bp(bytes, 0x28, 64);
  bp(bytes, 0xc0, 0xfff8 | (1 << 19));
  bp(bytes, 0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19));
  bytes.insert(bytes.end(), {0x80, 0, 4});
  for (const auto &vertex : std::array<std::array<float, 4>, 4>{
           std::array<float, 4>{-1, -1, 0, 0}, {1, -1, 1, 0}, {1, 1, 1, 1}, {-1, 1, 0, 1}}) {
    append(bytes, std::bit_cast<u32>(vertex[0]));
    append(bytes, std::bit_cast<u32>(vertex[1]));
    append(bytes, std::bit_cast<u32>(-0.5f));
    append(bytes, std::bit_cast<u32>(vertex[2]));
    append(bytes, std::bit_cast<u32>(vertex[3]));
  }
  cp(bytes, 0x60, 0);
  cp(bytes, 0x70, 1 | (4 << 1));
  xf(bytes, 0x103f, {0});
  bp(bytes, 0, 1 << 4);
  bp(bytes, 0x28, 0);
  bp(bytes, 0xc0, 0xfffa | (1 << 19));
  bp(bytes, 0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19));
  bp(bytes, 0x4d, 1);                      // 32-byte XFB row.
  bp(bytes, 0x53, (63 << 18) | (1 << 12)); // Center-only filter, weight 64.
  bp(bytes, 0x54, 0);
  // Exercise both scale directions through real guest copy triggers before the displayed copy.
  bp(bytes, 0x4b, 0x50000 >> 5);
  bp(bytes, 0x4e, 128);
  bp(bytes, 0x52, (1 << 14) | 3); // Half-height XFB.
  bp(bytes, 0x4b, 0x51000 >> 5);
  bp(bytes, 0x52, (1 << 14) | (1 << 10) | 3); // Inverse scale: 31 readback rows.
  bp(bytes, 0x4b, 0x20000 >> 5);
  bp(bytes, 0x4e, 256);           // Unit scale for the displayed frame and input redraw.
  bp(bytes, 0x52, (1 << 14) | 3); // EFB to RAM XFB, clamped copy edges.
  bytes.resize((bytes.size() + 31) & ~std::size_t{31}); // Flush whole gather bursts.
  return bytes;
}

u32 immediate(unsigned opcode, unsigned destination, unsigned source, u16 value) {
  return (opcode << 26) | (destination << 21) | (source << 16) | value;
}

struct GuestProgram {
  std::vector<u32> instructions;

  void constant(unsigned reg, u32 value) {
    instructions.push_back(immediate(15, reg, 0, static_cast<u16>(value >> 16)));
    instructions.push_back(immediate(24, reg, reg, static_cast<u16>(value)));
  }

  void write(unsigned base, u16 offset, u32 value, unsigned opcode = 36) {
    constant(6, value);
    instructions.push_back(immediate(opcode, 6, base, offset));
  }

  void fifo_pair(u16 offset, u32 value) {
    write(3, offset, value & 0xffff, 44);
    write(3, static_cast<u16>(offset + 2), value >> 16, 44);
  }

  void commands(std::span<const u8> bytes) {
    CHECK(bytes.size() % 32 == 0);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
      const u32 value = (u32(bytes[offset]) << 24) | (u32(bytes[offset + 1]) << 16) |
                        (u32(bytes[offset + 2]) << 8) | bytes[offset + 3];
      write(5, 0, value);
    }
  }

  std::vector<u8> dol() const {
    std::vector<u8> bytes(0x100);
    const auto put = [&](unsigned offset, u32 value) {
      for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<u8>(value >> (24 - i * 8));
    };
    put(0, 0x100);
    put(0x48, 0x80004000);
    put(0x90, static_cast<u32>(instructions.size() * 4));
    put(0xe0, 0x80004000);
    for (const auto instruction : instructions)
      append(bytes, instruction);
    bytes.resize((bytes.size() + 31) & ~std::size_t{31});
    return bytes;
  }
};

std::vector<u8> authored_dol() {
  GuestProgram guest;
  guest.constant(3, 0xcc000000);
  guest.constant(5, 0xcc008000);
  guest.constant(7, 0x80000000);
  guest.write(3, 0x3018, 1); // Reset gather/FIFO.
  guest.write(3, 0x300c, 0x10000);
  guest.write(3, 0x3010, 0x13fe0);
  guest.write(3, 0x3014, 0x10000);
  guest.fifo_pair(0x20, 0x10000);
  guest.fifo_pair(0x24, 0x13fe0);
  guest.fifo_pair(0x34, 0x10000);
  guest.fifo_pair(0x38, 0x10000);
  guest.write(3, 2, 0x11, 44); // Read enable and linked gather.
  const auto bytes = graphics_commands();
  guest.commands(bytes);
  guest.constant(9, 0x80040000);
  const std::array<u16, 8> readback_offsets{0, 32, 128, 160, 512, 544, 640, 672};
  for (unsigned index = 0; index < readback_offsets.size(); ++index) {
    guest.instructions.push_back(immediate(32, 8, 9, readback_offsets[index]));
    guest.instructions.push_back(immediate(36, 8, 7, static_cast<u16>(0x3010 + index * 4)));
  }
  guest.constant(9, 0x80050000);
  const std::array<u16, 8> scaled_offsets{0, 16, 128, 144, 0x11c0, 0x11e0, 0x13c0, 0x13e0};
  for (unsigned index = 0; index < scaled_offsets.size(); ++index) {
    guest.instructions.push_back(immediate(32, 8, 9, scaled_offsets[index]));
    guest.instructions.push_back(immediate(36, 8, 7, static_cast<u16>(0x3030 + index * 4)));
  }

  // A 16-line active image in a 525-half-line NTSC field. Timing advances only in step().
  guest.write(3, 0x2000, (16 << 4) | 6, 44);
  guest.write(3, 0x2004, (71u << 24) | (105u << 16) | 429);
  guest.write(3, 0x200c, (451 << 16) | 24);
  guest.write(3, 0x2010, (451 << 16) | 24);
  guest.write(3, 0x201c, 0x10000000 | (0x20000 >> 5));
  guest.write(3, 0x2024, 0x10000000 | (0x20000 >> 5));
  guest.write(3, 0x2048, 0x0101, 44);
  guest.write(3, 0x206c, 0, 44);    // 27 MHz sample clock.
  guest.write(3, 0x6400, 0x400300); // Controller full analog mode.
  guest.write(3, 0x6438, 0x80000000);
  guest.write(3, 0x6430, 0x10080);
  guest.write(3, 0x2002, 5, 44); // Enable noninterlaced display.
  guest.write(7, 0x3000, 0x56594431);
  guest.instructions.push_back(immediate(32, 8, 3, 0x6404));
  guest.instructions.push_back(immediate(36, 8, 7, 0x3004));
  guest.instructions.push_back(immediate(32, 6, 3, 0x6408));
  guest.instructions.push_back(immediate(36, 6, 7, 0x3008));
  guest.instructions.push_back((21u << 26) | (8u << 21) | (8u << 16) | (16u << 11) | (22u << 6) |
                               (22u << 1) | 1); // Test the B button.
  guest.instructions.push_back((16u << 26) | (4u << 21) | (2u << 16) | 8);
  guest.instructions.push_back((18u << 26) | 0x03ffffe8);

  std::vector<u8> redraw;
  xf(redraw, 0x100c, {0xffff00ff});
  redraw.insert(redraw.end(), {0x80, 0, 4});
  for (const auto &position : std::array<std::array<float, 2>, 4>{
           std::array<float, 2>{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}) {
    append(redraw, std::bit_cast<u32>(position[0]));
    append(redraw, std::bit_cast<u32>(position[1]));
    append(redraw, std::bit_cast<u32>(-0.5f));
  }
  bp(redraw, 0x52, (1 << 14) | 3);
  redraw.resize((redraw.size() + 31) & ~std::size_t{31});
  guest.commands(redraw);
  guest.write(7, 0x300c, 1);
  guest.instructions.push_back((18u << 26));
  return guest.dol();
}

void guest_video(Console console, bool hardware) {
  platform::Output output({true, false, {}, {}});
  runtime::Machine machine(console);
  if (hardware)
    machine.graphics().set_hardware_rendering(true);
  machine.set_output(&output);
  machine.set_limits({20000000, 0, 0});
  machine.load_executable(authored_dol());
  machine.devices().set_pad(0, {true, 0x100, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xd0, 0xe0});
  while (!output.presented_frames())
    machine.step();
  CHECK(output.presented_frames() == 1);
  CHECK(machine.devices().field_count() == 1);
  const u32 halfline_cycles = console == Console::GameCube ? 15444 : 23166;
  CHECK(machine.cpu().state().cycles >= 74u * halfline_cycles);
  CHECK(machine.memory().read32(0x3000) == 0x56594431);
  CHECK(machine.memory().read32(0x3004) == 0x01801234);
  CHECK(machine.memory().read32(0x3008) == 0x56789abc);
  const std::array<u32, 8> texture_words{0xffffffff, 0,          0xff00ff00, 0xff00ff00,
                                         0xff00ff00, 0x00ff00ff, 0xffffffff, 0xffffffff};
  for (unsigned index = 0; index < texture_words.size(); ++index)
    CHECK(machine.memory().read32(0x3010 + index * 4) == texture_words[index]);
  const std::array<u32, 8> scaled_words{0x525a52f0, 0x903f9055, 0x29f0296e, 0xeb9ceb7b,
                                        0x525a52f0, 0x29f0296e, 0x29f0296e, 0};
  for (unsigned index = 0; index < scaled_words.size(); ++index)
    CHECK(machine.memory().read32(0x3030 + index * 4) == scaled_words[index]);
  CHECK(machine.devices().xfb().width == 16 && machine.devices().xfb().height == 16);
  CHECK(machine.devices().xfb().address == 0x20000);
  CHECK(machine.memory().read32(0x20000) == 0x525a52f0);
  CHECK(machine.memory().read32(0x20010) == 0x903f9055);
  CHECK(machine.memory().read32(0x20100) == 0x29f0296e);
  CHECK(machine.memory().read32(0x20110) == 0xeb9ceb7b);
  const auto frame = platform::decode_xfb(machine.memory(), machine.devices().xfb());
  CHECK(frame.rgba.size() == 16 * 16 * 4);
  const std::array<std::array<u8, 4>, 6> expected{{{255, 0, 0, 255},
                                                   {0, 254, 0, 255},
                                                   {0, 0, 255, 255},
                                                   {254, 254, 254, 255},
                                                   {80, 209, 17, 255},
                                                   {246, 248, 255, 255}}};
  for (unsigned y = 0; y < 16; ++y)
    for (unsigned x = 0; x < 16; ++x) {
      const auto &pixel = expected[x < 8    ? (y < 8 ? 0 : 2)
                                   : x < 10 ? (y < 8 ? 4 : 5)
                                            : (y < 8 ? 1 : 3)];
      for (unsigned channel = 0; channel < 4; ++channel)
        CHECK(frame.rgba[(y * 16 + x) * 4 + channel] == pixel[channel]);
    }
  CHECK(output.last_frame_hash() == 0xd1b3bddfb83371c5ull);
  std::ostringstream state;
  machine.write_state(state, "authored-guest-frame");
  CHECK(state.str().find("\"gx_fragments\":512") != std::string::npos);

  // Change host controller state; the guest's ongoing SI loads observe the next real VI poll.
  machine.devices().set_pad(0, {true, 0x200, 0x80, 0x80, 0x80, 0x80, 0, 0, 0, 0});
  while (machine.memory().read32(0x3004) != 0x02808080 ||
         machine.memory().read32(0x3008) != 0x80800000)
    machine.step();
  CHECK(machine.memory().read32(0x3008) == 0x80800000);
  while (output.presented_frames() != 2)
    machine.step();
  CHECK(machine.devices().field_count() == 2);
  CHECK(machine.memory().read32(0x300c) == 1);
  CHECK(machine.memory().read32(0x20000) == 0xd210d292);
  CHECK(output.last_frame_hash() == 0xd083ce55750a7325ull);
  state.str({});
  state.clear();
  machine.write_state(state, "authored-guest-input-redraw");
  CHECK(state.str().find("\"gx_fragments\":768") != std::string::npos);
  if (hardware) {
    const auto stats = machine.graphics().hardware_stats();
    // The texture copy and two XFB copies flush the three ordered draw phases.
    CHECK(stats.dispatches == 3);
    CHECK(stats.fragments == 768);
    CHECK(stats.fallback_rectangles == 0);
    CHECK(machine.graphics().hardware_fallback_reason().empty());
    std::cout << (console == Console::GameCube ? "GameCube" : "Wii")
              << " CPU-to-VI hardware frames: 2, shader fragments: " << stats.fragments
              << ", device: " << machine.graphics().hardware_device() << '\n';
  }
}
} // namespace

int main(int argc, char **argv) {
  bool hardware = false, required = false;
  for (int i = 1; i < argc; ++i) {
    hardware |= std::strcmp(argv[i], "--hardware") == 0;
    required |= std::strcmp(argv[i], "--require-device") == 0;
  }
  try {
    guest_video(Console::GameCube, hardware);
    guest_video(Console::Wii, hardware);
    std::cout << "All checks passed\n";
    return 0;
  } catch (const gfx::HardwareUnavailable &error) {
    std::cerr << "Hardware backend unavailable: " << error.what() << '\n';
    return required ? 1 : 77;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
