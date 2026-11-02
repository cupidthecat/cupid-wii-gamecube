// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>

namespace cupid::gfx::detail {
struct BatchExtent {
  int left = 640, top = 528, right = 0, bottom = 0;
  std::uint64_t summed_area = 0;

  BatchExtent including(int x0, int y0, int x1, int y1) const {
    return {std::min(left, x0), std::min(top, y0), std::max(right, x1), std::max(bottom, y1),
            summed_area + std::uint64_t(x1 - x0) * std::uint64_t(y1 - y0)};
  }

  bool admitted() const {
    const auto union_area = std::uint64_t(right - left) * std::uint64_t(bottom - top);
    return union_area <= 4096 && union_area <= 4 * summed_area;
  }
};
} // namespace cupid::gfx::detail
