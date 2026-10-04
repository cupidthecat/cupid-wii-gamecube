// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/command_processor.hpp"
#include "cupid/gfx/xfb_copy.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {
using namespace cupid;

void word(std::vector<u8> &bytes, u32 value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes.push_back(static_cast<u8>(value >> (24 - i * 8)));
}

struct Fixture {
  Memory memory{Console::GameCube};
  hw::Devices devices{Console::GameCube};
  gfx::CommandProcessor graphics{memory, devices, Console::GameCube};

  void cp(u8 reg, u32 value) {
    std::vector<u8> bytes{8, reg};
    word(bytes, value);
    graphics.submit(bytes);
  }

  void bp(u8 reg, u32 value) {
    std::vector<u8> bytes{0x61};
    word(bytes, (u32(reg) << 24) | (value & 0xffffff));
    graphics.submit(bytes);
  }

  void xf(u32 address, std::initializer_list<u32> words) {
    std::vector<u8> bytes{0x10};
    word(bytes, (u32(words.size() - 1) << 16) | address);
    for (u32 value : words)
      word(bytes, value);
    graphics.submit(bytes);
  }

  void floats(u32 address, std::initializer_list<float> values) {
    std::vector<u8> bytes{0x10};
    word(bytes, (u32(values.size() - 1) << 16) | address);
    for (float value : values)
      word(bytes, std::bit_cast<u32>(value));
    graphics.submit(bytes);
  }

  Fixture() {
    cp(0x50, (1 << 9) | (1 << 13));
    cp(0x70, 1 | (4 << 1) | (5 << 14));
    floats(0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
    floats(0x1020, {1, 0, 1, 0, 1, 0});
    xf(0x1026, {1});
    floats(0x101a, {32, 32, 1000, 374, 374, 1000});
    xf(0x100e, {1});
    bp(0, 1 << 4);
    bp(0x20, (342 << 12) | 342);
    bp(0x21, (405 << 12) | 405);
    bp(0x59, 171 | (171 << 10));
    bp(0x41, 24);
    bp(0xf6, 4);
    bp(0xf7, 14);
    bp(0xf3, (7 << 16) | (7 << 19));
    bp(0xc0, 0xfffa | (1 << 19));
    bp(0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19));
  }

  void draw(u8 opcode, std::span<const std::array<float, 3>> positions,
            std::span<const u32> colors) {
    std::vector<u8> bytes{opcode, static_cast<u8>(positions.size() >> 8),
                          static_cast<u8>(positions.size())};
    for (unsigned i = 0; i < positions.size(); ++i) {
      for (float value : positions[i])
        word(bytes, std::bit_cast<u32>(value));
      word(bytes, colors[i % colors.size()]);
    }
    graphics.submit(bytes);
  }
};

void equal(Fixture &software, Fixture &hardware) {
  CHECK(software.graphics.fragment_count() == hardware.graphics.fragment_count());
  for (unsigned y = 0; y < 64; ++y)
    for (unsigned x = 0; x < 64; ++x) {
      if (software.graphics.color(x, y) != hardware.graphics.color(x, y) ||
          software.graphics.depth(x, y) != hardware.graphics.depth(x, y))
        throw std::runtime_error(
            "Hardware differential mismatch at " + std::to_string(x) + "," + std::to_string(y) +
            " software color=" + std::to_string(software.graphics.color(x, y)) +
            " hardware color=" + std::to_string(hardware.graphics.color(x, y)) +
            " software depth=" + std::to_string(software.graphics.depth(x, y)) +
            " hardware depth=" + std::to_string(hardware.graphics.depth(x, y)));
    }
  for (u32 address = 0x0c001010; address <= 0x0c001016; address += 2)
    CHECK(software.graphics.read(address, 2) == hardware.graphics.read(address, 2));
}

