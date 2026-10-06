// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/rasterizer.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace cupid::gfx {
namespace {
float xf_float(std::span<const u32> xf, unsigned address) {
  return std::bit_cast<float>(xf[address]);
}

int fixed_coordinate(float value) {
  value *= 16;
  if (!std::isfinite(value) || value < -67108864.0f || value > 67108864.0f)
    throw VertexError("Raster coordinate exceeds the supported fixed-point range");
  const int truncated = static_cast<int>(value);
  return truncated + (value - truncated >= 0.5f ? 1 : 0) - 9;
}

int depth_origin(float value) {
  const float scaled = value * 16;
  // A culled primitive uses only an interpolation origin, without the edge
  // products that require the smaller coverage-coordinate range.
  constexpr float limit = 2147483520.0f;
  if (!std::isfinite(scaled) || scaled < -limit || scaled > limit)
    throw VertexError("Raster depth origin exceeds the supported fixed-point range");
  const int truncated = static_cast<int>(scaled);
  const int rounded = truncated + (scaled - truncated >= 0.5f ? 1 : 0);
  return (rounded + 6) >> 4;
}

unsigned clip_mask(const Vector4 &v) {
  return (v[3] - v[0] < 0 ? 1u : 0) | (v[0] + v[3] < 0 ? 2u : 0) | (v[3] - v[1] < 0 ? 4u : 0) |
         (v[1] + v[3] < 0 ? 8u : 0) | (v[3] * v[2] > 0 ? 16u : 0) | (v[2] + v[3] < 0 ? 32u : 0);
}

TransformedVertex interpolate(const TransformedVertex &outside, const TransformedVertex &inside,
                              float fraction) {
  if (!std::isfinite(fraction) || fraction < 0 || fraction > 1)
    throw VertexError("Clipping interpolation requires a finite fraction between zero and one");
  TransformedVertex result;
  const auto blend = [fraction](float a, float b) { return a + (b - a) * fraction; };
  for (unsigned i = 0; i < 3; ++i)
    result.model_position[i] = blend(outside.model_position[i], inside.model_position[i]);
  for (unsigned i = 0; i < 4; ++i)
    result.clip_position[i] = blend(outside.clip_position[i], inside.clip_position[i]);
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned c = 0; c < 3; ++c)
      result.normals[i][c] = blend(outside.normals[i][c], inside.normals[i][c]);
  const int color_fraction = static_cast<int>(fraction * 256);
  for (unsigned i = 0; i < 2; ++i)
    for (unsigned c = 0; c < 4; ++c)
      result.colors[i][c] = static_cast<u8>(
          outside.colors[i][c] +
          ((int(inside.colors[i][c]) - outside.colors[i][c]) * color_fraction >> 8));
  for (unsigned i = 0; i < 8; ++i)
    for (unsigned c = 0; c < 3; ++c)
      result.texcoords[i][c] = blend(outside.texcoords[i][c], inside.texcoords[i][c]);
  return result;
}
} // namespace

Rasterizer::Rasterizer(std::span<const u32> bp, std::span<const u32> xf) : bp_(bp), xf_(xf) {
  if (bp.size() < 256 || xf.size() < 0x1058)
    throw VertexError("Rasterizer requires complete BP/XF state");
}

void Rasterizer::reset() {
  depth_ = {};
}

float RasterPlane::at(int x, int y) const {
  const float x_delta = x_offset + float(x - x_origin);
  const float y_delta = y_offset + float(y - y_origin);
  return value + dx * x_delta + dy * y_delta;
}

