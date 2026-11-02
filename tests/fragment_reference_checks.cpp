// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in direct fragment comparisons with controlled texture inputs.
#include "Common/MsgHandler.h"
#include "Core/System.h"
#include "VideoBackends/Software/SWBoundingBox.h"
#include "VideoBackends/Software/SWEfbInterface.h"
#include "VideoBackends/Software/Tev.h"
#include "VideoBackends/Software/TextureSampler.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/XFMemory.h"
#include "cupid/gfx/fragment.hpp"
#include <algorithm>
#include <iostream>
#include <type_traits>

XFMemory xfmem{};
Statistics g_stats{};
PixelShaderManager pixel_constants;
std::array<std::array<cupid::gfx::Color, 16>, 8> sampled_textures{};
std::optional<cupid::gfx::Color> collected_pixel;

// The target's actual arithmetic consumes its normal constant object. The test
// supplies a valid System object with no unrelated application subsystems.
namespace Core {

struct System::Impl {};

System::System() = default;

System::~System() = default;

PixelShaderManager &System::GetPixelShaderManager() const {
  return pixel_constants;
}

} // namespace Core

namespace TextureSampler {

void Sample(s32 s, s32 t, s32 lod, bool linear, u8 unit, u8 *output) {
  if (lod != 0 || linear || unit >= sampled_textures.size())
    throw std::runtime_error("Unexpected texture boundary in arithmetic comparison");
  const unsigned x = static_cast<unsigned>(s >> 7) & 3;
  const unsigned y = static_cast<unsigned>(t >> 7) & 3;
  const auto &texel = sampled_textures[unit][y * 4 + x];
  std::copy(texel.begin(), texel.end(), output);
}

} // namespace TextureSampler

namespace EfbInterface {

void BlendTev(u16 x, u16 y, u8 *color) {
  if (x || y || collected_pixel)
    throw std::runtime_error("Unexpected downstream pixel in arithmetic comparison");
  collected_pixel = cupid::gfx::Color{color[3], color[2], color[1], color[0]};
}

bool ZCompare(u16, u16, u32) {
  throw std::runtime_error("Depth comparison is outside this arithmetic comparison");
}

void IncPerfCounterQuadCount(PerfQueryType) {
}

} // namespace EfbInterface

namespace BBoxManager {

void Update(u16 left, u16 right, u16 top, u16 bottom) {
  if (left || top || right != 1 || bottom != 1)
    throw std::runtime_error("Unexpected bounding box in arithmetic comparison");
}

} // namespace BBoxManager

namespace Common {

bool MsgAlertFmtImpl(bool, MsgType, Log::LogType, const char *, int, fmt::string_view,
                     const fmt::format_args &) {
  throw std::runtime_error("Unexpected arithmetic comparison diagnostic");
}

namespace Log {

void GenericLogFmtImpl(LogLevel, LogType, const char *, int, fmt::string_view,
                       const fmt::format_args &) {
  throw std::runtime_error("Unexpected arithmetic comparison log");
}

} // namespace Log
} // namespace Common

