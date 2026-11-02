// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/types.hpp"
#include <array>

namespace cupid {
class Memory;

namespace dsp {
std::array<u32, 2> joyboot_authentication(u32 challenge, u32 palette, u32 speed, u32 length);
void process_joyboot_request(Memory &memory, u32 address);
} // namespace dsp
} // namespace cupid
