// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/xfb_copy.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>

using namespace cupid;
using namespace cupid::gfx;

namespace {
std::array<u32, 256> registers(unsigned width = 16, unsigned height = 16) {
  std::array<u32, 256> bp{};
  bp[0x4a] = (width - 1) | ((height - 1) << 10);
  bp[0x4b] = 0x1000 / 32;
  bp[0x4d] = width / 16;
  bp[0x4e] = 256;
  bp[0x52] = (1 << 14) | 3;
  bp[0x53] = (63 << 18) | (1 << 12);
  return bp;
}

void colors_and_gamma() {
  Memory memory(Console::GameCube);
  auto bp = registers();
  for (const auto values : {std::array<u32, 2>{0xff0000ff, 0x525a52f0},
                            {0x00ff00ff, 0x90369022},
                            {0x0000ffff, 0x29f0296e},
                            {0xffffffff, 0xeb80eb80},
                            {0, 0x10801080}}) {
    const auto layout = copy_efb_xfb(memory, bp, [=](unsigned, unsigned) { return values[0]; });
    CHECK(layout.width == 16 && layout.height == 16 && layout.encoded_height == 16);
    CHECK(layout.row_bytes == 32 && layout.copied_row_bytes == 32 && layout.stride == 32);
    for (unsigned offset = 0; offset < 512; offset += 4)
      CHECK(memory.read32(0x1000 + offset) == values[1]);
  }
  bp[0x52] |= 2 << 7;
  copy_efb_xfb(memory, bp, [](unsigned, unsigned) { return 0x404040ff; });
  CHECK(memory.read32(0x1000) == 0x85808580);
  bp[0x52] |= 1 << 7;
  copy_efb_xfb(memory, bp, [](unsigned, unsigned) { return 0x404040ff; });
  CHECK(memory.read32(0x1000) == 0x85808580); // Gamma encoding three aliases encoding two.
  bp = registers();
  bp[0x52] |= 1 << 9; // The XFB caller ignores the texture half-scale flag.
  CHECK(copy_efb_xfb(memory, bp, [](unsigned, unsigned) { return 0; }).height == 16);
}

void filters_and_clamps() {
  Memory memory(Console::GameCube);
  auto bp = registers();
  bp[0x49] = 2 << 10;
  bp[0x53] = 63 | (1 << 6); // Previous line only.
  const auto color = [](unsigned, unsigned y) {
    return y == 1 ? 0xff0000ffu : y >= 18 ? 0x00ff00ffu : 0x0000ffffu;
  };
  copy_efb_xfb(memory, bp, color);
  CHECK(memory.read32(0x1000) == 0x29f0296e);
  bp[0x52] &= ~1u;
  copy_efb_xfb(memory, bp, color);
  CHECK(memory.read32(0x1000) == 0x525a52f0);
  bp[0x53] = 0;
  bp[0x54] = (63 << 6) | (1 << 12); // Next line only.
  copy_efb_xfb(memory, bp, color);
  CHECK(memory.read32(0x1000 + 15 * 32) == 0x29f0296e);
  bp[0x52] &= ~2u;
  copy_efb_xfb(memory, bp, color);
  CHECK(memory.read32(0x1000 + 15 * 32) == 0x90369022);
  bp = registers();
  bp[0x53] = 32 << 12;
  copy_efb_xfb(memory, bp, [](unsigned, unsigned) { return 0xffffffff; });
  CHECK(memory.read32(0x1000) == 0x7d807d80);
  bp[0x53] = (63 << 12) | (63 << 18);
  bp[0x54] = 63;
  copy_efb_xfb(memory, bp, [](unsigned, unsigned) { return 0xffffffff; });
  CHECK(memory.read32(0x1000) == 0xeb80eb80);
}

void scaled_and_strided() {
  Memory memory(Console::Wii);
  auto bp = registers();
  bp[0x4b] = 0x10001000 / 32;
  const auto color = [](unsigned, unsigned y) {
    const u32 gray = (y * 16) & 255;
    return gray * 0x01010100 | 255;
  };
  bp[0x4e] = 128;
  auto layout = copy_efb_xfb(memory, bp, color);
  CHECK(layout.height == 8 && layout.encoded_height == 8 && layout.y_scale == 0.5f);
  const std::array<u8, 8> expected{16, 44, 71, 99, 126, 154, 181, 209};
  for (unsigned row = 0; row < expected.size(); ++row)
    CHECK(memory.read8(0x10001000 + row * 32) == expected[row]);
  bp[0x52] |= 1 << 10;
  layout = copy_efb_xfb(memory, bp, color);
  CHECK(layout.height == 31 && layout.encoded_height == 32 && layout.y_scale == 2);
  CHECK(memory.read8(0x10001000) == 16);
  CHECK(memory.read8(0x10001020) == 30);
  CHECK(memory.read8(0x10001040) == 30);
  CHECK(memory.read8(0x10001000 + 30 * 32) == 222);
  // A short stride reads sequential slices of the compact staging image, as in its caller.
  bp = registers(32, 16);
  bp[0x4d] = 1;
  const auto halves = [](unsigned x, unsigned) { return x < 16 ? 0xff0000ffu : 0x0000ffffu; };
  layout = copy_efb_xfb(memory, bp, halves);
  CHECK(layout.row_bytes == 64 && layout.copied_row_bytes == 32 && layout.height == 16);
  CHECK(memory.read32(0x1000) == 0x525a52f0);
  CHECK(memory.read32(0x1000 + 32 + 4) == 0x29f0296e);
  CHECK(memory.read32(0x1000 + 64) == 0x525a52f0);
}

void clipped_and_full_bounds() {
  Memory memory(Console::GameCube);
  auto bp = registers(640, 528);
  unsigned largest_x = 0, largest_y = 0;
  const auto white = [&](unsigned x, unsigned y) {
    CHECK(x < 640 && y < 528);
    largest_x = std::max(largest_x, x);
    largest_y = std::max(largest_y, y);
    return 0xffffffffu;
  };
  const auto full = copy_efb_xfb(memory, bp, white);
  CHECK(full.width == 640 && full.height == 528 && full.encoded_height == 528);
  CHECK(largest_x == 639 && largest_y == 527);
  CHECK(memory.read32(0x1000 + 640 * 528 * 2 - 4) == 0xeb80eb80);

  // The trigger keeps its raw row count while its source rectangle is clipped. A short
  // staging stride selects the defined compact bytes, including both slices of each row.
  bp = registers(32, 16);
  bp[0x49] = 520 << 10;
  bp[0x4d] = 1;
  const auto clipped = copy_efb_xfb(memory, bp, [](unsigned, unsigned y) {
    CHECK(y >= 520 && y < 528);
    const u32 gray = (y - 520) * 16;
    return (gray * 0x01010100) | 255;
  });
  CHECK(clipped.height == 16 && clipped.encoded_height == 8);
  CHECK(memory.read32(0x1000) == 0x10801080);
  CHECK(memory.read32(0x1000 + 32) == 0x10801080);
  CHECK(memory.read8(0x1000 + 14 * 32) == 112);
  CHECK(memory.read8(0x1000 + 15 * 32) == 112);
}

void atomic_failures() {
  Memory memory(Console::GameCube);
  auto bp = registers();
  auto guarded = memory.range(0x1000, 4096);
  std::fill(guarded.begin(), guarded.end(), 0x6b);
  unsigned samples = 0;
  const auto color = [&](unsigned, unsigned) {
    ++samples;
    return 0;
  };
  const auto reject = [&] {
    CHECK_THROWS(copy_efb_xfb(memory, bp, color));
    CHECK(samples == 0);
    CHECK(std::all_of(guarded.begin(), guarded.end(), [](u8 byte) { return byte == 0x6b; }));
  };
  bp[0x49] = 2;
  reject();
  bp = registers(15);
  reject();
  bp = registers();
  bp[0x4e] = 0;
  reject();
  bp[0x52] |= 1 << 10;
  reject();
  bp = registers(16, 3);
  bp[0x4e] = 128; // Caller requests two rows but the encoder writes one.
  reject();
  bp = registers();
  bp[0x4d] = 0;
  reject();
  bp = registers();
  bp[0x49] = 528 << 10;
  reject();
  bp = registers();
  bp[0x4d] = 2; // Readback would reach unwritten staging bytes.
  reject();
  bp = registers();
  bp[0x4b] = (Memory::mem1_size - 32) / 32;
  reject();
  bp = registers();
  bp[0x4e] = 0xffffff;
  reject();
  bp = registers();
  bp[0x43] = 7;
  reject();
  bp = registers();
  bp[0x52] = 0;
  reject();
  bp = registers();
  CHECK_THROWS(copy_efb_xfb(memory, std::span(bp).first(255), color));
  CHECK_THROWS(copy_efb_xfb(memory, bp, {}));
  CHECK_THROWS(copy_efb_xfb(memory, bp, [&](unsigned, unsigned) {
    if (++samples == 17)
      throw std::runtime_error("Authored XFB source failure");
    return 0;
  }));
  CHECK(std::all_of(guarded.begin(), guarded.end(), [](u8 byte) { return byte == 0x6b; }));
}
} // namespace

int main() {
  return run_tests([] {
    colors_and_gamma();
    filters_and_clamps();
    scaled_and_strided();
    clipped_and_full_bounds();
    atomic_failures();
  });
}
