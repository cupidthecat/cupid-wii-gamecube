// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hardware_batch_support.hpp"

namespace {
namespace hardware_batch_test::gather {
using namespace cupid;

void copy_latch_state_checks() {
  // One context covers both dispatch paths; each row starts with an explicit upload.
  Memory memory{Console::GameCube};
  Memory reference_memory{Console::GameCube};
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  std::vector<u8> tmem(1024 * 1024);
  gfx::FragmentShader shader(memory, bp, colors, konst, tmem);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::cout << "Device: " << renderer.device() << '\n';
  constexpr std::array<std::array<gfx::Color, 5>, 2> texels{{{{{17, 51, 85, 119},
                                                               {23, 47, 71, 95},
                                                               {29, 53, 79, 101},
                                                               {31, 59, 83, 107},
                                                               {37, 61, 89, 109}}},
                                                             {{{129, 65, 33, 241},
                                                               {43, 67, 91, 113},
                                                               {47, 71, 97, 127},
                                                               {53, 73, 101, 131},
                                                               {59, 79, 103, 137}}}}};
  const gfx::FragmentTextureState first{
      {85, 51, 17, 119},
      {17, 51, 85, 119},
      {{{23, 47, 71, 95}, {29, 53, 79, 101}, {31, 59, 83, 107}, {37, 61, 89, 109}}},
      {160, -288}};
  const gfx::FragmentTextureState second{
      {33, 65, 129, 241},
      {129, 65, 33, 241},
      {{{43, 67, 91, 113}, {47, 71, 97, 127}, {53, 73, 101, 131}, {59, 79, 103, 137}}},
      {-192, 352}};
  const gfx::FragmentTextureState sentinel{
      {1, 2, 3, 4},
      {5, 6, 7, 8},
      {{{9, 10, 11, 12}, {13, 14, 15, 16}, {17, 18, 19, 20}, {21, 22, 23, 24}}},
      {-777, 888}};
  const auto seed_recipient = [&] {
    shader.restore_texture_state(sentinel.texture, sentinel.raw);
    shader.restore_indirect_coordinates(sentinel.coordinates);
    for (unsigned stage = 0; stage < 4; ++stage)
      shader.restore_indirect_texture(stage, sentinel.indirect[stage]);
  };
  const auto fill_textures = [&](unsigned generation) {
    for (unsigned unit = 0; unit < 5; ++unit) {
      std::array<u8, 64> tile{};
      const auto &value = texels[generation][unit];
      for (unsigned pixel = 0; pixel < 16; ++pixel) {
        tile[pixel * 2] = value[3];
        tile[pixel * 2 + 1] = value[0];
        tile[32 + pixel * 2] = value[1];
        tile[33 + pixel * 2] = value[2];
      }
      memory.copy_in(0x80000 + unit * 64, tile);
      const unsigned address = 0x80 + (unit & 3) + ((unit & 4) << 3);
      bp[address + 8] = 3 | (3 << 10) | (6 << 20);
      bp[address + 20] = (0x80000 + unit * 64) >> 5;
    }
  };
  bp[0x27] = 1 | (2 << 6) | (3 << 12) | (4 << 18);
  bp[0x40] = 1 | 14 | 16;
  bp[0x41] = 24;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xf8] = 6;
  bp[0xf9] = 12;
  // Raster output stays constant; texture swap1 exchanges red and blue.
  bp[0xc0] = 0xfffa | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19) | 4;
  u64 completed_draws = 0, completed_readbacks = 0;
  for (const int rectangle_extent : {1, 17, 65}) {
    std::vector<u32> expected_colors(640 * 528, 0x13579b);
    std::vector<u32> expected_depths(expected_colors.size(), 0x2468ac);
    auto gpu_colors = expected_colors, gpu_depths = expected_depths;
    renderer.upload(gpu_colors, gpu_depths);
    u64 draws = 0, pending = 0, readbacks = 0;
    u32 left = 0xffffffff, right = 0, top = 0xffffffff, bottom = 0;
    const auto submit = [&](int x, int y, bool direct, unsigned maps, float s, float t) {
      bp[0] = 1 | (1 << 4) | (maps << 16);
      bp[0x28] = direct ? 64 : 0;
      gfx::RasterWork work{};
      work.left = x;
      work.top = y;
      work.right = x + rectangle_extent;
      work.bottom = y + rectangle_extent;
      // Integer samples satisfying x>=X, y>=Y and x+y<=X+Y select only (X,Y).
      work.edges = {{{0, -1, 1 - 16 * x}, {1, 0, 1 - 16 * y}, {-1, 1, 1 + 16 * (x + y)}}};
      unsigned covered = 0;
      for (int py = work.top; py < work.bottom; ++py)
        for (int px = work.left; px < work.right; ++px)
          covered += std::all_of(work.edges.begin(), work.edges.end(), [&](const auto &edge) {
            return edge.constant + edge.dx * py * 16 - edge.dy * px * 16 > 0;
          });
      CHECK(covered == 1);
      work.depth.value = 73;
      work.reciprocal_w.value = 1;
      for (unsigned channel = 0; channel < 4; ++channel)
        work.colors[0][channel].value = gfx::Color{73, 151, 217, 255}[channel];
      work.textures[0][0].value = s;
      work.textures[0][1].value = t;
      CHECK(renderer.begin(bp, colors, konst));
      CHECK(renderer.draw(work));
      expected_colors[y * 640 + x] = 0x4997d9;
      expected_depths[y * 640 + x] = 73;
      ++draws;
      ++pending;
      left = std::min(left, u32(x) & ~1u);
      right = std::max(right, u32(x) | 1u);
      top = std::min(top, u32(y) & ~1u);
      bottom = std::max(bottom, u32(y) | 1u);
      auto copy_bp = bp;
      const unsigned tile_format = static_cast<unsigned>(draws % 13);
      copy_bp[0x43] = 0;
      copy_bp[0x49] = 4 | (4 << 10);
      copy_bp[0x4a] = 12 | (6 << 10);
      copy_bp[0x4b] = 0x120000 >> 5;
      copy_bp[0x4d] = 1;
      copy_bp[0x52] = ((((tile_format << 1) & 15) | (tile_format >> 3)) << 3);
      const unsigned masks = static_cast<unsigned>(draws % 8);
      copy_bp[0x40] = masks & 4 ? 16 : 0;
      copy_bp[0x41] = ((masks & 1) ? 8 : 0) | ((masks & 2) ? 16 : 0);
      copy_bp[0x4f] = 0x71f4;
      copy_bp[0x50] = 0x38d2;
      copy_bp[0x51] = 0x815392;
      const bool clear = (draws & 1) != 0;
      if (clear)
        copy_bp[0x52] |= 1 << 11;
      const auto layout = gfx::copy_efb_texture(
          reference_memory, copy_bp,
          {[&](unsigned cx, unsigned cy) { return (expected_colors[cy * 640 + cx] << 8) | 255; },
           [&](unsigned cx, unsigned cy) { return expected_depths[cy * 640 + cx]; }});
      CHECK(renderer.copy_texture(copy_bp));
      const auto extent = std::size_t(layout.block_rows - 1) * layout.stride + layout.row_bytes;
      const auto actual = memory.range(layout.destination, extent);
      const auto wanted = reference_memory.range(layout.destination, extent);
      CHECK(std::equal(actual.begin(), actual.end(), wanted.begin()));
      if (clear)
        for (unsigned cy = 4; cy < 11; ++cy)
          for (unsigned cx = 4; cx < 17; ++cx) {
            if (masks & 1)
              expected_colors[cy * 640 + cx] = 0xf438d2;
            if (masks & 4)
              expected_depths[cy * 640 + cx] = copy_bp[0x51];
          }
    };
    const auto synchronize = [&](const gfx::FragmentTextureState &expected, bool recipient = true) {
      if (pending)
        ++readbacks;
      const auto stats =
          renderer.synchronize(gpu_colors, gpu_depths, recipient ? &shader : nullptr);
      CHECK(stats.dispatches == completed_draws + draws && stats.fragments == pending);
      CHECK(stats.fallback_rectangles == 0);
      CHECK(stats.left == left && stats.right == right && stats.top == top &&
            stats.bottom == bottom);
      CHECK(stats.efb_readbacks == completed_readbacks + readbacks);
      CHECK(stats.efb_readback_bytes == (completed_readbacks + readbacks) *
                                            (gpu_colors.size() + gpu_depths.size()) * sizeof(u32));
      CHECK(gpu_colors == expected_colors && gpu_depths == expected_depths);
      CHECK(shader.texture_state() == expected);
      pending = 0;
      left = top = 0xffffffff;
      right = bottom = 0;
    };
    fill_textures(0);
    seed_recipient();
    submit(7, 7, true, 4, 1.25f, -2.25f);
    synchronize(first); // All six restorations select one pixel.

    submit(7, 7, true, 4, 1.25f, -2.25f);
    submit(7, 7, false, 0, 3.25f, -2.5f);
    auto expected = first;
    expected.coordinates = {416, -320};
    synchronize(expected); // New coordinate key, older texture/map keys, same pixel.
    seed_recipient();
    synchronize(sentinel); // A clean synchronization must not restore old keys.
    fill_textures(1);
    submit(7, 7, true, 4, -1.5f, 2.75f);
    synchronize(second); // Same pixel, new dirty synchronization and new values.

    fill_textures(0);
    submit(7, 7, true, 0, 1.25f, -2.25f);
    submit(8, 7, false, 4, 1, 1);
    fill_textures(1);
    submit(7, 8, false, 3, 2, 2);
    fill_textures(0);
    submit(8, 8, false, 2, 3, 3);
    fill_textures(1);
    submit(9, 8, false, 1, 4, 4);
    submit(10, 8, false, 0, 4.5f, -3.75f);
    expected = first;
    expected.indirect = {
        {{43, 67, 91, 113}, {29, 53, 79, 101}, {53, 73, 101, 131}, {37, 61, 89, 109}}};
    expected.coordinates = {576, -480};
    synchronize(expected); // Six distinct sources span both 2x2 parity bits and blocks.

    fill_textures(1);
    submit(7, 7, true, 4, -1.5f, 2.75f);
    synchronize(expected, false); // A null recipient drains, but does not restore.
    seed_recipient();
    synchronize(sentinel);
    submit(7, 7, true, 4, 2.5f, -1.5f);
    expected = second;
    expected.coordinates = {320, -192};
    synchronize(expected);
    completed_draws += draws;
    completed_readbacks += readbacks;
  }
  std::cout << "Verified coincident, distinct and sequence-prefixed latch state across "
               "scalar/batch synchronizations and null-recipient drains\n";
}

