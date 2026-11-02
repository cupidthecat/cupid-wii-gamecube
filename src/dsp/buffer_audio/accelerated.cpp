// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal.hpp"
#include <algorithm>
#include <utility>

namespace cupid::dsp {
void BufferAudioProgram::Impl::mix_accelerated() {
  const u32 flags = integer(40);
  const unsigned format = flags & (new_flags ? 7 : 3);
  const bool paused = (flags & (new_flags ? 8u : 4u)) != 0;
  if (!paused && (flags & 0x40000000) && integer(4)) {
    const bool wide = (format & 2) != 0;
    accelerator.write(0xd1, wide ? 0x0a : 0x19);
    accelerator.write(0xde, wide ? 0x0800 : 0x0100);
    accelerator.write(0xd4, word(4));
    accelerator.write(0xd5, word(6));
    accelerator.write(0xd6, word(8));
    accelerator.write(0xd7, word(10));
    accelerator.write(0xd8, word(12));
    accelerator.write(0xd9, word(14));
    accelerator.write(0xdb, word(16));
    accelerator.write(0xdc, word(18));
    accelerator.write(0xda, word(20));
    unsigned remaining = 96, index = 0;
    u32 delay = integer(36);
    while (delay && remaining) {
      --remaining;
      --delay;
      if (!delay)
        break;
      ++index;
    }
    integer(36, delay);
    constexpr std::array<s16, 16> coefficients{};
    while (remaining) {
      --remaining;
      output[index * 2] =
          u16(std::clamp(s32(s16(output[index * 2])) + s16(word(30)), s32(-32768), s32(32767)));
      output[index * 2 + 1] =
          u16(std::clamp(s32(s16(output[index * 2 + 1])) + s16(word(28)), s32(-32768), s32(32767)));
      ++index;
      const u32 counter = u32(word(26)) + integer(22);
      word(26, u16(counter));
      unsigned consume = counter >> 16;
      if (consume) {
        s16 left = 0, right = 0;
        while (consume--) {
          right = s16(accelerator.read_sample(coefficients));
          left = (format & 1) ? s16(accelerator.read_sample(coefficients)) : right;
        }
        if (format & 4) {
          left = s16(u16(left) ^ 0x8000);
          right = s16(u16(right) ^ 0x8000);
        }
        if (swap_channels)
          std::swap(left, right);
        word(28, u16(std::clamp((s32(left) * word(32)) >> 8, s32(-32768), s32(32767))));
        word(30, u16(std::clamp((s32(right) * word(34)) >> 8, s32(-32768), s32(32767))));
      }
    }
    const auto &state = accelerator.state();
    word(20, state.predictor);
    word(18, u16(state.history2));
    word(16, u16(state.history1));
    integer(12, state.current);
  }
  integer(40, flags | 0x00100000);
  store_parameter();
  send(0xdcd10004, true);
}
} // namespace cupid::dsp
