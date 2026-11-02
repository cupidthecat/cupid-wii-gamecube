// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace projection_staging_test {
constexpr std::array<gfx::Color, 3> pattern{
    {{17, 23, 31, 255}, {54, 23, 44, 255}, {91, 23, 57, 255}}};

void configure(Memory &memory, std::array<u32, 256> &bp, unsigned count) {
  std::array<u8, 64> tile{};
  for (unsigned i = 0; i < pattern.size(); ++i) {
    tile[i * 2] = pattern[i][3];
    tile[i * 2 + 1] = pattern[i][0];
    tile[32 + i * 2] = pattern[i][1];
    tile[33 + i * 2] = pattern[i][2];
  }
  memory.copy_in(0x30000, tile);
  bp[0] = count | (1 << 4) | (1 << 19);
  bp[0x28] = 64;
  bp[0x40] = 1 | (7 << 1) | 16;
  bp[0x41] = 24;
  bp[0x80] = 1 | (1 << 2);
  bp[0x88] = 3 | (3 << 10) | (6 << 20);
  bp[0x94] = 0x30000 >> 5;
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xf6] = 4;
  bp[0xf7] = 14;
}

void direct_checks() {
  const HostFloatEnvironment host;
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  gfx::HardwareRenderer renderer(memory, tmem);
  gfx::FragmentShader retained(memory, bp, colors, konst, tmem);
  std::vector<u32> pixels(640 * 528), depths(pixels.size());

  struct Case {
    u32 w, s, q;
    bool admit;
    unsigned texel;
  };

  // Exact powers isolate reciprocal, Q-product and projection underflow.
  // The two ordinary controls retain GPU admission, including subnormal S.
  constexpr std::array<Case, 10> cases{{
      {0x7f000000, 0x7f000000, 0, false, 1},
      {0xff000000, 0xff000000, 0, false, 1},
      {0x7e800000, 0x3f800000, 0x3f000000, false, 2},
      {0xfe800000, 0x3f800000, 0x3f000000, false, 2},
      {0x3f800000, 0x3f800000, 0, true, 1},
      {0x3f800000, 1, 0, true, 0},
      {0x3f800000, 0x7f000000, 0x7f000000, false, 1},
      {0x3f800000, 0xff000000, 0xff000000, false, 1},
      {0x5d800000, 0x1c800000, 0x1c800000, false, 1},
      {0xdd800000, 0x1c800000, 0x1c800000, false, 1},
  }};
  constexpr gfx::FragmentTextureState sentinel{
      {1, 2, 3, 4},
      {5, 6, 7, 8},
      {{{9, 10, 11, 12}, {13, 14, 15, 16}, {17, 18, 19, 20}, {21, 22, 23, 24}}},
      {-777, 888}};
  unsigned checks = 0, admitted_count = 0;
  u64 fallbacks = 0;
  const auto check = [&](const Case &row, unsigned count, bool scalar, bool unused) {
    configure(memory, bp, count);
    std::fill(pixels.begin(), pixels.end(), 0x13579b);
    std::fill(depths.begin(), depths.end(), 0x2468ac);
    renderer.upload(pixels, depths);
    retained.restore_texture_state(sentinel.texture, sentinel.raw);
    retained.restore_indirect_coordinates(sentinel.coordinates);
    for (unsigned i = 0; i < 4; ++i)
      retained.restore_indirect_texture(i, sentinel.indirect[i]);
    gfx::RasterWork work{};
    work.right = work.bottom = scalar ? 17 : 1;
    work.edges.fill({0, 0, 1});
    work.depth.value = 73;
    work.reciprocal_w.value = std::bit_cast<float>(row.w);
    for (unsigned i = 0; i < count; ++i) {
      work.textures[i][0].value = work.reciprocal_w.value;
      work.textures[i][1].value = 0;
      work.textures[i][2].value = 0;
    }
    const unsigned coordinate = unused ? count - 1 : 0;
    work.textures[coordinate][0].value = std::bit_cast<float>(row.s);
    work.textures[coordinate][2].value = std::bit_cast<float>(row.q);
    CHECK(renderer.begin(bp, colors, konst));
    CHECK(renderer.draw(work) == row.admit);
    auto expected = sentinel;
    const auto stats = renderer.synchronize(pixels, depths, &retained);
    if (row.admit) {
      ++admitted_count;
      const unsigned texel = unused ? 1 : row.texel;
      expected.texture = expected.raw = pattern[texel];
      expected.coordinates = {s32(texel * 128), 0};
      CHECK(stats.fragments == unsigned(work.right * work.bottom));
    } else {
      ++fallbacks;
      CHECK(stats.fragments == 0);
    }
    CHECK(stats.fallback_rectangles == fallbacks);
    if (retained.texture_state() != expected) {
      const auto state = retained.texture_state();
      std::cerr << "Staged state case=" << checks << " W=" << row.w << " S=" << row.s
                << " Q=" << row.q << " count=" << count << " scalar=" << scalar
                << " unused=" << unused << " admitted=" << row.admit
                << " coordinates=" << state.coordinates[0] << ',' << state.coordinates[1]
                << " expected=" << expected.coordinates[0] << ',' << expected.coordinates[1]
                << '\n';
      CHECK(retained.texture_state() == expected);
    }
    for (unsigned y = 0; y < 528; ++y)
      for (unsigned x = 0; x < 640; ++x) {
        const auto &color = pattern[unused ? 1 : row.texel];
        const bool covered = row.admit && x < unsigned(work.right) && y < unsigned(work.bottom);
        const u32 rgb = (u32(color[0]) << 16) | (u32(color[1]) << 8) | color[2];
        CHECK(pixels[y * 640 + x] == (covered ? rgb : 0x13579bu));
        CHECK(depths[y * 640 + x] == (covered ? 73u : 0x2468acu));
      }
    ++checks;
  };
  for (const auto &row : cases)
    for (unsigned count : {1u, 2u, 8u})
      for (bool scalar : {false, true}) {
        check(row, count, scalar, false);
        if (count > 1)
          check(row, count, scalar, true);
      }

  struct Boundary {
    int w, q;
    bool zero_q, admit;
  };

  constexpr std::array<Boundary, 12> boundaries{{
      {123, 0, true, true},
      {124, 0, true, true},
      {125, 0, true, false},
      {126, 0, true, false},
      {0, 123, false, true},
      {0, 124, false, true},
      {0, 125, false, false},
      {0, 126, false, false},
      {60, -66, false, false},
      {60, -65, false, false},
      {60, -64, false, true},
      {60, -63, false, true},
  }};
  const auto power = [](int n) { return u32(n + 127) << 23; };
  for (const auto &row : boundaries)
    for (bool scalar : {false, true})
      check({power(row.w), power(row.zero_q ? row.w : row.q), row.zero_q ? 0 : power(row.q),
             row.admit, 1},
            1, scalar, false);
  CHECK(checks == 124 && admitted_count == 32 && fallbacks == 92);
  std::cout << "Verified 124 staged projection admission and complete EFB/retained-state cases\n";
}

