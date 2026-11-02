// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/dsp/handheld.hpp"
#include "cupid/core/memory.hpp"

namespace cupid::dsp {
std::array<u32, 2> joyboot_authentication(u32 challenge, u32 palette, u32 speed, u32 length) {
  const u32 payload = ((length + 7) & ~u32(7)) - 512;
  const u16 pairs = payload & 0x80000000 ? u16(0) : u16(payload / 8);
  const int interpolation = (speed & 0xff) < 128 ? int(speed & 0xff) : int(speed & 0xff) - 256;
  u32 color;
  if (interpolation == 0)
    color = (palette * 2) | 0x70;
  else {
    const int magnitude = interpolation < 0 ? 2 - interpolation : interpolation - 1;
    color = (palette << 4) | u32(magnitude * 2);
  }
  const u16 parameters = u16(color | ((pairs >> 14) & 1));
  const u16 count_code = u16(((u32(pairs) & 0x3f80) << 1) | (pairs & 0x7f));
  const u8 checksum = u8(u32(parameters) + u8(count_code) + u8(count_code >> 8));
  const u32 packed = (u32(parameters) << 16) | (u32(checksum) << 24) | count_code | 0x80808080;
  return {challenge ^ 0x6f646573, packed ^ (pairs & 0x100 ? 0x6f646573 : 0x6177614b)};
}

void process_joyboot_request(Memory &memory, u32 address) {
  const auto input = memory.range(Memory::boot_physical(address), 20);
  const auto word = [&](std::size_t offset) {
    return (u32(input[offset]) << 24) | (u32(input[offset + 1]) << 16) |
           (u32(input[offset + 2]) << 8) | input[offset + 3];
  };
  const u32 challenge =
      u32(input[0]) | (u32(input[1]) << 8) | (u32(input[2]) << 16) | (u32(input[3]) << 24);
  const auto result = joyboot_authentication(challenge, word(4), word(8), word(12));
  auto output = memory.range(Memory::boot_physical(word(16)), 8);
  for (unsigned index = 0; index < 8; ++index)
    output[index] = u8(result[index / 4] >> (24 - 8 * (index % 4)));
}
} // namespace cupid::dsp
