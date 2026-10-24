// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "hardware_batch_projection_cases.hpp"

namespace {
namespace hardware_batch_test::projection {
using namespace cupid;

constexpr unsigned width = 640, height = 528;
constexpr gfx::FragmentTextureState sentinel{
    {1, 2, 3, 4},
    {5, 6, 7, 8},
    {{{9, 10, 11, 12}, {13, 14, 15, 16}, {17, 18, 19, 20}, {21, 22, 23, 24}}},
    {-777, 888}};

inline void seed(gfx::FragmentShader &shader) {
  shader.restore_texture_state(sentinel.texture, sentinel.raw);
  shader.restore_indirect_coordinates(sentinel.coordinates);
  for (unsigned i = 0; i < 4; ++i)
    shader.restore_indirect_texture(i, sentinel.indirect[i]);
}

inline void configure(Memory &memory, std::array<u32, 256> &bp, unsigned count, unsigned family) {
  std::array<u8, 64> tile{};
  for (unsigned texel = 0; texel < 16; ++texel) {
    tile[texel * 2] = 255;
    tile[texel * 2 + 1] = 200;
    tile[32 + texel * 2] = 20;
    tile[33 + texel * 2] = 40;
  }
  memory.copy_in(0x30000, tile);
  bp[0] = count | (1u << 4) | (family << 16);
  bp[0x28] = 64;
  bp[0x40] = 1 | (7 << 1) | 16;
  bp[0x41] = 24;
  bp[0x88] = 3 | (3 << 10) | (6 << 20);
  bp[0x94] = 0x30000 >> 5;
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xf6] = 4;
  bp[0xf7] = 14;
}

// Independent fragment-input oracle uses actual RasterPlane::at and the CPU
// fragment shader. It does not call the additional admission helper.
inline u64 shade(const gfx::RasterWork &work, unsigned count, gfx::FragmentShader &shader,
                 std::vector<u32> &colors, std::vector<u32> &depths) {
  const HostFloatEnvironment environment;
  u64 fragments = 0;
  for (int by = work.top & ~1; by < work.bottom; by += 2)
    for (int bx = work.left & ~1; bx < work.right; bx += 2) {
      std::array<std::array<gfx::FragmentInputs, 2>, 2> quad{};
      for (unsigned iy = 0; iy < 2; ++iy)
        for (unsigned ix = 0; ix < 2; ++ix) {
          const int x = bx + int(ix), y = by + int(iy);
          auto &input = quad[ix][iy];
          input.x = unsigned(x);
          input.viewport_width = work.viewport_width;
          const float reciprocal = 1.0f / work.reciprocal_w.at(x, y);
          for (unsigned i = 0; i < count; ++i) {
            const float q = work.textures[i][2].at(x, y) * reciprocal;
            const float projection = q != 0 ? reciprocal / q : reciprocal;
            for (unsigned c = 0; c < 2; ++c)
              input.uv[i][c] = work.textures[i][c].at(x, y) * projection;
          }
          for (unsigned i = 0; i < 2; ++i)
            for (unsigned c = 0; c < 4; ++c)
              input.colors[i][c] = u8(std::clamp(work.colors[i][c].at(x, y), 0.0f, 255.0f));
        }
      for (unsigned iy = 0; iy < 2; ++iy)
        for (unsigned ix = 0; ix < 2; ++ix) {
          const int x = bx + int(ix), y = by + int(iy);
          if (x < work.left || x >= work.right || y < work.top || y >= work.bottom)
            continue;
          if (!std::all_of(work.edges.begin(), work.edges.end(), [&](const auto &e) {
                return e.constant + e.dx * s64(y * 16) - e.dy * s64(x * 16) > 0;
              }))
            continue;
          auto input = quad[ix][iy];
          for (unsigned i = 0; i < count; ++i)
            for (unsigned c = 0; c < 2; ++c) {
              input.dx[i][c] = quad[1][0].uv[i][c] - quad[0][0].uv[i][c];
              input.dy[i][c] = quad[0][1].uv[i][c] - quad[0][0].uv[i][c];
            }
          u32 depth = u32(std::clamp(work.depth.at(x, y), 0.0f, 16777215.0f));
          const auto color = shader.shade(input, &depth);
          CHECK(color);
          const auto offset = std::size_t(y) * width + unsigned(x);
          colors[offset] = (u32((*color)[0]) << 16) | (u32((*color)[1]) << 8) | (*color)[2];
          depths[offset] = depth;
          ++fragments;
        }
    }
  return fragments;
}

inline void direct(bool additional) {
  const HostFloatEnvironment environment;
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  gfx::HardwareRenderer renderer(memory, tmem);
  unsigned rows = 0;
  u64 dispatches = 0, fallbacks = 0;
  for (unsigned family : {0u, 1u})
    for (const auto &test : projection_cases::authored()) {
      // Invalid integer bounds/count and overflow-prone old-origin inputs are
      // helper-only tests. The unchanged legacy guard is outside this contract.
      if (test.name.starts_with("D9_") || test.name.starts_with("D10_") ||
          test.name.starts_with("D11_") || test.name.starts_with("D12_"))
        continue;
      std::array<u32, 256> bp{};
      configure(memory, bp, test.count, family);
      gfx::TevRegisters tev{}, konst{};
      gfx::FragmentShader software(memory, bp, tev, konst, tmem),
          retained(memory, bp, tev, konst, tmem);
      seed(software);
      seed(retained);
      std::vector<u32> pixels(width * height, 0x13579b), depths(pixels.size(), 0x2468ac);
      auto expected_pixels = pixels, expected_depths = depths;
      renderer.upload(pixels, depths);
      CHECK(renderer.begin(bp, tev, konst));
      // This tiny unused Q derivative leaves the legacy coarse domain intact.
      const bool legacy = test.name == "Tiny_input_plane3_field1";
      const bool expected = legacy || (additional && test.extra);
      const bool admitted = renderer.draw(test.work);
      if (admitted != expected)
        throw std::runtime_error("Additional projection direct admission " + test.name +
                                 " expected=" + std::to_string(expected) +
                                 " actual=" + std::to_string(admitted));
      u64 fragments = 0;
      if (admitted) {
        fragments = shade(test.work, test.count, software, expected_pixels, expected_depths);
        ++dispatches;
      } else {
        ++fallbacks;
        CHECK(renderer.fallback_reason() ==
              "Projected texture coordinate range requires the CPU fragment path");
      }
      const auto stats = renderer.synchronize(pixels, depths, &retained);
      CHECK(stats.dispatches == dispatches && stats.fallback_rectangles == fallbacks &&
            stats.fragments == fragments);
      CHECK(pixels == expected_pixels && depths == expected_depths);
      CHECK(retained.texture_state() == software.texture_state());
      if (admitted) {
        CHECK(stats.left == u32(test.work.left & ~1) && stats.top == u32(test.work.top & ~1));
        CHECK(stats.right == u32((test.work.right - 1) | 1) &&
              stats.bottom == u32((test.work.bottom - 1) | 1));
      } else
        CHECK(stats.left == 0xffffffffu && stats.top == 0xffffffffu && stats.right == 0 &&
              stats.bottom == 0);
      if (admitted && (test.name == "A1_W_cross" || test.name == "A3_exact_zero_Q_column"))
        CHECK((retained.texture_state().coordinates == std::array<s32, 2>{32, 16}));
      if (admitted && (test.name == "A2_Q_cross" || test.name == "A4_odd_partial_quad" ||
                       test.name == "A5_bottom_right"))
        CHECK((retained.texture_state().coordinates == std::array<s32, 2>{64, 32}));
      if (admitted && test.name == "A7_inside_UV_bound")
        CHECK((retained.texture_state().coordinates == std::array<s32, 2>{-64, 32}));
      std::cout << "[extra-direct] " << test.name << " family=" << family << " legacy=" << legacy
                << " admitted=" << admitted << " fragments=" << fragments << '\n';
      ++rows;
    }
  // Ordinary >256-pixel old-coarse-safe controls remain scalar; no additional
  // admission is claimed for this path.
  for (unsigned family : {0u, 1u}) {
    std::array<u32, 256> bp{};
    configure(memory, bp, 1, family);
    gfx::TevRegisters tev{}, konst{};
    gfx::FragmentShader software(memory, bp, tev, konst, tmem),
        retained(memory, bp, tev, konst, tmem);
    seed(software);
    seed(retained);
    auto work = projection_cases::seed();
    work.right = work.bottom = 17;
    std::vector<u32> pixels(width * height, 0x13579b), depths(pixels.size(), 0x2468ac);
    auto expected_pixels = pixels, expected_depths = depths;
    renderer.upload(pixels, depths);
    CHECK(renderer.begin(bp, tev, konst));
    CHECK(renderer.draw(work));
    ++dispatches;
    CHECK(shade(work, 1, software, expected_pixels, expected_depths) == 289);
    const auto stats = renderer.synchronize(pixels, depths, &retained);
    CHECK(stats.dispatches == dispatches && stats.fragments == 289 &&
          stats.fallback_rectangles == fallbacks);
    CHECK(pixels == expected_pixels && depths == expected_depths &&
          retained.texture_state() == software.texture_state());
    ++rows;
  }
  CHECK(rows == 86);
  std::cout << "[extra-direct] complete rows=" << rows << " additional=" << additional << '\n';
}

inline void patterned_tile(Memory &memory, unsigned unit = 0) {
  std::array<u8, 64> bytes{};
  for (unsigned y = 0; y < 4; ++y)
    for (unsigned x = 0; x < 4; ++x) {
      const unsigned texel = y * 4 + x;
      const gfx::Color color =
          unit == 0 ? gfx::Color{u8(17 + 37 * x), u8(23 + 41 * y), u8(31 + 13 * x + 7 * y), 255}
                    : gfx::Color{u8(17 + 13 * unit), u8(23 + 17 * unit), u8(31 + 19 * unit), 255};
      bytes[texel * 2] = color[3];
      bytes[texel * 2 + 1] = color[0];
      bytes[32 + texel * 2] = color[1];
      bytes[33 + texel * 2] = color[2];
    }
  memory.copy_in(0x30000 + unit * 64, bytes);
}

inline std::array<u8, 32> state_bytes(const gfx::FragmentTextureState &state) {
  std::array<u8, 32> bytes{};
  unsigned at = 0;
  for (const auto *color : {&state.texture, &state.raw})
    for (u8 channel : *color)
      bytes[at++] = channel;
  for (const auto &color : state.indirect)
    for (u8 channel : color)
      bytes[at++] = channel;
  for (s32 value : state.coordinates)
    for (unsigned byte = 0; byte < 4; ++byte)
      bytes[at++] = u8(u32(value) >> (byte * 8));
  CHECK(at == bytes.size());
  return bytes;
}

inline void pattern_cpu_controls() {
  const HostFloatEnvironment environment;
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  configure(memory, bp, 1, 0);
  bp[0x80] = 1 | (1 << 2); // Repeat makes negative retained coordinates observable too.
  patterned_tile(memory);
  gfx::TextureSampler sampler(memory, bp, tmem);
  for (unsigned y = 0; y < 4; ++y)
    for (unsigned x = 0; x < 4; ++x)
      CHECK((sampler.texel(0, x, y) ==
             gfx::Color{u8(17 + 37 * x), u8(23 + 41 * y), u8(31 + 13 * x + 7 * y), 255}));
  gfx::TevRegisters colors{}, konst{};
  gfx::FragmentShader seeded(memory, bp, colors, konst, tmem),
      unseeded(memory, bp, colors, konst, tmem);
  seed(seeded);
  seed(unseeded);
  bp[0x10] = 1u << 20;
  seeded.restore_indirect_coordinates({128, 256});
  unseeded.restore_indirect_coordinates({0, 0});
  gfx::FragmentInputs input;
  input.uv[0] = {.25f, .125f};
  const auto expected = seeded.shade(input), wrong = unseeded.shade(input);
  CHECK((expected == gfx::Color{54, 105, 58, 255}));
  CHECK((wrong == gfx::Color{17, 23, 31, 255}));
  CHECK(expected != wrong);
  CHECK((seeded.texture_state().coordinates == std::array<s32, 2>{160, 272}));
  auto exact = state_bytes(sentinel);
  CHECK(exact[0] == 1 && exact[4] == 5 && exact[8] == 9 && exact[23] == 24);
  CHECK(exact[24] == 0xf7 && exact[25] == 0xfc && exact[26] == 0xff && exact[27] == 0xff);
  CHECK(exact[28] == 0x78 && exact[29] == 0x03 && exact[30] == 0 && exact[31] == 0);
  bp[0x10] = 0;
  input.uv[0] = {-1, -1};
  CHECK((seeded.shade(input) == gfx::Color{128, 146, 91, 255}));
  std::cout << "{\"event\":\"pattern_CPU_controls\",\"literal_texels\":16,"
               "\"wrong_coordinate_changes_literal_color\":true,\"canonical_state_bytes\":32,"
               "\"GPU_children\":0}\n";
}

inline void mixed_setup(Memory &memory, std::array<u32, 256> &bp) {
  configure(memory, bp, 1, 4);
  bp[0x80] = 1 | (1 << 2);
  bp[0x27] = 1 | (2 << 6) | (3 << 12) | (4 << 18);
  for (unsigned unit = 0; unit < 5; ++unit) {
    patterned_tile(memory, unit);
    const unsigned address = 0x80 + (unit & 3) + ((unit & 4) << 3);
    bp[address + 8] = 3 | (3 << 10) | (6 << 20);
    bp[address + 20] = (0x30000 + unit * 64) >> 5;
  }
}

inline void mixed_reference_cpu_controls() {
  const HostFloatEnvironment environment;
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  mixed_setup(memory, bp);
  gfx::TevRegisters colors{}, konst{};
  gfx::FragmentShader shader(memory, bp, colors, konst, tmem);
  seed(shader);
  std::vector<u32> pixels(width * height, 0x13579b), depths(pixels.size(), 0x2468ac);
  auto old = projection_cases::seed(), extra = old;
  extra.textures[0][2] = {-.5f, 1, 0, 0, 0, 0, 0};
  auto cap = extra;
  cap.right = 26;
  cap.bottom = 10;
  auto ordered = old;
  ordered.textures[0][0].value = .875f;
  ordered.textures[0][1].value = .9375f;
  u64 fragments = shade(old, 1, shader, pixels, depths);
  fragments += shade(extra, 1, shader, pixels, depths);
  const gfx::FragmentTextureState literal{
      {17, 23, 31, 255},
      {17, 23, 31, 255},
      {{{30, 40, 50, 255}, {43, 57, 69, 255}, {56, 74, 88, 255}, {69, 91, 107, 255}}},
      {64, 32}};
  CHECK(state_bytes(shader.texture_state()) == state_bytes(literal));
  fragments += shade(cap, 1, shader, pixels, depths);
  CHECK((shader.texture_state().coordinates == std::array<s32, 2>{1, 0}));
  bp[0x10] = 1u << 20;
  fragments += shade(ordered, 1, shader, pixels, depths);
  CHECK((shader.texture_state().coordinates == std::array<s32, 2>{449, 480}));
  CHECK((shader.texture_state().raw == gfx::Color{128, 146, 91, 255}));
  bp[0x10] = 0;
  fragments += shade(extra, 1, shader, pixels, depths);
  CHECK(fragments == 276 && state_bytes(shader.texture_state()) == state_bytes(literal));
  for (unsigned y = 0; y < height; ++y)
    for (unsigned x = 0; x < width; ++x) {
      const bool covered = x < 26 && y < 10;
      CHECK(pixels[y * width + x] == (covered ? (x == 0 ? 0x80925bu : 0x11171fu) : 0x13579bu));
      CHECK(depths[y * width + x] == (covered ? 73u : 0x2468acu));
    }
  std::cout << "{\"event\":\"mixed_reference_CPU_controls\",\"literal_final_framebuffer\":true,"
               "\"literal_state32\":true,\"fragments\":276,\"GPU_children\":0}\n";
}

inline void mixed(bool additional) {
  const HostFloatEnvironment environment;
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  mixed_setup(memory, bp);
  gfx::TevRegisters colors{}, konst{};
  gfx::FragmentShader reference(memory, bp, colors, konst, tmem),
      retained(memory, bp, colors, konst, tmem);
  seed(reference);
  seed(retained);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::vector<u32> expected_colors(width * height, 0x13579b),
      expected_depths(expected_colors.size(), 0x2468ac);
  auto pixels = expected_colors, depths = expected_depths;
  renderer.upload(pixels, depths);
  u64 pending = 0, observed_hardware_fragments = 0, reference_fragments = 0,
      expected_dispatches = 0, expected_fallbacks = 0;
  bool upload_pending = false;
  unsigned checkpoints = 0;
  const auto equal = [&](std::string_view label) {
    const bool color = pixels == expected_colors, depth = depths == expected_depths,
               state =
                   state_bytes(retained.texture_state()) == state_bytes(reference.texture_state());
    std::cout << "[extra-mixed] checkpoint=" << label << " full_color=" << color
              << " full_depth=" << depth << " full_state32=" << state << '\n';
    CHECK(color && depth && state);
    ++checkpoints;
  };
  const auto synchronize = [&](std::string_view label) {
    const auto stats = renderer.synchronize(pixels, depths, &retained);
    observed_hardware_fragments += stats.fragments;
    if (pending)
      ++expected_dispatches;
    std::cout << "[extra-mixed] observed-sync=" << label << " dispatches=" << stats.dispatches
              << " interval_fragments=" << stats.fragments
              << " fallbacks=" << stats.fallback_rectangles << '\n';
    equal(label);
    CHECK(stats.fragments == pending && stats.dispatches == expected_dispatches &&
          stats.fallback_rectangles == expected_fallbacks);
    if (pending)
      CHECK(stats.left == 0 && stats.top == 0 && stats.right == 1 && stats.bottom == 1);
    else
      CHECK(stats.left == 0xffffffffu && stats.top == 0xffffffffu && stats.right == 0 &&
            stats.bottom == 0);
    pending = 0;
  };
  const auto submit = [&](gfx::RasterWork work, bool expected, std::string_view label) {
    const bool eligible = renderer.begin(bp, colors, konst);
    if (eligible && upload_pending) {
      renderer.upload(pixels, depths);
      upload_pending = false;
    }
    const bool actual = renderer.draw(work);
    CHECK(actual == expected);
    if (actual) {
      const u64 count = shade(work, 1, reference, expected_colors, expected_depths);
      CHECK(count == 4);
      reference_fragments += count;
      pending += count;
    } else {
      ++expected_fallbacks;
      synchronize(label); // The real CPU continuation receives all prior GPU state first.
      const u64 a = shade(work, 1, reference, expected_colors, expected_depths),
                b = shade(work, 1, retained, pixels, depths);
      CHECK(a == b);
      reference_fragments += a;
      upload_pending = true;
      equal("after-CPU-continuation");
    }
  };
  auto old = projection_cases::seed(), extra = old;
  extra.textures[0][2] = {-.5f, 1, 0, 0, 0, 0, 0};
  auto cap = extra;
  cap.right = 26;
  cap.bottom = 10;
  auto ordered = old;
  ordered.textures[0][0].value = .875f;
  ordered.textures[0][1].value = .9375f;
  submit(old, true, "old-safe");
  submit(extra, additional, "extra-or-CPU");
  submit(cap, false, "cap-CPU-handoff");
  bp[0x10] = 1u << 20;
  submit(ordered, false, "ordered-CPU-handoff");
  bp[0x10] = 0;
  submit(extra, additional, "last-extra-or-CPU");
  synchronize("final");
  const auto stats = renderer.synchronize(pixels, depths, &retained);
  equal("clean-sync-preserves-state");
  std::cout << "[extra-mixed] observed-final dispatches=" << stats.dispatches
            << " hardware_fragments=" << observed_hardware_fragments
            << " fallbacks=" << stats.fallback_rectangles
            << " reference_fragments=" << reference_fragments << '\n';
  CHECK(reference_fragments == 276 && observed_hardware_fragments == (additional ? 12u : 4u));
  CHECK(stats.dispatches == (additional ? 2u : 1u) &&
        stats.fallback_rectangles == (additional ? 2u : 4u));
  CHECK(stats.fragments == 0);
  CHECK(checkpoints == (additional ? 6u : 10u));
  std::cout << "[extra-mixed] complete calls=5 full640x528 full_state32=1 additional=" << additional
            << '\n';
}

inline void equal_full(Fixture &software, Fixture &hardware) {
  CHECK(software.graphics.fragment_count() == hardware.graphics.fragment_count());
  for (unsigned y = 0; y < height; ++y)
    for (unsigned x = 0; x < width; ++x) {
      CHECK(software.graphics.color(x, y) == hardware.graphics.color(x, y));
      CHECK(software.graphics.depth(x, y) == hardware.graphics.depth(x, y));
    }
  for (u32 address = 0x0c001010; address <= 0x0c001016; address += 2)
    CHECK(software.graphics.read(address, 2) == hardware.graphics.read(address, 2));
}

inline void setup_cp(Fixture &fixture) {
  std::array<u32, 256> bp{};
  configure(fixture.memory, bp, 1, 0);
  for (unsigned address : {0u, 0x28u, 0x40u, 0x41u, 0x88u, 0x94u, 0xc0u, 0xc1u, 0xf3u})
    fixture.bp(u8(address), bp[address]);
  fixture.bp(0x21, (981u << 12) | 869u);
  fixture.xf(0x1018, {3u << 6});
  fixture.xf(0x103f, {1});
  fixture.xf(0x1040, {2});
  fixture.floats(12, {0, 0, 0, .25f, 0, 0, 0, .125f, 32, 0, 0, 31.5f});
}

inline void cp_cpu_controls() {
  const HostFloatEnvironment environment;
  Fixture fixture;
  setup_cp(fixture);
  const std::array<u32, 1> colors{0x4997d9ff};
  const std::array<std::array<float, 3>, 3> triangle{
      {{-1, -1, -.5f}, {-.875f, -1, -.5f}, {-1, -.875f, -.5f}}};
  fixture.draw(0x90, triangle, colors);
  CHECK(fixture.graphics.fragment_count() == 6);
  unsigned writes = 0;
  for (unsigned y = 0; y < height; ++y)
    for (unsigned x = 0; x < width; ++x) {
      const bool covered = x + y <= 2;
      CHECK(fixture.graphics.color(x, y) == (covered ? 0xc81428ffu : 0x000000ffu));
      if (covered)
        ++writes;
    }
  CHECK(writes == 6);
  std::cout << "{\"event\":\"actual_CPU_CP_serialized_Qcross\",\"fragments\":6,\"full_640x528_"
               "literal_color_oracle\":true,\"GPU_children\":0}\n";
  pattern_cpu_controls();
  mixed_reference_cpu_controls();
}

inline void cp(bool additional) {
  const HostFloatEnvironment environment;
  Fixture software, hardware;
  setup_cp(software);
  setup_cp(hardware);
  patterned_tile(software.memory);
  patterned_tile(hardware.memory);
  software.bp(0x80, 1 | (1 << 2));
  hardware.bp(0x80, 1 | (1 << 2));
  hardware.graphics.set_hardware_rendering(true);
  const std::array<u32, 1> colors{0x4997d9ff};
  const auto draw = [&](unsigned w, unsigned h) {
    const std::array<std::array<float, 3>, 3> triangle{
        {{-1, -1, -.5f}, {float(w) / 32 - 1, -1, -.5f}, {-1, float(h) / 32 - 1, -.5f}}};
    software.draw(0x90, triangle, colors);
    hardware.draw(0x90, triangle, colors);
  };
  // Keep a prior old-safe GPU primitive pending, then the additional route,
  // then a cap rejection. No stats call is allowed between these submissions.
  for (auto *f : {&software, &hardware})
    f->floats(12, {0, 0, 0, .25f, 0, 0, 0, .125f, 0, 0, 0, 1});
  draw(4, 4);
  for (auto *f : {&software, &hardware})
    f->floats(12, {0, 0, 0, .25f, 0, 0, 0, .125f, 32, 0, 0, 31.5f});
  draw(4, 4);
  draw(26, 10);
  // Consume retained coordinate/map state through an existing unsupported
  // ordered indirect operation, then return to another admitted primitive.
  for (auto *f : {&software, &hardware})
    f->bp(0x10, 1u << 20);
  for (auto *f : {&software, &hardware})
    f->floats(12, {0, 0, 0, .875f, 0, 0, 0, .9375f, 0, 0, 0, 1});
  draw(4, 4);
  for (auto *f : {&software, &hardware})
    f->bp(0x10, 0);
  for (auto *f : {&software, &hardware})
    f->floats(12, {0, 0, 0, .25f, 0, 0, 0, .125f, 32, 0, 0, 31.5f});
  draw(4, 4);
  const auto stats = hardware.graphics.hardware_stats();
  std::cout << "[extra-CP] observed dispatches=" << stats.dispatches
            << " hardware_fragments=" << stats.fragments
            << " fallbacks=" << stats.fallback_rectangles << '\n';
  equal_full(software, hardware);
  std::cout << "[extra-CP] full-framebuffer comparison passed\n";
  CHECK(stats.dispatches == (additional ? 2u : 1u));
  CHECK(stats.fragments == (additional ? 18u : 6u));
  CHECK(stats.fallback_rectangles == (additional ? 2u : 4u));
  std::cout << "[extra-CP] complete full640x528 chain dispatches=" << stats.dispatches
            << " hardware_fragments=" << stats.fragments
            << " fallbacks=" << stats.fallback_rectangles << '\n';
}
} // namespace hardware_batch_test::projection
} // namespace
