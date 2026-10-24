// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/texture.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <cstring>
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

namespace cmpr_bulk_checks {
using namespace cupid;
constexpr u32 poison = 0x698ac4d2;

u32 pack(gfx::Color color) {
  return (u32(color[0]) << 24) | (u32(color[1]) << 16) | (u32(color[2]) << 8) | color[3];
}

void configure(std::array<u32, 256> &bp, unsigned width, unsigned height, u32 physical = 0x2000) {
  bp.fill(0);
  bp[0x88] = (14u << 20) | (width - 1) | ((height - 1) << 10);
  bp[0x94] = physical >> 5;
}

void block(std::span<u8> bytes, unsigned offset, u16 a, u16 b) {
  bytes[offset] = static_cast<u8>(a >> 8);
  bytes[offset + 1] = static_cast<u8>(a);
  bytes[offset + 2] = static_cast<u8>(b >> 8);
  bytes[offset + 3] = static_cast<u8>(b);
  constexpr std::array<u8, 4> rows{0x1b, 0xe4, 0x55, 0xaa};
  std::copy(rows.begin(), rows.end(), bytes.begin() + offset + 4);
}

std::array<u8, 32> literal_source() {
  std::array<u8, 32> source{};
  block(source, 0, 0xf800, 0x07e0);
  block(source, 8, 0x001f, 0xffff);
  block(source, 16, 0x8410, 0x8410);
  block(source, 24, 0xffff, 0x0000);
  return source;
}

constexpr std::array<std::array<u32, 4>, 4> literal_colors{
    {{0xff0000ff, 0x00ff00ff, 0x9f5f00ff, 0x5f9f00ff},
     {0x0000ffff, 0xffffffff, 0x7f7fffff, 0x7f7fff00},
     {0x848284ff, 0x848284ff, 0x848284ff, 0x84828400},
     {0xffffffff, 0x000000ff, 0x9f9f9fff, 0x5f5f5fff}}};

void literals() {
  Memory memory(Console::GameCube);
  std::array<u32, 256> bp{};
  std::vector<u8> tmem(1024 * 1024);
  gfx::TextureSampler sampler(memory, bp, tmem);
  configure(bp, 8, 8);
  const auto source = literal_source();
  memory.copy_in(0x2000, source);
  std::vector<u32> guarded(66, poison);
  auto output = std::span(guarded).subspan(1, 64);
  sampler.decode(0, 0, source, output);
  constexpr std::array<std::array<unsigned, 4>, 4> selects{
      {{0, 1, 2, 3}, {3, 2, 1, 0}, {1, 1, 1, 1}, {2, 2, 2, 2}}};
  for (unsigned y = 0; y < 8; ++y)
    for (unsigned x = 0; x < 8; ++x) {
      const u32 expected = literal_colors[(y / 4) * 2 + x / 4][selects[y % 4][x % 4]];
      CHECK(output[y * 8 + x] == expected);
      CHECK(pack(sampler.texel(0, x, y)) == expected);
    }
  CHECK(guarded.front() == poison && guarded.back() == poison);
}

void demand_equal(gfx::TextureSampler &sampler, unsigned mip, std::span<const u8> source,
                  unsigned width, unsigned height) {
  std::vector<u32> guarded(std::size_t(width) * height + 2, poison);
  auto output = std::span(guarded).subspan(1, std::size_t(width) * height);
  sampler.decode(0, mip, source, output);
  for (unsigned y = 0; y < height; ++y)
    for (unsigned x = 0; x < width; ++x)
      CHECK(output[std::size_t(y) * width + x] == pack(sampler.texel(0, x, y, mip)));
  CHECK(guarded.front() == poison && guarded.back() == poison);
}

void layouts_and_limits() {
  Memory memory(Console::GameCube);
  std::array<u32, 256> bp{};
  std::vector<u8> source(1024 * 1024), tmem(1024 * 1024);
  for (unsigned i = 0; i < source.size(); ++i)
    source[i] = static_cast<u8>((i * 71 + 93) ^ (i >> 4));
  memory.copy_in(0x2000, source);
  std::copy(source.begin(), source.end(), tmem.begin());
  gfx::TextureSampler sampler(memory, bp, tmem);
  constexpr std::array<std::array<unsigned, 2>, 9> dimensions{
      {{1, 1}, {5, 1}, {1, 5}, {7, 5}, {8, 8}, {9, 13}, {17, 9}, {32, 16}, {1024, 8}}};
  for (const auto size : dimensions) {
    configure(bp, size[0], size[1]);
    for (unsigned mip : {0u, 1u, 2u, 3u, 4u, 10u})
      demand_equal(sampler, mip, source, ((size[0] - 1) >> mip) + 1, ((size[1] - 1) >> mip) + 1);
    bp[0x8c] = 1u << 21;
    bp[0x90] = 32767; // CMPR ignores the manual odd-bank register.
    demand_equal(sampler, 0, {}, size[0], size[1]);
  }
  // Complete valid tile reaches the last physical byte without wrapping.
  configure(bp, 8, 8, 0xffffffe0u);
  std::array<u32, 64> decoded{};
  sampler.decode(0, 0, std::span(source).first(32), decoded);
  // At the next32-byte tile, scalar fallback must throw after the entire left tile.
  configure(bp, 16, 8, 0xffffffe0u);
  std::vector<u32> partial(128, poison);
  CHECK_THROWS(sampler.decode(0, 0, source, partial));
  for (unsigned y = 0; y < 8; ++y)
    for (unsigned x = 0; x < 16; ++x)
      CHECK((partial[y * 16 + x] == poison) == (x >= 8));
  // Original16x16 mip0 contributes128bytes; base+offset already wraps at mip1.
  configure(bp, 16, 16, 0xffffffe0u);
  decoded.fill(poison);
  CHECK_THROWS(sampler.decode(0, 1, source, decoded));
  CHECK(std::all_of(decoded.begin(), decoded.end(), [](u32 value) { return value == poison; }));
}

void minimum_and_partial_writes() {
  Memory memory(Console::GameCube);
  std::array<u32, 256> bp{};
  const auto source = literal_source();
  std::vector<u8> tmem(source.begin(), source.end());
  gfx::TextureSampler sampler(memory, bp, tmem);
  configure(bp, 1, 1);
  std::array<u32, 1> one{poison};
  sampler.decode(0, 0, std::span(source).first(5), one);
  CHECK(one[0] == 0xff0000ff);
  one.fill(poison);
  CHECK_THROWS(sampler.decode(0, 0, std::span(source).first(4), one));
  CHECK(one[0] == poison);
  configure(bp, 5, 1);
  std::array<u32, 5> five{};
  sampler.decode(0, 0, std::span(source).first(13), five);
  CHECK((five == std::array<u32, 5>{0xff0000ff, 0x00ff00ff, 0x9f5f00ff, 0x5f9f00ff, 0x0000ffff}));
  five.fill(poison);
  CHECK_THROWS(sampler.decode(0, 0, std::span(source).first(12), five));
  CHECK(five[0] == 0xff0000ff && five[3] == 0x5f9f00ff && five[4] == poison);
  configure(bp, 1, 5);
  std::array<u32, 5> vertical{};
  sampler.decode(0, 0, std::span(source).first(21), vertical);
  CHECK(vertical[0] == 0xff0000ff && vertical[4] == 0x848284ff);
  configure(bp, 8, 8);
  for (unsigned length : {5u, 13u, 31u}) {
    const unsigned prefix = length == 5 ? 4 : length == 13 ? 12 : 60;
    for (bool manual : {false, true}) {
      bp[0x8c] = manual ? 1u << 21 : 0;
      gfx::TextureSampler short_sampler(memory, bp, std::span(tmem).first(length));
      std::array<u32, 64> actual{};
      actual.fill(poison);
      CHECK_THROWS(short_sampler.decode(0, 0, std::span(source).first(length), actual));
      constexpr std::array<std::array<unsigned, 4>, 4> selects{
          {{0, 1, 2, 3}, {3, 2, 1, 0}, {1, 1, 1, 1}, {2, 2, 2, 2}}};
      for (unsigned i = 0; i < 64; ++i) {
        const unsigned x = i % 8, y = i / 8;
        const u32 expected =
            i < prefix ? literal_colors[(y / 4) * 2 + x / 4][selects[y % 4][x % 4]] : poison;
        CHECK(actual[i] == expected);
      }
    }
  }
  // Last aligned TMEM block is valid; one byte less is not. Odd bank remains unused.
  std::vector<u8> large_tmem(1024 * 1024);
  std::copy(source.begin(), source.end(), large_tmem.end() - 32);
  bp[0x8c] = (1u << 21) | 32767;
  bp[0x90] = 32767;
  gfx::TextureSampler tail(memory, bp, large_tmem);
  std::array<u32, 64> actual{};
  tail.decode(0, 0, {}, actual);
  CHECK(actual[0] == 0xff0000ff && actual[7] == 0x7f7fff00);
  gfx::TextureSampler short_tail(memory, bp, std::span(large_tmem).first(large_tmem.size() - 1));
  actual.fill(poison);
  CHECK_THROWS(short_tail.decode(0, 0, {}, actual));
  CHECK(actual[59] != poison && actual[60] == poison);
  CHECK_THROWS(tail.decode(0, 1, {}, std::span(actual).first(16)));
}

std::span<const u8> bytes(const std::vector<u32> &values) {
  return {reinterpret_cast<const u8 *>(values.data()), values.size() * sizeof(u32)};
}

void aliases() {
  const auto source = literal_source();
  // Expected alias chain asks the unchanged demand sampler for each texel after
  // publishing every preceding output byte. No bulk color-table oracle is used.
  for (bool manual : {false, true})
    for (unsigned source_shift : {0u, 8u})
      for (unsigned shift : {0u, 1u, 8u}) {
        Memory reference_memory(Console::GameCube), actual_memory(Console::GameCube);
        std::array<u32, 256> bp{};
        configure(bp, 8, 8);
        bp[0x8c] = manual ? 1u << 21 : 0;
        std::vector<u32> expected(64 + std::max(shift, source_shift), poison), actual;
        std::memcpy(expected.data() + source_shift, source.data(), source.size());
        actual = expected;
        const auto reference_source = bytes(expected).subspan(source_shift * sizeof(u32));
        const auto actual_source = bytes(actual).subspan(source_shift * sizeof(u32));
        std::array<u8, 32> independent_tmem{};
        gfx::TextureSampler reference(reference_memory, bp,
                                      manual ? reference_source
                                             : std::span<const u8>(independent_tmem));
        for (unsigned y = 0; y < 8; ++y)
          for (unsigned x = 0; x < 8; ++x) {
            if (!manual)
              reference_memory.copy_in(0x2000, reference_source);
            expected[shift + y * 8 + x] = pack(reference.texel(0, x, y));
          }
        gfx::TextureSampler sampler(actual_memory, bp,
                                    manual ? actual_source : std::span<const u8>(independent_tmem));
        sampler.decode(0, 0, manual ? std::span<const u8>{} : actual_source,
                       std::span(actual).subspan(shift, 64));
        CHECK(actual == expected);
      }
}

// Literal multi-tile oracle preserves the original tile/row fetch and store order.
// It checks the exact available-byte failure point before each literal output.
void multi_tile_prefixes() {
  Memory memory(Console::GameCube);
  std::array<u32, 256> bp{};
  constexpr std::array<std::array<unsigned, 4>, 4> selects{
      {{0, 1, 2, 3}, {3, 2, 1, 0}, {1, 1, 1, 1}, {2, 2, 2, 2}}};
  for (const auto size : {std::array<unsigned, 2>{9, 13}, {17, 9}, {16, 8}, {16, 16}, {17, 17}}) {
    const unsigned width = size[0], height = size[1];
    const unsigned count = ((width + 7) / 8) * ((height + 7) / 8);
    std::vector<u8> source(count * 32);
    for (unsigned tile = 0; tile < count; ++tile) {
      const auto data = literal_source();
      std::copy(data.begin(), data.end(), source.begin() + tile * 32);
      if (tile & 1)
        for (unsigned sub = 0; sub < 4; ++sub)
          for (unsigned y = 0; y < 4; ++y)
            source[tile * 32 + sub * 8 + 4 + y] ^= 0xff;
    }
    for (unsigned length : {unsigned(source.size()), 37u, 45u, 63u, unsigned(source.size() - 1)})
      for (bool manual : {false, true}) {
        configure(bp, width, height);
        bp[0x8c] = manual ? 1u << 21 : 0;
        std::vector<u32> expected(std::size_t(width) * height + 2, poison), actual = expected;
        bool expected_throw = false;
        const auto fetch = [&](unsigned offset) {
          if (offset >= length)
            throw gfx::VertexError(manual ? "Texture-memory fetch is out of bounds"
                                          : "Texture decode source is out of bounds");
        };
        std::string expected_error;
        try {
          unsigned tile = 0;
          for (unsigned by = 0; by < height; by += 8)
            for (unsigned bx = 0; bx < width; bx += 8, ++tile)
              for (unsigned y = 0; y < std::min(8u, height - by); ++y)
                for (unsigned x = 0; x < std::min(8u, width - bx); ++x) {
                  const unsigned sub = (y / 4) * 2 + x / 4, offset = tile * 32 + sub * 8;
                  fetch(offset);
                  fetch(offset + 1);
                  fetch(offset + 2);
                  fetch(offset + 3);
                  fetch(offset + 4 + y % 4);
                  const unsigned select = selects[y % 4][x % 4] ^ ((tile & 1) ? 3u : 0u);
                  expected[1 + std::size_t(by + y) * width + bx + x] = literal_colors[sub][select];
                }
        } catch (const gfx::VertexError &error) {
          expected_throw = true;
          expected_error = error.what();
        }
        gfx::TextureSampler sampler(memory, bp, std::span(source).first(length));
        bool actual_throw = false;
        std::string actual_error;
        try {
          sampler.decode(0, 0,
                         manual ? std::span<const u8>{} : std::span<const u8>(source).first(length),
                         std::span(actual).subspan(1, std::size_t(width) * height));
        } catch (const gfx::VertexError &error) {
          actual_throw = true;
          actual_error = error.what();
        }
        CHECK(actual_throw == expected_throw);
        CHECK(actual_error == expected_error);
        CHECK(actual == expected);
      }
  }
}

void multi_tile_aliases() {
  for (const auto size : {std::array<unsigned, 2>{16, 8}, {17, 9}})
    for (bool manual : {false, true})
      for (unsigned shift : {0u, 1u, 8u}) {
        const unsigned width = size[0], height = size[1];
        const unsigned count = ((width + 7) / 8) * ((height + 7) / 8);
        Memory reference_memory(Console::GameCube), actual_memory(Console::GameCube);
        std::array<u32, 256> bp{};
        configure(bp, width, height);
        bp[0x8c] = manual ? 1u << 21 : 0;
        std::vector<u32> expected(std::size_t(width) * height + shift + 1, poison), actual;
        for (unsigned tile = 0; tile < count; ++tile) {
          const auto source = literal_source();
          std::memcpy(reinterpret_cast<u8 *>(expected.data()) + tile * 32, source.data(), 32);
        }
        actual = expected;
        const auto reference_source = bytes(expected), actual_source = bytes(actual);
        std::array<u8, 32> independent_tmem{};
        gfx::TextureSampler reference(reference_memory, bp,
                                      manual ? reference_source
                                             : std::span<const u8>(independent_tmem));
        for (unsigned by = 0; by < height; by += 8)
          for (unsigned bx = 0; bx < width; bx += 8)
            for (unsigned y = 0; y < std::min(8u, height - by); ++y)
              for (unsigned x = 0; x < std::min(8u, width - bx); ++x) {
                if (!manual)
                  reference_memory.copy_in(0x2000, reference_source);
                expected[shift + std::size_t(by + y) * width + bx + x] =
                    pack(reference.texel(0, bx + x, by + y));
              }
        gfx::TextureSampler sampler(actual_memory, bp,
                                    manual ? actual_source : std::span<const u8>(independent_tmem));
        sampler.decode(0, 0, manual ? std::span<const u8>{} : actual_source,
                       std::span(actual).subspan(shift, std::size_t(width) * height));
        CHECK(actual == expected);
      }
}

} // namespace cmpr_bulk_checks

int main() {
  return run_tests([] {
    encodings();
    compressed();
    filtering();
    prepared_decode();
    cmpr_bulk_checks::literals();
    cmpr_bulk_checks::layouts_and_limits();
    cmpr_bulk_checks::minimum_and_partial_writes();
    cmpr_bulk_checks::aliases();
    cmpr_bulk_checks::multi_tile_prefixes();
    cmpr_bulk_checks::multi_tile_aliases();
  });
}