void copy_indirect_state_checks() {
  Memory memory{Console::GameCube};
  Memory reference_memory{Console::GameCube};
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  std::vector<u8> texture_memory(1024 * 1024);
  gfx::FragmentShader software(memory, bp, colors, konst, texture_memory);
  gfx::FragmentShader hardware(memory, bp, colors, konst, texture_memory);
  gfx::HardwareRenderer renderer(memory, texture_memory);
  std::cout << "Device: " << renderer.device() << '\n';
  std::vector<u32> cpu_colors(640 * 528), cpu_depths(640 * 528, 500);
  auto gpu_colors = cpu_colors, gpu_depths = cpu_depths;
  std::mt19937 generator(0x4928301);
  std::array<u8, 256> pixels{};
  for (unsigned unit = 0; unit < 5; ++unit) {
    for (auto &byte : pixels)
      byte = static_cast<u8>(generator());
    memory.copy_in(0x80000 + unit * 1024, pixels);
    const unsigned address = 0x80 + (unit & 3) + ((unit & 4) << 3);
    bp[address] = 1 | (1 << 2) | 16;
    bp[address + 8] = 7 | (7 << 10) | (6 << 20);
    bp[address + 20] = (0x80000 + unit * 1024) >> 5;
  }
  bp[0x27] = 1 | (2 << 6) | (3 << 12) | (4 << 18);
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0x41] = 24;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[6] = 513 | (127 << 11) | (1 << 22);
  bp[7] = 65 | (171 << 11) | (3 << 22);
  bp[8] = 8 | (768 << 11) | (1 << 22);
  gfx::RasterWork work{};
  work.left = 3;
  work.top = 5;
  work.right = 13;
  work.bottom = 15;
  work.edges.fill({0, 0, 1});
  work.depth.value = 100;
  work.depth.dx = 5;
  work.depth.dy = 3;
  work.reciprocal_w.value = 1;
  for (auto &color : work.colors)
    for (auto &plane : color) {
      plane.value = 96;
      plane.dx = 2;
      plane.dy = 1;
    }
  work.textures[0][0].value = -4;
  work.textures[0][0].dx = 0.75f;
  work.textures[0][0].dy = 1.25f;
  work.textures[0][1].value = 7;
  work.textures[0][1].dx = -0.5f;
  work.textures[0][1].dy = 0.875f;
  work.textures[1] = work.textures[0];
  work.textures[1][0].value = 3;
  work.textures[1][1].dx = 1.5f;
  u64 hardware_fragments = 0;
  const auto synchronize = [&] {
    const auto stats = renderer.synchronize(gpu_colors, gpu_depths, &hardware);
    hardware_fragments += stats.fragments;
    return stats;
  };
  const auto shade = [&](gfx::FragmentShader &shader, std::vector<u32> &efb,
                         std::vector<u32> &depths) {
    for (int block_y = work.top & ~1; block_y < work.bottom; block_y += 2)
      for (int block_x = work.left & ~1; block_x < work.right; block_x += 2)
        for (int iy = 0; iy < 2; ++iy)
          for (int ix = 0; ix < 2; ++ix) {
            const int x = block_x + ix, y = block_y + iy;
            if (x < work.left || x >= work.right || y < work.top || y >= work.bottom)
              continue;
            const unsigned offset = static_cast<unsigned>(y * 640 + x);
            u32 depth = static_cast<u32>(work.depth.at(x, y));
            const auto depth_test = [&] {
              if (!(bp[0x40] & 1))
                return true;
              if (!gfx::compare((bp[0x40] >> 1) & 7, depth, depths[offset]))
                return false;
              if (bp[0x40] & 16)
                depths[offset] = depth;
              return true;
            };
            const bool early = (bp[0x43] & 64) != 0;
            if (early && !depth_test())
              continue;
            gfx::FragmentInputs inputs;
            for (unsigned channel = 0; channel < 4; ++channel)
              inputs.colors[0][channel] = static_cast<u8>(work.colors[0][channel].at(x, y));
            for (unsigned coordinate = 0; coordinate < (bp[0] & 15); ++coordinate)
              for (unsigned component = 0; component < 2; ++component) {
                const auto &plane = work.textures[coordinate][component];
                inputs.uv[coordinate][component] = plane.at(x, y);
                inputs.dx[coordinate][component] =
                    plane.at(block_x + 1, block_y) - plane.at(block_x, block_y);
                inputs.dy[coordinate][component] =
                    plane.at(block_x, block_y + 1) - plane.at(block_x, block_y);
              }
            const auto result = shader.shade(inputs, &depth);
            if (!result || (!early && !depth_test()))
              continue;
            efb[offset] = (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
          }
  };
  const auto submit = [&](bool expect_hardware = true) {
    const bool supported = renderer.begin(bp, colors, konst);
    CHECK(supported == expect_hardware);
    if (supported)
      CHECK(renderer.draw(work));
    else {
      synchronize();
      shade(hardware, gpu_colors, gpu_depths);
    }
    shade(software, cpu_colors, cpu_depths);
    if (supported) {
      auto copy_bp = bp;
      copy_bp[0x49] = 3 | (5 << 10);
      copy_bp[0x4a] = 7 | (7 << 10);
      copy_bp[0x4b] = 0x80000 >> 5;
      copy_bp[0x4d] = 4;
      copy_bp[0x52] = 12 << 3;
      const auto layout = gfx::copy_efb_texture(
          reference_memory, copy_bp,
          {[&](unsigned x, unsigned y) { return (cpu_colors[y * 640 + x] << 8) | 255; },
           [&](unsigned x, unsigned y) { return cpu_depths[y * 640 + x]; }});
      CHECK(renderer.copy_texture(copy_bp));
      const auto extent = std::size_t(layout.block_rows - 1) * layout.stride + layout.row_bytes;
      const auto actual = memory.range(layout.destination, extent),
                 wanted = reference_memory.range(layout.destination, extent);
      CHECK(std::equal(actual.begin(), actual.end(), wanted.begin()));
    }
  };
  const auto equal_state = [&] {
    synchronize();
    CHECK(software.texture_state() == hardware.texture_state());
    CHECK(cpu_colors == gpu_colors);
    CHECK(cpu_depths == gpu_depths);
  };
  renderer.upload(gpu_colors, gpu_depths);
  bp[0] = 2 | (1 << 4) | (4 << 16);
  bp[0x28] = 64;
  bp[0x10] = 3 | (1 << 9) | (7 << 4) | (3 << 7);
  submit();
  // An XFB copy uses transient work storage while queued texture/map latches
  // remain available to the next draw and later CPU continuation.
  auto copy_bp = bp;
  copy_bp[0x49] = 0;
  copy_bp[0x4a] = 15 | (15 << 10);
  copy_bp[0x4b] = 0x10000 >> 5;
  copy_bp[0x4d] = 1;
  copy_bp[0x4e] = 256;
  copy_bp[0x52] = (1 << 14) | 3;
  copy_bp[0x53] = (1 << 12) | (63 << 18);
  const auto layout = gfx::copy_efb_xfb(memory, copy_bp, [&](unsigned x, unsigned y) {
    return (cpu_colors[y * 640 + x] << 8) | 255;
  });
  const auto bytes = memory.range(layout.destination, layout.height * layout.stride);
  const std::vector<u8> expected_copy(bytes.begin(), bytes.end());
  CHECK(renderer.copy_xfb(copy_bp));
  CHECK(std::equal(bytes.begin(), bytes.end(), expected_copy.begin()));
  // Queue a disabled direct stage and only one indirect stage before reading
  // back. Unwritten texture/map slots must keep the older dispatch's samples.
  bp[0] = 2 | (1 << 4) | (1 << 16);
  bp[0x28] = 0;
  bp[0x10] = 0;
  bp[0xc0] = 0xfffa | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
  work.textures[0][0].value += 9;
  submit();
  equal_state();
  const auto retained = software.texture_state();
  bp[0] = 2 | (1 << 4) | (4 << 16);
  bp[0x28] = 64;
  bp[0x10] = 3 | (1 << 9) | (1 << 7);
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[0xf3] = 0;
  work.textures[0][0].value += 3;
  submit();
  equal_state();
  CHECK(software.texture_state() != retained);
  const auto alpha_rejected = software.texture_state();
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0x40] = 1;
  bp[0x43] = 64;
  work.textures[0][0].value += 4;
  submit();
  equal_state();
  CHECK(software.texture_state() == alpha_rejected);
  bp[0x40] = 1 | 2 | 16;
  for (int y = work.top; y < work.bottom; ++y)
    for (int x = work.left; x < work.right; ++x)
      cpu_depths[static_cast<unsigned>(y * 640 + x)] = (x + y) % 3 == 0 ? 50 : 500;
  gpu_depths = cpu_depths;
  renderer.upload(gpu_colors, gpu_depths);
  submit();
  equal_state();
  CHECK(software.texture_state() != alpha_rejected);
  const auto early_partial = software.texture_state();
  bp[0x40] = 1;
  bp[0x43] = 0;
  work.textures[0][0].value += 5;
  submit();
  equal_state();
  CHECK(software.texture_state() != early_partial);
  // Disabled retained-map sampling and cross-fragment accumulation each
  // continue on the CPU with the complete GPU latch restored.
  bp[0] = 2 | (1 << 4);
  bp[0x40] = 0;
  bp[0x10] = 3 | (1 << 9) | (3 << 7);
  submit(false);
  equal_state();
  bp[0x10] = 1 << 20;
  submit(false);
  equal_state();
  std::cout << "Verified full texture/map/coordinate latches across disabled stages and "
               "alpha/early/late depth rejection\n";
  std::cout << "Latch fixture dispatches: " << synchronize().dispatches
            << ", hardware fragments: " << hardware_fragments << '\n';
}

