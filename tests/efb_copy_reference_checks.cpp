// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in byte comparisons against the separately compiled behavioral encoder.
#include "Common/MsgHandler.h"
#include "VideoBackends/Software/SWEfbInterface.h"
#include "VideoBackends/Software/SWTexture.h"
#include "VideoBackends/Software/TextureEncoder.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/TextureCacheBase.h"
#include "cupid/gfx/efb_copy.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

BPMemory bpmem{};

namespace {
std::vector<u8> packed_efb(640 * 528 * 3 + 4);
constexpr u8 untouched = 0xa7;

[[noreturn]] void unexpected() {
  throw std::runtime_error("Unexpected application path in EFB copy comparison");
}
} // namespace

// Original test-only RAM storage adapters. No conversion or expected pixel logic lives here.
AbstractStagingTexture::AbstractStagingTexture(StagingTextureType type, const TextureConfig &config)
    : m_type(type), m_config(config), m_texel_size(4) {
  if (config.format != AbstractTextureFormat::RGBA8)
    unexpected();
}

AbstractStagingTexture::~AbstractStagingTexture() = default;

namespace SW {
SWStagingTexture::SWStagingTexture(StagingTextureType type, const TextureConfig &config)
    : AbstractStagingTexture(type, config) {
  m_data.resize(static_cast<std::size_t>(config.width) * config.height * GetTexelSize());
  m_map_pointer = reinterpret_cast<char *>(m_data.data());
}

SWStagingTexture::~SWStagingTexture() = default;

void SWStagingTexture::CopyFromTexture(const AbstractTexture *, const MathUtil::Rectangle<int> &,
                                       u32, u32, const MathUtil::Rectangle<int> &) {
  unexpected();
}

void SWStagingTexture::CopyToTexture(const MathUtil::Rectangle<int> &, AbstractTexture *,
                                     const MathUtil::Rectangle<int> &, u32, u32) {
  unexpected();
}

bool SWStagingTexture::Map() {
  unexpected();
}

void SWStagingTexture::Unmap() {
  unexpected();
}

void SWStagingTexture::Flush() {
  unexpected();
}
} // namespace SW

namespace EfbInterface {
u8 *GetPixelPointer(u16 x, u16 y, bool) {
  if (x >= 640 || y >= 528)
    unexpected();
  return packed_efb.data() + (y * 640 + x) * 3;
}

void EncodeXFB(u8 *, u32, const MathUtil::Rectangle<int> &, float, float) {
  unexpected();
}
} // namespace EfbInterface

namespace Common {
bool MsgAlertFmtImpl(bool, MsgType, Log::LogType, const char *, int, fmt::string_view,
                     const fmt::format_args &) {
  unexpected();
}
} // namespace Common

