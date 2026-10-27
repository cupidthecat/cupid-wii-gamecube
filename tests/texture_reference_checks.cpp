// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in texel comparisons using independently compiled behavioral targets.
#include "Common/MsgHandler.h"
#include "Core/HW/Memmap.h"
#include "Core/System.h"
#include "VideoBackends/Software/TextureSampler.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/TextureDecoder.h"
#include "cupid/gfx/texture.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <type_traits>

BPMemory bpmem{};

namespace Core {

struct System::Impl {};

System::System() = default;

System::~System() = default;

Memory::MemoryManager &System::GetMemory() const {
  throw std::runtime_error("Texture comparison requested unrelated address mapping");
}

} // namespace Core

namespace Memory {

std::span<u8> MemoryManager::GetSpanForAddress(u32) const {
  throw std::runtime_error("Texture comparison requested unrelated address mapping");
}

} // namespace Memory

namespace Common {

bool MsgAlertFmtImpl(bool, MsgType, Log::LogType, const char *, int, fmt::string_view,
                     const fmt::format_args &) {
  throw std::runtime_error("Unexpected texture comparison diagnostic");
}

} // namespace Common

#if defined(_MSC_VER)
// COFF resolves discarded full-image entry points before eliminating them.
// This comparison calls the actual texel decoder; full-image decoding is out of scope.
void _TexDecoder_DecodeImpl(u32 *, const u8 *, int, int, TextureFormat, const u8 *, TLUTFormat) {
  throw std::runtime_error("Texture comparison unexpectedly requested a full image decode");
}
#endif

