// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace cupid::dsp {
using namespace command_audio;

namespace {
s16 clamp_sample(s64 value) {
  return s16(std::clamp(value, s64(-32768), s64(32767)));
}

int physical_word(unsigned logical, bool updates, bool biquad) {
  if (!updates && logical >= 53 && logical < 58)
    return -1;
  if (!biquad && logical >= 106 && logical < 112)
    return -1;
  if (!biquad && logical >= 112)
    return int(logical - 6);
  return int(logical - (!updates && logical >= 58 ? 5 : 0));
}

void low_pass(WiiVoice &voice, unsigned field, std::span<s16> samples) {
  s16 history = s16(voice[field + 1]);
  for (auto &sample : samples) {
    const s32 first = s32(voice[field + 2]) * sample;
    const s32 second = s32(s16(voice[field + 3])) * history;
    sample = history = clamp_sample(add32(first, second) >> 15);
  }
  voice[field + 1] = u16(history);
}

void biquad(WiiVoice &voice, unsigned field, std::span<s16> samples) {
  s16 x1 = s16(voice[field + 1]), x2 = s16(voice[field + 2]);
  s16 y1 = s16(voice[field + 3]), y2 = s16(voice[field + 4]);
  for (auto &sample : samples) {
    s64 value = s64(s16(voice[field + 5])) * sample + s64(s16(voice[field + 6])) * x1 +
                s64(s16(voice[field + 7])) * x2 + s64(s16(voice[field + 8])) * y1 +
                s64(s16(voice[field + 9])) * y2;
    value *= 4;
    value += (u64(value) & 0x10000) ? 0x8000 : 0x7fff;
    x2 = x1;
    x1 = sample;
    y2 = y1;
    sample = y1 = clamp_sample(value >> 16);
  }
  voice[field + 1] = u16(x1);
  voice[field + 2] = u16(x2);
  voice[field + 3] = u16(y1);
  voice[field + 4] = u16(y2);
}

void mix(std::span<s32> destination, std::span<const s16> samples, WiiVoice &voice,
         unsigned volume_field, unsigned last_field, bool ramp) {
  u16 volume = voice[volume_field];
  const u16 delta = ramp ? voice[volume_field + 1] : u16(0);
  s16 last = 0;
  for (unsigned i = 0; i < samples.size(); ++i) {
    last = clamp_sample((s64(samples[i]) * volume) >> 15);
    destination[i] = add32(destination[i], last);
    volume = u16(volume + delta);
  }
  voice[volume_field] = volume;
  voice[last_field] = u16(last);
}

void remote_resample(WiiVoice &voice, std::span<const s16> input, std::span<s16> output,
                     std::span<const s16> coefficients) {
  std::array<s16, 4> history{};
  for (unsigned i = 0; i < 4; ++i)
    history[i] = s16(voice[139 + i]);
  u32 position = voice[138];
  unsigned cursor = 0, consumed = 0;
  for (auto &sample : output) {
    position += 0x55555;
    while (position >= 65536) {
      if (consumed == input.size())
        throw std::runtime_error("DSP Remote resampler exceeded its input frame");
      history[cursor++ & 3] = input[consumed++];
      position -= 65536;
    }
    if (!coefficients.empty()) {
      const unsigned table = (position >> 9) * 4;
      s64 accumulated = 0;
      for (unsigned tap = 0; tap < 4; ++tap)
        accumulated += s64(history[(cursor + tap) & 3]) * coefficients[table + tap];
      sample = clamp_sample(accumulated >> 15);
    } else {
      sample = s16((s64(history[cursor & 3]) * (65536 - position) +
                    s64(history[(cursor + 1) & 3]) * position) >>
                   16);
    }
  }
  voice[138] = u16(position);
  for (unsigned i = 0; i < 4; ++i)
    voice[139 + i] = u16(history[(cursor + i) & 3]);
}
} // namespace

WiiVoice CommandAudioProgram::Impl::read_wii_voice(u32 address) {
  WiiVoice voice{};
  backing(address, new_filter ? 300 : 298);
  for (unsigned i = 0; i < voice.size(); ++i) {
    const int physical = physical_word(i, old_wii, new_filter);
    if (physical >= 0)
      voice[i] = read16(address + unsigned(physical) * 2);
  }
  return voice;
}

void CommandAudioProgram::Impl::write_wii_voice(u32 address, const WiiVoice &voice) {
  backing(address, new_filter ? 300 : 298);
  for (unsigned i = 0; i < voice.size(); ++i) {
    const int physical = physical_word(i, old_wii, new_filter);
    if (physical >= 0)
      write16(address + unsigned(physical) * 2, voice[i]);
  }
}

