// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in XFB comparison invoking the separately compiled actual color/filter/scale path.
#include "Common/MsgHandler.h"
#include "Core/Config/GraphicsSettings.h"
#include "VideoBackends/Software/SWEfbInterface.h"
#include "VideoCommon/BPMemory.h"
#include "cupid/gfx/xfb_copy.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

BPMemory bpmem{};
EFBInterfaceBase::~EFBInterfaceBase() = default;

namespace Config {
const Info<bool> GFX_ENHANCE_DISABLE_COPY_FILTER{{}, false};

u64 GetConfigVersion() {
  return 0; // The immutable test setting uses its actual cached default value.
}

std::optional<std::string> GetAsString(const Location &) {
  throw std::runtime_error("Unexpected application configuration lookup");
}
} // namespace Config

bool TryParse(const std::string &, bool *) {
  throw std::runtime_error("Unexpected application configuration parsing");
}

namespace Common {
bool MsgAlertFmtImpl(bool, MsgType, Log::LogType, const char *, int, fmt::string_view,
                     const fmt::format_args &) {
  throw std::runtime_error("Unexpected XFB comparison diagnostic");
}

namespace Log {
void GenericLogFmtImpl(LogLevel, LogType, const char *, int, fmt::string_view,
                       const fmt::format_args &) {
  throw std::runtime_error("Unexpected XFB comparison log");
}
} // namespace Log
} // namespace Common

