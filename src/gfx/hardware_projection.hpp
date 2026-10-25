// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/host_float_environment.hpp"
#include "cupid/gfx/rasterizer.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace cupid::gfx::detail {
// A continuous plane can cross zero between discrete raster samples. For small
// rectangles, validate the actual staged binary32 operations instead. Keep a
// two-exponent margin above the normal boundary: native shader multiplication
// and addition need not preserve denormal operands or intermediate results.
inline bool bounded_projection(const RasterWork &work, unsigned count) {
  if (!count || count > 8 || work.left < 0 || work.top < 0 || work.right > 640 ||
      work.bottom > 528 || work.left >= work.right || work.top >= work.bottom)
    return false;
  const int left = work.left & ~1, top = work.top & ~1;
  const int right = (work.right + 1) & ~1, bottom = (work.bottom + 1) & ~1;
  if ((right - left) / 2 * ((bottom - top) / 2) > 64)
    return false;

  const HostFloatEnvironment environment;
  const auto normal_or_zero = [](float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value) & 0x7fffffffu;
    const auto exponent = bits >> 23;
    return bits == 0 || (exponent >= 3 && exponent < 255);
  };
  const auto source = [&](const RasterPlane &plane) {
    return normal_or_zero(plane.value) && normal_or_zero(plane.dx) && normal_or_zero(plane.dy) &&
           normal_or_zero(plane.x_offset) && normal_or_zero(plane.y_offset);
  };
  if (!source(work.reciprocal_w))
    return false;
  for (unsigned i = 0; i < count; ++i)
    for (const auto &plane : work.textures[i])
      if (!source(plane))
        return false;

  const auto add = [&](float a, float b, float &result) {
    result = a + b;
    return normal_or_zero(result) && (result != 0 || a == -b);
  };
  const auto multiply = [&](float a, float b, float &result) {
    result = a * b;
    return normal_or_zero(result) && (result != 0 || a == 0 || b == 0);
  };
  const auto divide = [&](float a, float b, float &result) {
    if (b == 0)
      return false;
    result = a / b;
    return normal_or_zero(result) && (result != 0 || a == 0);
  };
  const auto evaluate = [&](const RasterPlane &plane, int x, int y, float &result) {
    // Match GLSL's signed subtraction and exact integer-to-binary32 conversion.
    const auto dx = std::int64_t(x) - plane.x_origin;
    const auto dy = std::int64_t(y) - plane.y_origin;
    if (dx < -16777216 || dx > 16777216 || dy < -16777216 || dy > 16777216)
      return false;
    float x_delta, y_delta, horizontal, vertical, first;
    return add(plane.x_offset, float(dx), x_delta) && add(plane.y_offset, float(dy), y_delta) &&
           multiply(plane.dx, x_delta, horizontal) && multiply(plane.dy, y_delta, vertical) &&
           add(plane.value, horizontal, first) && add(first, vertical, result);
  };

  for (int by = top; by < bottom; by += 2)
    for (int bx = left; bx < right; bx += 2) {
      std::array<std::array<std::array<float, 2>, 8>, 4> uv{};
      for (unsigned iy = 0; iy < 2; ++iy)
        for (unsigned ix = 0; ix < 2; ++ix) {
          const unsigned pixel = iy * 2 + ix;
          float w, reciprocal;
          if (!evaluate(work.reciprocal_w, bx + int(ix), by + int(iy), w) ||
              !divide(1, w, reciprocal))
            return false;
          for (unsigned i = 0; i < count; ++i) {
            float q_plane, q, projection = reciprocal;
            if (!evaluate(work.textures[i][2], bx + int(ix), by + int(iy), q_plane) ||
                !multiply(q_plane, reciprocal, q) || (q != 0 && !divide(reciprocal, q, projection)))
              return false;
            for (unsigned c = 0; c < 2; ++c) {
              float numerator, fixed;
              if (!evaluate(work.textures[i][c], bx + int(ix), by + int(iy), numerator) ||
                  !multiply(numerator, projection, uv[pixel][i][c]) ||
                  std::abs(uv[pixel][i][c]) >= 8388608.0f || !multiply(uv[pixel][i][c], 128, fixed))
                return false;
            }
          }
        }
      // Software rasterization validates derivatives for every generated
      // coordinate, even when no texture stage consumes that coordinate.
      for (unsigned i = 0; i < count; ++i)
        for (unsigned c = 0; c < 2; ++c) {
          float dx, dy;
          if (!add(uv[1][i][c], -uv[0][i][c], dx) || !add(uv[2][i][c], -uv[0][i][c], dy))
            return false;
        }
    }
  return true;
}
} // namespace cupid::gfx::detail
