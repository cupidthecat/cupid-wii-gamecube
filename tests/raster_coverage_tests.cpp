// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/gfx/raster_coverage.hpp"
#include "test_support.hpp"
#include <array>
#include <iostream>

namespace {
using namespace cupid;
using Edge = gfx::RasterWork::Edge;
using Edges = std::array<Edge, 3>;
u64 comparisons = 0;
std::array<u64, 16> masks{};

unsigned scalar(const Edges &edges, int block_x, int block_y) {
  unsigned mask = 0;
  for (int iy = 0; iy < 2; ++iy)
    for (int ix = 0; ix < 2; ++ix) {
      const int sample_x = block_x + ix, sample_y = block_y + iy;
      bool covered = true;
      for (const auto &edge : edges)
        covered =
            covered &&
            edge.constant + edge.dx * (s64(sample_y) * 16) - edge.dy * (s64(sample_x) * 16) > 0;
      if (covered)
        mask |= 1u << (iy * 2 + ix);
    }
  return mask;
}

unsigned check(const Edges &edges, int x, int y) {
  const unsigned expected = scalar(edges, x, y);
  const unsigned actual = gfx::detail::coverage_mask(edges, x, y);
  if (expected != actual)
    std::cerr << "Coverage case=" << comparisons << " anchor=" << x << ',' << y
              << " expected=" << expected << " actual=" << actual << '\n';
  CHECK(actual == expected && actual < 16);
  ++comparisons;
  ++masks[actual];
  return actual;
}

// A*x + B*y + C > 0 in local sample indices, with unused edges accepting all.
struct Local {
  s64 a, b, c;
};

Edge local_edge(Local line, int x, int y) {
  const s64 dx = line.b, dy = -line.a;
  return {dx, dy, line.c * 16 - dx * (s64(y) * 16) + dy * (s64(x) * 16)};
}

void every_mask() {
  constexpr Local any{0, 0, 1};
  constexpr std::array<std::array<Local, 3>, 16> cases{{
      {{{0, 0, 0}, any, any}},
      {{{-2, -2, 1}, any, any}},
      {{{2, -2, -1}, any, any}},
      {{{0, -2, 1}, any, any}},
      {{{-2, 2, -1}, any, any}},
      {{{-2, 0, 1}, any, any}},
      {{{2, 2, -1}, {-2, -2, 3}, any}},
      {{{-2, -2, 3}, any, any}},
      {{{2, 2, -3}, any, any}},
      {{{2, -2, 1}, {-2, 2, 1}, any}},
      {{{2, 0, -1}, any, any}},
      {{{2, -2, 1}, any, any}},
      {{{0, 2, -1}, any, any}},
      {{{-2, 2, 1}, any, any}},
      {{{2, 2, -1}, any, any}},
      {{any, any, any}},
  }};
  for (int x : {-1000000, -4095, -3, -1, 0, 1, 638, 639, 4095, 999999})
    for (int y : {-999999, -4096, -2, -1, 0, 1, 526, 527, 4097, 1000000})
      for (unsigned mask = 0; mask < cases.size(); ++mask) {
        Edges edges;
        for (unsigned i = 0; i < edges.size(); ++i)
          edges[i] = local_edge(cases[mask][i], x, y);
        CHECK(check(edges, x, y) == mask);
      }
}

Edge top_left(s64 x0, s64 y0, s64 x1, s64 y1) {
  Edge edge{x0 - x1, y0 - y1, 0};
  edge.constant = edge.dy * x0 - edge.dx * y0;
  if (edge.dy < 0 || (edge.dy == 0 && edge.dx > 0))
    ++edge.constant;
  return edge;
}

void boundaries() {
  const Edge any{0, 0, 1};
  for (s64 constant : {-1, 0, 1})
    CHECK(check({Edge{0, 0, constant}, any, any}, -1, 1) == (constant > 0 ? 15u : 0u));

  struct Segment {
    s64 x0, y0, x1, y1;
    int x, y;
    unsigned forward;
  };

  constexpr std::array<Segment, 4> segments{{
      {0, 0, 16, 0, 0, -1, 3},
      {0, 0, 0, 16, -1, 0, 10},
      {0, 0, 16, 16, 0, 0, 11},
      {0, 16, 16, 0, 0, 0, 1},
  }};
  for (const auto &s : segments)
    for (int shift_x : {-4095, 0, 639})
      for (int shift_y : {-4097, 0, 527}) {
        const s64 sx = s64(shift_x) * 16, sy = s64(shift_y) * 16;
        const Edge a = top_left(s.x0 + sx, s.y0 + sy, s.x1 + sx, s.y1 + sy);
        const Edge b = top_left(s.x1 + sx, s.y1 + sy, s.x0 + sx, s.y0 + sy);
        const int x = s.x + shift_x, y = s.y + shift_y;
        const unsigned forward = check({a, any, any}, x, y);
        const unsigned reverse = check({b, any, any}, x, y);
        CHECK(forward == s.forward && reverse == (15u ^ s.forward));
        CHECK((forward & reverse) == 0 && (forward | reverse) == 15);
        for (int offset : {-1, 0, 1}) {
          Edge moved = a;
          moved.constant += offset;
          (void)check({moved, any, any}, x, y);
        }
      }
}

u32 next(u32 &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

void generated() {
  u32 random = 0xb61d25e3;
  for (unsigned n = 0; n < 262144; ++n) {
    const int x = int(next(random) % 2000001) - 1000000;
    const int y = int(next(random) % 2000001) - 1000000;
    Edges edges;
    for (auto &edge : edges) {
      edge.dx = s64(next(random) & 0x0fffffff) - 0x08000000;
      edge.dy = s64(next(random) & 0x0fffffff) - 0x08000000;
      const s64 nearby = s64(next(random)) - 0x80000000ll;
      edge.constant = nearby - edge.dx * (s64(y) * 16) + edge.dy * (s64(x) * 16);
    }
    (void)check(edges, x, y);
  }
  // This corpus stays well below signed overflow: slopes are at most 2^27,
  // sample coordinates at most about 2^24, and constants at most 2^60.
  for (int x : {-1000000, -1, 0, 1, 1000000})
    for (int y : {-1000000, -1, 0, 1, 1000000})
      for (s64 dx : {-134217728ll, -1ll, 0ll, 1ll, 134217727ll})
        for (s64 dy : {-134217728ll, -1ll, 0ll, 1ll, 134217727ll})
          for (s64 constant : std::array<s64, 5>{-(s64{1} << 60), -1, 0, 1, s64{1} << 60}) {
            const Edge edge{dx, dy, constant};
            (void)check({edge, Edge{0, 0, 1}, Edge{0, 0, 1}}, x, y);
            (void)check({edge, Edge{-dx, -dy, -constant}, Edge{0, 0, 1}}, x, y);
          }
}
} // namespace

int main() {
  return run_tests([] {
    every_mask();
    boundaries();
    generated();
    for (u64 count : masks)
      CHECK(count != 0);
    std::cout << "Coverage-mask comparisons=" << comparisons << " masks=";
    for (unsigned i = 0; i < masks.size(); ++i)
      std::cout << (i ? "," : "") << masks[i];
    std::cout << '\n';
  });
}
