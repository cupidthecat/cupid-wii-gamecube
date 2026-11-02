// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/hw/remote.hpp"
#include <array>

namespace cupid::platform {
// Normalized horizontal and vertical inputs range from -1 to 1; vertical is positive upward.
std::array<hw::InfraredPoint, 4> pointer_camera(double horizontal, double vertical,
                                                bool sensor_bar_on_top = true);
} // namespace cupid::platform
