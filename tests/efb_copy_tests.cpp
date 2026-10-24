// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/efb_copy.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <vector>

using namespace cupid;
using namespace cupid::gfx;

namespace {
std::array<u32, 256> registers(unsigned format, unsigned source_format = 1, unsigned width = 1,
                               unsigned height = 1, bool half = false) {
  std::array<u32, 256> bp{};
  bp[0x43] = source_format;
  bp[0x4a] = (width - 1) | ((height - 1) << 10);
  bp[0x4b] = 0x1000 / 32;
  bp[0x4d] = 512 / 32;
  bp[0x52] = (((format << 1) & 15) | (format >> 3)) << 3;
  bp[0x52] |= half ? 1 << 9 : 0;
  return bp;
}

void color_formats() {
  Memory memory(Console::GameCube);
  const EfbCopySource source{[](unsigned, unsigned) { return 0x9a6538aau; }, {}};

  struct Test {
    unsigned format;
    std::array<u8, 2> prefix;
    unsigned length;
  };

  constexpr std::array<Test, 13> cases{{{0, {0x99, 0}, 1},
                                        {1, {0x9a, 0}, 1},
                                        {2, {0xa9, 0}, 1},
                                        {3, {0xaa, 0x9a}, 2},
                                        {4, {0x9b, 0x27}, 2},
                                        {5, {0x59, 0x63}, 2},
                                        {6, {0xaa, 0x9a}, 2},
                                        {7, {0xaa, 0}, 1},
                                        {8, {0x9a, 0}, 1},
                                        {9, {0x65, 0}, 1},
                                        {10, {0x38, 0}, 1},
                                        {11, {0x65, 0x9a}, 2},
                                        {12, {0x38, 0x65}, 2}}};
  for (const auto &test : cases) {
    auto bp = registers(test.format);
    const auto layout = copy_efb_texture(memory, bp, source);
    CHECK(layout.destination == 0x1000 && layout.width == 1 && layout.height == 1);
    CHECK(layout.block_rows == 1 && layout.stride == 512);
    const auto bytes = memory.range(0x1000, layout.row_bytes);
    if (test.format == 6) {
      CHECK(layout.row_bytes == 64);
      for (unsigned pixel = 0; pixel < 16; ++pixel) {
        CHECK(bytes[pixel * 2] == 0xaa && bytes[pixel * 2 + 1] == 0x9a);
        CHECK(bytes[32 + pixel * 2] == 0x65 && bytes[33 + pixel * 2] == 0x38);
      }
    } else {
      CHECK(layout.row_bytes == 32);
      for (unsigned offset = 0; offset < 32; ++offset)
        CHECK(bytes[offset] == test.prefix[offset % test.length]);
    }
  }
  for (unsigned source_format : {0u, 2u}) {
    auto bp = registers(3, source_format);
    copy_efb_texture(memory, bp, source);
    CHECK(memory.read16(0x1000) == 0xff9a);
    bp = registers(5, source_format);
    copy_efb_texture(memory, bp, source);
    CHECK(memory.read16(0x1000) == 0xcd87);
  }
  auto bp = registers(5);
  copy_efb_texture(memory, bp, {[](unsigned, unsigned) { return 0xffffffdfu; }, {}});
  CHECK(memory.read16(0x1000) == 0x6fff);
  copy_efb_texture(memory, bp, {[](unsigned, unsigned) { return 0xffffffe0u; }, {}});
  CHECK(memory.read16(0x1000) == 0xffff);
}

void intensity_and_halfscale() {
  Memory memory(Console::GameCube);
  const EfbCopySource source{[](unsigned, unsigned) { return 0x9a6538aau; }, {}};
  for (unsigned format : {0u, 1u, 2u, 3u, 8u}) {
    auto bp = registers(format);
    bp[0x52] |= (1 << 15) | (1 << 16);
    copy_efb_texture(memory, bp, source);
    if (format == 0)
      CHECK(memory.read8(0x1000) == 0x77);
    else if (format == 2)
      CHECK(memory.read8(0x1000) == 0xa7);
    else if (format == 3)
      CHECK(memory.read16(0x1000) == 0xaa70);
    else
      CHECK(memory.read8(0x1000) == 0x70);
    bp[0x52] &= ~(1 << 16);
    copy_efb_texture(memory, bp, source);
    CHECK(memory.read8(0x1000) == (format == 0   ? 0x99
                                   : format == 2 ? 0xa9
                                   : format == 3 ? 0xaa
                                                 : 0x9a));
  }
  auto bp = registers(8, 1, 9, 9, true);
  const EfbCopySource six_bit{[](unsigned, unsigned y) {
                                // Raw six-bit values 31,31,1,1 produce 65; averaging expanded
                                // eight-bit values would instead produce 64.
                                return ((y & 1) ? 4u : 125u) << 24;
                              },
                              {}};
  const auto layout = copy_efb_texture(memory, bp, six_bit);
  CHECK(layout.width == 5 && layout.height == 5);
  CHECK(layout.padded_width == 8 && layout.padded_height == 8 && layout.block_rows == 2);
  CHECK(memory.read8(0x1000) == 65 && memory.read8(0x1200) == 65);
  bp[0x43] = 0;
  copy_efb_texture(memory, bp, six_bit);
  CHECK(memory.read8(0x1000) == 64);
}

void depth_formats() {
  Memory memory(Console::GameCube);
  const EfbCopySource source{{}, [](unsigned, unsigned) { return 0xff123456u; }};
  for (bool half : {false, true}) {
    for (unsigned format : {0u, 1u, 6u, 8u, 9u, 10u, 11u, 12u}) {
      auto bp = registers(format, 3, 1, 1, half);
      bp[0x52] |= (1 << 15) | (1 << 16);
      copy_efb_texture(memory, bp, source);
      switch (format) {
      case 0:
        CHECK(memory.read8(0x1000) == 0x11);
        break;
      case 1:
      case 8:
        CHECK(memory.read8(0x1000) == 0x12);
        break;
      case 6:
        CHECK(memory.read16(0x1000) == (half ? 0xff56 : 0xff12));
        CHECK(memory.read16(0x1020) == (half ? 0x3412 : 0x3456));
        break;
      case 9:
        CHECK(memory.read8(0x1000) == 0x34);
        break;
      case 10:
        CHECK(memory.read8(0x1000) == 0x56);
        break;
      case 11:
        CHECK(memory.read16(0x1000) == (half ? 0x3456 : 0x3412));
        break;
      case 12:
        CHECK(memory.read16(0x1000) == (half ? 0x1234 : 0x5634));
        break;
      }
    }
  }
}

void full_efb_bounds() {
  Memory memory(Console::GameCube);
  auto bp = registers(8, 0, 640, 528);
  bp[0x4d] = 2560 / 32;
  unsigned samples = 0;
  const EfbCopySource source{[&](unsigned x, unsigned y) {
                               CHECK(x < 640 && y < 528);
                               ++samples;
                               return ((3 * x + 5 * y) & 255) << 24;
                             },
                             {}};
  auto layout = copy_efb_texture(memory, bp, source);
  CHECK(layout.row_bytes == 2560 && layout.block_rows == 132);
  CHECK(samples == 640 * 528);
  CHECK(memory.read8(0x1000) == 0);
  CHECK(memory.read8(0x1000 + 640 * 528 - 1) == ((3 * 639 + 5 * 527) & 255));
  samples = 0;
  bp[0x52] |= 1 << 9;
  bp[0x4d] = 1280 / 32;
  layout = copy_efb_texture(memory, bp, source);
  CHECK(layout.width == 320 && layout.height == 264);
  CHECK(layout.row_bytes == 1280 && layout.block_rows == 66);
  CHECK(samples == 640 * 528);
  CHECK(memory.read8(0x1000) == 4);
  const unsigned last = (((3 * 638 + 5 * 526) & 255) + ((3 * 639 + 5 * 526) & 255) +
                         ((3 * 638 + 5 * 527) & 255) + ((3 * 639 + 5 * 527) & 255)) /
                        4;
  CHECK(memory.read8(0x1000 + 320 * 264 - 1) == last);
}

void tiles_stride_and_bounds() {
  Memory memory(Console::Wii);
  auto bp = registers(8, 0, 13, 7);
  bp[0x49] = 3 | (5 << 10);
  bp[0x4d] = 96 / 32;
  bp[0x4b] = 0x10001000 / 32;
  auto destination = memory.range(0x10001000, 192);
  std::fill(destination.begin(), destination.end(), 0xa5);
  unsigned samples = 0;
  const EfbCopySource source{[&](unsigned x, unsigned y) {
                               ++samples;
                               return ((x + 17 * y) & 255) << 24;
                             },
                             {}};
  const auto layout = copy_efb_texture(memory, bp, source);
  CHECK(layout.row_bytes == 64 && layout.block_rows == 2 && samples == 128);
  CHECK(layout.padded_width == 16 && layout.padded_height == 8);
  for (unsigned row = 0; row < 2; ++row) {
    for (unsigned block = 0; block < 2; ++block)
      for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 8; ++x)
          CHECK(destination[row * 96 + block * 32 + y * 8 + x] ==
                ((3 + block * 8 + x + 17 * (5 + row * 4 + y)) & 255));
    for (unsigned gap = 64; gap < 96; ++gap)
      CHECK(destination[row * 96 + gap] == 0xa5);
  }
  // Rows can overlap. Later block rows replace earlier bytes at the same physical address.
  bp[0x4d] = 1;
  copy_efb_texture(memory, bp, source);
  CHECK(destination[32] == ((3 + 17 * 9) & 255));
  CHECK(destination[64] == ((11 + 17 * 9) & 255));
  bp[0x4d] = 0;
  copy_efb_texture(memory, bp, source);
  CHECK(destination[0] == ((3 + 17 * 9) & 255));
  CHECK(destination[32] == ((11 + 17 * 9) & 255));

