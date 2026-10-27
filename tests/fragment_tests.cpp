// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/fragment.hpp"
#include "test_support.hpp"
#include <vector>

namespace {
using namespace cupid;

struct Fixture {
  Memory memory{Console::GameCube};
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  std::vector<u8> tmem = std::vector<u8>(1024 * 1024);
  gfx::FragmentShader shader{memory, bp, colors, konst, tmem};
  gfx::FragmentInputs input{};

  Fixture() {
    bp[0xf3] = (7 << 16) | (7 << 19);
    bp[0xf6] = 0 | (1 << 2);
    bp[0xf7] = 2 | (3 << 2);
    bp[0xc0] = (15 << 12) | (15 << 8) | (15 << 4) | 10 | (1 << 19);
    bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
    input.colors[0] = {18, 52, 86, 120};
    input.colors[1] = {1, 2, 3, 4};
  }
};

void stages() {
  Fixture f;
  f.shader.validate();
  CHECK((f.shader.shade(f.input).value() == gfx::Color{18, 52, 86, 120}));
  f.bp[0x28] = 1 << 7;
  CHECK((f.shader.shade(f.input).value() == gfx::Color{1, 2, 3, 4}));
  f.bp[0x28] = 0;
  f.bp[0xc0] = (10 << 12) | (15 << 8) | (15 << 4) | 15 | (1 << 19);
  CHECK(f.shader.shade(f.input)->at(0) == 18);
  f.bp[0xc0] |= 1 << 20;
  CHECK(f.shader.shade(f.input)->at(0) == 36);
  f.bp[0xc0] = (15 << 12) | (10 << 8) | (12 << 4) | 15 | (1 << 19);
  CHECK(f.shader.shade(f.input)->at(0) == 18);
  f.bp[0xc0] = (15 << 12) | (15 << 8) | (15 << 4) | 14 | (1 << 19);
  f.bp[0xf6] = 0 | (1 << 2) | (4 << 4);
  CHECK(f.shader.shade(f.input)->at(0) == 128);
  f.konst[2] = {20, 40, 60, 80};
  f.bp[0xf6] = 0 | (1 << 2) | (14 << 4);
  CHECK((f.shader.shade(f.input).value() == gfx::Color{20, 40, 60, 120}));
  f.bp[0xc0] = (15 << 12) | (15 << 8) | (15 << 4) | 2;
  f.colors[1] = {-1, -1024, 1023, 0};
  CHECK((f.shader.shade(f.input).value() == gfx::Color{255, 0, 255, 120}));
  f.bp[0] = (1 << 10);
  f.bp[0xc0] = (15 << 12) | (15 << 8) | (15 << 4) | 10 | (1 << 19) | (2 << 22);
  f.bp[0xc2] = (15 << 12) | (15 << 8) | (15 << 4) | 4 | (1 << 19);
  f.bp[0xc3] = f.bp[0xc1];
  CHECK(f.shader.shade(f.input)->at(0) == 18);
}

void alpha_and_compare() {
  Fixture f;
  for (unsigned mode = 0; mode < 8; ++mode) {
    f.bp[0xf3] = (mode << 16) | (7 << 19) | 120;
    CHECK(f.shader.shade(f.input).has_value() == gfx::compare(mode, 120, 120));
  }
  f.bp[0xf3] = (7 << 19) | (1 << 22);
  CHECK(f.shader.shade(f.input).has_value());
  f.bp[0xf3] = (7 << 16) | (7 << 19);
  f.colors[1] = {100, 20, 5, 0};
  f.colors[2] = {10, 30, 2, 0};
  f.bp[0xc0] = (2 << 12) | (4 << 8) | (10 << 4) | 15 | (3 << 16) | (1 << 19);
  CHECK((f.shader.shade(f.input).value() == gfx::Color{18, 52, 86, 120}));
  f.bp[0xc0] |= 3 << 20;
  CHECK((f.shader.shade(f.input).value() == gfx::Color{18, 0, 86, 120}));
  f.bp[0xc0] |= 1 << 18;
  CHECK((f.shader.shade(f.input).value() == gfx::Color{0, 0, 0, 120}));
}

void textured() {
  Fixture f;
  f.bp[0] = 1;
  f.bp[0x28] = 64;
  f.bp[0x88] = (4 << 20) | 3 | (3 << 10);
  f.bp[0x94] = 0x2000 >> 5;
  f.memory.write16(0x2000, 0xf800);
  f.bp[0xc0] = (15 << 12) | (15 << 8) | (15 << 4) | 8 | (1 << 19);
  f.bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  CHECK((f.shader.shade(f.input).value() == gfx::Color{255, 0, 0, 255}));
  f.bp[0] |= 5 << 16;
  CHECK_THROWS(f.shader.validate());
  f.bp[0] = 1;
  f.bp[0xf1] = 1 << 21;
  f.shader.validate();
  CHECK_THROWS(f.shader.shade(f.input));
}

void texture_latches_and_depth() {
  Fixture f;
  f.bp[0] = 1;
  f.bp[0x28] = 64;
  f.bp[0x88] = 6 << 20;
  f.bp[0x94] = 0x2000 >> 5;
  f.memory.write16(0x2000, 0x4411);
  f.memory.write16(0x2020, 0x2233);
  f.bp[0xc0] = 0xfff8 | (1 << 19);
  f.bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  CHECK((f.shader.shade(f.input).value() == gfx::Color{17, 34, 51, 68}));
  f.bp[0x28] = 0;
  f.bp[0xc1] |= 1 << 2;
  // A disabled stage preserves the previously swapped texture, including across pixels.
  CHECK((f.shader.shade(f.input).value() == gfx::Color{17, 34, 51, 68}));
  f.bp[0x28] = 64;
  f.bp[0xf8] = f.bp[0xf9] = 2 | (2 << 2);
  f.bp[0xf4] = 5;
  f.bp[0xf5] = 1 << 2;
  u32 depth = 100;
  CHECK((f.shader.shade(f.input, &depth).value() == gfx::Color{51, 51, 51, 51}));
  CHECK(depth == 173);
  f.bp[0xf5] = (2 << 2) | 1;
  CHECK(f.shader.shade(f.input, &depth).has_value());
  CHECK(depth == 0x4416);
  f.bp[0xf5] = (2 << 2) | 2;
  CHECK(f.shader.shade(f.input, &depth).has_value());
  CHECK(depth == 0x112238);
  f.bp[0xf5] = 3 << 2;
  CHECK_THROWS(f.shader.shade(f.input));
}

void indirect_coordinates() {
  Fixture f;
  f.bp[0] = 1 | (1 << 16);
  f.bp[0x27] = 1;
  f.bp[0x28] = 64;
  f.bp[0x88] = (1 << 20) | 7 | (3 << 10);
  f.bp[0x94] = 0x2000 >> 5;
  for (unsigned i = 0; i < 8; ++i)
    f.memory.write8(0x2000 + i, static_cast<u8>((i + 1) * 10));
  f.bp[0x89] = 6 << 20;
  f.bp[0x95] = 0x3000 >> 5;
  f.memory.write16(0x3000, 0x1000);
  f.bp[0xc0] = 0xfff8 | (1 << 19);
  f.bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  f.bp[6] = 64 | (1 << 22);
  f.bp[8] = 1 << 22;
  f.bp[0x10] = 1 << 9;
  CHECK((f.shader.shade(f.input).value() == gfx::Color{20, 20, 20, 20}));
  f.bp[0x10] |= 1 << 4;
  CHECK(f.shader.shade(f.input)->at(0) == 10);
  f.bp[0x10] = (1 << 9) | (6 << 13);
  f.input.uv[0][0] = 7;
  CHECK(f.shader.shade(f.input)->at(0) == 20);
  f.bp[0x10] = 1 << 13;
  f.input.uv[0][0] = 257;
  CHECK(f.shader.shade(f.input)->at(0) == 20);
  f.input.uv[0][0] = 0;
  f.bp[0x10] = (1 << 9) | (1 << 20);
  CHECK(f.shader.shade(f.input)->at(0) == 30);
  CHECK(f.shader.shade(f.input)->at(0) == 40);
  f.memory.write16(0x3000, 0xe000);
  f.bp[0x10] = 1 << 7;
  f.bp[0x28] = 5 << 7;
  f.bp[0xc0] = 0xfffa | (1 << 19);
  f.bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
  CHECK((f.shader.shade(f.input).value() == gfx::Color{224, 224, 224, 224}));
  f.bp[0x28] = 6 << 7;
  CHECK((f.shader.shade(f.input).value() == gfx::Color{231, 231, 231, 231}));
  f.bp[0x10] = 7 << 13;
  CHECK_THROWS(f.shader.shade(f.input));
}

void fog() {
  Fixture f;
  f.input.colors[0] = {0, 0, 0, 255};
  f.bp[0xee] = 127 << 11;
  f.bp[0xf2] = 0xff0000;
  u32 depth = 0x400000;
  const std::array<unsigned, 5> modes{2, 4, 5, 6, 7};
  const std::array<u8, 5> values{63, 191, 73, 3, 10};
  for (unsigned i = 0; i < modes.size(); ++i) {
    f.bp[0xf1] = (1 << 20) | (modes[i] << 21);
    const auto result = f.shader.shade(f.input, &depth).value();
    CHECK((result == gfx::Color{values[i], 0, 0, 255}));
  }
  f.bp[0xe8] = 1024 | 342;
  for (unsigned i = 0; i < 5; ++i)
    f.bp[0xe9 + i] = 64 | (64 << 12);
  f.input.x = 0;
  f.input.viewport_width = 8;
  f.bp[0xf1] = (1 << 20) | (2 << 21);
  CHECK(f.shader.shade(f.input, &depth)->at(0) == 63);
  f.input.viewport_width = 0;
  CHECK_THROWS(f.shader.shade(f.input, &depth));
  f.bp[0xe8] = 0;
  f.bp[0xee] = 255 << 11;
  f.bp[0xf1] |= 255 << 11;
  CHECK(f.shader.shade(f.input, &depth)->at(0) == 255);
  f.bp[0xee] |= 0x80000;
  CHECK(f.shader.shade(f.input, &depth)->at(0) == 0);
}
} // namespace

int main() {
  return run_tests([] {
    stages();
    alpha_and_compare();
    textured();
    texture_latches_and_depth();
    indirect_coordinates();
    fog();
  });
}