void checks(Fixture &software, Fixture &hardware) {
  const std::array<std::array<float, 3>, 4> quad{
      {{-1, -1, -0.5f}, {1, -1, -0.5f}, {1, 1, -0.5f}, {-1, 1, -0.5f}}};
  const std::array<u32, 4> colors{0x124878d2, 0xf0d0b090, 0x3456ef78, 0x9abcedff};
  const auto bp = [&](u8 reg, u32 value) {
    software.bp(reg, value);
    hardware.bp(reg, value);
  };
  const auto draw = [&](u8 opcode, std::span<const std::array<float, 3>> positions) {
    software.draw(opcode, positions, colors);
    hardware.draw(opcode, positions, colors);
    equal(software, hardware);
  };
  draw(0x80, quad);
  CHECK(hardware.graphics.hardware_stats().dispatches >= 2);
  CHECK(hardware.graphics.hardware_stats().fragments == 4096);
  bp(0x22, 18 | (24 << 8));
  draw(0xa8, quad);
  draw(0xb0, quad);
  draw(0xb8, quad);
  draw(0x98, quad);
  draw(0xa0, quad);
  bp(0, (1 << 4) | (1 << 19));
  draw(0x90, std::span(quad).first(3));
  bp(0, 1 << 4);
  bp(0x43, 1);
  draw(0x80, quad);
  for (u32 logic = 0; logic < 16; ++logic) {
    bp(0x41, 24 | 2 | (logic << 12));
    draw(0x80, quad);
  }
  for (u32 source = 0; source < 8; ++source)
    for (u32 destination = 0; destination < 8; ++destination) {
      bp(0x41, 24 | 1 | (source << 8) | (destination << 5));
      draw(0x80, quad);
    }
  bp(0x41, 24 | 1 | 2048);
  draw(0x80, quad);
  bp(0x41, 24 | 4);
  draw(0x80, quad);
  bp(0x41, 16);
  bp(0x42, 256 | 91);
  draw(0x80, quad);
  bp(0x42, 0);
  bp(0x41, 24);
  std::mt19937 generator(0x457b291);
  for (unsigned test = 0; test < 96; ++test) {
    const unsigned stages = 1 + generator() % 4;
    bp(0, (1 << 4) | ((stages - 1) << 10));
    for (unsigned i = 0; i < 8; ++i)
      bp(static_cast<u8>(0xe0 + i), generator() & 0x7fffff);
    for (unsigned stage = 0; stage < stages; ++stage) {
      u32 cc = generator() & 0xffffff, ac = generator() & 0xffffff;
      for (unsigned operand = 0; operand < 4; ++operand) {
        unsigned color = (cc >> (operand * 4)) & 15;
        if (color == 8 || color == 9)
          cc = (cc & ~(15u << (operand * 4))) | (10u << (operand * 4));
        if (((ac >> (4 + operand * 3)) & 7) == 4)
          ac = (ac & ~(7u << (4 + operand * 3))) | (5u << (4 + operand * 3));
      }
      bp(static_cast<u8>(0xc0 + stage * 2), cc);
      bp(static_cast<u8>(0xc1 + stage * 2), ac & ~15u);
    }
    bp(0xf3, generator() & 0xffffff);
    bp(0x40, generator() & 31);
    bp(0x43, (generator() & 1) | ((generator() & 1) << 6));
    const std::array<std::array<float, 3>, 3> triangle{
        {{-1.2f, -0.85f, -0.4f}, {0.7f, -1.2f, -0.6f}, {0.9f, 0.8f, -0.3f}}};
    draw(0x90, triangle);
  }
  // Synchronization preserves raw EFB interpretation, CPU pokes, and clear/copy ordering.
  for (auto *fixture : {&software, &hardware}) {
    fixture->graphics.write_efb(0x08000000, 0x7f123456);
    fixture->graphics.write_efb(0x08400000, 0xab123456);
  }
  equal(software, hardware);
  bp(0, 1 << 4);
  bp(0xc0, 0xfffa | (1 << 19));
  bp(0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19));
  bp(0x40, 31);
  bp(0xf3, (7 << 16) | (7 << 19));
  bp(0x49, 0);
  bp(0x4a, 63 | (63 << 10));
  bp(0x4d, 4);
  bp(0x4e, 256);
  bp(0x4b, 0x10000 >> 5);
  bp(0x4f, 0x1234);
  bp(0x50, 0xabcd);
  bp(0x51, 0x654321);
  bp(0x52, (1 << 14) | (1 << 11));
  CHECK(std::equal(software.memory.range(0x10000, 64 * 128).begin(),
                   software.memory.range(0x10000, 64 * 128).end(),
                   hardware.memory.range(0x10000, 64 * 128).begin()));
  equal(software, hardware);
  draw(0x80, quad);
  // Exponential fog executes in hardware and preserves the CPU's final factor.
  bp(0xf1, 4 << 21);
  const auto before_exponential = hardware.graphics.hardware_stats().dispatches;
  draw(0x80, quad);
  CHECK(hardware.graphics.hardware_stats().dispatches > before_exponential);
  CHECK(hardware.graphics.hardware_fallback_reason().empty());
  bp(0xf1, 0);
  draw(0x80, quad);
  CHECK(hardware.graphics.hardware_fallback_reason().empty());
  const auto fog_word = [](float value) {
    const u32 bits = std::bit_cast<u32>(value);
    return ((bits >> 12) & 0x7ff) | (((bits >> 23) & 255) << 11) | ((bits >> 12) & 0x80000);
  };
  for (unsigned i = 0; i < 1000; ++i) {
    const float a = float(generator() % 10000) / 1000.0f;
    const float c = float(generator() % 4000) / 1000.0f;
    bp(0xee, fog_word(a));
    bp(0xf1, fog_word(c) | (2 << 21) | ((i & 1) << 20));
    bp(0xef, 16777215);
    bp(0xf0, i % 16);
    bp(0xf2, generator() & 0xffffff);
    const auto before = hardware.graphics.hardware_stats().dispatches;
    const std::array<std::array<float, 3>, 3> triangle{
        {{-1.0f, -1.0f, -0.99f}, {0.9f, -0.8f, -0.01f}, {0.8f, 0.95f, -0.31f}}};
    draw(0x90, triangle);
    CHECK(hardware.graphics.hardware_stats().dispatches > before);
  }
  bp(0xf1, 0);
  // Every implemented tiled texture encoding feeds integer GPU wrapping/filtering.
  std::vector<u8> texture_bytes(16384), palette_bytes(32768);
  for (auto &byte : texture_bytes)
    byte = static_cast<u8>(generator());
  for (auto &byte : palette_bytes)
    byte = static_cast<u8>(generator());
  for (auto *fixture : {&software, &hardware}) {
    fixture->memory.copy_in(0x30000, texture_bytes);
    fixture->memory.copy_in(0x40000, palette_bytes);
    fixture->bp(0x64, 0x40000 >> 5);
    fixture->bp(0x65, 1024 << 10);
    fixture->xf(0x1018, {3 << 6});
    fixture->xf(0x103f, {1});
    fixture->xf(0x1040, {0});
    fixture->floats(12, {1, 0, 0, 0.5f, 0, 1, 0, 0.5f, 0, 0, 0, 1});
  }
  bp(0, 1 | (1 << 4));
  bp(0x30, 7);
  bp(0x31, 7);
  bp(0x28, 64);
  bp(0xc0, 0xfff8 | (1 << 19));
  bp(0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19));
  bp(0x40, 0);
  bp(0x43, 1);
  bp(0x41, 24);
  bp(0x94, 0x30000 >> 5);
  bp(0x98, 1 << 10);
  for (unsigned format : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 8u, 9u, 10u, 14u}) {
    bp(0x88, 7 | (7 << 10) | (format << 20));
    for (unsigned filter : {0u, 16u, 128u | (1u << 5), 16u | 128u | (2u << 5)})
      for (unsigned wrap = 0; wrap < 3; ++wrap) {
        bp(0x80, filter | wrap | (((wrap + 1) % 3) << 2));
        bp(0x84, (4 << 4) << 8);
        const auto before = hardware.graphics.hardware_stats().dispatches;
        draw(0x80, quad);
        CHECK(hardware.graphics.hardware_stats().dispatches > before);
        CHECK(hardware.graphics.hardware_fallback_reason().empty());
      }
    // A RAM write invalidates cached pixels while unchanged state reuses them.
    for (auto *fixture : {&software, &hardware})
      fixture->memory.write8(0x30000, static_cast<u8>(format * 17));
    draw(0x80, quad);
  }
  // A sampled register survives a subsequent unsupported draw that retains it.
  bp(0x28, 0);
  draw(0x80, quad);
  CHECK(!hardware.graphics.hardware_fallback_reason().empty());
  bp(0x28, 64);
  draw(0x80, quad);
  // Split-bank manually preloaded RGBA8 shares the same decoded GPU sampling path.
  bp(0x60, 0x30000 >> 5);
  bp(0x61, 0);
  bp(0x62, 0x10000 >> 5);
  bp(0x63, (3 << 15) | 4);
  bp(0x88, 7 | (7 << 10) | (6 << 20));
  bp(0x8c, 1 << 21);
  bp(0x90, 0x10000 >> 5);
  bp(0x80, 16 | (2 << 2));
  bp(0x84, 0);
  draw(0x80, quad);
  CHECK(hardware.graphics.hardware_fallback_reason().empty());
  bp(0x8c, 0);
  bp(0x90, 0);
  // Depth textures execute after alpha and before late depth; early depth keeps
  // the unmodified raster depth while the shader still evaluates texture depth.
  bp(0x40, 1 | 14 | 16);
  for (unsigned encoding = 0; encoding < 3; ++encoding)
    for (unsigned operation = 1; operation < 3; ++operation)
      for (unsigned early : {0u, 64u}) {
        bp(0xf4, 0x153729);
        bp(0xf5, encoding | (operation << 2));
        bp(0x43, 1 | early);
        draw(0x80, quad);
      }
  bp(0xf5, 0);
  bp(0x40, 0);
  for (bool accelerated : {false, true}) {
    Fixture invalid;
    if (accelerated)
      invalid.graphics.set_hardware_rendering(true);
    invalid.bp(0xee, 0);
    invalid.bp(0xef, 500);
    invalid.bp(0xf0, 0);
    invalid.bp(0xf1, 2 << 21);
    CHECK_THROWS(invalid.draw(0x80, quad, colors));
    if (accelerated)
      CHECK(!invalid.graphics.hardware_fallback_reason().empty());
  }
  bp(0x28, 0);
  bp(0xc0, 0xfffa | (1 << 19));
  bp(0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19));
  bp(0xf5, 2 | (2 << 2));
  bp(0x40, 1 | 14 | 16);
  const auto before_retained_depth = hardware.graphics.hardware_stats();
  draw(0x80, quad);
  CHECK(hardware.graphics.hardware_stats().dispatches == before_retained_depth.dispatches);
  CHECK(hardware.graphics.hardware_stats().fallback_rectangles >
        before_retained_depth.fallback_rectangles);
  CHECK(!hardware.graphics.hardware_fallback_reason().empty());
  bp(0xf5, 0);
  bp(0x40, 0);
  std::cout << "Hardware device: " << hardware.graphics.hardware_device() << '\n';
  std::cout << "Verified dispatches: " << hardware.graphics.hardware_stats().dispatches
            << ", hardware fragments: " << hardware.graphics.hardware_stats().fragments << '\n';
}