void CommandAudioProgram::Impl::process_wii_voices(u32 address) {
  std::unordered_set<u32> visited;
  while (address) {
    if (!visited.insert(address).second)
      throw std::runtime_error("Wii DSP voice chain contains a cycle");
    auto voice = read_wii_voice(address);
    if (old_wii && (voice[53] || voice[54] || voice[55])) {
      const u32 update_address = (u32(voice[56]) << 16) | voice[57];
      backing(update_address, 128);
      std::array<std::array<u16, 2>, 32> updates{};
      for (unsigned i = 0; i < updates.size(); ++i)
        updates[i] = {read16(update_address + i * 4), read16(update_address + i * 4 + 2)};
      for (unsigned millisecond = 0; millisecond < 3; ++millisecond) {
        unsigned begin = 0;
        for (unsigned previous = 0; previous < millisecond; ++previous)
          begin += voice[53 + previous];
        const unsigned count = voice[53 + millisecond];
        if (begin < updates.size() && count <= updates.size() - begin)
          for (unsigned i = begin; i < begin + count; ++i)
            if (updates[i][0] < voice.size())
              voice[updates[i][0]] = updates[i][1];
        process_wii_voice(voice, millisecond * 32, 32);
      }
    } else {
      process_wii_voice(voice, 0, 96);
    }
    write_wii_voice(address, voice);
    address = (u32(voice[0]) << 16) | voice[1];
  }
}

void CommandAudioProgram::Impl::process_wii_voice(WiiVoice &voice, unsigned offset,
                                                  unsigned count) {
  if (voice[8] != 1)
    return;
  const u32 control = (u32(voice[6]) << 16) | voice[7];
  Voice decoder{};
  decoder[4] = voice[4];
  decoder[5] = voice[5];
  decoder[7] = voice[8];
  decoder[8] = voice[9];
  std::copy_n(voice.begin() + 60, 38, decoder.begin() + 55);
  std::array<s16, 96> storage{};
  auto samples = std::span<s16>(storage).first(count);
  decode_samples(decoder, samples);
  voice[8] = decoder[7];
  // Only decoder-mutated fields are written back; loop descriptors stay intact.
  std::copy_n(decoder.begin() + 61, 2, voice.begin() + 66);
  std::copy_n(decoder.begin() + 80, 3, voice.begin() + 85);
  std::copy_n(decoder.begin() + 85, 5, voice.begin() + 90);
  for (auto &sample : samples) {
    sample = clamp_sample((s32(sample) * s32(voice[58])) >> 15);
    voice[58] = u16(voice[58] + voice[59]);
  }
  if (voice[98])
    low_pass(voice, 98, samples);
  if (new_filter && voice[102])
    biquad(voice, 102, samples);

  constexpr std::array<unsigned, 12> volume_fields{10, 12, 26, 14, 16, 28, 18, 20, 30, 22, 24, 32};
  constexpr std::array<unsigned, 12> last_fields{41, 45, 49, 42, 46, 50, 43, 47, 51, 44, 48, 52};
  constexpr std::array<unsigned, 4> group_bits{0, 16, 21, 26};
  for (unsigned group = 0; group < 4; ++group) {
    const u32 bits = control >> group_bits[group];
    for (unsigned side = 0; side < 3; ++side) {
      const bool ramp = (bits & (side == 2 ? 16u : 4u)) != 0;
      if (!(bits & (1u << (side == 2 ? 3 : side))) && !ramp)
        continue;
      const unsigned channel = group * 3 + side;
      mix(std::span<s32>(wii_channels[channel]).subspan(offset, count), samples, voice,
          volume_fields[channel], last_fields[channel], ramp);
    }
  }
  if (!voice[112])
    return;
  if (new_filter && voice[143]) {
    if (voice[143] == 2)
      biquad(voice, 143, samples);
    else
      low_pass(voice, 143, samples);
  }
  std::array<s16, 18> remote_storage{};
  auto remote_samples = std::span<s16>(remote_storage).first(count == 96 ? 18 : 6);
  remote_resample(voice, samples, remote_samples,
                  have_coefficients ? std::span<const s16>(coefficients) : std::span<const s16>{});
  for (unsigned channel = 0; channel < 8; ++channel) {
    const u16 enabled = (voice[113] >> (channel * 2)) & 3;
    if (!enabled)
      continue;
    const unsigned last = 130 + channel / 2 + (channel & 1) * 4;
    mix(std::span<s32>(wii_channels[12 + channel]).subspan(offset / 32 * 6, remote_samples.size()),
        remote_samples, voice, 114 + channel * 2, last, (enabled & 2) != 0);
  }
}
} // namespace cupid::dsp
