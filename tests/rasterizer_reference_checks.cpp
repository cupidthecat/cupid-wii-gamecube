// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in coverage/interpolation comparison linking the pinned behavioral implementation.
#include "VideoBackends/Software/Clipper.h"
#include "VideoBackends/Software/NativeVertexFormat.h"
#include "VideoBackends/Software/Rasterizer.h"
#include "VideoBackends/Software/SWEfbInterface.h"
#include "VideoBackends/Software/Tev.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/EFBInterface.h"
#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/GraphicsModSystem/Config/GraphicsMod.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/XFMemory.h"
#include "cupid/gfx/rasterizer.hpp"
#include <iostream>
#include <type_traits>

XFMemory xfmem{};
BPMemory bpmem{};
Statistics g_stats{};
std::unique_ptr<AbstractGfx> g_gfx;
std::unique_ptr<EFBInterfaceBase> g_efb_interface;
std::unique_ptr<VertexManagerBase> g_vertex_manager;
BackendInfo g_backend_info{};
VideoConfig g_ActiveConfig{};

void (*unexpected_reference_destructor)() = [] { std::terminate(); };

GraphicsModGroupConfig::~GraphicsModGroupConfig() {
  // The comparison never engages the optional application configuration group.
  unexpected_reference_destructor();
}

// Test-only linker boundaries for unrelated application paths retained by some linkers.
// Every hook fails if called; the comparison uses the actual clipping and coverage functions.
void Tev::SetKonstColors() {
  throw std::runtime_error("Unexpected reference constant-color setup");
}

void AbstractGfx::StoreViewportAndScissor(const ViewportAndScissor &) {
  throw std::runtime_error("Unexpected reference backend viewport storage");
}

MathUtil::Rectangle<int>
AbstractGfx::ConvertFramebufferRectangle(const MathUtil::Rectangle<int> &,
                                         const AbstractFramebuffer *) const {
  throw std::runtime_error("Unexpected reference backend rectangle conversion");
}

MathUtil::Rectangle<int>
FramebufferManager::ConvertEFBRectangle(const MathUtil::Rectangle<int> &) const {
  throw std::runtime_error("Unexpected reference backend EFB conversion");
}

float FramebufferManager::EFBToScaledXf(float) const {
  throw std::runtime_error("Unexpected reference backend horizontal scaling");
}

float FramebufferManager::EFBToScaledYf(float) const {
  throw std::runtime_error("Unexpected reference backend vertical scaling");
}

void FramebufferManager::ClearEFB(const MathUtil::Rectangle<int> &, bool, bool, bool, u32, u32,
                                  PixelFormat) {
  throw std::runtime_error("Unexpected reference backend clear");
}

void VertexManagerBase::Flush() {
  throw std::runtime_error("Unexpected reference backend vertex flush");
}

bool VertexShaderManager::UseVertexDepthRange() {
  throw std::runtime_error("Unexpected reference shader depth-range selection");
}

struct Sample {
  cupid::u32 x, y, depth;
  std::array<cupid::gfx::Color, 2> colors;
  std::array<std::array<cupid::s32, 2>, 8> coordinates;
  bool operator==(const Sample &) const = default;
};

std::vector<Sample> expected_samples;

// Test-only downstream boundary: retain the interpolated inputs instead of shading them.
void Tev::Draw() {
  Sample sample{};
  sample.x = Position[0];
  sample.y = Position[1];
  sample.depth = Position[2];
  for (unsigned channel = 0; channel < 2; ++channel)
    for (unsigned component = 0; component < 4; ++component)
      sample.colors[channel][component] = Color[channel][component];
  for (unsigned texture = 0; texture < 8; ++texture)
    sample.coordinates[texture] = {Uv[texture].s, Uv[texture].t};
  expected_samples.push_back(sample);
}

namespace EfbInterface {
bool ZCompare(u16, u16, u32) {
  throw std::runtime_error("Unexpected reference depth comparison");
}

void IncPerfCounterQuadCount(PerfQueryType) {
}
} // namespace EfbInterface