void indirect_checks(Fixture &software, Fixture &hardware) {
  const std::array<std::array<float, 3>, 4> quad{
      {{-1, -1, -0.5f}, {1, -1, -0.5f}, {1, 1, -0.5f}, {-1, 1, -0.5f}}};
  const std::array<u32, 4> colors{0x124878d2, 0xf0d0b090, 0x3456ef78, 0x9abcedff};
  const auto bp = [&](u8 reg, u32 value) {
    software.bp(reg, value);
    hardware.bp(reg, value);
  };
  const auto submit = [&] {
    software.draw(0x80, quad, colors);
    hardware.draw(0x80, quad, colors);
  };
  const auto draw = [&] {
    const auto before = hardware.graphics.hardware_stats().dispatches;
    submit();
    equal(software, hardware);
    CHECK(hardware.graphics.hardware_stats().dispatches > before);
    CHECK(hardware.graphics.hardware_fallback_reason().empty());
  };
  std::mt19937 generator(0x318429ba);
  std::vector<u8> pixels(32768);
  for (unsigned unit = 0; unit < 8; ++unit) {
    for (auto &value : pixels)
      value = static_cast<u8>(generator());
    for (auto *fixture : {&software, &hardware})
      fixture->memory.copy_in(0x80000 + unit * 32768, pixels);
    const auto address = static_cast<u8>(0x80 + (unit & 3) + ((unit & 4) << 3));
    bp(static_cast<u8>(address + 8), 7 | (7 << 10) |
                                         ((unit % 3 == 0   ? 6
                                           : unit % 3 == 1 ? 5
                                                           : 14)
                                          << 20));
    bp(static_cast<u8>(address + 12), 0);
    bp(static_cast<u8>(address + 16), 0);
    bp(static_cast<u8>(address + 20), (0x80000 + unit * 32768) >> 5);
  }
  for (auto *fixture : {&software, &hardware}) {
    fixture->xf(0x1018, {(3 << 6) | (6 << 12)});
    fixture->xf(0x103f, {2});
    fixture->xf(0x1040, {0, 0});
    fixture->floats(12, {1, 0, 0, 0.5f, 0, 1, 0, 0.5f, 0, 0, 0, 1});
    fixture->floats(24, {0, -2, 0, 0.3f, 3, 0, 0, -0.7f, 0, 0, 0, 1});
  }
  bp(0x30, 7);
  bp(0x31, 7);
  bp(0x32, 7);
  bp(0x33, 7);
  bp(0xf1, 0);
  bp(0xf5, 0);
  bp(0x40, 0);
  bp(0x41, 24);
  bp(0x43, 1);
  for (unsigned test = 0; test < 1024; ++test) {
    const unsigned stages = 1 + generator() % 16, maps = 1 + test % 4;
    bp(0, 2 | (1 << 4) | ((stages - 1) << 10) | (maps << 16));
    u32 orders = 0;
    for (unsigned map = 0; map < 4; ++map)
      orders |= ((1 + map) | ((generator() % 8) << 3)) << (map * 6);
    bp(0x27, orders);
    bp(0x25, generator() & 0xffff);
    bp(0x26, generator() & 0xffff);
    for (unsigned unit = 0; unit < 8; ++unit) {
      const auto address = static_cast<u8>(0x80 + (unit & 3) + ((unit & 4) << 3));
      bp(address, (generator() % 3) | ((generator() % 3) << 2) | (generator() & 400) |
                      ((generator() % 3) << 5) | ((generator() % 256) << 9));
      bp(static_cast<u8>(address + 4), (test % 5 == 0 ? 32 : 0) | (64 << 8));
    }
    for (unsigned matrix = 0; matrix < 3; ++matrix) {
      const unsigned scale = (test + matrix * 7) % 32;
      for (unsigned column = 0; column < 3; ++column) {
        const u32 coefficients = generator() & 0x3fffff;
        bp(static_cast<u8>(6 + matrix * 3 + column),
           coefficients | (((scale >> (column * 2)) & (column == 2 ? 1u : 3u)) << 22));
      }
    }
    for (unsigned stage = 0; stage < stages; ++stage) {
      const unsigned matrix = (test + stage) % 4;
      const u32 indirect = generator() % maps | ((generator() % 4) << 2) |
                           ((generator() % 8) << 4) | ((generator() % 4) << 7) | (matrix << 9) |
                           ((matrix ? generator() % 3 : 0) << 11) | ((generator() % 7) << 13) |
                           ((generator() % 7) << 16) | ((generator() & 1) << 19) |
                           ((stage ? generator() & 1 : 0) << 20);
      bp(static_cast<u8>(0x10 + stage), indirect);
      const unsigned channel = (test + stage) % 4;
      const u32 order = (generator() % 8) | ((generator() % 8) << 3) | 64 |
                        ((channel < 2 ? channel : channel + 3) << 7);
      const auto address = static_cast<u8>(0x28 + stage / 2);
      if ((stage & 1) == 0)
        bp(address, order);
      else {
        // Keep the first stage's order while setting the second packed order.
        const auto first = software.graphics.bp_state(address) & 0xfff;
        bp(address, first | (order << 12));
      }
      const u32 cc = (test % 3 == 0 ? 0xfffa : test % 3 == 1 ? 0xfff8 : 0x08fa) | (1 << 19);
      bp(static_cast<u8>(0xc0 + stage * 2), cc);
      bp(static_cast<u8>(0xc1 + stage * 2), (7 << 13) | (7 << 10) | (7 << 7) |
                                                ((test % 3 == 0 ? 5 : 4) << 4) |
                                                (generator() & 15) | (1 << 19));
    }
    bp(0xf3, test % 11 == 0 ? 0 : (7 << 16) | (7 << 19));
    if (test % 17 == 0)
      for (auto *fixture : {&software, &hardware}) {
        const float magnitude = (test & 1) ? 900000.0f : -900000.0f;
        fixture->floats(12, {magnitude, 0, 0, -5, 0, -magnitude, 0, 7, 0, 0, 0, 1});
      }
    else if (test % 17 == 1)
      for (auto *fixture : {&software, &hardware})
        fixture->floats(12, {1, 0, 0, 0.5f, 0, 1, 0, 0.5f, 0, 0, 0, 1});
    try {
      draw();
    } catch (const std::exception &error) {
      throw std::runtime_error("Indirect generated profile " + std::to_string(test) + ": " +
                               error.what());
    }
  }
  bp(0xf3, (7 << 16) | (7 << 19));
  bp(0, 2 | (1 << 4) | (4 << 16));
  bp(0x28, 64);
  bp(0xc0, 0xfff8 | (1 << 19));
  bp(0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19));
  bp(0x10, 3 | (1 << 9));
  submit();
  // A later pending draw only updates map zero. The retained map-three sample
  // from the earlier draw must survive until the subsequent CPU fallback.
  bp(0, 2 | (1 << 4) | (1 << 16));
  bp(0x10, 0);
  submit();
  bp(0, 2 | (1 << 4));
  bp(0x10, 3 | (1 << 9));
  submit();
  equal(software, hardware);
  CHECK(!hardware.graphics.hardware_fallback_reason().empty());
  // First-stage add-previous depends on the previous fragment's final
  // coordinates, so it stays on the ordered CPU path after synchronization.
  bp(0x10, 1 << 20);
  submit();
  equal(software, hardware);
  CHECK(!hardware.graphics.hardware_fallback_reason().empty());
  bp(0x10, 0);
  draw();
  for (u32 invalid : {7u << 13, 7u << 16, 1u << 11, (1u << 9) | (3u << 11)}) {
    Fixture invalid_software, invalid_hardware;
    invalid_hardware.graphics.set_hardware_rendering(true);
    invalid_software.bp(0x10, invalid);
    invalid_hardware.bp(0x10, invalid);
    CHECK_THROWS(invalid_software.draw(0x80, quad, colors));
    CHECK_THROWS(invalid_hardware.draw(0x80, quad, colors));
    CHECK(invalid_hardware.graphics.hardware_stats().dispatches == 0);
    CHECK(invalid_hardware.graphics.hardware_stats().fallback_rectangles > 0);
    CHECK(!invalid_hardware.graphics.hardware_fallback_reason().empty());
  }
  bp(0x10, 0);
  draw();
  const auto stats = hardware.graphics.hardware_stats();
  std::cout
      << "Verified 1,024 generated indirect profiles and retained-state fallback boundaries\n";
  std::cout << "Total verified dispatches: " << stats.dispatches
            << ", hardware fragments: " << stats.fragments << '\n';
}

