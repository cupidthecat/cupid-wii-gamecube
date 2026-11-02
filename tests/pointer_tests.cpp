// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/platform/pointer.hpp"
#include "test_support.hpp"
#include <cmath>
#include <limits>

namespace {
void unavailable(const std::array<cupid::hw::InfraredPoint, 4> &points) {
  for (const auto &point : points)
    CHECK(point.x == 0xffff && point.y == 0xffff && point.size == 0xff);
}

void default_pose(bool on_top) {
  const auto center = cupid::platform::pointer_camera(0, 0, on_top);
  const cupid::u16 y = on_top ? 452 : 315;
  CHECK(center[0].x == 580 && center[1].x == 443);
  CHECK(center[0].y == y && center[1].y == y);
  CHECK(center[0].size == 1 && center[1].size == 1);
  CHECK(center[2].x == 0xffff && center[3].x == 0xffff);
  const auto left = cupid::platform::pointer_camera(-1, 0, on_top);
  const auto right = cupid::platform::pointer_camera(1, 0, on_top);
  const auto up = cupid::platform::pointer_camera(0, 1, on_top);
  const auto down = cupid::platform::pointer_camera(0, -1, on_top);
  for (unsigned led = 0; led < 2; ++led) {
    CHECK(left[led].x > center[led].x && right[led].x < center[led].x);
    CHECK(up[led].y < center[led].y && down[led].y > center[led].y);
  }
  for (int x = -16; x <= 16; ++x)
    for (int vertical = -16; vertical <= 16; ++vertical) {
      const auto points = cupid::platform::pointer_camera(x / 16.0, vertical / 16.0, on_top);
      for (unsigned led = 0; led < 2; ++led)
        CHECK(points[led].x < 1024 && points[led].y < 768 && points[led].size == 1);
      CHECK(points[2].x == 0xffff && points[3].x == 0xffff);
    }
}
} // namespace

int main() {
  return run_tests([] {
    default_pose(false);
    default_pose(true);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (double bad : {nan, infinity, -infinity, 1.001, -1.001}) {
      unavailable(cupid::platform::pointer_camera(bad, 0));
      unavailable(cupid::platform::pointer_camera(0, bad));
    }
  });
}
