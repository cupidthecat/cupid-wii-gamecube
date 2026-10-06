// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/texture.hpp"
#include "test_support.hpp"
#include <array>
#include <vector>

namespace {
using namespace cupid;

struct Fixture {
  Memory memory{Console::GameCube};
  std::array<u32, 256> bp{};
  std::vector<u8> tmem = std::vector<u8>(1024 * 1024);
  gfx::TextureSampler sampler{memory, bp, tmem};

  void image(unsigned format, unsigned width = 8, unsigned height = 8) {
    bp[0x88] = (format << 20) | (width - 1) | ((height - 1) << 10);
    bp[0x94] = 0x2000 >> 5;
  }
};

void encodings() {
  Fixture f;
  f.image(0, 16, 16);
  f.memory.write8(0x2000, 0xf0);
  f.memory.write8(0x2020, 0x7e);
  CHECK((f.sampler.texel(0, 0, 0) == gfx::Color{255, 255, 255, 255}));
  CHECK((f.sampler.texel(0, 1, 0) == gfx::Color{0, 0, 0, 0}));
  CHECK((f.sampler.texel(0, 8, 0) == gfx::Color{119, 119, 119, 119}));
  CHECK(f.sampler.texel(0, 9, 0)[0] == 238);
  f.image(1);
  f.memory.write8(0x2000, 0x73);
  CHECK((f.sampler.texel(0, 0, 0) == gfx::Color{115, 115, 115, 115}));
  f.image(2);
  f.memory.write8(0x2000, 0xa5);
  CHECK((f.sampler.texel(0, 0, 0) == gfx::Color{85, 85, 85, 170}));
  f.image(3);
  f.memory.write16(0x2000, 0xa573);
  CHECK((f.sampler.texel(0, 0, 0) == gfx::Color{115, 115, 115, 165}));
  f.image(4);
  f.memory.write16(0x2000, 0xf800);
  CHECK((f.sampler.texel(0, 0, 0) == gfx::Color{255, 0, 0, 255}));
  f.image(5);
  f.memory.write16(0x2000, 0x7f00);
  CHECK((f.sampler.texel(0, 0, 0) == gfx::Color{255, 0, 0, 255}));
  f.memory.write16(0x2000, 0xfc00);
  CHECK(f.sampler.texel(0, 0, 0)[0] == 255);
  f.image(6);
  f.memory.write16(0x2000, 0x1234);
  f.memory.write16(0x2020, 0x5678);
  CHECK((f.sampler.texel(0, 0, 0) == gfx::Color{52, 86, 120, 18}));
  f.bp[0x8c] = 1 << 21;
  f.bp[0x90] = 1;
  f.tmem[0] = 0x12;
  f.tmem[1] = 0x34;
  f.tmem[32] = 0x56;
  f.tmem[33] = 0x78;
  CHECK((f.sampler.texel(0, 0, 0) == gfx::Color{52, 86, 120, 18}));
  f.bp[0x8c] = 0;
  f.image(8);
  f.memory.write8(0x2000, 0x10);
  f.bp[0x98] = 1 << 10;
  f.tmem[2] = 0xf8;
  f.tmem[3] = 0;
  CHECK((f.sampler.texel(0, 0, 0) == gfx::Color{255, 0, 0, 255}));
  f.image(9);
  f.memory.write8(0x2000, 1);
  CHECK(f.sampler.texel(0, 0, 0)[0] == 255);
  f.image(10);
  f.memory.write16(0x2000, 0xc001);
  CHECK(f.sampler.texel(0, 0, 0)[0] == 255);
  f.bp[0x98] = 0;
  f.tmem[2] = 0xa5;
  f.tmem[3] = 0x73;
  CHECK((f.sampler.texel(0, 0, 0) == gfx::Color{115, 115, 115, 165}));
  f.bp[0x98] = 2 << 10;
  f.tmem[2] = 0x7f;
  f.tmem[3] = 0;
  CHECK(f.sampler.texel(0, 0, 0)[0] == 255);
  f.image(7);
  CHECK_THROWS(f.sampler.texel(0, 0, 0));
  f.image(4);
  CHECK_THROWS(f.sampler.texel(0, 8, 0));
  CHECK_THROWS(f.sampler.texel(8, 0, 0));
}

void compressed() {
  Fixture f;
  f.image(14);
  f.memory.write16(0x2000, 0xf800);
  f.memory.write16(0x2002, 0x07e0);
  f.memory.write8(0x2004, 0x1b);
  CHECK((f.sampler.texel(0, 0, 0) == gfx::Color{255, 0, 0, 255}));
  CHECK((f.sampler.texel(0, 1, 0) == gfx::Color{0, 255, 0, 255}));
  CHECK((f.sampler.texel(0, 2, 0) == gfx::Color{159, 95, 0, 255}));
  CHECK((f.sampler.texel(0, 3, 0) == gfx::Color{95, 159, 0, 255}));
  f.memory.write16(0x2000, 0);
  f.memory.write16(0x2002, 0xffff);
  CHECK((f.sampler.texel(0, 3, 0) == gfx::Color{127, 127, 127, 0}));
  f.memory.write16(0x2008, 0x001f);
  CHECK((f.sampler.texel(0, 4, 0) == gfx::Color{0, 0, 255, 255}));
}

void filtering() {
  Fixture f;
  f.image(1, 8, 4);
  f.memory.write8(0x2000, 10);
  f.memory.write8(0x2001, 50);
  f.memory.write8(0x2008, 90);
  f.memory.write8(0x2009, 130);
  CHECK(f.sampler.sample(0, 128, 128, 0, true)[0] == 70);
  CHECK(f.sampler.sample(0, 64, 64, 0, true)[0] == 10);
  CHECK(f.sampler.sample(0, 128, 0, 0, false)[0] == 50);
  f.bp[0x80] = 1;
  CHECK(f.sampler.sample(0, 8 * 128, 0, 0, false)[0] == 10);
  f.bp[0x80] = 2;
  f.memory.write8(0x2007, 210);
  CHECK(f.sampler.sample(0, 8 * 128, 0, 0, false)[0] == 210);
  CHECK(f.sampler.sample(0, -128, 0, 0, false)[0] == 10);
  f.bp[0x80] = 3;
  CHECK(f.sampler.sample(0, -128, 0, 0, false)[0] == 10);
  f.memory.write8(0x2020, 100);
  f.memory.write8(0x2040, 200);
  f.bp[0x80] = 1 << 5;
  CHECK(f.sampler.sample(0, 0, 0, 16, false)[0] == 100);
  CHECK(f.sampler.sample(0, 0, 0, 24, false)[0] == 200);
  f.bp[0x80] = 2 << 5;
  CHECK(f.sampler.sample(0, 0, 0, 24, false)[0] == 150);
}

void prepared_decode() {
  Fixture f;
  auto source = f.memory.range(0x2000, 65536);
  for (unsigned i = 0; i < source.size(); ++i)
    source[i] = static_cast<u8>((i * 37 + 19) ^ (i >> 3));
  for (unsigned i = 0; i < f.tmem.size(); ++i)
    f.tmem[i] = static_cast<u8>((i * 59 + 71) ^ (i >> 5));
  constexpr std::array<unsigned, 11> formats{0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 14};
  constexpr std::array<std::array<unsigned, 2>, 6> sizes{
      {{1, 1}, {7, 5}, {8, 8}, {9, 13}, {17, 9}, {32, 16}}};
  u64 compared = 0;
  for (unsigned unit = 0; unit < 8; ++unit) {
    const unsigned address = 0x80 + (unit & 3) + ((unit & 4) << 3);
    for (unsigned format : formats)
      for (const auto &size : sizes)
        for (unsigned palette = 0; palette < 3; ++palette) {
          f.bp[address + 8] = (format << 20) | (size[0] - 1) | ((size[1] - 1) << 10);
          f.bp[address + 12] = 0;
          f.bp[address + 20] = 0x2000 >> 5;
          f.bp[address + 24] = (palette << 10) | 23;
          for (unsigned mip : {0u, 1u, 2u, 3u, 4u, 10u}) {
            const unsigned width = ((size[0] - 1) >> mip) + 1, height = ((size[1] - 1) >> mip) + 1;
            std::vector<u32> decoded(std::size_t(width) * height);
            f.sampler.decode(unit, mip, source, decoded);
            for (unsigned y = 0; y < height; ++y)
              for (unsigned x = 0; x < width; ++x) {
                const auto expected = f.sampler.texel(unit, x, y, mip);
                const u32 packed = (u32(expected[0]) << 24) | (u32(expected[1]) << 16) |
                                   (u32(expected[2]) << 8) | expected[3];
                CHECK(decoded[std::size_t(y) * width + x] == packed);
                ++compared;
              }
          }
          f.bp[address + 12] = (1 << 21) | 128;
          f.bp[address + 16] = 512;
          std::vector<u32> decoded(static_cast<std::size_t>(size[0]) * size[1]);
          f.sampler.decode(unit, 0, {}, decoded);
          for (unsigned y = 0; y < size[1]; ++y)
            for (unsigned x = 0; x < size[0]; ++x) {
              const auto expected = f.sampler.texel(unit, x, y);
              const u32 packed = (u32(expected[0]) << 24) | (u32(expected[1]) << 16) |
                                 (u32(expected[2]) << 8) | expected[3];
              CHECK(decoded[std::size_t(y) * size[0] + x] == packed);
              ++compared;
            }
        }
  }

  f.bp[0x8c] = 0;
  f.image(6);
  std::vector<u32> decoded(64);
  CHECK_THROWS(f.sampler.decode(0, 0, {}, decoded));
  CHECK_THROWS(f.sampler.decode(0, 0, source.first(255), decoded));
  CHECK_THROWS(f.sampler.decode(0, 0, source, std::span(decoded).first(63)));
  CHECK_THROWS(f.sampler.decode(8, 0, source, decoded));
  CHECK_THROWS(f.sampler.decode(0, 11, source, decoded));
  f.bp[0x8c] = 1 << 21;
  std::array<u32, 16> mip{};
  CHECK_THROWS(f.sampler.decode(0, 1, {}, mip));
  f.bp[0x8c] = 0;
  f.image(7);
  CHECK_THROWS(f.sampler.decode(0, 0, source, decoded));
  f.image(8);
  f.bp[0x98] = 3 << 10;
  CHECK_THROWS(f.sampler.decode(0, 0, source, decoded));
  f.bp[0x98] = 2 << 10;
  gfx::TextureSampler short_palette(f.memory, f.bp, std::span(f.tmem).first(2));
  CHECK_THROWS(short_palette.decode(0, 0, source, decoded));
  f.bp[0x8c] = (1 << 21) | 32767;
  f.image(6);
  CHECK_THROWS(f.sampler.decode(0, 0, {}, decoded));
  std::cout << "Verified " << compared
            << " prepared/demand texels across all formats, units, palettes, uneven mips and "
               "split banks, plus source/destination bounds\n";
}
} // namespace

int main() {
  return run_tests([] {
    encodings();
    compressed();
    filtering();
    prepared_decode();
  });
}