void fog_checks() {
  Fixture software, hardware;
  hardware.graphics.set_hardware_rendering(true);
  const auto bp = [&](u8 reg, u32 value) {
    software.bp(reg, value);
    hardware.bp(reg, value);
  };
  const auto fog_word = [](float value) {
    const u32 bits = std::bit_cast<u32>(value);
    return ((bits >> 12) & 0x7ff) | (((bits >> 23) & 255) << 11) | ((bits >> 12) & 0x80000);
  };
  const std::array<std::array<float, 3>, 3> triangle{
      {{-1.0f, -1.0f, -0.99f}, {0.9f, -0.8f, -0.01f}, {0.8f, 0.95f, -0.31f}}};
  const std::array<u32, 3> colors{0x124878d2, 0xf0d0b090, 0x3456ef78};
  for (auto *fixture : {&software, &hardware})
    fixture->floats(0x101a, {32, 32, 16777215, 374, 374, 16777215});
  std::mt19937 generator(0x77fab389);
  for (unsigned profile = 0; profile < 2048; ++profile) {
    const unsigned type = 1 + profile % 7;
    const bool range = (profile / 7) & 1;
    bp(0xee, fog_word(float(int(generator() % 9217) - 1024) / 4096));
    bp(0xef, 16777215);
    bp(0xf0, generator() % 24);
    bp(0xf1, fog_word(float(int(generator() % 8193) - 2048) / 4096) | (type << 21) |
                 (((profile / 14) & 1) << 20));
    bp(0xf2, generator() & 0xffffff);
    bp(0xe8, (profile % 19 == 0 ? 342 : generator() % 1024) | (range ? 1024 : 0));
    for (unsigned i = 0; i < 5; ++i)
      bp(static_cast<u8>(0xe9 + i), (1 + generator() % 4095) | ((1 + generator() % 4095) << 12));
    if (profile % 29 == 0) {
      bp(0xee, 0x7f800 | ((generator() & 1) << 19));
      bp(0xf1, 0x7f800 | ((generator() & 1) << 19) | (type << 21) | (1 << 20));
    }
    const auto before = hardware.graphics.hardware_stats();
    software.draw(0x90, triangle, colors);
    hardware.draw(0x90, triangle, colors);
    try {
      equal(software, hardware);
    } catch (const std::exception &error) {
      throw std::runtime_error("Fog profile " + std::to_string(profile) +
                               " type=" + std::to_string(type) + " range=" + std::to_string(range) +
                               ": " + error.what());
    }
    CHECK(hardware.graphics.hardware_stats().dispatches > before.dispatches);
    CHECK(hardware.graphics.hardware_stats().fallback_rectangles == before.fallback_rectangles);
    CHECK(hardware.graphics.hardware_fallback_reason().empty());
  }
  // Unused zero table entries stay on the CPU. Valid draws then resume on the
  // GPU after the exact CPU EFB and bounding-box state have been synchronized.
  bp(0xee, fog_word(1));
  bp(0xf1, (2 << 21) | (1 << 20));
  bp(0xe8, 1023 | 1024);
  for (unsigned i = 0; i < 5; ++i)
    bp(static_cast<u8>(0xe9 + i), i == 0 ? (64 << 12) : 0);
  const auto before_fallback = hardware.graphics.hardware_stats();
  software.draw(0x90, triangle, colors);
  hardware.draw(0x90, triangle, colors);
  equal(software, hardware);
  CHECK(hardware.graphics.hardware_stats().dispatches == before_fallback.dispatches);
  CHECK(hardware.graphics.hardware_stats().fallback_rectangles >
        before_fallback.fallback_rectangles);
  CHECK(!hardware.graphics.hardware_fallback_reason().empty());
  bp(0xe8, 0);
  software.draw(0x90, triangle, colors);
  hardware.draw(0x90, triangle, colors);
  equal(software, hardware);
  CHECK(hardware.graphics.hardware_fallback_reason().empty());
  std::cout
      << "Verified 2,048 generated fog profiles, range-table endpoints and fallback ordering\n";
  std::cout << "Fog fixture dispatches: " << hardware.graphics.hardware_stats().dispatches
            << ", hardware fragments: " << hardware.graphics.hardware_stats().fragments << '\n';
}

void fog_bucket_checks() {
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  gfx::FragmentShader software(memory, bp, colors, konst, tmem);
  bp[0] = 1 << 4;
  bp[0x41] = 24;
  bp[0xc0] = 0xfffa | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xee] = 0x3f800;
  bp[0xf2] = 0x0771e5;
  std::vector<u32> pixels(640 * 528), depths(pixels.size()), expected(pixels.size());
  renderer.upload(pixels, depths);
  gfx::FragmentInputs input;
  input.colors[0] = {255, 173, 67, 203};
  unsigned cases = 0;
  for (unsigned type = 4; type < 8; ++type) {
    bp[0xf1] = (type << 21) | (1 << 20);
    CHECK(renderer.begin(bp, colors, konst));
    gfx::RasterWork work{};
    work.edges.fill({0, 0, 1});
    for (unsigned c = 0; c < 4; ++c)
      work.colors[0][c].value = input.colors[0][c];
    unsigned index = 0;
    for (unsigned bucket = 1; bucket <= 256; ++bucket) {
      // Analytic double-precision crossings locate adjacent 24-bit depths.
      // The expected pixels come from the actual CPU fragment implementation.
      const double magnitude =
          -std::log2(type < 6 ? 1 - double(bucket) / 256 : double(bucket) / 256) / 8;
      double crossing = type & 1 ? std::sqrt(magnitude) : magnitude;
      if (type >= 6)
        crossing = 1 - crossing;
      if (!std::isfinite(crossing) || crossing < 0 || crossing > 1)
        continue;
      const s64 center = static_cast<s64>(std::llround(crossing * 16777215));
      for (s64 delta = -4; delta <= 4; ++delta) {
        u32 depth = static_cast<u32>(std::clamp<s64>(center + delta, 0, 16777215));
        work.left = int(index % 640);
        work.top = int(index / 640);
        work.right = work.left + 1;
        work.bottom = work.top + 1;
        work.depth.value = float(depth);
        const auto result = software.shade(input, &depth);
        CHECK(result.has_value());
        expected[std::size_t(work.top) * 640 + work.left] =
            (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
        CHECK(renderer.draw(work));
        ++index;
        ++cases;
      }
    }
    const auto stats = renderer.synchronize(pixels, depths);
    CHECK(stats.fallback_rectangles == 0);
    CHECK(stats.fragments == index);
    CHECK(pixels == expected);
    CHECK(std::all_of(depths.begin(), depths.end(), [](u32 value) { return value == 0; }));
  }
  bp[0xf1] = (2 << 21) | (1 << 20);
  bp[0xe8] = 342 | 1024;
  CHECK(renderer.begin(bp, colors, konst));
  gfx::RasterWork invalid{};
  invalid.left = invalid.top = 0;
  invalid.right = invalid.bottom = 1;
  invalid.edges.fill({0, 0, 1});
  CHECK(!renderer.draw(invalid));
  u32 invalid_depth = 0;
  CHECK_THROWS(software.shade(input, &invalid_depth));
  for (unsigned i = 0; i < 5; ++i)
    bp[0xe9 + i] = 64 | (64 << 12);
  CHECK(renderer.begin(bp, colors, konst));
  invalid.viewport_width = input.viewport_width = 0;
  CHECK(!renderer.draw(invalid));
  CHECK_THROWS(software.shade(input, &invalid_depth));
  std::cout << "Verified " << cases << " exponential fog bucket-boundary pixels without fallback\n";
}