void Rasterizer::draw(u8 opcode, std::span<const std::optional<TransformedVertex>> input) {
  if (!fragment)
    throw VertexError("Rasterizer has no fragment destination");
  if ((bp_[0] & 15) > 8 || ((bp_[0] >> 4) & 7) > 2)
    throw VertexError("BP vertex channel count exceeds its hardware limit");
  if (bp_[0] & (1u << 9))
    throw VertexError("Multisample triangle coverage is not implemented");
  for (unsigned i = 0; i < 6; ++i)
    if (!std::isfinite(xf_float(xf_, 0x101a + i)) ||
        ((i != 2 && i != 5) && std::abs(xf_float(xf_, 0x101a + i)) > 1000000))
      throw VertexError("Non-finite or out-of-range XF viewport");
  if ((xf_[0x103f] & 15) > 8)
    throw VertexError("XF texture generator count exceeds eight");
  const std::array<u32, 7> scissor_state{bp_[0x20],   bp_[0x21],   bp_[0x59],  xf_[0x101a],
                                         xf_[0x101b], xf_[0x101d], xf_[0x101e]};
  if (!scissor_state_valid_ || scissor_state != scissor_state_) {
    scissor_rectangles_ = scissors();
    scissor_state_ = scissor_state;
    scissor_state_valid_ = true;
  }
  auto &vertices = primitive_vertices_;
  vertices.clear();
  vertices.reserve(input.size());
  for (const auto &vertex : input)
    if (vertex) {
      for (float coordinate : vertex->clip_position)
        if (!std::isfinite(coordinate))
          throw VertexError("Non-finite homogeneous vertex position");
      auto scaled = *vertex;
      for (unsigned i = 0; i < (bp_[0] & 15); ++i) {
        scaled.texcoords[i][0] *= (bp_[0x30 + i * 2] & 65535) + 1;
        scaled.texcoords[i][1] *= (bp_[0x31 + i * 2] & 65535) + 1;
      }
      vertices.push_back(scaled);
    }
  const auto emit = [&](std::size_t a, std::size_t b, std::size_t c) {
    triangle(vertices[a], vertices[b], vertices[c]);
  };
  switch (opcode & 0xf8) {
  case 0x80:
  case 0x88: {
    std::size_t i = 0;
    for (; i + 3 < vertices.size(); i += 4) {
      emit(i, i + 1, i + 2);
      emit(i, i + 2, i + 3);
    }
    if (vertices.size() - i == 3)
      emit(i, i + 1, i + 2);
    break;
  }
  case 0x90:
    for (std::size_t i = 0; i + 2 < vertices.size(); i += 3)
      emit(i, i + 1, i + 2);
    break;
  case 0x98:
    for (std::size_t i = 2; i < vertices.size(); ++i)
      emit(i - 2, i - ((i & 1) ? 0 : 1), i - ((i & 1) ? 1 : 0));
    break;
  case 0xa0:
    for (std::size_t i = 2; i < vertices.size(); ++i)
      emit(0, i - 1, i);
    break;
  case 0xa8:
    for (std::size_t i = 1; i < vertices.size(); i += 2)
      line(vertices[i - 1], vertices[i]);
    break;
  case 0xb0:
    for (std::size_t i = 1; i < vertices.size(); ++i)
      line(vertices[i - 1], vertices[i]);
    break;
  case 0xb8:
    for (const auto &vertex : vertices)
      point(vertex);
    break;
  default:
    throw VertexError("Invalid GX primitive opcode");
  }
}

void Rasterizer::line(TransformedVertex a, TransformedVertex b) {
  const unsigned a_mask = clip_mask(a.clip_position), b_mask = clip_mask(b.clip_position);
  const unsigned mask = a_mask | b_mask;
  float trim_a = 0, trim_b = 0;
  for (unsigned plane = 0; plane < 6; ++plane) {
    if (!(mask & (1u << plane)))
      continue;
    const auto distance = [plane](const Vector4 &v) {
      switch (plane) {
      case 0:
        return v[3] - v[0];
      case 1:
        return v[0] + v[3];
      case 2:
        return v[3] - v[1];
      case 3:
        return v[1] + v[3];
      case 4:
        return v[3] - v[2];
      default:
        return v[2] + v[3];
      }
    };
    const float first = distance(a.clip_position), second = distance(b.clip_position);
    if (first < 0 && second < 0)
      return;
    if (second < 0)
      trim_b = std::max(trim_b, second / (second - first));
    else if (first < 0)
      trim_a = std::max(trim_a, first / (first - second));
  }
  const auto original_a = a, original_b = b;
  if (a_mask)
    a = interpolate(original_a, original_b, trim_a);
  if (b_mask)
    b = interpolate(original_b, original_a, trim_b);
  const auto start = project(a), end = project(b);
  const float dx = end.screen[0] - start.screen[0], dy = end.screen[1] - start.screen[1];
  const float radius = (bp_[0x22] & 255) / 12.0f;
  const float x_side = std::abs(dx) > std::abs(dy) ? 0.0f : (dy > 0 ? radius : -radius);
  const float y_side = std::abs(dx) > std::abs(dy) ? (dx > 0 ? -radius : radius) : 0.0f;
  constexpr std::array<float, 8> offsets{0, 1 / 16.0f, 1 / 8.0f, 1 / 4.0f, 1 / 2.0f, 1, 1, 1};
  const float texture_offset = offsets[(bp_[0x22] >> 16) & 7];
  const auto side = [&](ScreenVertex vertex, bool opposite) {
    vertex.screen[0] += opposite ? -x_side : x_side;
    vertex.screen[1] += opposite ? -y_side : y_side;
    if (opposite && texture_offset != 0)
      for (unsigned i = 0; i < (xf_[0x103f] & 15); ++i)
        if (bp_[0x30 + i * 2] & (1u << 18))
          vertex.vertex.texcoords[i][0] += ((bp_[0x30 + i * 2] & 65535) + 1) * texture_offset;
    return vertex;
  };
  const auto start_near = side(start, false), end_near = side(end, false);
  const auto start_far = side(start, true), end_far = side(end, true);
  rasterize(end_far, end_near, start_near);
  rasterize(start_near, start_far, end_far);
}

