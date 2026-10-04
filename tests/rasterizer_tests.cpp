// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/rasterizer.hpp"
#include "test_support.hpp"
#include <bit>
#include <cmath>
#include <map>

using namespace cupid;
using namespace cupid::gfx;

namespace {
struct Sample {
  u32 depth;
  FragmentInputs inputs;
  unsigned writes = 0;
};

struct Fixture {
  std::array<u32, 256> bp{};
  std::array<u32, 0x1058> xf{};
  Rasterizer rasterizer{bp, xf};
  std::map<unsigned, Sample> samples;

  Fixture() {
    bp[0] = 1 << 4;
    bp[0x20] = (342 << 12) | 342;
    bp[0x21] = (357 << 12) | 357;
    bp[0x59] = 171 | (171 << 10);
    put(0x101a, 8);
    put(0x101b, 8);
    put(0x101c, 1000);
    put(0x101d, 350);
    put(0x101e, 350);
    put(0x101f, 1000);
    rasterizer.fragment = [&](unsigned x, unsigned y, u32 depth, const FragmentInputs &inputs) {
      CHECK(x < 640 && y < 528);
      auto &sample = samples[y * 640 + x];
      sample.depth = depth;
      sample.inputs = inputs;
      ++sample.writes;
    };
  }

  void put(unsigned address, float value) {
    xf[address] = std::bit_cast<u32>(value);
  }

  TransformedVertex vertex(float x, float y, float depth = 500, float w = 1) {
    TransformedVertex result;
    result.clip_position = {(x - 8) / 8 * w, (y - 8) / 8 * w, (depth - 1000) / 1000 * w, w};
    result.colors[0] = {20, 40, 60, 255};
    result.texcoords[0] = {x, y, 1};
    return result;
  }