void indirect_state_checks() {
  Memory memory{Console::GameCube};
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  std::vector<u8> texture_memory(1024 * 1024);
  gfx::FragmentShader software(memory, bp, colors, konst, texture_memory);
  gfx::FragmentShader hardware(memory, bp, colors, konst, texture_memory);
  gfx::HardwareRenderer renderer(memory, texture_memory);
  std::vector<u32> cpu_colors(640 * 528), cpu_depths(640 * 528, 500);
  auto gpu_colors = cpu_colors, gpu_depths = cpu_depths;
  std::mt19937 generator(0x4928301);
  std::array<u8, 256> pixels{};
  for (unsigned unit = 0; unit < 5; ++unit) {
    for (auto &byte : pixels)
      byte = static_cast<u8>(generator());
    memory.copy_in(0x80000 + unit * 1024, pixels);
    const unsigned address = 0x80 + (unit & 3) + ((unit & 4) << 3);
    bp[address] = 1 | (1 << 2) | 16;
    bp[address + 8] = 7 | (7 << 10) | (6 << 20);
    bp[address + 20] = (0x80000 + unit * 1024) >> 5;
  }
  bp[0x27] = 1 | (2 << 6) | (3 << 12) | (4 << 18);
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0x41] = 24;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[6] = 513 | (127 << 11) | (1 << 22);
  bp[7] = 65 | (171 << 11) | (3 << 22);
  bp[8] = 8 | (768 << 11) | (1 << 22);
  gfx::RasterWork work{};
  work.left = 3;
  work.top = 5;
  work.right = 13;
  work.bottom = 15;
  work.edges.fill({0, 0, 1});
  work.depth.value = 100;
  work.depth.dx = 5;
  work.depth.dy = 3;
  work.reciprocal_w.value = 1;
  for (auto &color : work.colors)
    for (auto &plane : color) {
      plane.value = 96;
      plane.dx = 2;
      plane.dy = 1;
    }
  work.textures[0][0].value = -4;
  work.textures[0][0].dx = 0.75f;
  work.textures[0][0].dy = 1.25f;
  work.textures[0][1].value = 7;
  work.textures[0][1].dx = -0.5f;
  work.textures[0][1].dy = 0.875f;
  work.textures[1] = work.textures[0];
  work.textures[1][0].value = 3;
  work.textures[1][1].dx = 1.5f;
  u64 hardware_fragments = 0;
  const auto synchronize = [&] {
    const auto stats = renderer.synchronize(gpu_colors, gpu_depths, &hardware);
    hardware_fragments += stats.fragments;
    return stats;
  };
  const auto shade = [&](gfx::FragmentShader &shader, std::vector<u32> &efb,
                         std::vector<u32> &depths) {
    for (int block_y = work.top & ~1; block_y < work.bottom; block_y += 2)
      for (int block_x = work.left & ~1; block_x < work.right; block_x += 2)
        for (int iy = 0; iy < 2; ++iy)
          for (int ix = 0; ix < 2; ++ix) {
            const int x = block_x + ix, y = block_y + iy;
            if (x < work.left || x >= work.right || y < work.top || y >= work.bottom)
              continue;
            const unsigned offset = static_cast<unsigned>(y * 640 + x);
            u32 depth = static_cast<u32>(work.depth.at(x, y));
            const auto depth_test = [&] {
              if (!(bp[0x40] & 1))
                return true;
              if (!gfx::compare((bp[0x40] >> 1) & 7, depth, depths[offset]))
                return false;
              if (bp[0x40] & 16)
                depths[offset] = depth;
              return true;
            };
            const bool early = (bp[0x43] & 64) != 0;
            if (early && !depth_test())
              continue;
            gfx::FragmentInputs inputs;
            for (unsigned channel = 0; channel < 4; ++channel)
              inputs.colors[0][channel] = static_cast<u8>(work.colors[0][channel].at(x, y));
            for (unsigned coordinate = 0; coordinate < (bp[0] & 15); ++coordinate)
              for (unsigned component = 0; component < 2; ++component) {
                const auto &plane = work.textures[coordinate][component];
                inputs.uv[coordinate][component] = plane.at(x, y);
                inputs.dx[coordinate][component] =
                    plane.at(block_x + 1, block_y) - plane.at(block_x, block_y);
                inputs.dy[coordinate][component] =
                    plane.at(block_x, block_y + 1) - plane.at(block_x, block_y);
              }
            const auto result = shader.shade(inputs, &depth);
            if (!result || (!early && !depth_test()))
              continue;
            efb[offset] = (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
          }
  };
  const auto submit = [&](bool expect_hardware = true) {
    const bool supported = renderer.begin(bp, colors, konst);
    CHECK(supported == expect_hardware);
    if (supported)
      CHECK(renderer.draw(work));
    else {
      synchronize();
      shade(hardware, gpu_colors, gpu_depths);
    }
    shade(software, cpu_colors, cpu_depths);
  };
  const auto equal_state = [&] {
    synchronize();
    CHECK(software.texture_state() == hardware.texture_state());
    CHECK(cpu_colors == gpu_colors);
    CHECK(cpu_depths == gpu_depths);
  };
  renderer.upload(gpu_colors, gpu_depths);
  bp[0] = 2 | (1 << 4) | (4 << 16);
  bp[0x28] = 64;
  bp[0x10] = 3 | (1 << 9) | (7 << 4) | (3 << 7);
  submit();
  // An XFB copy uses transient work storage while queued texture/map latches
  // remain available to the next draw and later CPU continuation.
  auto copy_bp = bp;
  copy_bp[0x49] = 0;
  copy_bp[0x4a] = 15 | (15 << 10);
  copy_bp[0x4b] = 0x10000 >> 5;
  copy_bp[0x4d] = 1;
  copy_bp[0x4e] = 256;
  copy_bp[0x52] = (1 << 14) | 3;
  copy_bp[0x53] = (1 << 12) | (63 << 18);
  const auto layout = gfx::copy_efb_xfb(memory, copy_bp, [&](unsigned x, unsigned y) {
    return (cpu_colors[y * 640 + x] << 8) | 255;
  });
  const auto bytes = memory.range(layout.destination, layout.height * layout.stride);
  const std::vector<u8> expected_copy(bytes.begin(), bytes.end());
  CHECK(renderer.copy_xfb(copy_bp));
  CHECK(std::equal(bytes.begin(), bytes.end(), expected_copy.begin()));
  // Queue a disabled direct stage and only one indirect stage before reading
  // back. Unwritten texture/map slots must keep the older dispatch's samples.
  bp[0] = 2 | (1 << 4) | (1 << 16);
  bp[0x28] = 0;
  bp[0x10] = 0;
  bp[0xc0] = 0xfffa | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
  work.textures[0][0].value += 9;
  submit();
  equal_state();
  const auto retained = software.texture_state();
  bp[0] = 2 | (1 << 4) | (4 << 16);
  bp[0x28] = 64;
  bp[0x10] = 3 | (1 << 9) | (1 << 7);
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[0xf3] = 0;
  work.textures[0][0].value += 3;
  submit();
  equal_state();
  CHECK(software.texture_state() != retained);
  const auto alpha_rejected = software.texture_state();
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0x40] = 1;
  bp[0x43] = 64;
  work.textures[0][0].value += 4;
  submit();
  equal_state();
  CHECK(software.texture_state() == alpha_rejected);
  bp[0x40] = 1 | 2 | 16;
  for (int y = work.top; y < work.bottom; ++y)
    for (int x = work.left; x < work.right; ++x)
      cpu_depths[static_cast<unsigned>(y * 640 + x)] = (x + y) % 3 == 0 ? 50 : 500;
  gpu_depths = cpu_depths;
  renderer.upload(gpu_colors, gpu_depths);
  submit();
  equal_state();
  CHECK(software.texture_state() != alpha_rejected);
  const auto early_partial = software.texture_state();
  bp[0x40] = 1;
  bp[0x43] = 0;
  work.textures[0][0].value += 5;
  submit();
  equal_state();
  CHECK(software.texture_state() != early_partial);
  // Disabled retained-map sampling and cross-fragment accumulation each
  // continue on the CPU with the complete GPU latch restored.
  bp[0] = 2 | (1 << 4);
  bp[0x40] = 0;
  bp[0x10] = 3 | (1 << 9) | (3 << 7);
  submit(false);
  equal_state();
  bp[0x10] = 1 << 20;
  submit(false);
  equal_state();
  std::cout << "Verified full texture/map/coordinate latches across disabled stages and "
               "alpha/early/late depth rejection\n";
  std::cout << "Latch fixture dispatches: " << synchronize().dispatches
            << ", hardware fragments: " << hardware_fragments << '\n';
}