int main() {
  using namespace cupid;
  cupid::Memory memory(Console::GameCube);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  std::vector<u8> tmem(1024 * 1024);
  gfx::FragmentShader shader(memory, bp, colors, konst, tmem);
  Tev target;
  u64 random = 0xe073281fb9254a6cULL;
  u64 checks = 0;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 7;
    random ^= random << 17;
    return static_cast<u32>(random);
  };
  constexpr std::array<unsigned, 5> channels{0, 1, 5, 6, 7};
  for (unsigned iteration = 0; iteration < 500000; ++iteration) {
    bp.fill(0);
    const unsigned profile = iteration / 100000;
    const bool use_indirect = profile >= 2, use_depth_fog = (profile & 1) != 0;
    const unsigned stages = 1 + next() % 16;
    const unsigned texgens = use_indirect ? 1 + next() % 8 : 1;
    const unsigned indirect_stages = use_indirect ? 1 + next() % 4 : 0;
    bp[0] = texgens | (2 << 4) | ((stages - 1) << 10) | (indirect_stages << 16);
    for (unsigned unit = 0; unit < 8; ++unit) {
      for (unsigned texel = 0; texel < 16; ++texel)
        for (unsigned component = 0; component < 4; ++component)
          sampled_textures[unit][texel][component] = !use_indirect && texel
                                                         ? sampled_textures[unit][0][component]
                                                         : static_cast<u8>(next());
      const unsigned base = 0x80 + (unit & 3) + ((unit & 4) << 3);
      bp[base] = 5;
      bp[base + 8] = (6 << 20) | (3 << 10) | 3;
      bp[base + 20] = (0x10000 + unit * 64) / 32;
      for (unsigned texel = 0; texel < 16; ++texel) {
        const auto &value = sampled_textures[unit][texel];
        const u32 address = 0x10000 + unit * 64 + texel * 2;
        memory.write8(address, value[3]);
        memory.write8(address + 1, value[0]);
        memory.write8(address + 32, value[1]);
        memory.write8(address + 33, value[2]);
      }
    }
    gfx::FragmentInputs input;
    for (unsigned coordinate = 0; coordinate < 8; ++coordinate) {
      const s32 s = use_indirect ? static_cast<s32>(next() % 8192) - (profile == 4 ? 0 : 4096) : 0;
      const s32 t = use_indirect ? static_cast<s32>(next() % 8192) - (profile == 4 ? 0 : 4096) : 0;
      input.uv[coordinate] = {static_cast<float>(s) / 128, static_cast<float>(t) / 128};
      target.Uv[coordinate].s = s;
      target.Uv[coordinate].t = t;
    }
    for (unsigned channel = 0; channel < 2; ++channel)
      for (unsigned component = 0; component < 4; ++component) {
        const u8 value = static_cast<u8>(next());
        input.colors[channel][component] = target.Color[channel][component] = value;
      }
    for (unsigned reg = 0; reg < 4; ++reg)
      for (unsigned component = 0; component < 4; ++component) {
        colors[reg][component] = static_cast<s32>(next() & 2047) - 1024;
        konst[reg][component] = static_cast<s32>(next() & 255);
        pixel_constants.constants.colors[reg][component] = colors[reg][component];
        pixel_constants.constants.kcolors[reg][component] = konst[reg][component];
      }
    for (unsigned stage = 0; stage < stages; ++stage) {
      const u32 order = (next() & 63) | ((next() & 1) << 6) | (channels[next() % 5] << 7);
      bp[0x28 + stage / 2] |= order << ((stage & 1) * 12);
      bp[0xc0 + stage * 2] = next() & 0xffffff;
      bp[0xc1 + stage * 2] = next() & 0xffffff;
      if (use_indirect) {
        const unsigned matrix = next() % 4, matrix_id = matrix ? next() % 3 : 0;
        bp[0x10 + stage] = (next() % indirect_stages) | ((next() % 4) << 2) | ((next() % 8) << 4) |
                           ((next() % 4) << 7) | (matrix << 9) | (matrix_id << 11) |
                           ((next() % 7) << 13) | ((next() % 7) << 16) | ((next() & 1) << 20);
        if (profile == 4)
          bp[0x10 + stage] &= ~(7u << 4);
      }
    }
    for (unsigned stage = 0; stage < indirect_stages; ++stage) {
      bp[0x27] |= (next() & 63) << (stage * 6);
      bp[0x25 + stage / 2] |= (next() & 255) << ((stage & 1) * 8);
    }
    if (use_indirect)
      for (unsigned matrix = 0; matrix < 3; ++matrix) {
        const unsigned scale = profile == 4 ? 17 + next() % 15 : next() % 18;
        for (unsigned column = 0; column < 3; ++column)
          bp[6 + matrix * 3 + column] =
              (profile == 4 ? (next() % 1024) | ((next() % 1024) << 11) : next() & 0x3fffff) |
              (((scale >> (column * 2)) & 3) << 22);
      }
    for (unsigned reg = 0xf6; reg <= 0xfd; ++reg)
      bp[reg] = next() & 0xffffff;
    bp[0xf3] = next() & 0xffffff;
    u32 depth = next() & 0xffffff;
    target.Position[2] = static_cast<s32>(depth);
    if (use_depth_fog) {
      // The authored texel boundary keeps the actual target's sampling and
      // shader constant state isolated from unrelated application subsystems.
      bp[0xf3] = (7 << 16) | (7 << 19);
      bp[0xf4] = next() & 0xffffff;
      bp[0xf5] = next() % 3 | ((next() % 3) << 2);
      const auto fog_bits = [](float value) { return std::bit_cast<u32>(value) >> 12; };
      bp[0xee] = fog_bits(static_cast<float>(static_cast<s32>(next() % 4097) - 2048) / 512);
      bp[0xef] = 0x800000 | (next() & 0x7fffff);
      bp[0xf0] = 1 + next() % 7;
      bp[0xf1] = fog_bits(static_cast<float>(static_cast<s32>(next() % 2049) - 1024) / 512) |
                 ((next() & 1) << 20) | ((1 + next() % 7) << 21);
      if (iteration % 251 == 0) {
        bp[0xee] = 0x7f800 | ((next() & 1) << 19);
        bp[0xf1] = (bp[0xf1] & 0xf00000) | 0x7f800 | ((next() & 1) << 19);
      }
      bp[0xf2] = next() & 0xffffff;
      input.viewport_width = xfmem.viewport.wd = 320;
      if (next() & 1) {
        // Noncentral samples keep both actual coefficient indices within the
        // ten defined values, including their interpolation boundaries.
        bp[0xe8] = 1024 | (343 + next() % 319);
        for (unsigned coefficient = 0; coefficient < 5; ++coefficient)
          bp[0xe9 + coefficient] = (1 + next() % 4095) | ((1 + next() % 4095) << 12);
      }
    }
    static_assert(std::is_trivially_copyable_v<BPMemory>);
    static_assert(sizeof(bp) == sizeof(bpmem));
    const auto bytes = std::as_bytes(std::span(bp));
    auto destination = std::as_writable_bytes(std::span(&bpmem, 1));
    std::copy(bytes.begin(), bytes.end(), destination.begin());
    target.SetKonstColors();
    collected_pixel.reset();
    target.Draw();
    shader.validate();
    const auto actual = shader.shade(input, &depth);
    ++checks;
    if (actual != collected_pixel || depth != static_cast<u32>(target.Position[2])) {
      std::cerr << "Fragment divergence iteration=" << iteration << " stages=" << stages
                << " profile=" << profile << " alpha_test=" << std::hex << bp[0xf3]
                << " depth=" << depth << " expected_depth=" << target.Position[2]
                << " fog_a=" << bp[0xee] << " fog_c=" << bp[0xf1] << " fog_range=" << bp[0xe8]
                << '\n';
      for (unsigned stage = 0; stage < stages; ++stage)
        std::cerr << "stage=" << stage << " order=" << bp[0x28 + stage / 2]
                  << " color=" << bp[0xc0 + stage * 2] << " alpha=" << bp[0xc1 + stage * 2] << '\n';
      for (unsigned component = 0; component < 4; ++component)
        std::cerr << "channel=" << component
                  << " actual=" << (actual ? int((*actual)[component]) : -1)
                  << " expected=" << (collected_pixel ? int((*collected_pixel)[component]) : -1)
                  << '\n';
      return 1;
    }
  }
  std::cout << "Passed " << checks
            << " fragment comparisons with signed registers, regular/compare arithmetic, konst, "
               "swaps, texture persistence, alpha tests, indirect coordinates, depth textures "
               "and fog\n";
}