void gather_first_read_environment_checks(bool require_first_read_flags) {
  const TestFloatEnvironment restore;
  constexpr std::array<int, 4> modes{FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
  unsigned rows = 0;
  for (unsigned mode = 0; mode < modes.size(); ++mode)
    for (unsigned denormal = 0; denormal < 4; ++denormal)
      for (const unsigned extent : {7u, 17u, 65u}) {
        const bool batch = extent <= 64;
        CHECK(std::fesetenv(FE_DFL_ENV) == 0);
        Memory memory{Console::GameCube};
        std::vector<u8> tmem(1024 * 1024);
        std::array<u32, 256> bp{};
        gfx::TevRegisters colors{}, konst{};
        gfx::FragmentShader shader(memory, bp, colors, konst, tmem);
        gfx::HardwareRenderer renderer(memory, tmem);
        std::cout << "Device: " << renderer.device() << '\n';
        const auto get_integer =
            graphics_function<void(APIENTRY *)(GLenum, GLint *)>("glGetIntegerv");
        const auto get_indexed = graphics_function<PFNGLGETINTEGERI_VPROC>("glGetIntegeri_v");
        std::vector<u32> pixels(640 * 528), depths(pixels.size());
        renderer.upload(pixels, depths);
        bp[0] = 1 | (1 << 4);
        bp[0xf6] = 4;
        bp[0xf7] = 14;
        bp[0x41] = 24;
        bp[0xf3] = (7 << 16) | (7 << 19);
        bp[0xc0] = 0xfffa | (1 << 19);
        bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
        gfx::RasterWork work{};
        work.left = 13;
        work.top = 11;
        work.right = work.left + int(extent);
        work.bottom = work.top + int(extent);
        work.edges.fill({0, 0, 1});
        work.reciprocal_w.value = 1;
        work.depth.value = 12345;
        for (unsigned c = 0; c < 4; ++c)
          work.colors[0][c].value = gfx::Color{211, 105, 65, 177}[c];
        const auto submit_flush = [&] {
          CHECK(renderer.begin(bp, colors, konst));
          CHECK(renderer.draw(work));
          if (batch) {
            auto changed = konst;
            changed[0][0] ^= 1;
            CHECK(renderer.begin(bp, colors, changed));
            CHECK(renderer.begin(bp, colors, konst));
          }
        };
        submit_flush();
        GLint null_program = 0;
        get_integer(GL_CURRENT_PROGRAM, &null_program);
        CHECK(null_program != 0);
        const auto null_stats = renderer.synchronize(pixels, depths, nullptr);
        GLint after_null = 0;
        get_integer(GL_CURRENT_PROGRAM, &after_null);
        CHECK(after_null == null_program && null_stats.fragments == extent * extent);
        submit_flush();
        GLint previous_program = 0, copy_buffer = 0, counter_buffer = 0;
        get_integer(GL_CURRENT_PROGRAM, &previous_program);
        get_indexed(GL_SHADER_STORAGE_BUFFER_BINDING, 7, &copy_buffer);
        get_indexed(GL_SHADER_STORAGE_BUFFER_BINDING, 2, &counter_buffer);
        CHECK(previous_program == null_program && copy_buffer != 0 && counter_buffer != 0);
        CHECK(std::fesetround(modes[mode]) == 0);
        CHECK(std::feclearexcept(FE_ALL_EXCEPT) == 0);
        CHECK(std::feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0);
#if defined(_M_X64) || defined(__x86_64__)
        constexpr unsigned simd_modes[]{0u, 0x6000u, 0x4000u, 0x2000u};
        _mm_setcsr((_mm_getcsr() & ~0xe040u) | simd_modes[(mode + 1) % 4] |
                   ((denormal & 1) ? 0x8000u : 0u) | ((denormal & 2) ? 0x40u : 0u));
#endif
        const FloatEnvironmentBits before;
        const auto stats = renderer.synchronize(pixels, depths, &shader);
        const FloatEnvironmentBits after;
        if (require_first_read_flags)
          CHECK(after == before);
        CHECK(after.round == before.round);
#if defined(_M_X64) || defined(__x86_64__)
        CHECK(after.control == before.control);
        CHECK((after.simd & ~0x3fu) == (before.simd & ~0x3fu));
#endif
        CHECK(stats.dispatches == 2 && stats.fragments == extent * extent);
        CHECK(stats.efb_readbacks == 2 && stats.fallback_rectangles == 0);
        GLint program = 0, copy_after = 0, generic = 0;
        get_integer(GL_CURRENT_PROGRAM, &program);
        get_indexed(GL_SHADER_STORAGE_BUFFER_BINDING, 7, &copy_after);
        get_integer(GL_SHADER_STORAGE_BUFFER_BINDING, &generic);
        CHECK(program == previous_program && copy_after == copy_buffer &&
              generic == counter_buffer);
        for (unsigned y = 0; y < 528; ++y)
          for (unsigned x = 0; x < 640; ++x)
            CHECK(pixels[y * 640 + x] ==
                  (x >= 13 && x < 13 + extent && y >= 11 && y < 11 + extent ? 0xd36941u : 0u));
        const FloatEnvironmentBits before_clean;
        const auto clean = renderer.synchronize(pixels, depths, &shader);
        CHECK(FloatEnvironmentBits{} == before_clean);
        CHECK(clean.dispatches == 2 && clean.fragments == 0 && clean.efb_readbacks == 2);
        ++rows;
      }
  std::cout << "Verified " << rows
            << " first non-null completed-read environments after null drains, scalar/batch "
               "program restoration, indexed7 and clean-sync behavior\n";
}

void gather_copy_capacity_checks() {
  Memory memory{Console::GameCube}, reference{Console::GameCube};
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  gfx::FragmentShader shader(memory, bp, colors, konst, tmem);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::cout << "Device: " << renderer.device() << '\n';
  std::vector<u32> pixels(640 * 528), depths(pixels.size());
  for (unsigned i = 0; i < pixels.size(); ++i) {
    pixels[i] = (i * 491u) & 0xffffffu;
    depths[i] = (i * 971u) & 0xffffffu;
  }
  const auto expected_pixels = pixels, expected_depths = depths;
  const gfx::FragmentTextureState sentinel{
      {1, 2, 3, 4},
      {5, 6, 7, 8},
      {{{9, 10, 11, 12}, {13, 14, 15, 16}, {17, 18, 19, 20}, {21, 22, 23, 24}}},
      {-777, 888}};
  shader.restore_texture_state(sentinel.texture, sentinel.raw);
  shader.restore_indirect_coordinates(sentinel.coordinates);
  for (unsigned stage = 0; stage < 4; ++stage)
    shader.restore_indirect_texture(stage, sentinel.indirect[stage]);
  const auto unchanged = [&] {
    CHECK(pixels == expected_pixels && depths == expected_depths);
    CHECK(shader.texture_state() == sentinel);
  };
  renderer.upload(pixels, depths);
  const auto copy = [&](unsigned width, unsigned height, unsigned stride, bool clear) {
    auto cp = bp;
    cp[0x49] = 3 | (5 << 10);
    cp[0x4a] = (width - 1) | ((height - 1) << 10);
    cp[0x4b] = 0x160000 >> 5;
    cp[0x4d] = stride;
    cp[0x52] = ((6 << 1) << 3) | (clear ? (1 << 11) : 0);
    cp[0x40] = 0;
    cp[0x41] = 0;
    const auto layout = gfx::copy_efb_texture(
        reference, cp,
        {[&](unsigned x, unsigned y) { return (expected_pixels[y * 640 + x] << 8) | 255u; },
         [&](unsigned x, unsigned y) { return expected_depths[y * 640 + x]; }});
    CHECK(renderer.copy_texture(cp));
    const auto extent = std::size_t(layout.block_rows - 1) * layout.stride + layout.row_bytes;
    const auto wanted = reference.range(layout.destination, extent);
    const auto actual = memory.range(layout.destination, extent);
    CHECK(std::equal(wanted.begin(), wanted.end(), actual.begin()));
  };
  copy(4, 4, 2, true); // A mask-zero clear still submits work and dirties the EFB.
  const auto first = renderer.synchronize(pixels, depths, &shader);
  CHECK(first.fragments == 0 && first.efb_readbacks == 1);
  unchanged();
  copy(129, 65, 1, false); // Grow output after first gather; ordered rows overlap.
  const auto second = renderer.synchronize(pixels, depths, &shader);
  CHECK(second.fragments == 0 && second.efb_readbacks == 1);
  unchanged();
  copy(8, 8, 4, true); // Clear-only dirty state carries no submitted fragment keys.
  const auto third = renderer.synchronize(pixels, depths, &shader);
  CHECK(third.fragments == 0 && third.efb_readbacks == 2);
  unchanged();
  copy(257, 129, 8, false); // Grow copy storage again after a clear-only gather.
  renderer.synchronize(pixels, depths, &shader);
  unchanged();
  std::cout << "Verified small copy/gather/growing-overlap copy/clear-only gather/copy chains\n";
}

void gather_boundary_key_checks() {
  Memory memory{Console::GameCube};
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  gfx::FragmentShader shader(memory, bp, colors, konst, tmem);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::cout << "Device: " << renderer.device() << '\n';
  const std::array<gfx::Color, 5> texels{{{17, 51, 85, 119},
                                          {23, 47, 71, 95},
                                          {29, 53, 79, 101},
                                          {31, 59, 83, 107},
                                          {37, 61, 89, 109}}};
  for (unsigned unit = 0; unit < 5; ++unit) {
    std::array<u8, 64> tile{};
    for (unsigned pixel = 0; pixel < 16; ++pixel) {
      tile[pixel * 2] = texels[unit][3];
      tile[pixel * 2 + 1] = texels[unit][0];
      tile[32 + pixel * 2] = texels[unit][1];
      tile[33 + pixel * 2] = texels[unit][2];
    }
    memory.copy_in(0x80000 + unit * 64, tile);
    const unsigned address = 0x80 + (unit & 3) + ((unit & 4) << 3);
    bp[address + 8] = 3 | (3 << 10) | (6 << 20);
    bp[address + 20] = (0x80000 + unit * 64) >> 5;
  }
  bp[0] = 1 | (1 << 4) | (4 << 16);
  bp[0x27] = 1 | (2 << 6) | (3 << 12) | (4 << 18);
  bp[0x28] = 64;
  bp[0x40] = 1 | 14 | 16;
  bp[0x41] = 24;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xf8] = 6;
  bp[0xf9] = 12;
  bp[0xc0] = 0xfffa | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19) | 4;
  std::vector<u32> pixels(640 * 528, 0x13579b), depths(pixels.size(), 0x2468ac);
  auto expected_pixels = pixels, expected_depths = depths;
  expected_pixels.back() = 0x4997d9;
  expected_depths.back() = 73;
  renderer.upload(pixels, depths);
  gfx::RasterWork work{};
  work.left = 639;
  work.top = 527;
  work.right = 640;
  work.bottom = 528;
  work.edges.fill({0, 0, 1});
  work.depth.value = 73;
  work.reciprocal_w.value = 1;
  for (unsigned c = 0; c < 4; ++c)
    work.colors[0][c].value = gfx::Color{73, 151, 217, 255}[c];
  work.textures[0][0].value = 1.25f;
  work.textures[0][1].value = -2.25f;
  for (unsigned sequence = 1; sequence <= 4094; ++sequence) {
    CHECK(renderer.begin(bp, colors, konst));
    CHECK(renderer.draw(work));
  }
  const auto stats = renderer.synchronize(pixels, depths, &shader);
  const gfx::FragmentTextureState expected{
      {85, 51, 17, 119},
      {17, 51, 85, 119},
      {{{23, 47, 71, 95}, {29, 53, 79, 101}, {31, 59, 83, 107}, {37, 61, 89, 109}}},
      {160, -288}};
  CHECK(shader.texture_state() == expected);
  CHECK(pixels == expected_pixels && depths == expected_depths);
  CHECK(stats.fragments == 4094 && stats.dispatches == 256 && stats.fallback_rectangles == 0);
  CHECK(stats.left == 638 && stats.right == 639 && stats.top == 526 && stats.bottom == 527);
  std::cout << "Verified six nonzero golden latch fields at last retained pixel639,527 and "
               "sequence4094\n";
}

