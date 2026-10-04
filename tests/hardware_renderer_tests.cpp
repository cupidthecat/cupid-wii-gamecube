// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/command_processor.hpp"
#include "test_support.hpp"
#include <array>
#include <bit>
#include <chrono>
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
        throw std::runtime_error("Hardware differential mismatch at " + std::to_string(x) + "," +
                                 std::to_string(y));
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
  // Unsupported fog is explicit and preserves the CPU result before hardware resumes.
  bp(0xf1, 4 << 21);
  draw(0x80, quad);
  CHECK(!hardware.graphics.hardware_fallback_reason().empty());
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
  bool required = false, measure = false;
  for (int i = 1; i < argc; ++i) {
    required |= std::strcmp(argv[i], "--require-device") == 0;
    measure |= std::strcmp(argv[i], "--benchmark") == 0;
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
    checks(software, hardware);
    if (measure)
      benchmark(software, hardware);
  });
}