int main() {
  using namespace cupid;
  cupid::Memory memory(Console::GameCube);
  std::array<u32, 256> bp{};
  std::vector<u8> tmem(1024 * 1024), source(65536), odd_source(65536), palette(32768);
  gfx::TextureSampler sampler(memory, bp, tmem);
  u64 random = 0x983a0f458a273c61ULL;
  u64 comparisons = 0;
  u64 sampling_comparisons = 0;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 7;
    random ^= random << 17;
    return static_cast<u32>(random);
  };
  const auto fill = [&](std::span<u8> bytes) {
    for (auto &byte : bytes)
      byte = static_cast<u8>(next());
  };
  constexpr std::array<std::array<unsigned, 2>, 7> dimensions{
      {{1, 1}, {3, 5}, {7, 9}, {8, 4}, {9, 8}, {17, 11}, {63, 47}}};
  constexpr std::array<unsigned, 11> formats{0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 14};
  for (unsigned seed = 0; seed < 12; ++seed) {
    fill(source);
    fill(odd_source);
    fill(palette);
    memory.copy_in(0x10000, source);
    std::copy(source.begin(), source.end(), tmem.begin() + 0x2000);
    std::copy(odd_source.begin(), odd_source.end(), tmem.begin() + 0x22000);
    std::copy(palette.begin(), palette.end(), tmem.begin() + 0x40000);
    std::copy(tmem.begin(), tmem.end(), s_tex_mem.begin());
    for (unsigned format : formats) {
      const auto encoding = static_cast<TextureFormat>(format);
      const unsigned palettes = IsColorIndexed(encoding) ? 3u : 1u;
      for (unsigned palette_format = 0; palette_format < palettes; ++palette_format)
        for (unsigned unit = 0; unit < 8; ++unit)
          for (const auto &size : dimensions)
            for (bool manual : {false, true}) {
              const unsigned base = 0x80 + (unit & 3) + ((unit & 4) << 3);
              bp[base + 8] = (format << 20) | ((size[1] - 1) << 10) | (size[0] - 1);
              bp[base + 12] = manual ? (1u << 21) | (0x2000u / 32) : 0;
              bp[base + 16] = 0x22000 / 32;
              bp[base + 20] = 0x10000 / 32;
              bp[base + 24] = (0x40000 / 512) | (palette_format << 10);
              std::size_t offset = 0;
              const unsigned levels = manual ? 1u : 4u;
              for (unsigned mip = 0; mip < levels; ++mip) {
                const unsigned width = ((size[0] - 1) >> mip) + 1;
                const unsigned height = ((size[1] - 1) >> mip) + 1;
                for (unsigned sample = 0; sample < 48; ++sample) {
                  const unsigned x = sample == 0 ? 0 : sample == 1 ? width - 1 : next() % width;
                  const unsigned y = sample == 0 ? 0 : sample == 1 ? height - 1 : next() % height;
                  u32 packed = 0;
                  auto *bytes = reinterpret_cast<u8 *>(&packed);
                  if (manual && format == 6)
                    TexDecoder_DecodeTexelRGBA8FromTmem(bytes, source, odd_source,
                                                        static_cast<int>(x), static_cast<int>(y),
                                                        static_cast<int>(width - 1));
                  else
                    TexDecoder_DecodeTexel(bytes, std::span<const u8>(source).subspan(offset),
                                           static_cast<int>(x), static_cast<int>(y),
                                           static_cast<int>(width - 1), encoding, palette,
                                           static_cast<TLUTFormat>(palette_format));
                  const auto expected = std::bit_cast<gfx::Color>(packed);
                  const auto actual = sampler.texel(unit, x, y, mip);
                  ++comparisons;
                  if (actual != expected) {
                    std::cerr << "Texture divergence seed=" << seed << " format=" << format
                              << " palette=" << palette_format << " unit=" << unit
                              << " size=" << size[0] << 'x' << size[1] << " manual=" << manual
                              << " mip=" << mip << " coordinate=" << x << ',' << y << '\n';
                    for (unsigned component = 0; component < 4; ++component)
                      std::cerr << "channel=" << component
                                << " actual=" << unsigned(actual[component])
                                << " expected=" << unsigned(expected[component]) << '\n';
                    return 1;
                  }
                }
                const int width_bytes = std::max(static_cast<int>(size[0] >> mip),
                                                 TexDecoder_GetBlockWidthInTexels(encoding));
                const int height_bytes = std::max(static_cast<int>(size[1] >> mip),
                                                  TexDecoder_GetBlockHeightInTexels(encoding));
                offset += static_cast<std::size_t>(
                    TexDecoder_GetTextureSizeInBytes(width_bytes, height_bytes, encoding));
              }
              if (manual)
                for (unsigned wrap_s = 0; wrap_s < 3; ++wrap_s)
                  for (unsigned wrap_t = 0; wrap_t < 3; ++wrap_t) {
                    bp[base] = wrap_s | (wrap_t << 2);
                    static_assert(std::is_trivially_copyable_v<BPMemory>);
                    static_assert(sizeof(bp) == sizeof(bpmem));
                    const auto bytes = std::as_bytes(std::span(bp));
                    auto destination = std::as_writable_bytes(std::span(&bpmem, 1));
                    std::copy(bytes.begin(), bytes.end(), destination.begin());
                    for (bool linear : {false, true})
                      for (unsigned sample = 0; sample < 16; ++sample) {
                        constexpr std::array<s32, 8> boundaries{-513, -128, -65, -64,
                                                                -1,   0,    63,  64};
                        const s32 s = sample < boundaries.size()
                                          ? boundaries[sample]
                                          : static_cast<s32>(next() % (size[0] * 512)) -
                                                static_cast<s32>(size[0] * 256);
                        const s32 t = sample < boundaries.size()
                                          ? boundaries[boundaries.size() - sample - 1]
                                          : static_cast<s32>(next() % (size[1] * 512)) -
                                                static_cast<s32>(size[1] * 256);
                        const s32 lod = static_cast<s32>(next() % 97) - 32;
                        u32 packed = 0;
                        ::TextureSampler::Sample(s, t, lod, linear, static_cast<u8>(unit),
                                                 reinterpret_cast<u8 *>(&packed));
                        const auto expected = std::bit_cast<gfx::Color>(packed);
                        const auto actual = sampler.sample(unit, s, t, lod, linear);
                        ++sampling_comparisons;
                        if (actual != expected) {
                          std::cerr << "Texture sampling divergence seed=" << seed
                                    << " format=" << format << " palette=" << palette_format
                                    << " unit=" << unit << " size=" << size[0] << 'x' << size[1]
                                    << " wrap=" << wrap_s << ',' << wrap_t << " linear=" << linear
                                    << " coordinate=" << s << ',' << t << " lod=" << lod << '\n';
                          for (unsigned component = 0; component < 4; ++component)
                            std::cerr << "channel=" << component
                                      << " actual=" << unsigned(actual[component])
                                      << " expected=" << unsigned(expected[component]) << '\n';
                          return 1;
                        }
                      }
                  }
            }
    }
  }
  std::cout
      << "Passed " << comparisons << " texel comparisons (" << comparisons * 4
      << " component checks) across all texture formats, palettes, units, RAM mips and TMEM\n";
  std::cout << "Passed " << sampling_comparisons
            << " nearest/bilinear sampling comparisons across clamp/repeat/mirror modes, "
               "negative coordinates, fractional boundaries and manual texture banks\n";
}