int main() {
  try {
    using cupid::u32;
    using cupid::u64;
    cupid::Memory memory(cupid::Console::GameCube);
    std::array<u32, 256> bp{};
    std::vector<u32> pixels(640 * 528);
    std::vector<u8> expected(640 * 528 * 8);
    u64 random = 0x394e6792cb7b1ca3ULL;
    const auto next = [&] {
      random ^= random << 13;
      random ^= random >> 7;
      random ^= random << 17;
      return static_cast<u32>(random);
    };
    constexpr std::array<unsigned, 9> widths{16, 18, 20, 32, 48, 64, 128, 320, 640};
    constexpr std::array<unsigned, 8> heights{1, 3, 4, 7, 16, 32, 64, 128};
    constexpr std::array<unsigned, 12> scales{64,  85,  128, 170, 192, 256,
                                              257, 320, 384, 512, 768, 1024};
    u64 bytes_checked = 0, cases_checked = 0, undefined_cases_rejected = 0;
    for (unsigned source_format = 0; source_format < 4; ++source_format) {
      bpmem.zcontrol.hex = source_format;
      for (unsigned y = 0; y < 528; ++y) {
        for (unsigned x = 0; x < 640; ++x) {
          const u32 packed = next() & 0xffffff;
          auto *actual_pixel =
              EfbInterface::GetPixelPointer(static_cast<u16>(x), static_cast<u16>(y), false);
          for (unsigned byte = 0; byte < 3; ++byte)
            actual_pixel[byte] = static_cast<u8>(packed >> (byte * 8));
          u32 rgba = (packed << 8) | 255;
          if (source_format == 1) {
            rgba = 0;
            for (unsigned channel = 0; channel < 4; ++channel) {
              const u32 component = (packed >> (18 - channel * 6)) & 63;
              rgba = (rgba << 8) | (component << 2) | (component >> 4);
            }
          }
          pixels[y * 640 + x] = rgba;
        }
      }
      const auto color = [&](unsigned x, unsigned y) { return pixels.at(y * 640 + x); };
      for (unsigned iteration = 0; iteration < 2500; ++iteration) {
        const unsigned width = widths[next() % widths.size()];
        const unsigned height = heights[next() % heights.size()];
        const unsigned top = next() % (528 - height + 1);
        const unsigned scale_register = scales[next() % scales.size()];
        const bool inverse = (next() & 1) != 0;
        const unsigned gamma = next() & 3;
        const unsigned stride_mode = next() % 3;
        const unsigned row_bytes = ((width + 15) & ~15u) * 2;
        const unsigned stride = stride_mode == 0   ? row_bytes
                                : stride_mode == 1 ? 32
                                                   : row_bytes + 32;
        bp[0x43] = source_format;
        bp[0x49] = top << 10;
        bp[0x4a] = (width - 1) | ((height - 1) << 10);
        bp[0x4b] = 0x10000 / 32;
        bp[0x4d] = stride / 32;
        bp[0x4e] = scale_register;
        bp[0x52] = (1 << 14) | (inverse ? 1 << 10 : 0) | (gamma << 7) | (next() & 3);
        std::array<unsigned, 7> coefficients{};
        if ((iteration & 7) == 0) {
          coefficients[2] = 1;
          coefficients[3] = 63;
        } else if ((iteration & 7) == 1) {
          coefficients = {8, 8, 16, 0, 16, 8, 8};
        } else {
          for (auto &coefficient : coefficients)
            coefficient = next() & 63;
        }
        bp[0x53] = bp[0x54] = 0;
        for (unsigned tap = 0; tap < 7; ++tap)
          bp[tap < 4 ? 0x53 : 0x54] |= coefficients[tap] << ((tap < 4 ? tap : tap - 4) * 6);
        auto memory_bytes = memory.range(0x10000, expected.size());
        std::fill(memory_bytes.begin(), memory_bytes.end(), 0x6b);
        cupid::gfx::XfbCopyLayout layout;
        try {
          layout = cupid::gfx::copy_efb_xfb(memory, bp, color);
        } catch (const std::runtime_error &error) {
          const std::string reason = error.what();
          if (reason.find("Unsupported XFB copy: staging readback") != 0 &&
              reason.find("Unsupported XFB copy: nearest-row") != 0 &&
              reason.find("Unsupported XFB copy: vertical scale produces") != 0)
            throw;
          if (!std::all_of(memory_bytes.begin(), memory_bytes.end(),
                           [](u8 value) { return value == 0x6b; }))
            throw std::runtime_error("Rejected XFB edge modified RAM");
          ++undefined_cases_rejected;
          continue;
        }
        bpmem.copyTexSrcXY.x = 0;
        bpmem.copyTexSrcXY.y = top;
        bpmem.copyTexSrcWH.x = width - 1;
        bpmem.copyTexSrcWH.y = height - 1;
        bpmem.copyDestStride = stride / 32;
        bpmem.triggerEFBCopy.Hex = bp[0x52];
        bpmem.copyfilter.Low = bp[0x53];
        bpmem.copyfilter.High = bp[0x54];
        std::fill(expected.begin(), expected.end(), 0xa7);
        const float gamma_value = std::array<float, 4>{1, 1.7f, 2.2f, 2.2f}[gamma];
        EfbInterface::EncodeXFB(expected.data(), width,
                                MathUtil::Rectangle<int>(0, static_cast<int>(top),
                                                         static_cast<int>(width),
                                                         static_cast<int>(top + height)),
                                layout.y_scale, gamma_value);
        for (unsigned row = 0; row < layout.height; ++row) {
          for (unsigned byte = 0; byte < layout.copied_row_bytes; ++byte) {
            const std::size_t offset = static_cast<std::size_t>(row) * stride + byte;
            ++bytes_checked;
            if (memory_bytes[offset] != expected[offset]) {
              std::cerr << "XFB divergence source=" << source_format << " case=" << iteration
                        << " width=" << width << " height=" << height << " top=" << top
                        << " scale=" << scale_register << " inverse=" << inverse
                        << " gamma=" << gamma << " stride=" << stride << " byte=" << offset
                        << " actual=" << unsigned(memory_bytes[offset])
                        << " expected=" << unsigned(expected[offset]) << '\n';
              return 1;
            }
          }
          for (unsigned byte = layout.copied_row_bytes; byte < stride; ++byte) {
            const std::size_t offset = static_cast<std::size_t>(row) * stride + byte;
            if (memory_bytes[offset] != 0x6b)
              throw std::runtime_error("XFB readback changed a destination stride gap");
          }
        }
        ++cases_checked;
      }
    }
    std::cout << "Verified " << bytes_checked << " XFB bytes across " << cases_checked
              << " defined generated cases; rejected " << undefined_cases_rejected
              << " undefined extents or unwritten staging/source edges before mutation\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