namespace Common {
bool MsgAlertFmtImpl(bool, MsgType, Log::LogType, const char *, int, fmt::string_view,
                     const fmt::format_args &) {
  throw std::runtime_error("Behavioral reference emitted an unexpected alert");
}

namespace Log {
void GenericLogFmtImpl(LogLevel, LogType, const char *, int, fmt::string_view,
                       const fmt::format_args &) {
  throw std::runtime_error("Behavioral reference emitted an unexpected log");
}
} // namespace Log
} // namespace Common

int main() {
  std::array<cupid::u32, 256> bp{};
  std::array<cupid::u32, 0x1058> xf{};
  cupid::gfx::Rasterizer actual(bp, xf);
  std::vector<Sample> actual_samples;
  actual.fragment = [&](unsigned x, unsigned y, cupid::u32 depth,
                        const cupid::gfx::FragmentInputs &input) {
    Sample sample{x, y, depth, input.colors, {}};
    for (unsigned texture = 0; texture < 8; ++texture)
      for (unsigned component = 0; component < 2; ++component)
        sample.coordinates[texture][component] =
            static_cast<cupid::s32>(input.uv[texture][component] * 128);
    actual_samples.push_back(sample);
  };
  const auto put = [&](unsigned address, float value) {
    xf[address] = std::bit_cast<cupid::u32>(value);
  };
  unsigned long long random = 0x1500319832120092ull, compared = 0;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 7;
    random ^= random << 17;
    return static_cast<cupid::u32>(random);
  };
  Clipper::Init();
  Rasterizer::Init();
  put(0x101a, 8);
  put(0x101b, 8);
  put(0x101c, 10000);
  put(0x101d, 350);
  put(0x101e, 350);
  put(0x101f, 10000);
  xf[0x103f] = 8;
  bp[0] = 2 << 4 | 8;
  for (unsigned iteration = 0; iteration < 10000; ++iteration) {
    Rasterizer::Init();
    actual.reset();
    bp[0] = (2 << 4) | 8 | ((next() % 4) << 14) | ((next() % 4 == 0) ? 1 << 19 : 0);
    bp[0x20] = ((342 + next() % 4) << 12) | (342 + next() % 4);
    bp[0x21] = ((350 + next() % 8) << 12) | (350 + next() % 8);
    bp[0x59] = 171 | (171 << 10);
    bp[0x22] =
        (6 + next() % 25) | ((6 + next() % 25) << 8) | ((next() & 7) << 16) | ((next() & 7) << 19);
    xf[0x1005] = next() & 1;
    for (unsigned texture = 0; texture < 8; ++texture) {
      bp[0x30 + texture * 2] = (next() % 4) | ((next() & 1) << 18) | ((next() & 1) << 19);
      bp[0x31 + texture * 2] = next() % 4;
    }
    std::array<cupid::gfx::TransformedVertex, 3> vertices;
    std::array<OutputVertexData, 3> reference;
    for (unsigned i = 0; i < 3; ++i) {
      auto &vertex = vertices[i];
      const float w = 0.5f + (next() % 1500) / 1000.0f;
      vertex.clip_position = {(static_cast<int>(next() % 3001) - 1500) / 1000.0f * w,
                              (static_cast<int>(next() % 3001) - 1500) / 1000.0f * w,
                              -float(next() % 1501) / 1000.0f * w, w};
      reference[i].projectedPosition = {vertex.clip_position[0], vertex.clip_position[1],
                                        vertex.clip_position[2], vertex.clip_position[3]};
      for (unsigned channel = 0; channel < 2; ++channel)
        for (unsigned component = 0; component < 4; ++component)
          reference[i].color[channel][component] = vertex.colors[channel][component] =
              static_cast<cupid::u8>(next());
      for (unsigned texture = 0; texture < 8; ++texture) {
        vertex.texcoords[texture] = {(next() % 1601) / 100.0f, (next() % 1601) / 100.0f,
                                     0.5f + (next() % 1501) / 1000.0f};
        reference[i].texCoords[texture] = {
            vertex.texcoords[texture][0] * ((bp[0x30 + texture * 2] & 65535) + 1),
            vertex.texcoords[texture][1] * ((bp[0x31 + texture * 2] & 65535) + 1),
            vertex.texcoords[texture][2]};
      }
    }
    bpmem.genMode.hex = bp[0];
    bpmem.scissorTL.hex = bp[0x20];
    bpmem.scissorBR.hex = bp[0x21];
    bpmem.scissorOffset.hex = bp[0x59];
    bpmem.lineptwidth.hex = bp[0x22];
    for (unsigned texture = 0; texture < 8; ++texture) {
      bpmem.texcoords[texture].s.hex = bp[0x30 + texture * 2];
      bpmem.texcoords[texture].t.hex = bp[0x31 + texture * 2];
    }
    static_assert(std::is_trivially_copyable_v<XFMemory>);
    const auto state_bytes = std::as_bytes(std::span(xf));
    const auto reference_bytes = std::as_writable_bytes(std::span(&xfmem, 1));
    std::copy(state_bytes.begin(), state_bytes.end(), reference_bytes.begin());
    Rasterizer::ScissorChanged();
    expected_samples.clear();
    actual_samples.clear();
    if (bp[0] & (1u << 19)) {
      // Seed a defined persistent depth plane before comparing the frozen primitive.
      const auto saved_mode = bp[0];
      bp[0] &= ~(1u << 19);
      bpmem.genMode.hex = bp[0];
      std::array<OutputVertexData, 3> reference_seed;
      std::array<std::optional<cupid::gfx::TransformedVertex>, 3> seed;
      const std::array<cupid::gfx::Vector4, 3> positions{cupid::gfx::Vector4{-1, -1, -0.5f, 1},
                                                         cupid::gfx::Vector4{-1, 1, -0.5f, 1},
                                                         cupid::gfx::Vector4{1, -1, -0.5f, 1}};
      for (unsigned i = 0; i < 3; ++i) {
        seed[i].emplace();
        seed[i]->clip_position = positions[i];
        reference_seed[i].projectedPosition = {positions[i][0], positions[i][1], positions[i][2],
                                               positions[i][3]};
        for (unsigned texture = 0; texture < 8; ++texture) {
          seed[i]->texcoords[texture] = {0, 0, 1};
          reference_seed[i].texCoords[texture] = {0, 0, 1};
        }
      }
      Clipper::ProcessTriangle(&reference_seed[0], &reference_seed[1], &reference_seed[2]);
      actual.draw(0x90, seed);
      bp[0] = saved_mode;
      bpmem.genMode.hex = bp[0];
      expected_samples.clear();
      actual_samples.clear();
    }
    const unsigned kind = iteration % 3;
    if (kind == 0)
      Clipper::ProcessTriangle(&reference[0], &reference[1], &reference[2]);
    else if (kind == 1)
      Clipper::ProcessLine(&reference[0], &reference[1]);
    else
      for (auto &vertex : reference)
        Clipper::ProcessPoint(&vertex);
    const std::array<std::optional<cupid::gfx::TransformedVertex>, 3> input{
        vertices[0], vertices[1], vertices[2]};
    actual.draw(kind == 0 ? 0x90 : kind == 1 ? 0xa8 : 0xb8, input);
    if (actual_samples != expected_samples) {
      std::cerr << "Raster divergence iteration=" << iteration
                << " actual samples=" << actual_samples.size()
                << " expected samples=" << expected_samples.size() << '\n';
      const auto size = std::min(actual_samples.size(), expected_samples.size());
      for (std::size_t i = 0; i < size; ++i)
        if (actual_samples[i] != expected_samples[i]) {
          const auto &a = actual_samples[i], &e = expected_samples[i];
          std::cerr << "First sample " << i << " actual=" << a.x << ',' << a.y << ',' << a.depth
                    << " expected=" << e.x << ',' << e.y << ',' << e.depth << '\n';
          return 1;
        }
      return 1;
    }
    compared += actual_samples.size();
  }
  std::cout << "Passed " << compared
            << " rasterized sample comparisons over 10000 primitive commands\n";
}
