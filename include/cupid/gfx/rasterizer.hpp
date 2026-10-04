// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/gfx/fragment.hpp"
#include <functional>
#include <optional>
#include <vector>

namespace cupid::gfx {
class Rasterizer {
public:
  Rasterizer(std::span<const u32> bp, std::span<const u32> xf);
  std::function<void(unsigned x, unsigned y, u32 depth, const FragmentInputs &)> fragment;
  // Skipped indexed vertices are compacted before primitive assembly.
  void draw(u8 primitive_opcode, std::span<const std::optional<TransformedVertex>> vertices);
  void reset();

private:
  struct ScreenVertex {
    TransformedVertex vertex;
    Vector3 screen;
  };

  struct Scissor {
    int left, top, right, bottom, x_offset, y_offset;
  };

  struct Plane {
    float value = 1, dx = 0, dy = 0, x_offset = 0, y_offset = 0;
    int x_origin = 0, y_origin = 0;
    float at(int x, int y) const;
  };

  std::span<const u32> bp_, xf_;
  Plane depth_{};
  void triangle(TransformedVertex a, TransformedVertex b, TransformedVertex c);
  void line(TransformedVertex a, TransformedVertex b);
  void point(const TransformedVertex &vertex);
  ScreenVertex project(const TransformedVertex &vertex) const;
  std::vector<Scissor> scissors() const;
  void update_depth(const ScreenVertex &a, const ScreenVertex &b, const ScreenVertex &c,
                    int x_offset, int y_offset);
  void rasterize(const ScreenVertex &a, const ScreenVertex &b, const ScreenVertex &c);
};
} // namespace cupid::gfx