  void draw(std::initializer_list<std::optional<TransformedVertex>> vertices, u8 opcode = 0x90) {
    rasterizer.draw(opcode, std::span(vertices.begin(), vertices.size()));
  }
};

void coverage_tests() {
  Fixture f;
  f.draw({f.vertex(0, 0), f.vertex(4, 0), f.vertex(0, 4)});
  CHECK(f.samples.size() == 6);
  for (unsigned y = 0; y < 3; ++y)
    for (unsigned x = 0; x < 3 - y; ++x) {
      CHECK(f.samples.contains(y * 640 + x));
      CHECK(f.samples.at(y * 640 + x).depth == 500);
      CHECK((f.samples.at(y * 640 + x).inputs.colors[0] == Color{20, 40, 60, 255}));
    }
  f.samples.clear();
  f.draw({f.vertex(0, 0), f.vertex(0, 4), f.vertex(4, 0)});
  CHECK(f.samples.size() == 6);
  f.samples.clear();
  f.draw({f.vertex(0, 0), f.vertex(4, 0), f.vertex(4, 4), f.vertex(0, 4)}, 0x80);
  CHECK(f.samples.size() == 16);
  for (const auto &[coordinate, sample] : f.samples) {
    CHECK(coordinate % 640 < 4 && coordinate / 640 < 4);
    CHECK(sample.writes == 1);
  }
  // The alternate quad opcode and a three-vertex remainder have ordinary coverage.
  f.samples.clear();
  f.draw({f.vertex(0, 0), f.vertex(4, 0), f.vertex(0, 4)}, 0x8f);
  CHECK(f.samples.size() == 6);
  f.samples.clear();
  f.draw({f.vertex(0, 0), f.vertex(4, 4), f.vertex(8, 8)});
  CHECK(f.samples.empty());
  f.draw({f.vertex(0, 0), f.vertex(4, 0)});
  CHECK(f.samples.empty());
}

void assembly_tests() {
  Fixture f;
  // Removing a skipped position compacts the input before forming triangles.
  f.draw({f.vertex(0, 0), std::nullopt, f.vertex(4, 0), f.vertex(0, 4)});
  CHECK(f.samples.size() == 6);
  f.samples.clear();
  f.draw({f.vertex(0, 0), f.vertex(4, 0), std::nullopt, f.vertex(0, 4), f.vertex(4, 4)}, 0x98);
  CHECK(f.samples.size() == 16);
  for (const auto &[coordinate, sample] : f.samples) {
    (void)coordinate;
    CHECK(sample.writes == 1);
  }
  f.samples.clear();
  f.draw({f.vertex(0, 0), f.vertex(4, 0), f.vertex(4, 4), f.vertex(0, 4)}, 0xa0);
  CHECK(f.samples.size() == 16);
  CHECK_THROWS(f.draw({f.vertex(1, 1)}, 0xc0));
}

void interpolation_tests() {
  Fixture f;
  f.bp[0] |= 1;
  f.bp[0x30] = 1;
  f.bp[0x31] = 3;
  auto a = f.vertex(0, 0, 0), b = f.vertex(4, 0, 400), c = f.vertex(0, 4, 800);
  a.colors[0] = {0, 0, 0, 255};
  b.colors[0] = {200, 0, 0, 255};
  c.colors[0] = {0, 100, 0, 255};
  f.draw({a, b, c});
  const auto &sample = f.samples.at(0);
  CHECK(sample.depth == static_cast<u32>(100 * 0.495f + 200 * 0.495f));
  CHECK(sample.inputs.colors[0][0] == static_cast<u8>(50 * 0.495f));
  CHECK(sample.inputs.colors[0][1] == static_cast<u8>(25 * 0.495f));
  CHECK(sample.inputs.uv[0][0] == 2 * 0.495f && sample.inputs.uv[0][1] == 4 * 0.495f);
  CHECK(std::abs(sample.inputs.dx[0][0] - 2) < 0.000001f);
  CHECK(std::abs(sample.inputs.dy[0][1] - 4) < 0.000001f);
  CHECK(sample.inputs.dx[0][1] == 0 && sample.inputs.dy[0][0] == 0);
  // W is projected per vertex, while STQ are interpolated as attributes divided by W.
  f.samples.clear();
  f.bp[0x30] = f.bp[0x31] = 0;
  a = f.vertex(0, 0, 500, 1);
  b = f.vertex(4, 0, 500, 2);
  c = f.vertex(0, 4, 500, 1);
  a.texcoords[0] = {0, 0, 1};
  b.texcoords[0] = {4, 0, 1};
  c.texcoords[0] = {0, 4, 1};
  f.draw({a, b, c});
  const auto &projected = f.samples.at(0).inputs;
  const float inverse_w = 1 - 0.125f * 0.495f;
  CHECK(std::abs(projected.uv[0][0] - (0.5f * 0.495f) / inverse_w) < 0.000001f);
  CHECK(std::abs(projected.uv[0][1] - 0.495f / inverse_w) < 0.000001f);
  // Quad derivatives remain the same for every covered sample in the 2x2 block.
  CHECK(projected.dx[0] == f.samples.at(1).inputs.dx[0]);
  CHECK(projected.dy[0] == f.samples.at(640).inputs.dy[0]);
}

void scissor_tests() {
  Fixture f;
  f.bp[0x20] = (343 << 12) | 343;
  f.bp[0x21] = (344 << 12) | 344;
  f.draw({f.vertex(0, 0), f.vertex(8, 0), f.vertex(0, 8)});
  CHECK(f.samples.size() == 4);
  CHECK(f.samples.contains(641) && f.samples.contains(642) && f.samples.contains(1281) &&
        f.samples.contains(1282));
  f.samples.clear();
  f.bp[0x20] = (344 << 12) | 344;
  f.bp[0x21] = (343 << 12) | 343;
  f.draw({f.vertex(0, 0), f.vertex(8, 0), f.vertex(0, 8)});
  CHECK(f.samples.empty());
  // An offset/scissor combination wrapping at 1024 draws the translated range.
  f.bp[0x20] = (1366 << 12) | 1366;
  f.bp[0x21] = (1369 << 12) | 1369;
  f.put(0x101d, 1374);
  f.put(0x101e, 1374);
  f.draw({f.vertex(0, 0), f.vertex(4, 0), f.vertex(4, 4), f.vertex(0, 4)}, 0x80);
  CHECK(f.samples.size() == 16);
  CHECK(f.samples.contains(0) && f.samples.contains(1923));
}

void scissor_state_change_tests() {
  struct Work {
    std::array<int, 6> rectangle_and_origin;
    std::array<s64, 9> edges;
    bool operator==(const Work &) const = default;
  };

  const auto collect = [](std::vector<Work> &commands) {
    return [&commands](const RasterWork &work) {
      Work result{
          {work.left, work.top, work.right, work.bottom, work.depth.x_origin, work.depth.y_origin},
          {}};
      for (unsigned i = 0; i < 3; ++i) {
        result.edges[i * 3] = work.edges[i].dx;
        result.edges[i * 3 + 1] = work.edges[i].dy;
        result.edges[i * 3 + 2] = work.edges[i].constant;
      }
      commands.push_back(result);
      return true;
    };
  };
  Fixture retained;
  retained.bp[0x20] = 0;
  retained.bp[0x21] = (2047 << 12) | 2047;
  retained.xf[0x1005] = 1;
  retained.put(0x101a, 1024);
  retained.put(0x101b, 1024);
  std::vector<Work> commands;
  retained.rasterizer.hardware = collect(commands);
  // Cover every wrapped region so viewport changes can alter the submission order.
  const std::array<std::optional<TransformedVertex>, 3> vertices{
      retained.vertex(-248, -248), retained.vertex(520, -248), retained.vertex(-248, 520)};
  const auto compare = [&] {
    commands.clear();
    retained.rasterizer.draw(0x90, vertices);
    Fixture fresh;
    fresh.bp = retained.bp;
    fresh.xf = retained.xf;
    std::vector<Work> expected;
    fresh.rasterizer.hardware = collect(expected);
    fresh.rasterizer.draw(0x90, vertices);
    CHECK(commands == expected);
  };
  compare();
  compare();
  retained.bp[0x20] = (100 << 12) | 100;
  compare();
  retained.bp[0x21] = (500 << 12) | 500;
  compare();
  retained.bp[0x21] = (2047 << 12) | 2047;
  compare();
  retained.bp[0x59] = 10 | (10 << 10);
  compare();
  retained.put(0x101a, 512);
  compare();
  retained.put(0x101b, 512);
  compare();
  retained.put(0x101d, 1374);
  compare();
  retained.put(0x101e, 1374);
  compare();
  retained.bp[0x20] = (1600 << 12) | 1600;
  compare();
  CHECK(commands.empty());
  retained.bp[0x20] = 0;
  compare();
  CHECK(!commands.empty());
}

void clipping_and_depth_tests() {
  Fixture f;
  f.draw({f.vertex(-8, 0), f.vertex(4, 0), f.vertex(4, 4)});
  CHECK(!f.samples.empty());
  for (const auto &[coordinate, sample] : f.samples) {
    (void)sample;
    CHECK(coordinate % 640 < 4 && coordinate / 640 < 4);
  }
  f.samples.clear();
  f.draw({f.vertex(-12, 0), f.vertex(-8, 0), f.vertex(-8, 4)});
  CHECK(f.samples.empty());
  // Scissor-rejected triangles still update the persistent depth plane.
  f.bp[0x20] = (350 << 12) | 350;
  f.bp[0x21] = (357 << 12) | 357;
  f.draw({f.vertex(0, 0, 700), f.vertex(4, 0, 700), f.vertex(0, 4, 700)});
  f.bp[0] |= 1 << 19;
  f.bp[0x20] = (342 << 12) | 342;
  f.draw({f.vertex(0, 0, 100), f.vertex(4, 0, 100), f.vertex(0, 4, 100)});
  CHECK(f.samples.at(0).depth == 700);
  f.rasterizer.reset();
  f.samples.clear();
  f.draw({f.vertex(0, 0), f.vertex(4, 0), f.vertex(0, 4)});
  CHECK(f.samples.at(0).depth == 1);
  // A trivially clipped triangle must not replace the remembered slope.
  f.bp[0] &= ~(u32{1} << 19);
  f.draw({f.vertex(-12, 0, 200), f.vertex(-8, 0, 200), f.vertex(-8, 4, 200)});
  f.bp[0] |= 1 << 19;
  f.samples.clear();
  f.draw({f.vertex(0, 0), f.vertex(4, 0), f.vertex(0, 4)});
  CHECK(f.samples.at(0).depth == 1);
  auto invalid = f.vertex(0, 0);
  invalid.clip_position[0] = std::bit_cast<float>(u32{0x7fc00000});
  CHECK_THROWS(f.draw({invalid, f.vertex(4, 0), f.vertex(0, 4)}));
}

void line_and_point_tests() {
  Fixture f;
  f.bp[0x22] = 12 | (12 << 8);
  f.draw({f.vertex(1, 2), f.vertex(5, 2)}, 0xa8);
  CHECK(f.samples.size() == 8);
  for (unsigned y : {1u, 2u})
    for (unsigned x = 1; x < 5; ++x)
      CHECK(f.samples.at(y * 640 + x).writes == 1);
  f.samples.clear();
  f.draw({f.vertex(2, 1), f.vertex(2, 5)}, 0xb0);
  CHECK(f.samples.size() == 8);
  for (unsigned y = 1; y < 5; ++y)
    for (unsigned x : {1u, 2u})
      CHECK(f.samples.at(y * 640 + x).writes == 1);
  f.samples.clear();
  f.draw({f.vertex(2, 2)}, 0xb8);
  CHECK(f.samples.size() == 4);
  CHECK(f.samples.at(641).writes == 1 && f.samples.at(1282).writes == 1);
  // Cull state does not affect line and point expansion.
  f.bp[0] |= 3 << 14;
  f.samples.clear();
  f.draw({f.vertex(2, 2)}, 0xb8);
  CHECK(f.samples.size() == 4);
  // Point texture offsets interpolate across the expanded square.
  f.samples.clear();
  f.bp[0] |= 1;
  f.xf[0x103f] = 1;
  f.bp[0x22] |= 5 << 19;
  f.bp[0x30] = (1 << 19) | 1;
  f.bp[0x31] = 3;
  auto center = f.vertex(2, 2);
  center.texcoords[0] = {0, 0, 1};
  f.draw({center}, 0xb8);
  const auto &sample = f.samples.at(641).inputs;
  CHECK(std::abs(sample.uv[0][0] - 0.495f) < 0.000001f);
  CHECK(std::abs(sample.uv[0][1] - 0.99f) < 0.000001f);
  CHECK(std::abs(sample.dx[0][0] - 1) < 0.000001f);
  CHECK(std::abs(sample.dy[0][1] - 2) < 0.000001f);
  f.samples.clear();
  f.bp[0x22] = 12 | (5 << 16);
  f.bp[0x30] = (1 << 18) | 1;
  auto a = f.vertex(1, 2), b = f.vertex(5, 2);
  a.texcoords[0] = b.texcoords[0] = {0, 0, 1};
  f.draw({a, b}, 0xa8);
  CHECK(std::abs(f.samples.at(641).inputs.uv[0][0] - 0.495f) < 0.000001f);
  CHECK(std::abs(f.samples.at(1281).inputs.uv[0][0] - 1.495f) < 0.000001f);
}
} // namespace

int main() {
  return run_tests([] {
    coverage_tests();
    assembly_tests();
    interpolation_tests();
    scissor_tests();
    scissor_state_change_tests();
    clipping_and_depth_tests();
    line_and_point_tests();
  });
}
