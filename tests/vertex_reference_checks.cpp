// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in transform comparison linking the pinned behavioral reference implementation.
#include "VideoBackends/Software/NativeVertexFormat.h"
#include "VideoBackends/Software/TransformUnit.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/XFMemory.h"
#include "cupid/gfx/vertex.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <type_traits>

XFMemory xfmem{};
BPMemory bpmem{};
static_assert(std::is_trivially_copyable_v<XFMemory>);
static_assert(sizeof(XFMemory) == sizeof(std::array<cupid::u32, 0x1058>));

// Test-only diagnostics stop on unexpected reference paths; no application setup is needed.
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
  cupid::Memory memory(cupid::Console::GameCube);
  std::array<cupid::u32, 256> cp{};
  std::array<cupid::u32, 0x1058> xf{};
  cp[0x50] = 1 << 9;
  cp[0x70] = 1 | (4 << 1);
  cupid::gfx::VertexDecoder decoder(memory, cp, xf, 0);
  unsigned long long random = 0x502036198efd7012ull, count = 0;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 7;
    random ^= random << 17;
    return static_cast<cupid::u32>(random);
  };
  const auto put = [&](unsigned address, float value) {
    xf[address] = std::bit_cast<cupid::u32>(value);
  };
  const auto number = [&] { return (static_cast<int>(next() % 2001) - 1000) / 100.0f; };
  const auto equal = [&](float actual, float expected, const char *component) {
    ++count;
    if (std::bit_cast<cupid::u32>(actual) == std::bit_cast<cupid::u32>(expected))
      return true;
    std::cerr << "Transform divergence iteration=" << count << " component=" << component
              << " actual=" << std::hex << std::bit_cast<cupid::u32>(actual)
              << " expected=" << std::bit_cast<cupid::u32>(expected) << '\n';
    return false;
  };
  for (unsigned iteration = 0; iteration < 100000; ++iteration) {
    cupid::gfx::DecodedVertex vertex;
    vertex.position_matrix = static_cast<cupid::u8>(next() % 61);
    vertex.normal_count = 3;
    for (float &component : vertex.position)
      component = number();
    for (auto &normal : vertex.normals)
      for (float &component : normal)
        component = number();
    for (auto &color : vertex.colors)
      for (auto &component : color)
        component = static_cast<cupid::u8>(next());
    for (auto &texture : vertex.texcoords)
      for (auto &component : texture)
        component = number();
    for (auto &matrix : vertex.texture_matrices)
      matrix = static_cast<cupid::u8>(next() % 61);
    for (unsigned address = 0; address < 256; ++address)
      put(address, number());
    for (unsigned address = 0x400; address < 0x468; ++address)
      put(address, number());
    for (unsigned address = 0x500; address < 0x608; ++address)
      put(address, number());
    for (unsigned i = 0; i < 6; ++i)
      put(0x1020 + i, number());
    xf[0x1026] = next() & 1;
    xf[0x1012] = next() & 1;
    for (unsigned light = 0; light < 8; ++light) {
      const unsigned base = 0x600 + light * 16;
      xf[base + 3] = next();
      for (unsigned i = 4; i < 10; ++i)
        put(base + i, 0.1f + (next() % 1000) / 1000.0f);
      for (unsigned i = 10; i < 16; ++i)
        put(base + i, number());
    }
    for (unsigned channel = 0; channel < 2; ++channel) {
      xf[0x100a + channel] = next();
      xf[0x100c + channel] = next();
      for (unsigned base : {0x100eu, 0x1010u})
        xf[base + channel] = (next() & 0x787f) | ((next() % 3) << 7) | ((next() % 4) << 9);
    }
    xf[0x103f] = 8;
    for (unsigned texture = 0; texture < 8; ++texture) {
      unsigned source = next() % 13;
      if (source == 2)
        source = 5;
      unsigned type = next() % 4;
      if (texture == 0 && type == 1)
        type = 0;
      xf[0x1040 + texture] = (source << 7) | (type << 4) | ((next() & 1) << 1) |
                             ((type >= 2 ? 0 : next() & 1) << 2) |
                             ((texture == 0 ? 0 : next() % texture) << 12) | ((next() & 7) << 15);
      xf[0x1050 + texture] = (next() % 61) | ((next() & 1) << 8);
    }
    const auto state_bytes = std::as_bytes(std::span(xf));
    const auto reference_bytes = std::as_writable_bytes(std::span(&xfmem, 1));
    std::copy(state_bytes.begin(), state_bytes.end(), reference_bytes.begin());
    InputVertexData input{};
    input.posMtx = vertex.position_matrix;
    input.position = {vertex.position[0], vertex.position[1], vertex.position[2]};
    for (unsigned i = 0; i < 3; ++i)
      input.normal[i] = {vertex.normals[i][0], vertex.normals[i][1], vertex.normals[i][2]};
    for (unsigned i = 0; i < 2; ++i)
      for (unsigned component = 0; component < 4; ++component)
        input.color[i][component] = vertex.colors[i][3 - component];
    for (unsigned i = 0; i < 8; ++i) {
      input.texMtx[i] = vertex.texture_matrices[i];
      input.texCoords[i] = {vertex.texcoords[i][0], vertex.texcoords[i][1]};
    }
    OutputVertexData expected;
    TransformUnit::TransformPosition(&input, &expected);
    TransformUnit::TransformNormal(&input, &expected);
    TransformUnit::TransformColor(&input, &expected);
    TransformUnit::TransformTexCoord(&input, &expected);
    const auto actual = decoder.transform(vertex);
    for (unsigned i = 0; i < 3; ++i)
      if (!equal(actual.model_position[i], expected.mvPosition.data[i], "model"))
        return 1;
    for (unsigned i = 0; i < 4; ++i)
      if (!equal(actual.clip_position[i], expected.projectedPosition.data[i], "clip"))
        return 1;
    for (unsigned i = 0; i < 3; ++i)
      for (unsigned component = 0; component < 3; ++component)
        if (!equal(actual.normals[i][component], expected.normal[i].data[component], "normal"))
          return 1;
    for (unsigned i = 0; i < 2; ++i)
      for (unsigned component = 0; component < 4; ++component) {
        ++count;
        if (actual.colors[i][component] != expected.color[i][component]) {
          std::cerr << "Color divergence iteration=" << iteration << " channel=" << i
                    << " component=" << component
                    << " actual=" << unsigned(actual.colors[i][component])
                    << " expected=" << unsigned(expected.color[i][component]) << '\n';
          return 1;
        }
      }
    for (unsigned i = 0; i < 8; ++i)
      for (unsigned component = 0; component < 3; ++component)
        if (!equal(actual.texcoords[i][component], expected.texCoords[i].data[component],
                   "texcoord"))
          return 1;
  }
  std::cout << "Passed " << count << " transform component comparisons\n";
}
