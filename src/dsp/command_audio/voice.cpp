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
} // namespace

u32 CommandAudioProgram::Impl::mix_control(u16 value) const {
  u32 result = 0;
  const auto enable = [&](unsigned channel) { result |= 1u << (channel * 2); };
  const auto ramp = [&](unsigned channel) { result |= 2u << (channel * 2); };
  if (program_hash == 0x4e8a8b21) {
    enable(0);
    enable(1);
    if (value & 16) {
      if (!(value & 6)) {
        enable(6);
        enable(7);
      }
      if ((value & 7) == 1) {
        enable(3);
        enable(4);
        enable(5);
      }
    } else {
      if (value & 1) {
        enable(3);
        enable(4);
      }
      if (value & 2) {
        enable(6);
        enable(7);
      }
      if (value & 4) {
        enable(2);
        if (value & 1)
          enable(5);
        if (value & 2)
          enable(8);
      }
    }
    if (value & 8)
      result |= 0xaaaaaa;
  } else {
    constexpr std::array<unsigned, 9> bits{0, 1, 2, 4, 5, 7, 9, 10, 12};
    constexpr std::array<unsigned, 9> ramps{3, 3, 3, 6, 6, 8, 11, 11, 13};
    for (unsigned channel = 0; channel < 9; ++channel) {
      if (value & (1u << bits[channel]))
        enable(channel);
      if (value & (1u << ramps[channel]))
        ramp(channel);
    }
  }
  return result;
}

Voice CommandAudioProgram::Impl::read_voice(u32 address) {
  Voice voice{};
  const bool compact = program_hash == 0x4e8a8b21;
  backing(address, (voice_words - (compact ? 4 : 0)) * 2);
  for (unsigned i = 0; i < voice.size(); ++i) {
    if (compact && i >= 93 && i < 97)
      continue;
    voice[i] = read16(address + (i - (compact && i >= 97 ? 4 : 0)) * 2);
  }
  return voice;
}

void CommandAudioProgram::Impl::write_voice(u32 address, const Voice &voice) {
  const bool compact = program_hash == 0x4e8a8b21;
  backing(address, (voice_words - (compact ? 4 : 0)) * 2);
  for (unsigned i = 0; i < voice.size(); ++i) {
    if (compact && i >= 93 && i < 97)
      continue;
    write16(address + (i - (compact && i >= 97 ? 4 : 0)) * 2, voice[i]);
  }
}

void CommandAudioProgram::Impl::process_voices(u32 address) {
  std::unordered_set<u32> visited;
  while (address) {
    if (!visited.insert(address).second)
      throw std::runtime_error("DSP audio voice chain contains a cycle");
    auto voice = read_voice(address);
    std::array<std::array<u16, 2>, 32> updates{};
    const u32 update_address = pair(voice, 39);
    backing(update_address, 128);
    for (unsigned i = 0; i < updates.size(); ++i) {
      updates[i][0] = read16(update_address + i * 4);
      updates[i][1] = read16(update_address + i * 4 + 2);
    }
    for (unsigned millisecond = 0; millisecond < 5; ++millisecond) {
      unsigned begin = 0;
      for (unsigned previous = 0; previous < millisecond; ++previous)
        begin += voice[34 + previous];
      const unsigned count = voice[34 + millisecond];
      if (begin < updates.size() && count <= updates.size() - begin)
        for (unsigned i = begin; i < begin + count; ++i)
          if (updates[i][0] < voice.size())
            voice[updates[i][0]] = updates[i][1];
      process_voice(voice, millisecond * 32);
    }
    write_voice(address, voice);
    address = pair(voice, 0);
  }
}