void texture_cache_checks(bool measure) {
  Memory memory{Console::GameCube};
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  gfx::HardwareRenderer renderer(memory, tmem);
  gfx::FragmentShader software(memory, bp, colors, konst, tmem);
  std::vector<u32> pixels(640 * 528), depths(pixels.size()), expected(pixels.size());
  renderer.upload(pixels, depths);
  bp[0] = 1;
  bp[0x28] = 64;
  bp[0x41] = 24;
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[0x88] = 255 | (255 << 10) | (9 << 20);
  bp[0x94] = 0x80000 >> 5;
  bp[0x98] = 9;
  std::vector<u8> source(256 * 256);
  for (unsigned i = 0; i < source.size(); ++i)
    source[i] = u8((i * 29) ^ (i >> 8));
  memory.copy_in(0x80000, source);
  for (unsigned i = 0; i < 256; ++i) {
    tmem[9 * 512 + i * 2] = u8(255 - i);
    tmem[9 * 512 + i * 2 + 1] = u8(i);
  }
  gfx::RasterWork work{};
  work.edges.fill({0, 0, 1});
  work.textures[0][0].value = 7.25f;
  work.textures[0][1].value = 3.75f;
  work.textures[0][2].value = 0;
  unsigned index = 0;
  const auto submit = [&] {
    work.left = int(index % 640);
    work.top = int(index / 640);
    work.right = work.left + 1;
    work.bottom = work.top + 1;
    CHECK(renderer.begin(bp, colors, konst));
    CHECK(renderer.draw(work));
    gfx::FragmentInputs input;
    input.uv[0] = {work.textures[0][0].value, work.textures[0][1].value};
    u32 depth = 0;
    const auto result = software.shade(input, &depth);
    CHECK(result.has_value());
    expected[index++] = (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
  };
  const auto verify = [&] {
    const auto stats = renderer.synchronize(pixels, depths);
    CHECK(stats.fallback_rectangles == 0);
    CHECK(pixels == expected);
    CHECK(std::all_of(depths.begin(), depths.end(), [](u32 value) { return value == 0; }));
    return stats;
  };
  submit();
  CHECK(verify().texture_decodes == 1);
  CHECK(verify().texture_uploads == 1);
  // Every mode update reaches the shader. Decoder reuse must still invalidate
  // changed RAM or palette bytes, and neither source may be replaced by a hash.
  for (unsigned mode = 0; mode < 24; ++mode) {
    bp[0x80] = mode % 3 | (((mode / 3) % 3) << 2) | ((mode & 1) ? 16 : 0);
    bp[0x84] = mode << 16;
    submit();
  }
  CHECK(verify().texture_decodes == 1);
  CHECK(verify().texture_snapshot_bytes == 24 * (256 * 256 + 512));
  memory.write8(0x80000, 129);
  work.textures[0][0].value = work.textures[0][1].value = 0;
  submit();
  CHECK(verify().texture_decodes == 2);
  tmem[9 * 512 + 129 * 2 + 1] ^= 0x5a;
  submit();
  CHECK(verify().texture_decodes == 3);
  tmem[9 * 512 - 1] ^= 0x5a;
  tmem[9 * 512 + 512] ^= 0x5a;
  submit();
  CHECK(verify().texture_decodes == 3);
  CHECK(verify().texture_uploads == 3);
  std::cout << "Verified exact indexed-texture cache invalidation and sampler updates\n";
  if (!measure)
    return;
  for (unsigned repetition = 0; repetition < 3; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    for (unsigned draw = 0; draw < 1024; ++draw)
      submit();
    verify();
    const auto steady = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
    const auto varied_start = std::chrono::steady_clock::now();
    // Sampler changes affect filtering without changing decoded source pixels.
    for (unsigned draw = 0; draw < 64; ++draw) {
      bp[0x80] ^= 16;
      submit();
    }
    verify();
    const auto varied =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - varied_start);
    std::cout << "Texture cache repetition " << repetition + 1
              << ": 1,024 steady draws seconds=" << steady.count()
              << ", 64 sampler updates seconds=" << varied.count() << '\n';
  }
}