void state_sequences() {
  const TestFloatEnvironment restore;
  constexpr int modes[]{FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
  unsigned cases = 0;
  for (unsigned mode = 0; mode < 4; ++mode)
    for (unsigned denormal = 0; denormal < 4; ++denormal) {
      CHECK(std::fesetenv(FE_DFL_ENV) == 0);
      Memory memory(Console::GameCube), reference_memory(Console::GameCube);
      std::vector<u8> tmem(1024 * 1024), reference_tmem(1024 * 1024);
      std::array<u32, 256> bp{};
      projection::mixed_setup(memory, bp);
      projection::mixed_setup(reference_memory, bp);
      gfx::TevRegisters colors{}, konst{};
      gfx::FragmentShader recipient(memory, bp, colors, konst, tmem),
          reference(reference_memory, bp, colors, konst, reference_tmem);
      projection::seed(recipient);
      projection::seed(reference);
      gfx::HardwareRenderer renderer(memory, tmem);
      std::cout << "Device: " << renderer.device() << '\n';
      auto get = graphics_function<void(APIENTRY *)(GLenum, GLint *)>("glGetIntegerv");
      auto indexed = graphics_function<PFNGLGETINTEGERI_VPROC>("glGetIntegeri_v");
      std::vector<u32> pixels(640 * 528, 0x13579b), depths(pixels.size(), 0x2468ac);
      auto expected = pixels, expected_depths = depths;
      renderer.upload(pixels, depths);
      const auto work = projection_cases::seed();
      const auto hostile = [&] {
        CHECK(std::fesetround(modes[mode]) == 0);
        CHECK(std::feclearexcept(FE_ALL_EXCEPT) == 0);
        CHECK(std::feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0);
#if defined(_M_X64) || defined(__x86_64__)
        constexpr unsigned simd_modes[]{0, 0x6000, 0x4000, 0x2000};
        _mm_setcsr((_mm_getcsr() & ~0xe040u) | simd_modes[(mode + 1) % 4] |
                   ((denormal & 1) ? 0x8000u : 0u) | ((denormal & 2) ? 0x40u : 0u));
#endif
      };
      const auto clear = [&] {
        auto cp = bp;
        cp[0x43] = 0;
        cp[0x49] = 0;
        cp[0x4a] = 3 | (3 << 10);
        cp[0x4b] = 0x140000 >> 5;
        cp[0x4d] = 2;
        cp[0x52] = ((6 << 1) << 3) | (1 << 11);
        cp[0x40] = cp[0x41] = 0;
        CHECK(renderer.copy_texture(cp));
      };
      const auto checked_sync = [&](u64 fragments, u64 dispatches, u64 readbacks) {
        GLint before_program = 0, before_binding = 0;
        get(GL_CURRENT_PROGRAM, &before_program);
        indexed(GL_SHADER_STORAGE_BUFFER_BINDING, 7, &before_binding);
        hostile();
        const FloatEnvironmentBits before;
        const auto stats = renderer.synchronize(pixels, depths, &recipient);
        CHECK(FloatEnvironmentBits{} == before);
        GLint after_program = 0, after_binding = 0;
        get(GL_CURRENT_PROGRAM, &after_program);
        indexed(GL_SHADER_STORAGE_BUFFER_BINDING, 7, &after_binding);
        CHECK(after_program == before_program && after_binding == before_binding);
        CHECK(stats.fragments == fragments && stats.dispatches == dispatches &&
              stats.efb_readbacks == readbacks);
        CHECK(pixels == expected && depths == expected_depths);
        CHECK(recipient.texture_state() == reference.texture_state());
        CHECK(std::fesetenv(FE_DFL_ENV) == 0);
      };
      // First dirty synchronization is a clear with no traversal selector.
      clear();
      checked_sync(0, 0, 1);
      CHECK(renderer.begin(bp, colors, konst));
      CHECK(renderer.draw(work));
      CHECK(projection::shade(work, 1, reference, expected, expected_depths) == 4);
      const auto null = renderer.synchronize(pixels, depths, nullptr);
      CHECK(null.fragments == 4 && null.dispatches == 1 && null.efb_readbacks == 2);
      CHECK(pixels == expected && depths == expected_depths);
      // A null recipient intentionally drains selectors without publishing them.
      // A later clear-only dirty sync must not resurrect the previous GPU latches.
      CHECK(recipient.texture_state() == projection::sentinel);
      projection::seed(reference);
      clear();
      checked_sync(0, 1, 3);
      bp[0xf3] = 0; // All fragments fail alpha after writing texture selectors.
      CHECK(renderer.begin(bp, colors, konst));
      CHECK(renderer.draw(work));
      for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 2; ++x) {
          gfx::FragmentInputs input{};
          input.x = x;
          input.viewport_width = 32;
          input.uv[0] = {.25f, .125f};
          u32 depth = 73;
          CHECK(!reference.shade(input, &depth));
        }
      CHECK(reference.texture_state() != projection::sentinel);
      auto changed = konst;
      changed[0][0] ^= 1;
      CHECK(renderer.begin(bp, colors, changed)); // Finish the batch before hostile FENV.
      clear();
      checked_sync(0, 2, 4);
      ++cases;
    }
  CHECK(cases == 16);
  std::cout << "Verified16 initial-clear/null-drain/clear-only/alpha-reject sequences with full "
               "EFB/state and FENV/program/binding7 assertions\n";
}

void checks() {
  gather_first_read_environment_checks(true);
  gather_copy_capacity_checks();
  gather_boundary_key_checks();
  latch_state_readback_checks();
  copy_latch_state_checks();
  copy_indirect_state_checks();
  state_sequences();
}
} // namespace hardware_batch_test::gather
} // namespace
