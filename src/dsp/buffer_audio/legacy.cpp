// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal.hpp"
#include <algorithm>
#include <utility>

namespace cupid::dsp {
void BufferAudioProgram::Impl::load_input(bool align) {
  const u32 address = align ? integer(12) & ~31u : integer(12);
  const auto bytes = backing(address, 32);
  // The upper half is deliberately retained: unaligned stereo reads can use it.
  for (unsigned i = 0; i < 16; ++i)
    input[i] = u16((u16(bytes[i * 2]) << 8) | bytes[i * 2 + 1]);
}

void BufferAudioProgram::Impl::change_buffer() {
  word(32, word(44));
  word(34, word(46));
  integer(16, integer(40));
  integer(12, integer(36));
  integer(48, integer(36));
  if (!(integer(8) & (new_flags ? 0x100u : 4u))) {
    integer(36, 0);
    integer(40, 0);
  }
}

std::array<s16, 2> BufferAudioProgram::Impl::legacy_sample(unsigned format) const {
  const u32 address = integer(12);
  const unsigned index = (address >> 1) & 15;
  const u16 sample = input[index];
  const auto swapped = [](u16 value) { return u16((value >> 8) | (value << 8)); };
  switch (format) {
  case 0:
  case 4: {
    const u16 result =
        u16(((address & 1) ? u16(sample << 8) : u16(sample & 0xff00)) ^ (format == 4 ? 0x8000 : 0));
    return {s16(result), s16(result)};
  }
  case 1:
    return {s16(sample), s16(sample)};
  case 2:
  case 6: {
    const u16 biased = sample ^ (format == 6 ? 0x8080 : 0);
    return {s16(biased & 0xff00), s16(u16(biased << 8))};
  }
  case 3:
    return {s16(sample), s16(input[index + 1])};
  case 5:
    return {s16(swapped(sample)), s16(swapped(sample))};
  default:
    return {s16(swapped(sample)), s16(swapped(input[index + 1]))};
  }
}

void BufferAudioProgram::Impl::mix_legacy() {
  const u32 flags = integer(8);
  const unsigned format = flags & (new_flags ? 7 : 3);
  const u32 step = flags >> 16;
  const bool paused = (flags & (new_flags ? 0x200u : 0x20u)) != 0;
  if (!paused) {
    if (integer(12) == 0)
      change_buffer();
    if (integer(12) != 0) {
      unsigned remaining = 1024;
      const u32 delay = integer(4);
      if (delay) {
        word(24, 0);
        word(26, 0);
        // The task drops the entire remainder when a delay expires mid-buffer.
        // A full delayed buffer decrements the count only 1023 times.
        integer(4, delay > 1023 ? delay - 1023 : 0);
        remaining = 0;
      }
      load_input(true);
      for (unsigned sample_index = 0; sample_index < remaining; ++sample_index) {
        output[sample_index * 2] = u16(std::clamp(
            s32(s16(output[sample_index * 2])) + s16(word(26)), s32(-32768), s32(32767)));
        output[sample_index * 2 + 1] = u16(std::clamp(
            s32(s16(output[sample_index * 2 + 1])) + s16(word(24)), s32(-32768), s32(32767)));
        const u32 frequency = integer(20);
        u32 counter = integer(28) + frequency;
        if (counter >= 48000) {
          if (frequency <= 48000) {
            counter -= 48000;
            integer(12, integer(12) + step);
          } else {
            while (counter >= 48000) {
              counter -= 48000;
              integer(12, integer(12) + step);
              if (!(integer(12) & 31))
                load_input(true);
            }
          }
          integer(28, counter);
          if (integer(12) >= integer(16)) {
            change_buffer();
            if (integer(12) == 0) {
              word(24, 0);
              word(26, 0);
              continue;
            }
            load_input(true);
          }
          if (!((integer(12) >> 1) & 15))
            load_input(false);
          auto samples = legacy_sample(format);
          if (swap_channels)
            std::swap(samples[0], samples[1]);
          // This family truncates rather than saturates its scaled voice state.
          word(24, u16((s32(samples[0]) * word(32)) >> 8));
          word(26, u16((s32(samples[1]) * word(34)) >> 8));
        } else {
          integer(28, counter);
        }
      }
      if (integer(12) == 0)
        change_buffer();
    }
  }
  store_parameter();
  send(0xdcd10004, true);
}
} // namespace cupid::dsp