void texture_cache_bank_checks() {
  Memory memory{Console::GameCube};
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  gfx::HardwareRenderer renderer(memory, tmem);
  gfx::FragmentShader software(memory, bp, colors, konst, tmem);
  std::vector<u32> pixels(640 * 528), depths(pixels.size()), expected(pixels.size());
  renderer.upload(pixels, depths);
  bp[0] = 1;
  bp[0x28] = 64;
  bp[0x41] = 24;
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[0x94] = 0x80000 >> 5;
  bp[0x98] = 9;
  gfx::RasterWork work{};
  work.edges.fill({0, 0, 1});
  work.textures[0][0].value = work.textures[0][1].value = work.textures[0][2].value = 0;
  unsigned index = 0;
  const auto submit = [&] {
    work.left = int(index);
    work.right = work.left + 1;
    work.bottom = 1;
    CHECK(renderer.begin(bp, colors, konst));
    CHECK(renderer.draw(work));
    gfx::FragmentInputs input;
    u32 depth = 0;
    const auto result = software.shade(input, &depth);
    CHECK(result.has_value());
    expected[index++] = (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
    const auto stats = renderer.synchronize(pixels, depths);
    CHECK(stats.fallback_rectangles == 0);
    CHECK(pixels == expected);
    CHECK(std::all_of(depths.begin(), depths.end(), [](u32 value) { return value == 0; }));
    return stats;
  };
  for (unsigned format : {8u, 9u, 10u}) {
    const unsigned palette_bytes = format == 8 ? 32 : format == 9 ? 512 : 32768;
    bp[0x88] = 7 | (7 << 10) | (format << 20);
    memory.write16(0x80000, format == 10 ? 0x3fff : 0xffff);
    auto stats = submit();
    tmem[9 * 512 + palette_bytes - 1] ^= 0x3d;
    CHECK(submit().texture_decodes == stats.texture_decodes + 1);
    stats = renderer.synchronize(pixels, depths);
    tmem[9 * 512 - 1] ^= 0x7a;
    tmem[9 * 512 + palette_bytes] ^= 0x6b;
    CHECK(submit().texture_decodes == stats.texture_decodes);
    bp[0x98] |= 1 << 10;
    CHECK(submit().texture_decodes == stats.texture_decodes + 1);
    bp[0x98] &= ~(1 << 10);
  }
  // RGBA8's two preloaded banks have independent byte ranges. Source RAM,
  // padding after either bank and palette state cannot invalidate this decode.
  bp[0x88] = 3 | (3 << 10) | (6 << 20);
  bp[0x8c] = (1 << 21) | (0x10000 >> 5);
  bp[0x90] = 0x20000 >> 5;
  tmem[0x10000] = 197;
  tmem[0x10001] = 13;
  tmem[0x20000] = 41;
  tmem[0x20001] = 103;
  auto stats = submit();
  tmem[0x10001] ^= 0x1f;
  CHECK(submit().texture_decodes == stats.texture_decodes + 1);
  stats = renderer.synchronize(pixels, depths);
  tmem[0x20000] ^= 0x2b;
  CHECK(submit().texture_decodes == stats.texture_decodes + 1);
  stats = renderer.synchronize(pixels, depths);
  tmem[0x10000 - 1] ^= 1;
  tmem[0x10000 + 32] ^= 1;
  tmem[0x20000 - 1] ^= 1;
  tmem[0x20000 + 32] ^= 1;
  memory.write8(0x80000, 27);
  bp[0x94] ^= 1;
  bp[0x98] ^= 7;
  CHECK(submit().texture_decodes == stats.texture_decodes);
  bp[0x90] += 1;
  CHECK(submit().texture_decodes == stats.texture_decodes + 1);
  // Uneven mip dimensions use the decoder's minimum-tile offsets. The checked
  // source extent also includes every tile reached by each mip's texel reads.
  bp[0x8c] = bp[0x90] = 0;
  bp[0x94] = 0x80000 >> 5;
  bp[0x88] = 10 | (8 << 10) | (1 << 20);
  bp[0x80] = 1 << 5;
  bp[0x84] = 32 | (32 << 8);
  stats = submit();
  // Point and linear mip modes with the same reachable level count reuse data.
  bp[0x80] = 2 << 5;
  CHECK(submit().texture_decodes == stats.texture_decodes);
  memory.write8(0x80000 + 99 + 32, 233);
  CHECK(submit().texture_decodes == stats.texture_decodes + 1);
  stats = renderer.synchronize(pixels, depths);
  bp[0x84] = 32 | (48 << 8);
  CHECK(submit().texture_decodes == stats.texture_decodes + 1);
  std::cout
      << "Verified C4/C8/C14X2 palette bounds, split-bank RGBA8 and uneven mip invalidation\n";
}

void xfb_checks(bool measure) {
  Memory cpu_memory{Console::Wii}, gpu_memory{Console::Wii};
  std::vector<u8> tmem(1024 * 1024);
  gfx::HardwareRenderer renderer(gpu_memory, tmem);
  std::vector<u32> pixels(640 * 528), depths(pixels.size(), 0x176543), rgba(pixels.size());
  std::array<u32, 256> bp{};
  std::mt19937 generator(0x9182342);
  const auto color = [&](unsigned x, unsigned y) { return rgba[y * 640 + x]; };
  const auto fill = [&](unsigned format) {
    for (unsigned i = 0; i < pixels.size(); ++i) {
      pixels[i] = generator() & 0xffffff;
      if (format == 1) {
        rgba[i] = 0;
        for (unsigned c = 0; c < 4; ++c) {
          const u32 value = (pixels[i] >> (18 - c * 6)) & 63;
          rgba[i] = (rgba[i] << 8) | (value << 2) | (value >> 4);
        }
      } else
        rgba[i] = (pixels[i] << 8) | 255;
    }
    renderer.upload(pixels, depths);
    bp[0x43] = format;
  };
  constexpr std::array<unsigned, 9> widths{16, 18, 20, 32, 48, 64, 128, 320, 640};
  constexpr std::array<unsigned, 8> heights{1, 3, 4, 7, 16, 32, 64, 128};
  constexpr std::array<unsigned, 12> scales{64,  85,  128, 170, 192, 256,
                                            257, 320, 384, 512, 768, 1024};
  constexpr std::size_t checked_bytes = 640 * 528 * 8;
  unsigned valid = 0, invalid = 0;
  u64 compared = 0;
  for (unsigned format = 0; format < 4; ++format) {
    fill(format);
    for (unsigned profile = 0; profile < 512; ++profile) {
      const unsigned width = widths[generator() % widths.size()];
      const unsigned height = heights[generator() % heights.size()];
      const unsigned top = generator() % (528 - height + 1);
      const unsigned row_bytes = ((width + 15) & ~15u) * 2;
      const unsigned stride_mode = generator() % 3;
      const unsigned stride = stride_mode == 0 ? row_bytes : stride_mode == 1 ? 32 : row_bytes + 32;
      bp[0x49] = top << 10;
      bp[0x4a] = (width - 1) | ((height - 1) << 10);
      bp[0x4b] = (profile & 1 ? 0x10010000 : 0x10000) >> 5;
      bp[0x4d] = stride >> 5;
      bp[0x4e] = scales[generator() % scales.size()];
      bp[0x52] = (1 << 14) | ((generator() & 1) ? 1 << 10 : 0) | ((generator() & 3) << 7) |
                 (generator() & 3);
      bp[0x53] = bp[0x54] = 0;
      for (unsigned tap = 0; tap < 7; ++tap) {
        const unsigned weight = profile % 8 == 0 ? (tap == 2   ? 1
                                                    : tap == 3 ? 63
                                                               : 0)
                                                 : generator() & 63;
        bp[tap < 4 ? 0x53 : 0x54] |= weight << ((tap < 4 ? tap : tap - 4) * 6);
      }
      auto cpu_bytes = cpu_memory.range(bp[0x4b] << 5, checked_bytes);
      auto gpu_bytes = gpu_memory.range(bp[0x4b] << 5, checked_bytes);
      std::fill(cpu_bytes.begin(), cpu_bytes.end(), 0xa7);
      std::fill(gpu_bytes.begin(), gpu_bytes.end(), 0xa7);
      bool rejected = false;
      try {
        gfx::copy_efb_xfb(cpu_memory, bp, color);
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      if (rejected) {
        CHECK_THROWS(renderer.copy_xfb(bp));
        ++invalid;
      } else {
        CHECK(renderer.copy_xfb(bp));
        ++valid;
      }
      if (!std::equal(cpu_bytes.begin(), cpu_bytes.end(), gpu_bytes.begin()))
        throw std::runtime_error("XFB GPU profile mismatch format=" + std::to_string(format) +
                                 " profile=" + std::to_string(profile));
      compared += checked_bytes;
    }
  }
  const auto stats = renderer.synchronize(pixels, depths);
  CHECK(stats.xfb_copies == valid);
  CHECK(stats.efb_readbacks == 0);
  std::cout << "Verified " << valid << " GPU XFB profiles and " << invalid
            << " rejected staging boundaries; compared " << compared << " RAM bytes\n";
  if (!measure)
    return;
  fill(1);
  bp[0x49] = 0;
  bp[0x4a] = 639 | (223 << 10);
  bp[0x4b] = 0x10000 >> 5;
  bp[0x4d] = 1280 >> 5;
  bp[0x4e] = 256;
  bp[0x52] = (1 << 14) | 3;
  bp[0x53] = (8 << 0) | (8 << 6) | (16 << 12) | (16 << 18);
  bp[0x54] = (8 << 6) | (8 << 12);
  std::array<std::vector<u8>, 4> encoded;
  for (unsigned gamma = 0; gamma < 4; ++gamma) {
    bp[0x52] = (1 << 14) | 3 | (gamma << 7);
    gfx::copy_efb_xfb(cpu_memory, bp, color);
    const auto bytes = cpu_memory.range(0x10000, 640 * 224 * 2);
    encoded[gamma].assign(bytes.begin(), bytes.end());
  }
  for (unsigned repetition = 0; repetition < 3; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    for (unsigned copy = 0; copy < 32; ++copy) {
      bp[0x52] = (1 << 14) | 3 | ((copy & 3) << 7);
      gfx::copy_efb_xfb(cpu_memory, bp, color);
    }
    const double cpu =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto gpu_start = std::chrono::steady_clock::now();
    for (unsigned copy = 0; copy < 32; ++copy) {
      bp[0x52] = (1 << 14) | 3 | ((copy & 3) << 7);
      CHECK(renderer.copy_xfb(bp));
      CHECK(std::equal(encoded[copy & 3].begin(), encoded[copy & 3].end(),
                       gpu_memory.range(0x10000, 640 * 224 * 2).begin()));
    }
    const double gpu =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - gpu_start).count();
    CHECK(std::equal(cpu_memory.range(0x10000, 640 * 224 * 2).begin(),
                     cpu_memory.range(0x10000, 640 * 224 * 2).end(),
                     gpu_memory.range(0x10000, 640 * 224 * 2).begin()));
    std::cout << "XFB copy repetition " << repetition + 1 << ": CPU seconds=" << cpu
              << ", GPU seconds=" << gpu << '\n';
  }
}

void xfb_clear_checks() {
  const std::array<std::array<float, 3>, 4> quad{
      {{-1, -1, -0.5f}, {1, -1, -0.5f}, {1, 1, -0.5f}, {-1, 1, -0.5f}}};
  const std::array<u32, 4> colors{0x124878d2, 0xf0d0b090, 0x3456ef78, 0x9abcedff};
  Fixture software, hardware;
  hardware.graphics.set_hardware_rendering(true);
  for (unsigned format = 0; format < 4; ++format)
    for (unsigned mask = 0; mask < 8; ++mask) {
      for (auto *fixture : {&software, &hardware}) {
        fixture->bp(0x43, format);
        fixture->bp(0x40, 1 | 14 | 16);
        fixture->bp(0x41, 24);
        fixture->draw(0x80, quad, colors);
        fixture->bp(0x40, mask & 4 ? 16 : 0);
        fixture->bp(0x41, ((mask & 1) ? 8 : 0) | ((mask & 2) ? 16 : 0));
        fixture->bp(0x49, 5 << 10);
        fixture->bp(0x4a, 15 | (15 << 10));
        fixture->bp(0x4b, 0x20000 >> 5);
        fixture->bp(0x4d, 1);
        fixture->bp(0x4e, 256);
        fixture->bp(0x4f, 0x71f4);
        fixture->bp(0x50, 0x38d2);
        fixture->bp(0x51, 0x815392);
        fixture->bp(0x53, (1 << 12) | (63 << 18));
        fixture->bp(0x54, 0);
        fixture->bp(0x52, (1 << 14) | (1 << 11) | 3 | ((mask & 3) << 7));
      }
      equal(software, hardware);
      CHECK(std::equal(software.memory.range(0x20000, 512).begin(),
                       software.memory.range(0x20000, 512).end(),
                       hardware.memory.range(0x20000, 512).begin()));
      CHECK(hardware.graphics.hardware_stats().xfb_copies == format * 8 + mask + 1);
    }
  const auto before = hardware.graphics.hardware_stats();
  for (unsigned copy = 0; copy < 8; ++copy)
    for (auto *fixture : {&software, &hardware}) {
      fixture->bp(0x43, 1);
      fixture->bp(0x40, 16);
      fixture->bp(0x41, 24);
      if (copy == 0)
        fixture->draw(0x80, quad, colors);
      fixture->bp(0x4f, copy * 913 + 127);
      fixture->bp(0x50, copy * 1777 + 83);
      fixture->bp(0x51, copy * 8153 + 391);
      fixture->bp(0x52, (1 << 14) | (1 << 11) | 3 | ((copy & 3) << 7));
    }
  const auto after = hardware.graphics.hardware_stats();
  CHECK(after.xfb_copies == before.xfb_copies + 8);
  CHECK(after.efb_readbacks == before.efb_readbacks + 1);
  equal(software, hardware);
  CHECK(std::equal(software.memory.range(0x20000, 512).begin(),
                   software.memory.range(0x20000, 512).end(),
                   hardware.memory.range(0x20000, 512).begin()));
  std::cout << "Verified 32 GPU XFB copy/clear states across EFB formats and write masks\n";
  std::cout << "Verified eight queued GPU copy/clear commands with one later EFB readback\n";
}

void benchmark(Fixture &software, Fixture &hardware) {
  const std::array<std::array<float, 3>, 4> quad{
      {{-1, -1, -0.5f}, {1, -1, -0.5f}, {1, 1, -0.5f}, {-1, 1, -0.5f}}};
  const std::array<u32, 4> colors{0x124878d2, 0xf0d0b090, 0x3456ef78, 0x9abcedff};
  for (auto *fixture : {&software, &hardware}) {
    fixture->floats(0x101a, {320, 264, 1000, 662, 606, 1000});
    fixture->bp(0x21, (981 << 12) | 869);
    fixture->bp(0, 1 << 4);
    fixture->bp(0x28, 0);
    fixture->bp(0x40, 0);
    fixture->bp(0x41, 24);
    fixture->bp(0x43, 0);
    fixture->bp(0xf1, 0);
    fixture->bp(0xf5, 0);
    fixture->bp(0xf3, (7 << 16) | (7 << 19));
    fixture->bp(0xc0, 0xfffa | (1 << 19));
    fixture->bp(0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19));
  }
  const auto elapsed = [&](Fixture &fixture) {
    const auto started = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 64; ++i)
      fixture.draw(0x80, quad, colors);
    fixture.graphics.fragment_count();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  };
  const double cpu = elapsed(software), gpu = elapsed(hardware);
  CHECK(software.graphics.fragment_count() == hardware.graphics.fragment_count());
  for (unsigned y = 0; y < 528; ++y)
    for (unsigned x = 0; x < 640; ++x) {
      CHECK(software.graphics.color(x, y) == hardware.graphics.color(x, y));
      CHECK(software.graphics.depth(x, y) == hardware.graphics.depth(x, y));
    }
  std::cout << "Verified 64 full EFB quads: CPU seconds=" << cpu << ", OpenGL seconds=" << gpu
            << ", rendering ratio=" << cpu / gpu << '\n';
}
} // namespace