  samples = 0;
  bp[0x49] = 637;
  CHECK_THROWS(copy_efb_texture(memory, bp, source));
  CHECK(samples == 0);
  bp = registers(8, 0, 16, 8);
  bp[0x4b] = (Memory::mem1_size - 32) / 32;
  CHECK_THROWS(copy_efb_texture(memory, bp, source));
  CHECK(samples == 0);
  bp = registers(8, 0, 16, 8);
  bp[0x4d] = 0xffffff;
  CHECK_THROWS(copy_efb_texture(memory, bp, source));
  CHECK(samples == 0);
  bp = registers(8, 0, 16, 8);
  auto guarded = memory.range(0x1000, 1024);
  std::fill(guarded.begin(), guarded.end(), 0x6b);
  const EfbCopySource throwing{[&](unsigned, unsigned) -> u32 {
                                 if (++samples == 17)
                                   throw std::runtime_error("Authored EFB read failure");
                                 return 0;
                               },
                               {}};
  CHECK_THROWS(copy_efb_texture(memory, bp, throwing));
  CHECK(std::all_of(guarded.begin(), guarded.end(), [](u8 byte) { return byte == 0x6b; }));
  samples = 0;
  for (unsigned format : {13u, 14u, 15u}) {
    bp = registers(format);
    CHECK_THROWS(copy_efb_texture(memory, bp, source));
  }
  for (unsigned format : {2u, 3u, 4u, 5u, 7u}) {
    bp = registers(format, 3);
    CHECK_THROWS(copy_efb_texture(memory, bp, source));
  }
  bp = registers(8, 7);
  CHECK_THROWS(copy_efb_texture(memory, bp, source));
  bp = registers(8);
  bp[0x52] |= 1 << 14;
  CHECK_THROWS(copy_efb_texture(memory, bp, source));
  CHECK(samples == 0);
  CHECK_THROWS(copy_efb_texture(memory, std::span(bp).first(255), source));
  bp = registers(8);
  CHECK_THROWS(copy_efb_texture(memory, bp, {}));
}
} // namespace

int main() {
  return run_tests([] {
    color_formats();
    intensity_and_halfscale();
    depth_formats();
    full_efb_bounds();
    tiles_stride_and_bounds();
  });
}
