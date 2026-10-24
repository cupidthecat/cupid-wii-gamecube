// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hardware_batch_support.hpp"

namespace {
namespace hardware_batch_test::interaction {
using namespace cupid;

using projection::seed;
using projection::shade;
using projection::state_bytes;
constexpr unsigned width = 640, height = 528;
constexpr gfx::FragmentTextureState final_literal{
    {17, 23, 31, 255},
    {17, 23, 31, 255},
    {{{30, 40, 50, 255}, {43, 57, 69, 255}, {56, 74, 88, 255}, {69, 91, 107, 255}}},
    {64, 32}};

gfx::RasterWork old_work() {
  return projection_cases::seed();
}

gfx::RasterWork extra_work() {
  auto w = old_work();
  w.textures[0][2] = {-.5f, 1, 0, 0, 0, 0, 0};
  return w;
}

gfx::RasterWork scalar_work() {
  auto w = old_work();
  w.left = w.top = 20;
  w.right = w.bottom = 37;
  return w;
}

gfx::RasterWork ordered_work() {
  auto w = old_work();
  w.textures[0][0].value = .875f;
  w.textures[0][1].value = .9375f;
  return w;
}

void clear_oracle(std::vector<u32> &colors, std::vector<u32> &depths) {
  for (unsigned y = 0; y < 4; ++y)
    for (unsigned x = 0; x < 4; ++x) {
      colors[y * width + x] = 0xf438d2;
      depths[y * width + x] = 0x815392;
    }
}

void cpu_controls() {
  // These controls use literal patterned texels and independently staged CPU shade.
  // Neither the new admission helper nor gather packet implementation is queried.
  projection::pattern_cpu_controls();
  projection::mixed_reference_cpu_controls();
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  projection::mixed_setup(memory, bp);
  gfx::TevRegisters colors{}, konst{};
  gfx::FragmentShader shader(memory, bp, colors, konst, tmem);
  seed(shader);
  std::vector<u32> pixels(width * height, 0x13579b), depths(pixels.size(), 0x2468ac);
  u64 fragments = 0;
  for (const auto &w : {old_work(), extra_work(), extra_work(), old_work(), scalar_work()})
    fragments += shade(w, 1, shader, pixels, depths);
  bp[0x10] = 1u << 20;
  fragments += shade(ordered_work(), 1, shader, pixels, depths);
  bp[0x10] = 0;
  fragments += shade(extra_work(), 1, shader, pixels, depths);
  CHECK(fragments == 313);
  CHECK(state_bytes(shader.texture_state()) == state_bytes(final_literal));
  clear_oracle(pixels, depths);
  for (unsigned y = 0; y < height; ++y)
    for (unsigned x = 0; x < width; ++x) {
      const auto i = y * width + x;
      if (x < 4 && y < 4)
        CHECK(pixels[i] == 0xf438d2 && depths[i] == 0x815392);
      else if (x >= 20 && x < 37 && y >= 20 && y < 37)
        CHECK(pixels[i] == 0x11171f && depths[i] == 73);
      else
        CHECK(pixels[i] == 0x13579b && depths[i] == 0x2468ac);
    }
  std::cout << "{\"event\":\"union_cpu_controls\",\"fragments\":313,\"state_literal_bytes\":32,"
               "\"GPU_calls\":0}\n";
}

struct Expected {
  u64 dispatches, fragments, fallbacks, readbacks;
};

void run(bool additional) {
  const HostFloatEnvironment environment;
  Memory memory(Console::GameCube), reference_memory(Console::GameCube),
      copy_reference(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024), reference_tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  projection::mixed_setup(memory, bp);
  projection::mixed_setup(reference_memory, bp);
  constexpr std::array<u32, 2> guarded_bases{0x160000 - 64, 0x180000 - 64};
  constexpr unsigned guarded_bytes = 65536 + 128;
  for (unsigned region = 0; region < guarded_bases.size(); ++region) {
    std::vector<u8> sentinel(guarded_bytes);
    for (unsigned i = 0; i < sentinel.size(); ++i)
      sentinel[i] = u8(0x5d + region * 37 + i * 13);
    memory.copy_in(guarded_bases[region], sentinel);
    copy_reference.copy_in(guarded_bases[region], sentinel);
  }
  gfx::TevRegisters colors{}, konst{};
  gfx::FragmentShader reference(reference_memory, bp, colors, konst, reference_tmem),
      retained(memory, bp, colors, konst, tmem);
  seed(reference);
  seed(retained);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::cout << "Device: " << renderer.device() << '\n';
  std::vector<u32> expected_colors(width * height, 0x13579b),
      expected_depths(expected_colors.size(), 0x2468ac);
  auto pixels = expected_colors, depths = expected_depths;
  renderer.upload(pixels, depths);
  u64 observed_fragments = 0, reference_fragments = 0;
  unsigned handoffs = 0;
  const auto ram_equal = [&] {
    for (u32 address : guarded_bases) {
      const auto actual = memory.range(address, guarded_bytes),
                 expected = copy_reference.range(address, guarded_bytes);
      CHECK(std::equal(actual.begin(), actual.end(), expected.begin()));
    }
  };
  const auto equal = [&] {
    CHECK(pixels == expected_colors && depths == expected_depths);
    CHECK(state_bytes(retained.texture_state()) == state_bytes(reference.texture_state()));
    ram_equal();
    ++handoffs;
  };
  const auto sync = [&](Expected expected, bool recipient = true) {
    const auto before = state_bytes(retained.texture_state());
    const auto stats = renderer.synchronize(pixels, depths, recipient ? &retained : nullptr);
    CHECK(stats.dispatches == expected.dispatches && stats.fragments == expected.fragments);
    CHECK(stats.fallback_rectangles == expected.fallbacks &&
          stats.efb_readbacks == expected.readbacks);
    if (!recipient)
      CHECK(state_bytes(retained.texture_state()) == before);
    observed_fragments += stats.fragments;
    equal();
    return stats;
  };
  const auto submit = [&](const gfx::RasterWork &w, bool accepted, Expected before_cpu) {
    const bool begin = renderer.begin(bp, colors, konst);
    CHECK(begin == ((bp[0x10] & (1u << 20)) == 0));
    CHECK(renderer.draw(w) == accepted);
    if (!accepted)
      sync(before_cpu);
    const u64 count = shade(w, 1, reference, expected_colors, expected_depths);
    CHECK(count == ((w.right - w.left == 17) ? 289u : 4u));
    reference_fragments += count;
    if (!accepted) {
      CHECK(shade(w, 1, retained, pixels, depths) == count);
      equal();
      renderer.upload(pixels, depths);
    }
  };
  const auto texture_copy = [&](unsigned copy_width, unsigned copy_height, unsigned stride,
                                bool clear, bool masks) {
    auto cp = bp;
    cp[0x43] = 0;
    cp[0x49] = 0;
    cp[0x4a] = (copy_width - 1) | ((copy_height - 1) << 10);
    cp[0x4b] = 0x160000 >> 5;
    cp[0x4d] = stride;
    cp[0x52] = ((6 << 1) << 3) | (clear ? (1 << 11) : 0);
    cp[0x40] = masks ? 16 : 0;
    cp[0x41] = masks ? 8 : 0;
    cp[0x4f] = 0x71f4;
    cp[0x50] = 0x38d2;
    cp[0x51] = 0x815392;
    const auto layout = gfx::copy_efb_texture(
        copy_reference, cp,
        {[&](unsigned x, unsigned y) { return (expected_colors[y * width + x] << 8) | 255u; },
         [&](unsigned x, unsigned y) { return expected_depths[y * width + x]; }});
    const auto extent = std::size_t(layout.block_rows - 1) * layout.stride + layout.row_bytes;
    CHECK(renderer.copy_texture(cp));
    const auto a = memory.range(layout.destination, extent),
               b = copy_reference.range(layout.destination, extent);
    CHECK(std::equal(a.begin(), a.end(), b.begin()));
    ram_equal();
    if (clear && masks) {
      CHECK(copy_width == 4 && copy_height == 4);
      clear_oracle(expected_colors, expected_depths);
    }
  };
  submit(old_work(), true, {});
  submit(extra_work(), additional, {1, 4, 1, 1});
  texture_copy(4, 4, 2, false, false); // Flush pending batch, preserve its retained keys.
  sync(additional ? Expected{1, 8, 0, 1} : Expected{1, 0, 1, 1});
  submit(extra_work(), additional, {1, 0, 2, 1});
  submit(old_work(), true, {});
  auto xfb = bp;
  xfb[0x49] = 0;
  xfb[0x4a] = 15 | (15 << 10);
  xfb[0x4b] = 0x180000 >> 5;
  xfb[0x4d] = 1;
  xfb[0x4e] = 256;
  xfb[0x52] = (1 << 14) | 3;
  xfb[0x53] = (1 << 12) | (63 << 18);
  const auto layout = gfx::copy_efb_xfb(copy_reference, xfb, [&](unsigned x, unsigned y) {
    return (expected_colors[y * width + x] << 8) | 255u;
  });
  CHECK(renderer.copy_xfb(xfb));
  const auto xa = memory.range(layout.destination, layout.height * layout.stride);
  const auto xb = copy_reference.range(layout.destination, layout.height * layout.stride);
  CHECK(std::equal(xa.begin(), xa.end(), xb.begin()));
  ram_equal();
  renderer.reset_bounding_box(false);
  submit(scalar_work(), true, {}); // 289pixels selects scalar; dispatch is counted separately.
  const auto b = sync(additional ? Expected{3, 297, 0, 2} : Expected{3, 293, 2, 2});
  CHECK(b.left == 20 && b.right == 37 && b.top == 0 && b.bottom == 37);
  bp[0x10] = 1u << 20;
  submit(ordered_work(), false, additional ? Expected{3, 0, 1, 2} : Expected{3, 0, 3, 2});
  bp[0x10] = 0;
  submit(extra_work(), additional, {3, 0, 4, 2});
  texture_copy(
      4, 4, 2, true,
      true); // Encode before masked RGB/depth clear; gather still restores preclear latches.
  sync(additional ? Expected{4, 4, 1, 3} : Expected{3, 0, 4, 3});
  CHECK(state_bytes(retained.texture_state()) == state_bytes(final_literal));
  texture_copy(129, 65, 1, false,
               false); // Grow/reuse binding7 after gather; ordered output rows overlap.
  sync(additional ? Expected{4, 0, 1, 3} : Expected{3, 0, 4, 3});
  texture_copy(8, 8, 4, true,
               false); // Mask-zero dirty clear, null drain and subsequent clean nonnull sync.
  sync(additional ? Expected{4, 0, 1, 4} : Expected{3, 0, 4, 4}, false);
  const auto final = sync(additional ? Expected{4, 0, 1, 4} : Expected{3, 0, 4, 4});
  CHECK(reference_fragments == 313 && observed_fragments == (additional ? 309u : 297u));
  CHECK(final.xfb_copies == 1 && final.efb_readback_bytes == u64(4) * width * height * 8);
  std::cout << "{\"event\":\"union_interaction_complete\",\"additional\":" << additional
            << ",\"draw_calls\":7,\"reference_fragments\":313,\"hardware_fragments\":"
            << observed_fragments << ",\"dispatches\":" << final.dispatches
            << ",\"fallbacks\":" << final.fallback_rectangles << ",\"handoffs\":" << handoffs
            << ",\"full_color_depth\":true,\"state32\":true}\n";
}

void cold_null_then_first_gather_environment(bool require_gather_flags) {
  const TestFloatEnvironment restore;
  constexpr std::array<int, 4> modes{FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
  for (unsigned mode = 0; mode < 4; ++mode)
    for (unsigned denormal = 0; denormal < 4; ++denormal) {
      CHECK(std::fesetenv(FE_DFL_ENV) == 0);
      Memory memory(Console::GameCube), reference_memory(Console::GameCube);
      std::vector<u8> tmem(1024 * 1024), reference_tmem(1024 * 1024);
      std::array<u32, 256> bp{};
      projection::mixed_setup(memory, bp);
      projection::mixed_setup(reference_memory, bp);
      gfx::TevRegisters colors{}, konst{};
      gfx::FragmentShader reference(reference_memory, bp, colors, konst, reference_tmem),
          recipient(memory, bp, colors, konst, tmem);
      seed(reference);
      seed(recipient);
      gfx::HardwareRenderer renderer(memory, tmem);
      std::cout << "Device: " << renderer.device() << '\n';
      auto get = graphics_function<void(APIENTRY *)(GLenum, GLint *)>("glGetIntegerv");
      auto indexed = graphics_function<PFNGLGETINTEGERI_VPROC>("glGetIntegeri_v");
      std::vector<u32> expected(width * height, 0x13579b),
          expected_depths(expected.size(), 0x2468ac);
      auto pixels = expected, depths = expected_depths;
      renderer.upload(pixels, depths);
      GLint binding7 = 0;
      indexed(GL_SHADER_STORAGE_BUFFER_BINDING, 7, &binding7);
      CHECK(renderer.begin(bp, colors, konst));
      CHECK(renderer.draw(old_work()));
      CHECK(shade(old_work(), 1, reference, expected, expected_depths) == 4);
      const auto set_modes = [&] {
        CHECK(std::fesetround(modes[mode]) == 0);
        CHECK(std::feclearexcept(FE_ALL_EXCEPT) == 0);
        CHECK(std::feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0);
#if defined(_M_X64) || defined(__x86_64__)
        constexpr unsigned simd_modes[]{0u, 0x6000u, 0x4000u, 0x2000u};
        _mm_setcsr((_mm_getcsr() & ~0xe040u) | simd_modes[(mode + 1) % 4] |
                   ((denormal & 1) ? 0x8000u : 0u) | ((denormal & 2) ? 0x40u : 0u));
#endif
      };
      set_modes();
      const FloatEnvironmentBits before_null;
      const auto null_stats = renderer.synchronize(pixels, depths, nullptr);
      const FloatEnvironmentBits after_null;
      CHECK(after_null.round == before_null.round);
#if defined(_M_X64) || defined(__x86_64__)
      CHECK(after_null.control == before_null.control);
      CHECK((after_null.simd & ~0x3fu) == (before_null.simd & ~0x3fu));
#endif
      // Cold null completion's flag equality is retained as a baseline/union observation.
      // It is not silently promoted into a new flag guarantee for the existing null path.
      CHECK(null_stats.fragments == 4 && null_stats.dispatches == 1 &&
            null_stats.efb_readbacks == 1);
      CHECK(pixels == expected && depths == expected_depths);
      CHECK(state_bytes(recipient.texture_state()) == state_bytes(projection::sentinel));
      GLint null_program = 0, after_null_binding = 0;
      get(GL_CURRENT_PROGRAM, &null_program);
      indexed(GL_SHADER_STORAGE_BUFFER_BINDING, 7, &after_null_binding);
      CHECK(null_program != 0 && after_null_binding == binding7);
      CHECK(std::fesetenv(FE_DFL_ENV) == 0);
      CHECK(renderer.begin(bp, colors, konst));
      CHECK(renderer.draw(old_work()));
      CHECK(shade(old_work(), 1, reference, expected, expected_depths) == 4);
      // Finish the batch in the default environment before isolating first gather/read.
      // This avoids claiming the gather guard encloses the earlier cold/warm flush.
      auto changed = konst;
      changed[0][0] ^= 1;
      const FloatEnvironmentBits before_preflush;
      CHECK(renderer.begin(bp, colors, changed));
      CHECK(renderer.begin(bp, colors, konst));
      const FloatEnvironmentBits after_preflush;
      const auto current_context = SDL_GL_GetCurrentContext();
      CHECK(current_context != nullptr);
      set_modes();
      const FloatEnvironmentBits before_gather;
      const auto stats = renderer.synchronize(pixels, depths, &recipient);
      const FloatEnvironmentBits after_gather;
      if (require_gather_flags)
        CHECK(after_gather == before_gather);
      CHECK(after_gather.round == before_gather.round);
#if defined(_M_X64) || defined(__x86_64__)
      CHECK(after_gather.control == before_gather.control);
      CHECK((after_gather.simd & ~0x3fu) == (before_gather.simd & ~0x3fu));
#endif
      CHECK(stats.fragments == 4 && stats.dispatches == 2 && stats.efb_readbacks == 2);
      CHECK(pixels == expected && depths == expected_depths);
      CHECK(state_bytes(recipient.texture_state()) == state_bytes(reference.texture_state()));
      GLint program = 0, after_binding = 0;
      get(GL_CURRENT_PROGRAM, &program);
      indexed(GL_SHADER_STORAGE_BUFFER_BINDING, 7, &after_binding);
      CHECK(program == null_program && after_binding == binding7);
      std::cout << "{\"event\":\"cold_null_first_gather_environment\",\"mode\":" << mode
                << ",\"denormal\":" << denormal
                << ",\"null_flags_equal\":" << (before_null == after_null)
                << ",\"default_preflush_flags_equal\":" << (before_preflush == after_preflush)
                << ",\"first_nonnull_preflushed_flags_equal\":" << (before_gather == after_gather)
                << "}\n";
    }
}

void checks(bool additional) {
  cpu_controls();
  run(additional);
  cold_null_then_first_gather_environment(true);
}
} // namespace hardware_batch_test::interaction
} // namespace