void Rasterizer::point(const TransformedVertex &vertex) {
  const auto center = project(vertex);
  const float radius = ((bp_[0x22] >> 8) & 255) / 12.0f;
  constexpr std::array<float, 8> offsets{0, 1 / 16.0f, 1 / 8.0f, 1 / 4.0f, 1 / 2.0f, 1, 1, 1};
  const float texture_offset = offsets[(bp_[0x22] >> 19) & 7];
  const auto corner = [&](bool right, bool bottom) {
    auto result = center;
    result.screen[0] += right ? radius : -radius;
    result.screen[1] += bottom ? radius : -radius;
    if (texture_offset != 0)
      for (unsigned i = 0; i < (xf_[0x103f] & 15); ++i)
        if (bp_[0x30 + i * 2] & (1u << 19)) {
          if (right)
            result.vertex.texcoords[i][0] += ((bp_[0x30 + i * 2] & 65535) + 1) * texture_offset;
          if (bottom)
            result.vertex.texcoords[i][1] += ((bp_[0x31 + i * 2] & 65535) + 1) * texture_offset;
        }
    return result;
  };
  const auto top_left = corner(false, false), top_right = corner(true, false);
  const auto bottom_left = corner(false, true), bottom_right = corner(true, true);
  rasterize(top_left, bottom_left, top_right);
  rasterize(bottom_right, top_right, bottom_left);
}

Rasterizer::ScreenVertex Rasterizer::project(const TransformedVertex &vertex) const {
  if (vertex.clip_position[3] == 0)
    throw VertexError("A zero clip W cannot be divided for rasterization");
  ScreenVertex result{vertex, {}};
  const float inverse_w = 1.0f / vertex.clip_position[3];
  for (unsigned i = 0; i < 3; ++i)
    result.screen[i] =
        vertex.clip_position[i] * inverse_w * xf_float(xf_, 0x101a + i) + xf_float(xf_, 0x101d + i);
  return result;
}

