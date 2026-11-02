// SPDX-License-Identifier: GPL-2.0-or-later
#include "Common/Align.h"
#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"
#include "Common/SpanUtils.h"
#include "VideoCommon/TextureDecoder.h"
#include "VideoCommon/TextureInfo.h"
#include "cupid/gfx/texture.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <vector>

namespace Common::Log {
void GenericLogFmtImpl(LogLevel, LogType, const char *, int, fmt::string_view,
                       const fmt::format_args &) {
  throw std::runtime_error("Unexpected texture layout comparison diagnostic");
}
} // namespace Common::Log

#include "texture_mip_reference_functions.inc"

void texture_mip_reference_checks() {
  using namespace cupid;
  cupid::Memory memory(Console::GameCube);
  std::array<u32, 256> bp{};
  std::vector<u8> tmem(1024 * 1024);
  gfx::TextureSampler sampler(memory, bp, tmem);
  u64 comparisons = 0, digest = 1469598103934665603ull;
  constexpr std::array<unsigned, 11> formats{0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 14};
  constexpr std::array<std::array<unsigned, 2>, 8> sizes{
      {{1, 1}, {3, 5}, {7, 9}, {8, 4}, {9, 8}, {17, 11}, {32, 16}, {63, 47}}};
  for (unsigned seed = 0; seed < 3; ++seed) {
    for (unsigned i = 0; i < tmem.size(); ++i)
      tmem[i] = static_cast<u8>((i * (37 + seed * 12) + (i >> (3 + seed))) ^ (i >> 9));
    for (unsigned unit = 0; unit < 8; ++unit) {
      const unsigned address = 0x80 + (unit & 3) + ((unit & 4) << 3);
      const unsigned even = 0x2000 + unit * 128, odd = 0x22000 + unit * 256;
      const auto even_data = std::span<const u8>(tmem).subspan(even, 65536);
      const auto odd_data = std::span<const u8>(tmem).subspan(odd, 65536);
      const auto palette = std::span<const u8>(tmem).subspan(0x40000, 32768);
      for (unsigned format : formats)
        for (const auto &size : sizes)
          for (unsigned palette_format = 0; palette_format < 3; ++palette_format) {
            bp[address + 8] = (format << 20) | (size[0] - 1) | ((size[1] - 1) << 10);
            bp[address + 12] = (1u << 21) | (even / 32);
            bp[address + 16] = odd / 32;
            bp[address + 24] = (palette_format << 10) | (0x40000 / 512);
            const auto encoding = static_cast<TextureFormat>(format);
            const auto tlut = static_cast<TLUTFormat>(palette_format);
            const TextureInfo reference(unit, even_data, palette, 0, encoding, tlut, size[0],
                                        size[1], true, odd_data, even_data, 10);
            CHECK(reference.IsDataValid());
            for (const auto &mip : reference.GetMipMapLevels()) {
              CHECK(mip.IsDataValid());
              const unsigned width = mip.GetRawWidth(), height = mip.GetRawHeight();
              std::vector<u32> decoded(std::size_t(width) * height);
              sampler.decode(unit, mip.GetLevel(), {}, decoded);
              for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x) {
                  u32 packed = 0;
                  TexDecoder_DecodeTexel(reinterpret_cast<u8 *>(&packed),
                                         {mip.GetData(), mip.GetTextureSize()}, static_cast<int>(x),
                                         static_cast<int>(y), static_cast<int>(width - 1), encoding,
                                         palette, tlut);
                  const auto expected = std::bit_cast<gfx::Color>(packed);
                  CHECK(sampler.texel(unit, x, y, mip.GetLevel()) == expected);
                  const u32 ordered = (u32(expected[0]) << 24) | (u32(expected[1]) << 16) |
                                      (u32(expected[2]) << 8) | expected[3];
                  CHECK(decoded[std::size_t(y) * width + x] == ordered);
                  digest = (digest ^ ordered) * 1099511628211ull;
                  ++comparisons;
                }
            }
          }
    }
  }
  std::cout << "Manual mip source comparisons=" << comparisons << " digest=" << digest << '\n';
}