void command_processor_checks() {
  const HostFloatEnvironment host;
  Fixture reference, actual;
  actual.graphics.set_hardware_rendering(true);
  CHECK(!actual.graphics.hardware_device().empty());
  for (auto *fixture : {&reference, &actual}) {
    auto &f = *fixture;
    std::array<u32, 256> bp{};
    configure(f.memory, bp, 1);
    bp[0] &= ~(1u << 19);
    for (unsigned r :
         {0u, 0x28u, 0x40u, 0x41u, 0x80u, 0x88u, 0x94u, 0xc0u, 0xc1u, 0xf3u, 0xf6u, 0xf7u})
      f.bp(u8(r), bp[r]);
    f.xf(0x1018, {3u << 6});
    f.xf(0x103f, {1});
    f.xf(0x1040, {2});
    const auto draw = [&](unsigned offset, float s, float q) {
      f.floats(12, {0, 0, 0, s, 0, 0, 0, 0, 0, 0, 0, q});
      const float x = float(offset) / 32 - 1;
      const std::array<std::array<float, 3>, 3> positions{
          {{x, -1, -.5f}, {x + .125f, -1, -.5f}, {x, -.875f, -.5f}}};
      const std::array<u32, 1> colors{0x4997d9ff};
      f.draw(0x90, positions, colors);
    };
    draw(0, 2, 1);
    // Fetch-enabled large Q underflows the rounded projection. Its retained
    // texture is then consumed by a distinct draw with texture fetch disabled.
    draw(8, 0x1p127f, 0x1p127f);
    f.bp(0x28, 0);
    draw(16, 1, 1);
    f.bp(0x28, 64);
    draw(24, 2, 1);
    f.bp(0x49, 0);
    f.bp(0x4a, 31 | (3 << 10));
    f.bp(0x4b, 0x40000 >> 5);
    f.bp(0x4d, 8);
    f.bp(0x52, 12u << 3);
  }
  CHECK(reference.graphics.fragment_count() == 24 && actual.graphics.fragment_count() == 24);
  for (unsigned y = 0; y < 528; ++y)
    for (unsigned x = 0; x < 640; ++x) {
      u32 color = 255, depth = 0;
      for (unsigned offset : {0u, 8u, 16u, 24u})
        if (x >= offset && x - offset + y <= 2) {
          const auto &c = pattern[offset == 8 || offset == 16 ? 1 : 2];
          color = (u32(c[0]) << 24) | (u32(c[1]) << 16) | (u32(c[2]) << 8) | c[3];
          depth = 500;
        }
      CHECK(reference.graphics.color(x, y) == color);
      CHECK(actual.graphics.color(x, y) == color);
      CHECK(reference.graphics.depth(x, y) == depth);
      CHECK(actual.graphics.depth(x, y) == depth);
    }
  const auto copied = actual.memory.range(0x40000, 512);
  const auto expected = reference.memory.range(0x40000, 512);
  CHECK(std::equal(copied.begin(), copied.end(), expected.begin()));
  const auto stats = actual.graphics.hardware_stats();
  CHECK(stats.fragments == 12 && stats.dispatches == 2 && stats.efb_readbacks == 2);
  CHECK(stats.fallback_rectangles == 2);
  std::cout << "Verified queued GPU, staged CPU fallback, retained texture and EFB copy chain\n";
}

void checks() {
  direct_checks();
  command_processor_checks();
}
} // namespace projection_staging_test
