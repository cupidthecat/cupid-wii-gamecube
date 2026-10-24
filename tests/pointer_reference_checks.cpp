// SPDX-License-Identifier: GPL-2.0-or-later
#include "Common/MathUtil.h"
#include "Core/HW/WiimoteEmu/Camera.h"
#include "cupid/platform/pointer.hpp"
#include <iostream>

namespace {
std::array<WiimoteEmu::CameraPoint, 4> target(double horizontal, double vertical, bool on_top) {
  using Common::Matrix33;
  using Common::Matrix44;
  const float yaw = static_cast<float>(25 * MathUtil::TAU / 360 / 2);
  const float pitch = static_cast<float>(20 * MathUtil::TAU / 360 / 2);
  const auto rotation = Matrix33::RotateZ(static_cast<float>(yaw * horizontal)) *
                        Matrix33::RotateY(0) *
                        Matrix33::RotateX(static_cast<float>(pitch * vertical));
  const auto transform = Matrix44::Translate({0, 0, 0}) *
                         Matrix44::FromMatrix33(Matrix33::Identity() * Matrix33::Identity() *
                                                rotation * Matrix33::Identity()) *
                         Matrix44::Translate({0, -2, on_top ? 0.1f : -0.1f});
  using Camera = WiimoteEmu::CameraLogic;
  const auto degrees =
      Common::DVec2(Camera::CAMERA_FOV_X, Camera::CAMERA_FOV_Y) / MathUtil::TAU * 360;
  const auto fov = Common::Vec2(static_cast<float>(degrees.x), static_cast<float>(degrees.y)) /
                   360 * float(MathUtil::TAU);
  return Camera::GetCameraPoints(transform, fov);
}

bool compare(double horizontal, double vertical, bool on_top) {
  const auto expected = target(horizontal, vertical, on_top);
  const auto actual = cupid::platform::pointer_camera(horizontal, vertical, on_top);
  for (unsigned led = 0; led < actual.size(); ++led)
    if (actual[led].x != expected[led].position.x || actual[led].y != expected[led].position.y ||
        actual[led].size != expected[led].size) {
      std::cerr << "Pointer divergence x=" << horizontal << " y=" << vertical << " top=" << on_top
                << " led=" << led << " actual=" << actual[led].x << ',' << actual[led].y << ','
                << unsigned(actual[led].size) << " expected=" << expected[led].position.x << ','
                << expected[led].position.y << ',' << unsigned(expected[led].size) << '\n';
      return false;
    }
  return true;
}
} // namespace

int main() {
  unsigned cases = 0;
  for (bool on_top : {false, true})
    for (int x = -128; x <= 128; ++x)
      for (int y = -128; y <= 128; ++y) {
        ++cases;
        if (!compare(x / 128.0, y / 128.0, on_top))
          return 1;
      }
  cupid::u32 random = 0x713562a9;
  for (unsigned sample = 0; sample < 100000; ++sample) {
    random = random * 1664525 + 1013904223;
    const double horizontal = double(random) / 2147483647.5 - 1;
    random = random * 1664525 + 1013904223;
    const double vertical = double(random) / 2147483647.5 - 1;
    ++cases;
    if (!compare(horizontal, vertical, sample & 1))
      return 1;
  }
  std::cout << cases << " exact pointer projections; zero divergences\n";
}