void Rasterizer::triangle(TransformedVertex a, TransformedVertex b, TransformedVertex c) {
  const unsigned a_mask = clip_mask(a.clip_position), b_mask = clip_mask(b.clip_position),
                 c_mask = clip_mask(c.clip_position);
  if (a_mask & b_mask & c_mask)
    return;
  const auto &p = a.clip_position, &q = b.clip_position, &r = c.clip_position;
  const float orientation = (p[0] * r[3] - r[0] * p[3]) * q[1] +
                            (r[0] * p[1] - p[0] * r[1]) * q[3] + (r[1] * p[3] - p[1] * r[3]) * q[0];
  if (orientation == 0)
    return;
  bool back = orientation <= 0;
  if (xf_float(xf_, 0x101b) > 0)
    back = !back;
  if (back)
    std::swap(b, c);
  const unsigned cull = (bp_[0] >> 14) & 3;
  if ((!back && (cull == 1 || cull == 3)) || (back && (cull == 2 || cull == 3))) {
    update_depth(project(a), project(b), project(c), int(bp_[0x59] & 511) * 2,
                 int((bp_[0x59] >> 10) & 511) * 2);
    return;
  }
  const bool bypass = (xf_[0x1005] & 1) && a.clip_position[3] >= 0 && b.clip_position[3] >= 0 &&
                      c.clip_position[3] >= 0;
  const unsigned mask = a_mask | b_mask | c_mask;
  if (!mask || bypass) {
    rasterize(project(a), project(b), project(c));
    return;
  }
  std::vector<TransformedVertex> polygon{a, b, c};
  if (!bypass) {
    for (unsigned plane = 0; plane < 6; ++plane) {
      if (!(mask & (1u << plane)))
        continue;
      const auto distance = [plane](const TransformedVertex &vertex) {
        const auto &v = vertex.clip_position;
        switch (plane) {
        case 0:
          return v[3] - v[0];
        case 1:
          return v[0] + v[3];
        case 2:
          return v[3] - v[1];
        case 3:
          return v[1] + v[3];
        case 4:
          return v[3];
        default:
          return v[2] + v[3];
        }
      };
      std::vector<TransformedVertex> clipped;
      clipped.reserve(polygon.size() + 2);
      for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto &previous = polygon[i], &current = polygon[(i + 1) % polygon.size()];
        const float before = distance(previous), after = distance(current);
        if (before >= 0)
          clipped.push_back(previous);
        if ((before <= 0 && after > 0) || (before > 0 && after <= 0)) {
          if (after < 0)
            clipped.push_back(interpolate(current, previous, after / (after - before)));
          else
            clipped.push_back(interpolate(previous, current, before / (before - after)));
        }
      }
      if (clipped.size() < 3)
        return;
      polygon = std::move(clipped);
    }
  }
  for (std::size_t i = 2; i < polygon.size(); ++i)
    rasterize(project(polygon[0]), project(polygon[i - 1]), project(polygon[i]));
}

std::vector<Rasterizer::Scissor> Rasterizer::scissors() const {
  const int left = (bp_[0x20] >> 12) & 2047, top = bp_[0x20] & 2047;
  const int right = (bp_[0x21] >> 12) & 2047, bottom = bp_[0x21] & 2047;
  std::vector<Scissor> result;
  if (left > right || top > bottom)
    return result;
  const int x_offset = (bp_[0x59] & 511) * 2, y_offset = ((bp_[0x59] >> 10) & 511) * 2;
  for (int x_wrap = -4096; x_wrap <= 4096; x_wrap += 1024)
    for (int y_wrap = -4096; y_wrap <= 4096; y_wrap += 1024) {
      const int x = x_offset + x_wrap, y = y_offset + y_wrap;
      Scissor rect{std::clamp(left - x, 0, 640),
                   std::clamp(top - y, 0, 528),
                   std::clamp(right - x + 1, 0, 640),
                   std::clamp(bottom - y + 1, 0, 528),
                   x,
                   y};
      if (rect.left < rect.right && rect.top < rect.bottom)
        result.push_back(rect);
    }
  const float center_x = xf_float(xf_, 0x101d), center_y = xf_float(xf_, 0x101e);
  const float half_x = std::abs(xf_float(xf_, 0x101a)), half_y = std::abs(xf_float(xf_, 0x101b));
  const int viewport_left = static_cast<int>(center_x - half_x),
            viewport_right = static_cast<int>(center_x + half_x);
  const int viewport_top = static_cast<int>(center_y - half_y),
            viewport_bottom = static_cast<int>(center_y + half_y);
  const auto areas = [&](const Scissor &rect) {
    const int x0 = std::clamp(rect.left + rect.x_offset, viewport_left, viewport_right);
    const int x1 = std::clamp(rect.right + rect.x_offset, viewport_left, viewport_right);
    const int y0 = std::clamp(rect.top + rect.y_offset, viewport_top, viewport_bottom);
    const int y1 = std::clamp(rect.bottom + rect.y_offset, viewport_top, viewport_bottom);
    return std::pair{(x1 - x0) * (y1 - y0), (rect.right - rect.left) * (rect.bottom - rect.top)};
  };
  std::sort(result.begin(), result.end(),
            [&](const Scissor &a, const Scissor &b) { return areas(a) < areas(b); });
  return result;
}