int main(int argc, char **argv) {
  Fixture software, hardware;
  bool required = false, measure = false, fog_only = false, texture_cache_only = false,
       xfb_only = false;
  for (int i = 1; i < argc; ++i) {
    required |= std::strcmp(argv[i], "--require-device") == 0;
    measure |= std::strcmp(argv[i], "--benchmark") == 0;
    fog_only |= std::strcmp(argv[i], "--fog-only") == 0;
    texture_cache_only |= std::strcmp(argv[i], "--texture-cache-only") == 0;
    xfb_only |= std::strcmp(argv[i], "--xfb-only") == 0;
  }
  try {
    hardware.graphics.set_hardware_rendering(true);
  } catch (const gfx::HardwareUnavailable &error) {
    std::cerr << "Hardware backend unavailable: " << error.what() << '\n';
    return required ? 1 : 77;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return run_tests([&] {
    if (xfb_only) {
      xfb_checks(measure);
      xfb_clear_checks();
      return;
    }
    if (texture_cache_only) {
      texture_cache_checks(measure);
      texture_cache_bank_checks();
      return;
    }
    if (!fog_only) {
      checks(software, hardware);
      indirect_checks(software, hardware);
      indirect_state_checks();
      texture_cache_checks(false);
      texture_cache_bank_checks();
      xfb_checks(false);
      xfb_clear_checks();
    }
    fog_checks();
    fog_bucket_checks();
    if (measure)
      benchmark(software, hardware);
  });
}