int main() {
  try {
    using cupid::u32;
    using cupid::u64;
    cupid::Memory memory(cupid::Console::GameCube);
    std::array<u32, 256> bp{};
    const TextureConfig config{
        4096, 132, 1, 1, 1, AbstractTextureFormat::RGBA8, 0, AbstractTextureType::Texture_2D};
    SW::SWStagingTexture staging(StagingTextureType::Readback, config);
    auto *expected = reinterpret_cast<u8 *>(staging.GetMappedPointer());
    u64 random = 0x617cbe793aad21e5ULL;
    const auto next = [&] {
      random ^= random << 13;
      random ^= random >> 7;
      random ^= random << 17;
      return static_cast<u32>(random);
    };
    constexpr std::array<std::array<unsigned, 2>, 8> sizes{
        {{1, 1}, {4, 4}, {7, 3}, {9, 9}, {17, 11}, {32, 16}, {63, 47}, {128, 128}}};
    constexpr std::array<std::array<unsigned, 2>, 3> offsets{{{0, 0}, {3, 5}, {233, 191}}};
    u64 cases = 0, bytes_compared = 0;
    for (unsigned seed = 0; seed < 5; ++seed) {
      for (auto &byte : packed_efb)
        byte = static_cast<u8>(next());
      for (unsigned source_format = 0; source_format < 4; ++source_format) {
        const auto raw = [](unsigned x, unsigned y) {
          const auto *pixel = packed_efb.data() + (y * 640 + x) * 3;
          return u32(pixel[0]) | (u32(pixel[1]) << 8) | (u32(pixel[2]) << 16);
        };
        const cupid::gfx::EfbCopySource source{
            [&](unsigned x, unsigned y) {
              const u32 value = raw(x, y);
              if (source_format != 1)
                return (value << 8) | 255;
              u32 rgba = 0;
              for (unsigned channel = 0; channel < 4; ++channel) {
                const u32 component = (value >> (18 - channel * 6)) & 63;
                rgba = (rgba << 8) | (component << 2) | (component >> 4);
              }
              return rgba;
            },
            raw};
        for (unsigned format = 0; format <= 12; ++format) {
          if (source_format == 3 &&
              (format == 2 || format == 3 || format == 4 || format == 5 || format == 7))
            continue;
          for (bool half : {false, true}) {
            for (unsigned intensity_mode = 0; intensity_mode < 4; ++intensity_mode) {
              for (const auto &size : sizes) {
                for (const auto &offset : offsets) {
                  bp[0x43] = source_format;
                  bp[0x49] = offset[0] | (offset[1] << 10);
                  bp[0x4a] = (size[0] - 1) | ((size[1] - 1) << 10);
                  bp[0x4b] = 0x10000 / 32;
                  bp[0x52] = ((((format << 1) & 15) | (format >> 3)) << 3) | (half ? 1 << 9 : 0) |
                             (intensity_mode << 15);
                  const unsigned block_width = format <= 2 || (format >= 7 && format <= 10) ? 8 : 4;
                  const unsigned block_height = format == 0 ? 8 : 4;
                  const unsigned block_bytes = format == 6 ? 64 : 32;
                  const unsigned width = (size[0] - 1) / (half ? 2 : 1) + 1;
                  const unsigned height = (size[1] - 1) / (half ? 2 : 1) + 1;
                  const unsigned row_bytes = (width + block_width - 1) / block_width * block_bytes;
                  const unsigned rows = (height + block_height - 1) / block_height;
                  for (unsigned stride : {row_bytes, row_bytes + 32, 32u, 0u}) {
                    bp[0x4d] = stride / 32;
                    bpmem.zcontrol.hex = source_format;
                    bpmem.copyTexSrcXY.x = offset[0];
                    bpmem.copyTexSrcXY.y = offset[1];
                    bpmem.copyTexSrcWH.x = size[0] - 1;
                    bpmem.copyTexSrcWH.y = size[1] - 1;
                    bpmem.copyDestStride = stride / 32;
                    bpmem.triggerEFBCopy.Hex = bp[0x52];
                    const std::size_t extent = (rows - 1) * stride + row_bytes;
                    std::fill_n(expected, extent + 64, untouched);
                    auto actual = memory.range(0x10000, extent + 64);
                    std::fill(actual.begin(), actual.end(), untouched);
                    const EFBCopyParams params{static_cast<PixelFormat>(source_format),
                                               static_cast<EFBCopyFormat>(format),
                                               source_format == 3,
                                               intensity_mode == 3,
                                               false,
                                               false,
                                               false};
                    TextureEncoder::Encode(
                        &staging, params, width, row_bytes, rows, stride,
                        MathUtil::Rectangle<int>(static_cast<int>(offset[0]),
                                                 static_cast<int>(offset[1]),
                                                 static_cast<int>(offset[0] + size[0]),
                                                 static_cast<int>(offset[1] + size[1])),
                        half, 1.0f, 1.0f);
                    const auto layout = cupid::gfx::copy_efb_texture(memory, bp, source);
                    if (layout.width != width || layout.height != height ||
                        layout.row_bytes != row_bytes || layout.block_rows != rows)
                      throw std::runtime_error("EFB copy layout differs from authored calculation");
                    for (std::size_t byte = 0; byte < actual.size(); ++byte) {
                      ++bytes_compared;
                      if (actual[byte] != expected[byte]) {
                        std::cerr << "EFB copy divergence seed=" << seed
                                  << " source=" << source_format << " format=" << format
                                  << " half=" << half << " intensity=" << intensity_mode
                                  << " size=" << size[0] << 'x' << size[1]
                                  << " offset=" << offset[0] << ',' << offset[1]
                                  << " stride=" << stride << " byte=" << byte
                                  << " actual=" << unsigned(actual[byte])
                                  << " expected=" << unsigned(expected[byte]) << '\n';
                        return 1;
                      }
                    }
                    ++cases;
                  }
                }
              }
            }
          }
        }
      }
    }
    std::cout << "Verified " << bytes_compared << " EFB copy bytes across " << cases
              << " original generated cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