void Rasterizer::update_depth(const ScreenVertex &a, const ScreenVertex &b, const ScreenVertex &c,
                              int x_offset, int y_offset) {
  if (bp_[0] & (1u << 19))
    return;
  const float dx1 = b.screen[0] - a.screen[0], dx2 = c.screen[0] - a.screen[0];
  const float dy1 = b.screen[1] - a.screen[1], dy2 = c.screen[1] - a.screen[1];
  const float determinant = dx2 * dy1 - dx1 * dy2;
  if (determinant == 0)
    return;
  depth_.value = a.screen[2];
  depth_.dx = ((c.screen[2] - a.screen[2]) * dy1 - (b.screen[2] - a.screen[2]) * dy2) / determinant;
  depth_.dy = (dx2 * (b.screen[2] - a.screen[2]) - dx1 * (c.screen[2] - a.screen[2])) / determinant;
  depth_.x_origin = depth_origin(a.screen[0] - x_offset);
  depth_.y_origin = depth_origin(a.screen[1] - y_offset);
  depth_.x_offset = float(depth_.x_origin) - (a.screen[0] - x_offset) + 0.495f;
  depth_.y_offset = float(depth_.y_origin) - (a.screen[1] - y_offset) + 0.495f;
}

void Rasterizer::rasterize(const ScreenVertex &a, const ScreenVertex &b, const ScreenVertex &c) {
  const float dx1 = b.screen[0] - a.screen[0], dx2 = c.screen[0] - a.screen[0];
  const float dy1 = b.screen[1] - a.screen[1], dy2 = c.screen[1] - a.screen[1];
  const float determinant = dx2 * dy1 - dx1 * dy2;
  if (determinant == 0)
    return;
  const unsigned color_count = (bp_[0] >> 4) & 7, texture_count = bp_[0] & 15;
  const std::array<const ScreenVertex *, 3> vertices{&a, &b, &c};
  const std::array<float, 3> inverse_w{1.0f / a.vertex.clip_position[3],
                                       1.0f / b.vertex.clip_position[3],
                                       1.0f / c.vertex.clip_position[3]};
  for (const auto &scissor : scissor_rectangles_) {
    update_depth(a, b, c, scissor.x_offset, scissor.y_offset);
    std::array<int, 3> x{}, y{};
    for (unsigned i = 0; i < 3; ++i) {
      x[i] = fixed_coordinate(vertices[i]->screen[0] - scissor.x_offset);
      y[i] = fixed_coordinate(vertices[i]->screen[1] - scissor.y_offset);
    }
    const int min_x = std::max((std::min({x[0], x[1], x[2]}) + 15) >> 4, scissor.left);
    const int max_x = std::min((std::max({x[0], x[1], x[2]}) + 15) >> 4, scissor.right);
    const int min_y = std::max((std::min({y[0], y[1], y[2]}) + 15) >> 4, scissor.top);
    const int max_y = std::min((std::max({y[0], y[1], y[2]}) + 15) >> 4, scissor.bottom);
    if (min_x >= max_x || min_y >= max_y)
      continue;
    const int origin_x = (x[0] + 15) >> 4, origin_y = (y[0] + 15) >> 4;
    const auto plane = [&](float first, float second, float third) {
      Plane result;
      result.value = first;
      result.dx = ((third - first) * dy1 - (second - first) * dy2) / determinant;
      result.dy = (dx2 * (second - first) - dx1 * (third - first)) / determinant;
      result.x_origin = origin_x;
      result.y_origin = origin_y;
      result.x_offset = float(origin_x) - (a.screen[0] - scissor.x_offset) + 0.495f;
      result.y_offset = float(origin_y) - (a.screen[1] - scissor.y_offset) + 0.495f;
      return result;
    };
    const Plane w_plane = plane(inverse_w[0], inverse_w[1], inverse_w[2]);
    std::array<std::array<Plane, 4>, 2> colors;
    for (unsigned i = 0; i < color_count; ++i)
      for (unsigned component = 0; component < 4; ++component)
        colors[i][component] = plane(a.vertex.colors[i][component], b.vertex.colors[i][component],
                                     c.vertex.colors[i][component]);
    std::array<std::array<Plane, 3>, 8> textures;
    for (unsigned i = 0; i < texture_count; ++i)
      for (unsigned component = 0; component < 3; ++component)
        textures[i][component] = plane(a.vertex.texcoords[i][component] * inverse_w[0],
                                       b.vertex.texcoords[i][component] * inverse_w[1],
                                       c.vertex.texcoords[i][component] * inverse_w[2]);

    std::array<RasterWork::Edge, 3> edges;
    for (unsigned i = 0; i < 3; ++i) {
      const unsigned next = (i + 1) % 3;
      auto &edge = edges[i];
      edge.dx = s64(x[i]) - x[next];
      edge.dy = s64(y[i]) - y[next];
      edge.constant = edge.dy * x[i] - edge.dx * y[i];
      if (edge.dy < 0 || (edge.dy == 0 && edge.dx > 0))
        ++edge.constant;
    }
    if (hardware && hardware({min_x, min_y, max_x, max_y, edges, depth_, w_plane, colors, textures,
                              xf_float(xf_, 0x101a)}))
      continue;
    const auto covered = [&](int sample_x, int sample_y) {
      return std::all_of(edges.begin(), edges.end(), [&](const RasterWork::Edge &edge) {
        return edge.constant + edge.dx * (sample_y * 16) - edge.dy * (sample_x * 16) > 0;
      });
    };
    for (int block_y = min_y & ~1; block_y < max_y; block_y += 2)
      for (int block_x = min_x & ~1; block_x < max_x; block_x += 2) {
        std::array<std::array<std::array<std::array<float, 2>, 8>, 2>, 2> block{};
        for (int iy = 0; iy < 2; ++iy)
          for (int ix = 0; ix < 2; ++ix) {
            const int px = block_x + ix, py = block_y + iy;
            const float reciprocal = 1.0f / w_plane.at(px, py);
            for (unsigned i = 0; i < texture_count; ++i) {
              float projection = reciprocal;
              const float q = textures[i][2].at(px, py) * reciprocal;
              if (q != 0)
                projection = reciprocal / q;
              for (unsigned component = 0; component < 2; ++component)
                block[ix][iy][i][component] = textures[i][component].at(px, py) * projection;
            }
          }
        for (int iy = 0; iy < 2; ++iy)
          for (int ix = 0; ix < 2; ++ix) {
            const int px = block_x + ix, py = block_y + iy;
            if (px < min_x || px >= max_x || py < min_y || py >= max_y || !covered(px, py))
              continue;
            FragmentInputs inputs;
            inputs.x = static_cast<unsigned>(px);
            inputs.viewport_width = xf_float(xf_, 0x101a);
            for (unsigned i = 0; i < color_count; ++i)
              for (unsigned component = 0; component < 4; ++component) {
                const float color = colors[i][component].at(px, py);
                if (!std::isfinite(color))
                  throw VertexError("Non-finite interpolated vertex color");
                inputs.colors[i][component] = static_cast<u8>(std::clamp(color, 0.0f, 255.0f));
              }
            for (unsigned i = 0; i < texture_count; ++i)
              for (unsigned component = 0; component < 2; ++component) {
                inputs.uv[i][component] = block[ix][iy][i][component];
                inputs.dx[i][component] = block[1][0][i][component] - block[0][0][i][component];
                inputs.dy[i][component] = block[0][1][i][component] - block[0][0][i][component];
                if (!std::isfinite(inputs.uv[i][component]) ||
                    !std::isfinite(inputs.dx[i][component]) ||
                    !std::isfinite(inputs.dy[i][component]))
                  throw VertexError("Non-finite projected texture coordinate");
              }
            const float depth = depth_.at(px, py);
            if (!std::isfinite(depth))
              throw VertexError("Non-finite interpolated depth");
            fragment(static_cast<unsigned>(px), static_cast<unsigned>(py),
                     static_cast<u32>(std::clamp(depth, 0.0f, 16777215.0f)), inputs);
          }
      }
  }
}
} // namespace cupid::gfx
