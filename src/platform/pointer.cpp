// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/platform/pointer.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace cupid::platform {
namespace {
using Matrix = std::array<std::array<float, 4>, 4>;

Matrix combine(const Matrix &left, const Matrix &right) {
  Matrix product{};
  for (unsigned inner = 0; inner < 4; ++inner)
    for (unsigned row = 0; row < 4; ++row)
      for (unsigned column = 0; column < 4; ++column)
        product[row][column] += left[row][inner] * right[inner][column];
  return product;
}

Matrix pose(float yaw, float pitch, float height) {
  const float sy = std::sin(yaw), cy = std::cos(yaw);
  const float sp = std::sin(pitch), cp = std::cos(pitch);
  Matrix rotation{
      {{cy, -sy * cp, sy * sp, 0}, {sy, cy * cp, -cy * sp, 0}, {0, sp, cp, 0}, {0, 0, 0, 1}}};
  for (unsigned row = 0; row < 3; ++row)
    rotation[row][3] = rotation[row][1] * -2 + rotation[row][2] * height;
  return rotation;
}

Matrix lens() {
  constexpr double tau = 2 * std::numbers::pi;
  constexpr float turn = static_cast<float>(tau);
  constexpr float horizontal = 42 * turn / 360;
  constexpr float vertical = horizontal / (4.0f / 3);
  constexpr float fov_x = static_cast<float>(double(horizontal) / tau * 360) / 360 * turn;
  constexpr float fov_y = static_cast<float>(double(vertical) / tau * 360) / 360 * turn;
  const float tangent = std::tan(fov_y / 2);
  Matrix projection{{{1 / ((fov_x / fov_y) * tangent), 0, 0, 0},
                     {0, 1 / tangent, 0, 0},
                     {0, 0, -(1000.0f + 0.001f) / (1000.0f - 0.001f),
                      -(2 * 1000.0f * 0.001f) / (1000.0f - 0.001f)},
                     {0, 0, -1, 0}}};
  const float quarter_turn = static_cast<float>(tau / 4);
  const float sine = std::sin(quarter_turn), cosine = std::cos(quarter_turn);
  const Matrix axes{{{1, 0, 0, 0}, {0, cosine, -sine, 0}, {0, sine, cosine, 0}, {0, 0, 0, 1}}};
  return combine(projection, axes);
}
} // namespace

std::array<hw::InfraredPoint, 4> pointer_camera(double horizontal, double vertical,
                                                bool sensor_bar_on_top) {
  std::array<hw::InfraredPoint, 4> points{};
  if (!std::isfinite(horizontal) || !std::isfinite(vertical) || horizontal < -1 || horizontal > 1 ||
      vertical < -1 || vertical > 1)
    return points;
  constexpr double tau = 2 * std::numbers::pi;
  constexpr float yaw_scale = static_cast<float>(25 * tau / 360 / 2);
  constexpr float pitch_scale = static_cast<float>(20 * tau / 360 / 2);
  const auto camera = combine(lens(), pose(static_cast<float>(yaw_scale * horizontal),
                                           static_cast<float>(pitch_scale * vertical),
                                           sensor_bar_on_top ? 0.1f : -0.1f));
  for (unsigned led = 0; led < 2; ++led) {
    std::array<float, 4> clip{};
    const std::array<float, 4> location{led ? 0.1f : -0.1f, 0, 0, 1};
    for (unsigned axis = 0; axis < 4; ++axis)
      for (unsigned component = 0; component < 4; ++component)
        clip[axis] += camera[axis][component] * location[component];
    if (clip[2] < 0 || clip[3] == 0)
      continue;
    const int x = static_cast<int>((1 - clip[0] / clip[3]) * 512);
    const int y = static_cast<int>((1 - clip[1] / clip[3]) * 384);
    if (x < 0 || x >= 1024 || y < 0 || y >= 768)
      continue;
    const long size = std::lround(2.37f * std::pow(clip[2], -0.778f));
    points[led] = {static_cast<u16>(x), static_cast<u16>(y),
                   static_cast<u8>(std::clamp(size, 1L, 15L))};
  }
  return points;
}
} // namespace cupid::platform
