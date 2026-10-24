// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hardware_batch_support.hpp"

namespace {
namespace hardware_batch_test::coverage {
using namespace cupid;

gfx::RasterWork empty_work(int width, int height, bool vertical, bool outside = false) {
  auto work = projection_cases::seed();
  work.left = 17;
  work.top = 13;
  work.right = work.left + width;
  work.bottom = work.top + height;
  work.edges.fill({0, 0, 1});
  if (outside)
    work.edges[0].constant = -1;
  else if (vertical) {
    work.edges[0] = {1, 0, -s64(work.top) * 16};
    work.edges[1] = {-1, 0, s64(work.top + 1) * 16};
  } else {
    work.edges[0] = {0, -1, -s64(work.left) * 16};
    work.edges[1] = {0, 1, s64(work.left + 1) * 16};
  }
  return work;
}

void risky(gfx::RasterWork &w, unsigned kind) {
  w.textures[0][1].value = 0;
  if (kind == 0) {
    w.reciprocal_w.value = 0x1p127f;
    w.textures[0][0].value = 0x1p127f;
    w.textures[0][2].value = 0;
  }
  if (kind == 1) {
    w.reciprocal_w.value = 1;
    w.textures[0][0].value = w.textures[0][2].value = 0x1p127f;
  }
  if (kind == 2) {
    w.reciprocal_w.value = 0x1p60f;
    w.textures[0][0].value = w.textures[0][2].value = 0x1p-70f;
  }
  if (kind == 3) {
    w.reciprocal_w.value = 1;
    w.textures[0][2].value = std::numeric_limits<float>::quiet_NaN();
  }
}

void empty_and_neighbor_checks() {
  const HostFloatEnvironment host;
  unsigned cases = 0, neighbors = 0;
  for (unsigned family : {0u, 1u}) {
    Memory memory(Console::GameCube);
    std::vector<u8> tmem(1024 * 1024);
    std::array<u32, 256> bp{};
    projection::configure(memory, bp, 1, family);
    projection::patterned_tile(memory);
    bp[0x80] = 1 | (1 << 2);
    gfx::TevRegisters colors{}, konst{};
    gfx::FragmentShader recipient(memory, bp, colors, konst, tmem);
    gfx::HardwareRenderer renderer(memory, tmem);
    std::cout << "Device: " << renderer.device() << '\n';
    std::vector<u32> pixels(640 * 528, 0x13579b), depths(pixels.size(), 0x2468ac);
    const auto original = pixels, original_depths = depths;
    u64 fallbacks = 0;

    struct Shape {
      int w, h;
      bool vertical, outside;
    };

    constexpr std::array<Shape, 13> shapes{{{2, 2, false, false},
                                            {1, 64, true, false},
                                            {1, 65, true, false},
                                            {1, 256, true, false},
                                            {1, 257, true, false},
                                            {64, 1, false, false},
                                            {65, 1, false, false},
                                            {256, 1, false, false},
                                            {257, 1, false, false},
                                            {2, 128, false, false},
                                            {17, 15, false, false},
                                            {257, 1, false, true},
                                            {1, 257, true, true}}};
    for (const auto &shape : shapes)
      for (unsigned kind = 0; kind < 4; ++kind) {
        auto work = empty_work(shape.w, shape.h, shape.vertical, shape.outside);
        risky(work, kind);
        const bool wanted = shape.outside || shape.w * shape.h <= 256;
        projection::seed(recipient);
        renderer.upload(pixels, depths);
        CHECK(renderer.begin(bp, colors, konst));
        CHECK(renderer.draw(work) == wanted);
        fallbacks += !wanted;
        auto stats = renderer.synchronize(pixels, depths, &recipient);
        CHECK(stats.dispatches == 0 && stats.fragments == 0 && stats.efb_readbacks == 0 &&
              stats.fallback_rectangles == fallbacks);
        CHECK(pixels == original && depths == original_depths &&
              recipient.texture_state() == projection::sentinel);
        ++cases;
        if (!shape.outside) {
          work.edges[1].constant += 1;
          CHECK(!renderer.draw(work));
          ++fallbacks;
          stats = renderer.synchronize(pixels, depths, &recipient);
          CHECK(stats.dispatches == 0 && stats.fragments == 0 &&
                stats.fallback_rectangles == fallbacks);
          CHECK(pixels == original && depths == original_depths &&
                recipient.texture_state() == projection::sentinel);
          ++neighbors;
        }
      }
    auto repeated = empty_work(2, 128, false);
    risky(repeated, 0);
    CHECK(renderer.begin(bp, colors, konst));
    for (unsigned n = 0; n < 4096; ++n)
      CHECK(renderer.draw(repeated));
    const auto empty_stats = renderer.synchronize(pixels, depths, &recipient);
    CHECK(empty_stats.dispatches == 0 && empty_stats.fragments == 0 &&
          empty_stats.fallback_rectangles == fallbacks);
    // Earlier validation retains priority even when one edge proves emptiness.
    auto invalid = empty_work(2, 2, false, true);
    risky(invalid, 0);
    invalid.depth.value = std::numeric_limits<float>::infinity();
    CHECK(!renderer.draw(invalid));
    CHECK(renderer.fallback_reason() == "Non-finite interpolation requires CPU diagnostics");
    invalid.depth.value = 73;
    invalid.colors[0][0].value = std::numeric_limits<float>::quiet_NaN();
    CHECK(!renderer.draw(invalid));
    CHECK(renderer.fallback_reason() == "Non-finite interpolation requires CPU diagnostics");
    invalid.colors[0][0].value = 0;
    bp[0xf1] = (2 << 21) | (1 << 20);
    bp[0xe8] = 342 | 1024;
    CHECK(renderer.begin(bp, colors, konst));
    CHECK(!renderer.draw(invalid));
    CHECK(renderer.fallback_reason() == "Fog range parameters require CPU diagnostics");
    bp[0xf1] = 1u << 21;
    bp[0xe8] = bp[0xee] = bp[0xf0] = 0;
    bp[0xef] = 73;
    invalid.viewport_width = 32;
    CHECK(renderer.begin(bp, colors, konst));
    CHECK(!renderer.draw(invalid));
    CHECK(renderer.fallback_reason() == "Undefined fog division requires CPU diagnostics");
    bp[0xf1] = (2 << 21) | (1 << 20);
    bp[0xe8] = 342 | 1024;
    for (unsigned i = 0; i < 5; ++i)
      bp[0xe9 + i] = 64 | (64 << 12);
    invalid.viewport_width = 0;
    CHECK(renderer.begin(bp, colors, konst));
    CHECK(!renderer.draw(invalid));
    CHECK(renderer.fallback_reason() == "Fog range parameters require CPU diagnostics");
  }
  CHECK(cases == 104 && neighbors == 88);
  std::cout << "Verified104 unsafe-W/Q empty/budget cases and88 visible neighbors; prior "
               "depth/color/fog declines retained\n";
}

void extreme_edge_checks() {
  const HostFloatEnvironment host;
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  projection::configure(memory, bp, 1, 0);
  gfx::TevRegisters colors{}, konst{};
  gfx::FragmentShader recipient(memory, bp, colors, konst, tmem);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::cout << "Device: " << renderer.device() << '\n';
  std::vector<u32> pixels(640 * 528, 0x13579b), depths(pixels.size(), 0x2468ac);
  renderer.upload(pixels, depths);
  CHECK(renderer.begin(bp, colors, konst));
  u64 fallbacks = 0;
  unsigned rejected = 0, valid = 0;
  const std::array<s64, 6> extremes{std::numeric_limits<s64>::min(),
                                    std::numeric_limits<s64>::max(),
                                    -134217728,
                                    134217728,
                                    -2147483648LL,
                                    2147483648LL};
  for (unsigned axis = 0; axis < 3; ++axis)
    for (unsigned component = 0; component < 3; ++component)
      for (s64 value : extremes)
        for (bool bad_q : {false, true}) {
          if (component == 2 && (value == -134217728 || value == 134217728))
            continue;
          auto work = projection_cases::seed();
          work.left = 639;
          work.top = 527;
          work.right = 640;
          work.bottom = 528;
          if (component == 0)
            work.edges[axis].dx = value;
          else if (component == 1)
            work.edges[axis].dy = value;
          else
            work.edges[axis].constant = value;
          if (bad_q)
            work.textures[0][2].value = std::numeric_limits<float>::quiet_NaN();
          CHECK(!renderer.draw(work));
          CHECK(renderer.fallback_reason() ==
                "Triangle edge range exceeds 32-bit hardware coverage");
          ++fallbacks;
          ++rejected;
        }
  for (unsigned dimension = 0; dimension < 4; ++dimension)
    for (int value : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
      auto work = projection_cases::seed();
      if (dimension == 0)
        work.left = value;
      if (dimension == 1)
        work.top = value;
      if (dimension == 2)
        work.right = value;
      if (dimension == 3)
        work.bottom = value;
      work.textures[0][2].value = std::numeric_limits<float>::quiet_NaN();
      CHECK(!renderer.draw(work));
      CHECK(renderer.fallback_reason() == "Raster rectangle bounds require CPU validation");
      ++fallbacks;
      ++rejected;
    }
  auto stats = renderer.synchronize(pixels, depths, &recipient);
  CHECK(stats.fragments == 0 && stats.dispatches == 0 && stats.fallback_rectangles == fallbacks);
  // Old valid bases may cancel an origin much wider than signed32. Keep them.
  constexpr s64 limit = 134217727;
  u64 dispatches = 0;
  for (s64 dx : {-limit, s64(0), limit})
    for (s64 dy : {-limit, s64(0), limit})
      for (bool far_corner : {false, true}) {
        auto work = projection_cases::seed();
        work.left = far_corner ? 639 : 0;
        work.top = far_corner ? 527 : 0;
        work.right = work.left + 1;
        work.bottom = work.top + 1;
        work.edges[0].dx = dx;
        work.edges[0].dy = dy;
        const s64 origin = dx * s64(work.top) * 16 - dy * s64(work.left) * 16;
        work.edges[0].constant = 1 - origin;
        renderer.upload(pixels, depths);
        projection::seed(recipient);
        CHECK(renderer.draw(work));
        stats = renderer.synchronize(pixels, depths, &recipient);
        CHECK(stats.fragments == 1 && stats.dispatches == ++dispatches &&
              stats.fallback_rectangles == fallbacks);
        CHECK(pixels[work.top * 640 + work.left] == 0xc81428u);
        ++valid;
      }
  for (s64 base :
       {s64(-2147483647), s64(-134217728), s64(-1), s64(1), s64(134217728), s64(2147483647)}) {
    auto work = projection_cases::seed();
    work.right = work.bottom = 1;
    work.edges[0].constant = base;
    std::fill(pixels.begin(), pixels.end(), 0x13579b);
    std::fill(depths.begin(), depths.end(), 0x2468ac);
    renderer.upload(pixels, depths);
    CHECK(renderer.draw(work));
    stats = renderer.synchronize(pixels, depths, &recipient);
    if (base > 0)
      ++dispatches;
    CHECK(stats.fragments == (base > 0 ? 1u : 0u) && stats.dispatches == dispatches &&
          stats.fallback_rectangles == fallbacks);
    CHECK(pixels[0] == (base > 0 ? 0xc81428u : 0x13579bu));
    ++valid;
  }
  CHECK(rejected == 104 && valid == 24);
  std::cout << "Verified104 full-signed edge/bounds declines with valid/invalid-Q controls and24 "
               "retained large-origin cancellation/base-boundary cases\n";
}

void queued_empty_copy_checks() {
  const HostFloatEnvironment host;
  Memory memory(Console::GameCube), expected_memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024), expected_tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  projection::mixed_setup(memory, bp);
  projection::mixed_setup(expected_memory, bp);
  gfx::TevRegisters colors{}, konst{};
  gfx::FragmentShader reference(expected_memory, bp, colors, konst, expected_tmem),
      recipient(memory, bp, colors, konst, tmem);
  projection::seed(reference);
  projection::seed(recipient);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::cout << "Device: " << renderer.device() << '\n';
  std::vector<u32> pixels(640 * 528, 0x13579b), depths(pixels.size(), 0x2468ac);
  auto expected = pixels, expected_depths = depths;
  renderer.upload(pixels, depths);
  auto first = projection_cases::seed();
  CHECK(renderer.begin(bp, colors, konst));
  CHECK(renderer.draw(first));
  CHECK(projection::shade(first, 1, reference, expected, expected_depths) == 4);
  auto empty = empty_work(2, 128, false);
  risky(empty, 0);
  CHECK(renderer.draw(empty));
  renderer.reset_bounding_box(false);
  auto copy = bp;
  copy[0x43] = 0;
  copy[0x49] = 0;
  copy[0x4a] = 7 | (3 << 10);
  copy[0x4b] = 0x140000 >> 5;
  copy[0x4d] = 4;
  copy[0x52] = ((6 << 1) << 3) | (1 << 11);
  copy[0x40] = copy[0x41] = 0;
  const auto check_copy = [&] {
    CHECK(renderer.copy_texture(copy));
    gfx::copy_efb_texture(
        expected_memory, copy,
        {[&](unsigned x, unsigned y) { return (expected[y * 640 + x] << 8) | 255u; },
         [&](unsigned x, unsigned y) { return expected_depths[y * 640 + x]; }});
    const auto actual = memory.range(0x140000, 128), gold = expected_memory.range(0x140000, 128);
    CHECK(std::equal(actual.begin(), actual.end(), gold.begin()));
  };
  check_copy();
  auto second = first;
  second.left = 4;
  second.right = 6;
  second.textures[0][0].value = 1.25f;
  second.textures[0][1].value = 1.125f;
  CHECK(renderer.begin(bp, colors, konst));
  CHECK(renderer.draw(second));
  CHECK(projection::shade(second, 1, reference, expected, expected_depths) == 4);
  risky(empty, 1);
  CHECK(renderer.draw(empty));
  check_copy();
  const auto stats = renderer.synchronize(pixels, depths, &recipient);
  CHECK(pixels == expected && depths == expected_depths &&
        recipient.texture_state() == reference.texture_state());
  CHECK(stats.dispatches == 2 && stats.fragments == 8 && stats.fallback_rectangles == 0 &&
        stats.efb_readbacks == 1);
  CHECK(stats.left == 4 && stats.right == 5 && stats.top == 0 && stats.bottom == 1);
  std::cout << "Verified queued textured draws through empty-W/Q, bounding reset, two copy/clear "
               "packets, full EFB/state and256 RAM bytes\n";
}

void empty_first_environment_checks() {
  const TestFloatEnvironment restore;
  constexpr int modes[]{FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
  unsigned contexts = 0;
  for (unsigned mode = 0; mode < 4; ++mode)
    for (unsigned denormal = 0; denormal < 4; ++denormal) {
      CHECK(std::fesetenv(FE_DFL_ENV) == 0);
      Memory memory(Console::GameCube);
      std::vector<u8> tmem(1024 * 1024);
      std::array<u32, 256> bp{};
      projection::mixed_setup(memory, bp);
      gfx::TevRegisters colors{}, konst{};
      gfx::FragmentShader recipient(memory, bp, colors, konst, tmem),
          reference(memory, bp, colors, konst, tmem);
      projection::seed(recipient);
      projection::seed(reference);
      gfx::HardwareRenderer renderer(memory, tmem);
      std::cout << "Device: " << renderer.device() << '\n';
      std::vector<u32> pixels(640 * 528, 0x13579b), depths(pixels.size(), 0x2468ac);
      auto expected = pixels, expected_depths = depths;
      renderer.upload(pixels, depths);
      auto empty = empty_work(2, 128, false);
      risky(empty, 0);
      CHECK(renderer.begin(bp, colors, konst));
      CHECK(renderer.draw(empty));
      const auto hostile = [&] {
        CHECK(std::fesetround(modes[mode]) == 0);
        CHECK(std::feclearexcept(FE_ALL_EXCEPT) == 0);
        CHECK(std::feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0);
#if defined(_M_X64) || defined(__x86_64__)
        constexpr unsigned simd_modes[]{0u, 0x6000u, 0x4000u, 0x2000u};
        _mm_setcsr((_mm_getcsr() & ~0xe040u) | simd_modes[(mode + 1) % 4] |
                   ((denormal & 1) ? 0x8000u : 0u) | ((denormal & 2) ? 0x40u : 0u));
#endif
      };
      hostile();
      const FloatEnvironmentBits before_empty;
      CHECK(renderer.draw(empty));
      CHECK(FloatEnvironmentBits{} == before_empty);
      CHECK(std::fesetenv(FE_DFL_ENV) == 0);
      auto ordinary = projection_cases::seed();
      CHECK(renderer.draw(ordinary));
      CHECK(projection::shade(ordinary, 1, reference, expected, expected_depths) == 4);
      auto changed = konst;
      changed[0][0] ^= 1;
      CHECK(renderer.begin(bp, colors, changed));
      auto get = graphics_function<void(APIENTRY *)(GLenum, GLint *)>("glGetIntegerv");
      auto indexed = graphics_function<PFNGLGETINTEGERI_VPROC>("glGetIntegeri_v");
      GLint old_program = 0, old_binding = 0;
      get(GL_CURRENT_PROGRAM, &old_program);
      indexed(GL_SHADER_STORAGE_BUFFER_BINDING, 7, &old_binding);
      hostile();
      const FloatEnvironmentBits before_gather;
      const auto stats = renderer.synchronize(pixels, depths, &recipient);
      CHECK(FloatEnvironmentBits{} == before_gather);
      GLint program = 0, binding = 0;
      get(GL_CURRENT_PROGRAM, &program);
      indexed(GL_SHADER_STORAGE_BUFFER_BINDING, 7, &binding);
      CHECK(program == old_program && binding == old_binding);
      CHECK(stats.dispatches == 1 && stats.fragments == 4 && stats.fallback_rectangles == 0 &&
            stats.efb_readbacks == 1);
      CHECK(pixels == expected && depths == expected_depths &&
            recipient.texture_state() == reference.texture_state());
      ++contexts;
    }
  CHECK(contexts == 16);
  std::cout << "Verified16 empty-first/cached-empty/first-gather contexts with complete "
               "FENV/program/binding7 and EFB/state assertions\n";
}

void zero_endpoint_checks(bool zero_aware) {
  const HostFloatEnvironment host;

  struct Case {
    const char *name;
    float w, q0, qdx, s0, sdx;
    bool admit;
    u32 u0, u1;
    int zero_at;
    bool negative_zero;
  };

  constexpr u32 quarter = 0x3e800000, negative_quarter = 0xbe800000, one = 0x3f800000;
  const std::array<Case, 18> cases{
      {{"positive-zero-to-unit", 1, 0, 1, .25f, 0, true, quarter, quarter, 0, false},
       {"negative-zero-to-negative-unit", 1, -0.0f, -1, .25f, 0, true, quarter, negative_quarter, 0,
        true},
       {"unit-to-positive-zero", 1, 1, -1, .25f, 0, true, quarter, quarter, 1, false},
       {"negative-unit-to-zero", 1, -1, 1, .25f, 0, true, negative_quarter, quarter, 1, false},
       {"negative-W-zero-column", -1, 0, 1, .25f, 0, true, negative_quarter, quarter, 0, false},
       {"relative-Q-minus64", 0x1p60f, 0, 0x1p-64f, 0, 0x1p-64f, true, 0, one, 0, false},
       {"relative-Q-minus65-hard-margin", 0x1p60f, 0, 0x1p-65f, 0, 0x1p-65f, false, 0, one, 0,
        false},
       {"relative-Q-minus70-underflow", 0x1p60f, 0, 0x1p-70f, 0, 0x1p-70f, false, 0, one, 0, false},
       {"W-power124", 0x1p124f, 0, 1, 0, 1, true, 0, one, 0, false},
       {"W-power125-hard-limit", 0x1p125f, 0, 1, 0, 1, false, 0, one, 0, false},
       {"W-power127-underflow", 0x1p127f, 0, 1, 0, 1, false, 0, one, 0, false},
       {"negative-W-power127-underflow", -0x1p127f, -0.0f, -1, -0.0f, -1, false, 0, one, 0, true},
       {"Q-power124", 1, 0, 0x1p124f, 0, 0x1p124f, true, 0, one, 0, false},
       {"Q-power125-hard-limit", 1, 0, 0x1p125f, 0, 0x1p125f, false, 0, one, 0, false},
       {"Q-power127-underflow", 1, 0, 0x1p127f, 0, 0x1p127f, false, 0, one, 0, false},
       {"negative-Q-power127-underflow", 1, -0.0f, -0x1p127f, -0.0f, -0x1p127f, false, 0x80000000,
        one, 0, true},
       {"constant-positive-zero", 1, 0, 0, 1, 0, true, one, one, 0, false},
       {"constant-negative-zero", -1, -0.0f, -0.0f, -1, 0, true, one, one, 0, true}}};
  unsigned checks = 0, admitted_total = 0;
  for (unsigned family : {0u, 1u}) {
    Memory memory(Console::GameCube);
    std::vector<u8> tmem(1024 * 1024);
    gfx::HardwareRenderer renderer(memory, tmem);
    std::cout << "Device: " << renderer.device() << '\n';
    u64 fallbacks = 0, dispatches = 0;
    for (unsigned index = 0; index < cases.size(); ++index)
      for (bool unused : {false, true}) {
        const auto &row = cases[index];
        const unsigned count = unused ? 8 : 1, coord = count - 1;
        std::array<u32, 256> bp{};
        gfx::TevRegisters colors{}, konst{};
        projection::configure(memory, bp, count, family);
        projection::patterned_tile(memory);
        bp[0x80] = 1 | (1 << 2);
        gfx::FragmentShader reference(memory, bp, colors, konst, tmem),
            recipient(memory, bp, colors, konst, tmem);
        projection::seed(reference);
        projection::seed(recipient);
        auto work = projection_cases::seed();
        work.reciprocal_w.value = row.w;
        work.textures[coord][0] = {row.s0, row.sdx, std::signbit(row.s0) ? -0.0f : 0.0f};
        work.textures[coord][1] = {0, 0, 0};
        work.textures[coord][2] = {row.q0, row.qdx, std::signbit(row.q0) ? -0.0f : 0.0f};
        CHECK(std::bit_cast<u32>(work.textures[coord][2].at(row.zero_at, 0)) ==
              (row.negative_zero ? 0x80000000u : 0u));
        for (int x = 0; x < 2; ++x) {
          const float reciprocal = 1.0f / work.reciprocal_w.at(x, 0);
          const float q = work.textures[coord][2].at(x, 0) * reciprocal;
          const float projection = q != 0 ? reciprocal / q : reciprocal;
          const float u = work.textures[coord][0].at(x, 0) * projection;
          CHECK(std::bit_cast<u32>(u) == (x ? row.u1 : row.u0));
        }
        std::vector<u32> pixels(640 * 528, 0x13579b), depths(pixels.size(), 0x2468ac);
        auto expected = pixels, expected_depths = depths;
        CHECK(projection::shade(work, count, reference, expected, expected_depths) == 4);
        renderer.upload(pixels, depths);
        CHECK(renderer.begin(bp, colors, konst));
        const bool wanted = index >= 16 || (zero_aware && row.admit);
        const bool admitted = renderer.draw(work);
        if (admitted != wanted)
          throw std::runtime_error(std::string("Zero endpoint admission ") + row.name +
                                   " unused=" + std::to_string(unused));
        if (admitted)
          ++dispatches;
        else
          ++fallbacks;
        const auto stats = renderer.synchronize(pixels, depths, &recipient);
        CHECK(stats.dispatches == dispatches && stats.fallback_rectangles == fallbacks &&
              stats.fragments == (admitted ? 4u : 0u));
        if (!admitted) {
          CHECK(projection::shade(work, count, recipient, pixels, depths) == 4);
          renderer.upload(pixels, depths);
        }
        CHECK(pixels == expected && depths == expected_depths);
        CHECK(recipient.texture_state() == reference.texture_state());
        ++checks;
        admitted_total += admitted;
      }
  }
  CHECK(checks == 72);
  CHECK(admitted_total == (zero_aware ? 40u : 8u));
  std::cout
      << "Verified72 signed-zero endpoint/hard-limit cases with literal CPU UV, full patterned "
         "EFB/depth, complete retained state, and actual CPU fragment continuation; admits="
      << admitted_total << "\n";
}

void empty_checks() {
  empty_and_neighbor_checks();
  extreme_edge_checks();
  queued_empty_copy_checks();
  empty_first_environment_checks();
}
} // namespace hardware_batch_test::coverage
} // namespace