void CommandAudioProgram::Impl::decode_samples(Voice &voice, std::span<s16, 32> samples) {
  active_voice = &voice;
  accelerator.write(0xd4, voice[57]);
  accelerator.write(0xd5, voice[58]);
  accelerator.write(0xd6, voice[59]);
  accelerator.write(0xd7, voice[60]);
  accelerator.write(0xd8, voice[61]);
  accelerator.write(0xd9, voice[62]);
  accelerator.write(0xd1, voice[56]);
  accelerator.write(0xdb, voice[81]);
  accelerator.write(0xdc, voice[82]);
  accelerator.write(0xde, voice[79]);
  accelerator.write(0xda, voice[80]);
  std::array<s16, 16> predictor_coefficients{};
  for (unsigned i = 0; i < predictor_coefficients.size(); ++i)
    predictor_coefficients[i] = s16(voice[63 + i]);
  const auto sample = [&] { return s16(accelerator.read_sample(predictor_coefficients)); };
  const unsigned kind = voice[4];
  u32 position = voice[85];
  if (kind < 2) {
    const bool polyphase = kind == 0 && have_coefficients;
    if (polyphase && voice[5] >= 4)
      throw std::runtime_error("DSP resampling coefficient bank is outside the supplied table");
    std::array<s16, 4> history{};
    for (unsigned i = 0; i < 4; ++i)
      history[i] = s16(voice[86 + i]);
    unsigned cursor = 0;
    const u32 ratio = pair(voice, 83);
    for (auto &output_sample : samples) {
      position += ratio;
      while (position >= 65536) {
        history[cursor++ & 3] = sample();
        position -= 65536;
      }
      if (polyphase) {
        const unsigned table = unsigned(voice[5]) * 512 + ((position & 65535) >> 9) * 4;
        s64 accumulated = 0;
        for (unsigned tap = 0; tap < 4; ++tap)
          accumulated += s64(history[(cursor + tap) & 3]) * coefficients[table + tap];
        output_sample = clamp_sample(accumulated >> 15);
      } else if (position & 65535) {
        const u32 fraction = position & 65535;
        output_sample = s16((s64(history[cursor & 3]) * (65536 - fraction) +
                             s64(history[(cursor + 1) & 3]) * fraction) >>
                            16);
      } else {
        output_sample = history[cursor & 3];
      }
    }
    for (unsigned i = 0; i < 4; ++i)
      voice[86 + i] = u16(history[(cursor + i) & 3]);
  } else {
    for (auto &output_sample : samples)
      output_sample = sample();
    for (unsigned i = 0; i < 4; ++i)
      voice[86 + i] = u16(samples[28 + i]);
  }
  voice[85] = u16(position);
  const auto &decoded = accelerator.state();
  pair(voice, 61, decoded.current);
  voice[81] = u16(decoded.history1);
  voice[82] = u16(decoded.history2);
  voice[80] = decoded.predictor;
  active_voice = nullptr;
}

void CommandAudioProgram::Impl::process_voice(Voice &voice, unsigned offset) {
  if (voice[7] != 1)
    return;
  const u32 control = mix_control(voice[6]);
  std::array<s16, 32> samples{};
  decode_samples(voice, samples);
  for (auto &sample : samples) {
    sample = clamp_sample((s32(sample) * s32(s16(voice[50]))) >> 15);
    voice[50] = u16(voice[50] + voice[51]);
  }
  if (voice[93]) {
    s16 history = s16(voice[94]);
    for (auto &sample : samples) {
      const s32 first = s32(voice[95]) * sample;
      const s32 second = s32(s16(voice[96])) * history;
      history = sample = clamp_sample(add32(first, second) >> 15);
    }
    voice[94] = u16(history);
  }
  constexpr std::array<unsigned, 9> volume_offsets{9, 11, 23, 13, 15, 25, 17, 19, 21};
  constexpr std::array<unsigned, 9> drop_offsets{41, 44, 47, 42, 45, 48, 43, 46, 49};
  for (unsigned channel = 0; channel < 9; ++channel) {
    if (!(control & (1u << (channel * 2))))
      continue;
    const unsigned volume_offset = volume_offsets[channel];
    u16 volume = voice[volume_offset];
    const u16 delta = control & (2u << (channel * 2)) ? voice[volume_offset + 1] : 0;
    s16 last = 0;
    for (unsigned i = 0; i < samples.size(); ++i) {
      last = clamp_sample((s64(samples[i]) * volume) >> 15);
      channels[channel][offset + i] = add32(channels[channel][offset + i], last);
      volume = u16(volume + delta);
    }
    voice[volume_offset] = volume;
    voice[drop_offsets[channel]] = u16(last);
  }
  // The selected task retains initial-time-delay parameters without applying
  // a channel phase shift. Other filters and volume state still advance.
}
} // namespace cupid::dsp
