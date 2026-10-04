// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/dsp/audio.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <deque>
#include <limits>
#include <sstream>
#include <vector>

namespace cupid::dsp {
namespace {

constexpr unsigned frame_samples = 80;
using Samples = std::array<s16, frame_samples>;

struct Profile {
  bool light = false;
  bool four_channels = false;
  bool small_voice = false;
  bool explicit_ramp = false;
  bool frame_sync = false;
  bool louder_position = false;
  bool no_command_13 = false;
  bool combined = false;
  bool main_ram_audio = false;
  bool special_command_12 = false;
};

std::optional<Profile> profile_for(u32 hash) {
  Profile profile;
  switch (hash) {
  case 0x24b22038:
    profile.light = profile.four_channels = profile.small_voice = profile.explicit_ramp = true;
    profile.no_command_13 = profile.special_command_12 = true;
    break;
  case 0x6ba3b3ea:
    profile.light = profile.four_channels = profile.no_command_13 = true;
    break;
  case 0xdf059f68:
  case 0x4be6a5cb:
  case 0x42f64ac4:
    profile.light = profile.no_command_13 = profile.special_command_12 = true;
    break;
  case 0x267fd05a:
  case 0x56d36052:
    profile.frame_sync = profile.no_command_13 = true;
    break;
  case 0x86840740:
    break;
  case 0x2fcdf1ec:
    profile.louder_position = true;
    break;
  case 0x6ca33a6d:
    profile.louder_position = profile.combined = true;
    break;
  case 0x6c3f6f94:
  case 0xd643001f:
  case 0xb7eb9a9c:
  case 0xeaeb38cc:
    profile.louder_position = profile.combined = profile.main_ram_audio = true;
    break;
  default:
    return std::nullopt;
  }
  return profile;
}

s16 word_signed(u16 value) {
  return std::bit_cast<s16>(value);
}

s16 truncate16(s64 value) {
  return word_signed(static_cast<u16>(value));
}

s32 truncate32(s64 value) {
  return std::bit_cast<s32>(static_cast<u32>(value));
}

s16 saturate(s64 value) {
  return static_cast<s16>(std::clamp<s64>(value, -32768, 32767));
}

u32 checked_address(u32 base, u64 displacement) {
  if (displacement > std::numeric_limits<u32>::max() - base)
    throw std::runtime_error("DSP guest address overflow");
  return base + static_cast<u32>(displacement);
}

struct Voice {
  std::array<u16, 0xc0> words{};

  u16 &operator[](unsigned index) {
    return words.at(index);
  }

  u16 operator[](unsigned index) const {
    return words.at(index);
  }

  s16 signed_word(unsigned index) const {
    return word_signed(words.at(index));
  }

  u32 pair(unsigned index) const {
    return (u32(words.at(index)) << 16) | words.at(index + 1);
  }

  void pair(unsigned index, u32 value) {
    words.at(index) = static_cast<u16>(value >> 16);
    words.at(index + 1) = static_cast<u16>(value);
  }
};

class Mixer {
public:
  Mixer(Memory &memory, Console console, Profile profile,
        std::function<void(u32, std::span<u8>)> reader)
      : memory_(memory), console_(console), profile_(profile), read_aram_(std::move(reader)) {
  }

  void configure(u32 voices, u32 tables, u32 afc, u32 reverb) {
    read_words(tables, resampling_);
    read_words(checked_address(tables, 0x200), patterns_);
    if (!profile_.light)
      read_words(checked_address(tables, 0x400), sine_);
    read_words(afc, afc_);
    if (reverb)
      memory_.range(Memory::boot_physical(reverb), 128);
    voices_ = voices;
    reverb_ = reverb;
  }

  void output(u32 left, u32 right, u16 volume, u32 frames) {
    memory_.range(Memory::boot_physical(left), u64(frames) * 160);
    memory_.range(Memory::boot_physical(right), u64(frames) * 160);
    checked_address(left, u64(frames) * 160);
    checked_address(right, u64(frames) * 160);
    left_ = left;
    right_ = right;
    volume_ = volume;
  }

  void main_audio_base(u32 address) {
    audio_base_ = address;
  }

  void prepare();
  void voice(u16 index);
  void finish();

private:
  Memory &memory_;
  Console console_;
  Profile profile_;
  std::function<void(u32, std::span<u8>)> read_aram_;
  u32 voices_ = 0, reverb_ = 0, audio_base_ = 0, left_ = 0, right_ = 0;
  u16 volume_ = 0;
  bool prepared_ = false;
  std::array<Samples, 13> channels_{};
  std::array<s16, 256> resampling_{}, patterns_{};
  std::array<s16, 128> sine_{};
  std::array<s16, 32> afc_{};
  std::array<u16, 4> reverb_cursor_{};
  std::array<std::array<s16, 8>, 4> reverb_history_{};

  template <typename T, std::size_t N> void read_words(u32 address, std::array<T, N> &words) {
    const auto bytes = memory_.range(Memory::boot_physical(address), N * 2);
    for (std::size_t i = 0; i < N; ++i)
      words[i] = static_cast<T>(word_signed((u16(bytes[2 * i]) << 8) | bytes[2 * i + 1]));
  }

  template <typename T> void write_words(u32 address, std::span<const T> words) {
    auto bytes = memory_.range(Memory::boot_physical(address), words.size() * 2);
    for (std::size_t i = 0; i < words.size(); ++i) {
      bytes[2 * i] = static_cast<u8>(static_cast<u16>(words[i]) >> 8);
      bytes[2 * i + 1] = static_cast<u8>(words[i]);
    }
  }

  std::vector<u8> audio_bytes(u32 address, unsigned count) {
    std::vector<u8> bytes(count);
    if (console_ == Console::Wii) {
      memory_.copy_out(Memory::boot_physical(checked_address(audio_base_, address)), bytes);
    } else {
      if (!read_aram_)
        throw std::runtime_error("DSP audio requires connected ARAM storage");
      read_aram_(address, bytes);
    }
    return bytes;
  }

  Samples &destination(u16 id) {
    constexpr std::array<u16, 13> ids{0x0d00, 0x0d60, 0x0f40, 0x0ca0, 0x0e80, 0x0ee0, 0x0c00,
                                      0x0c50, 0x0dc0, 0x0e20, 0x09a0, 0x0fa0, 0x0b00};
    const auto found = std::find(ids.begin(), ids.end(), id);
    if (found == ids.end())
      throw std::runtime_error("DSP voice targets an unsupported mixing buffer");
    return channels_[static_cast<std::size_t>(found - ids.begin())];
  }

  static s32 ramp(Samples &destination, const Samples &source, s16 start, s16 delta) {
    s32 volume = s32(start) * 65536;
    const s32 step = s32(delta) * 65536 / s32(frame_samples);
    for (unsigned i = 0; i < frame_samples; ++i) {
      destination[i] = truncate16(s32(destination[i]) + ((volume >> 16) * source[i] >> 16));
      volume = truncate32(s64(volume) + step);
    }
    return volume;
  }

  static void add(Samples &target, const s16 *source, unsigned count, u16 volume) {
    for (unsigned i = 0; i < count; ++i)
      target[i] = truncate16(s32(target[i]) + saturate(s64(source[i]) * volume >> 15));
  }

  static void scale(Samples &samples, u16 volume, unsigned fractional_bits) {
    for (auto &sample : samples)
      sample = saturate((s32(sample) * s32(volume)) >> fractional_bits);
  }

  void reverb(bool store);
  void input(Voice &voice, Samples &samples);
  void resample(Voice &voice, std::span<const s16> raw, Samples &samples);
  void pcm(Voice &voice, std::span<s16> raw, bool sixteen_bit);
  void afc(Voice &voice, std::span<s16> raw);
  void afc_blocks(Voice &voice, std::span<s16> output, unsigned blocks);
  void main_pcm(Voice &voice, std::span<s16> raw);
  static void filter(Voice &voice, Samples &samples);
};

void Mixer::prepare() {
  if (prepared_)
    return;
  channels_[0].fill(0);
  channels_[1].fill(0);
  scale(channels_[2], 0x6784, 15);
  scale(channels_[3], 0x6784, 15);
  reverb(false);
  add(channels_[4], channels_[6].data(), 80, 0x7fff);
  add(channels_[5], channels_[6].data(), 80, 0xb820);
  add(channels_[4], channels_[7].data() + 40, 40, 0xb820);
  add(channels_[5], channels_[7].data() + 40, 40, 0x7fff);
  channels_[6].fill(0);
  channels_[7].fill(0);
  s32 older = patterns_[190], newer = patterns_[191];
  for (unsigned i = 128; i < 192; ++i) {
    const s32 value = patterns_[i];
    const s64 next = i & 1 ? 2 * (s64(older) * newer + s64(value) * 65536)
                           : s64(older) * newer - s64(value) * 65536;
    patterns_[i] = truncate16(truncate32(next) >> 16);
    older = newer;
    newer = value;
  }
  const s32 a = patterns_[254], b = patterns_[255];
  s16 step = truncate16(s32(patterns_[192]) + (truncate32(s64(a) * b + s64(a) * 65536 + b) >> 16));
  step = word_signed((static_cast<u16>(step) & 0x1ff) | 0x2000);
  for (unsigned i = 0; i < 64; ++i)
    patterns_[192 + i] = truncate16(b + s32(i + 1) * step);
  prepared_ = true;
}

void Mixer::reverb(bool store) {
  if (!reverb_)
    return;
  constexpr std::array<unsigned, 4> indices{8, 9, 4, 5};
  for (unsigned index = 0; index < 4; ++index) {
    std::array<u16, 16> parameters{};
    read_words(checked_address(reverb_, index * 32), parameters);
    if (!parameters[0])
      continue;
    if (!parameters[1] || reverb_cursor_[index] >= parameters[1])
      throw std::runtime_error("DSP reverb has an invalid circular buffer size");
    const u32 base = (u32(parameters[2]) << 16) | parameters[3];
    const u32 address = checked_address(base, u64(reverb_cursor_[index]) * 160);
    if (store) {
      write_words(address, std::span<const s16>(channels_[indices[index]]));
      reverb_cursor_[index] = (reverb_cursor_[index] + 1) % parameters[1];
      continue;
    }
    std::array<s16, 88> samples{};
    std::copy(reverb_history_[index].begin(), reverb_history_[index].end(), samples.begin());
    std::array<s16, 80> downloaded{};
    read_words(address, downloaded);
    std::copy(downloaded.begin(), downloaded.end(), samples.begin() + 8);
    std::copy(samples.begin() + 80, samples.end(), reverb_history_[index].begin());
    auto apply_filter = [&] {
      for (unsigned i = 0; i < 80; ++i) {
        s64 sum = 0;
        for (unsigned tap = 0; tap < 8; ++tap)
          sum += s32(samples[i + tap]) * s32(word_signed(parameters[8 + tap]));
        samples[i] = saturate(truncate32(sum) >> 15);
      }
    };
    if (parameters[0] & 1)
      apply_filter();
    for (unsigned slot = 4; slot < 8; slot += 2) {
      if (parameters[slot])
        add(destination(parameters[slot]), samples.data(), 80, parameters[slot + 1]);
    }
    if (parameters[0] & 2)
      apply_filter();
    std::copy_n(samples.begin(), 80, channels_[indices[index]].begin());
  }
}

void Mixer::pcm(Voice &voice, std::span<s16> raw, bool sixteen_bit) {
  if (voice[1]) {
    std::fill(raw.begin(), raw.end(), 0);
    return;
  }
  const unsigned width = sixteen_bit ? 2 : 1;
  if (voice[4]) {
    if (voice.pair(0x8a) < voice.pair(0x34))
      throw std::runtime_error("DSP PCM position exceeds sample length");
    voice.pair(0x3a, voice.pair(0x8a) - voice.pair(0x34));
    voice.pair(0x38, checked_address(voice.pair(0x8c), u64(voice.pair(0x34)) * width));
  }
  voice[5] = 0;
  unsigned done = 0;
  while (done < raw.size()) {
    if (!voice.pair(0x3a)) {
      if (!voice[0x81]) {
        std::fill(raw.begin() + done, raw.end(), 0);
        voice[1] = 1;
        break;
      }
      const u32 start = voice.pair(0x88), end = voice.pair(0x8a);
      if (start >= end)
        throw std::runtime_error("DSP PCM loop has zero or negative length");
      voice.pair(0x34, start);
      voice.pair(0x3a, end - start);
      voice.pair(0x38, checked_address(voice.pair(0x8c), u64(start) * width));
    }
    const unsigned count =
        std::min<unsigned>(voice.pair(0x3a), static_cast<unsigned>(raw.size()) - done);
    const auto bytes = audio_bytes(voice.pair(0x38), count * width);
    for (unsigned i = 0; i < count; ++i)
      raw[done + i] = sixteen_bit ? word_signed((u16(bytes[2 * i]) << 8) | bytes[2 * i + 1])
                                  : truncate16(s32(std::bit_cast<s8>(bytes[i])) * 256);
    done += count;
    voice.pair(0x3a, voice.pair(0x3a) - count);
    voice.pair(0x38, checked_address(voice.pair(0x38), count * width));
    voice[5] = voice.pair(0x3a) == 0;
  }
}

void Mixer::afc_blocks(Voice &voice, std::span<s16> output, unsigned blocks) {
  if (output.size() < blocks * 16)
    throw std::runtime_error("DSP AFC decoder output range is too small");
  const unsigned width = voice[0x80];
  const auto bytes = audio_bytes(voice.pair(0x38), blocks * width);
  voice.pair(0x38, checked_address(voice.pair(0x38), blocks * width));
  s32 newer = voice.signed_word(0x67), older = voice.signed_word(0x66);
  for (unsigned block = 0; block < blocks; ++block) {
    const unsigned offset = block * width;
    const s16 scale_value = word_signed(static_cast<u16>(1u << (bytes[offset] >> 4)));
    const unsigned coefficients = (bytes[offset] & 15) * 2;
    for (unsigned i = 0; i < 16; ++i) {
      s32 encoded;
      if (width == 9) {
        const unsigned nibble = (bytes[offset + 1 + i / 2] >> (i % 2 ? 0 : 4)) & 15;
        encoded = (nibble < 8 ? s32(nibble) : s32(nibble) - 16) * 2048;
      } else {
        const unsigned pair = (bytes[offset + 1 + i / 4] >> (6 - 2 * (i % 4))) & 3;
        encoded = (pair < 2 ? s32(pair) : s32(pair) - 4) * 8192;
      }
      const s64 sum = s64(scale_value) * encoded + s64(newer) * afc_[coefficients] +
                      s64(older) * afc_[coefficients + 1];
      const s16 sample = saturate(truncate32(sum) >> 11);
      output[block * 16 + i] = sample;
      older = newer;
      newer = sample;
    }
  }
  voice[0x66] = static_cast<u16>(older);
  voice[0x67] = static_cast<u16>(newer);
}

void Mixer::afc(Voice &voice, std::span<s16> raw) {
  if (voice[4]) {
    voice[0x66] = voice[0x67] = voice[0x32] = 0;
    voice.pair(0x3a, voice.pair(0x8a));
    voice.pair(0x38, voice.pair(0x8c));
  }
  if (voice[1]) {
    std::fill(raw.begin(), raw.end(), 0);
    return;
  }
  if (voice[0x32] > 16)
    throw std::runtime_error("DSP AFC cached sample count exceeds sixteen");
  unsigned done = 0;
  while (done < raw.size()) {
    const unsigned cached =
        std::min<unsigned>(voice[0x32], static_cast<unsigned>(raw.size()) - done);
    for (unsigned i = 0; i < cached; ++i)
      raw[done + i] = voice.signed_word(0x68 - voice[0x32] + i);
    voice[0x32] -= static_cast<u16>(cached);
    done += cached;
    if (done == raw.size())
      break;
    const unsigned requested = static_cast<unsigned>(raw.size()) - done;
    const u32 remaining = voice.pair(0x3a);
    if (!remaining) {
      if (!voice[0x81]) {
        std::fill(raw.begin() + done, raw.end(), 0);
        voice[1] = 1;
        break;
      }
      const u32 start = voice.pair(0x88), end = voice.pair(0x8a);
      if (start >= end)
        throw std::runtime_error("DSP AFC loop has zero or negative length");
      voice.pair(0x38, checked_address(voice.pair(0x8c), u64(start / 16) * voice[0x80]));
      voice[0x66] = voice[0x83];
      voice[0x67] = voice[0x82];
      std::array<s16, 16> block{};
      afc_blocks(voice, block, 1);
      for (unsigned i = 0; i < 16; ++i)
        voice[0x58 + i] = static_cast<u16>(block[i]);
      voice[0x32] = static_cast<u16>(16 - (start & 15));
      if (end - start < voice[0x32])
        throw std::runtime_error("DSP AFC loop ends inside its first decoded block");
      voice.pair(0x3a, end - start - voice[0x32]);
      continue;
    }
    const unsigned wanted = std::min<u32>(requested, remaining);
    const unsigned blocks = (wanted + 15) / 16;
    std::vector<s16> decoded(blocks * 16);
    afc_blocks(voice, decoded, blocks);
    std::copy_n(decoded.begin(), wanted, raw.begin() + done);
    done += wanted;
    if (requested > remaining && !voice[0x81]) {
      voice[1] = 1;
      std::fill(raw.begin() + done, raw.end(), 0);
      break;
    }
    const unsigned consumed = std::min<u32>(blocks * 16, remaining);
    voice.pair(0x3a, remaining - consumed);
    voice[0x32] = static_cast<u16>(consumed - wanted);
    if (voice[0x32]) {
      for (unsigned i = 0; i < 16; ++i)
        voice[0x58 + i] = static_cast<u16>(decoded[decoded.size() - 16 + i]);
      if (!voice.pair(0x3a) && voice.pair(0x8a)) {
        const unsigned tail = (voice.pair(0x8a) + 15) & 15;
        if (voice[0x32] > tail + 1)
          throw std::runtime_error("DSP AFC terminal cached sample alignment is invalid");
        const auto saved = voice.words;
        for (unsigned i = 0; i < voice[0x32]; ++i)
          voice[0x67 - i] = saved[0x58 + tail - i];
      }
    }
  }
}

void Mixer::main_pcm(Voice &voice, std::span<s16> raw) {
  const u32 base = voice.pair(0x8c);
  const u32 address = checked_address(base, u32(voice[0x34]) * 2);
  const u32 remaining = voice.pair(0x3a);
  const unsigned count = static_cast<unsigned>(raw.size());
  if (count > remaining)
    throw std::runtime_error("DSP main-memory PCM terminal padding behavior is not implemented");
  auto download = [&](u32 source, unsigned amount, unsigned at) {
    const auto bytes = memory_.range(Memory::boot_physical(source), amount * 2);
    for (unsigned i = 0; i < amount; ++i)
      raw[at + i] = word_signed((u16(bytes[2 * i]) << 8) | bytes[2 * i + 1]);
  };
  voice.pair(0x3a, remaining - count);
  voice[0x36] = voice[0x8a] - voice[0x34];
  const unsigned before = voice[0x36];
  if (count <= before) {
    download(address, count, 0);
    voice[0x34] += static_cast<u16>(count);
  } else {
    download(address, before, 0);
    voice.pair(0x8c, voice.pair(0x88));
    voice[0x34] = static_cast<u16>(count - before);
    download(voice.pair(0x88), count - before, before);
  }
}

void Mixer::resample(Voice &voice, std::span<const s16> raw, Samples &samples) {
  u32 position = voice[0x30];
  const u32 ratio = voice[2];
  for (auto &sample : samples) {
    if (ratio >= 0x4000) {
      position += ratio;
      sample = raw[position >> 12];
    } else {
      const unsigned coefficient = ((position & 0xfff) >> 6) * 4;
      s64 value = 0;
      for (unsigned tap = 0; tap < 4; ++tap)
        value += s64(resampling_[coefficient + tap]) * raw[(position >> 12) + tap] * 2;
      sample = saturate(value >> 16);
      position += ratio;
    }
  }
  for (unsigned i = 0; i < 4; ++i)
    voice[0x3c + i] = static_cast<u16>(raw[(position >> 12) + i]);
  voice[0x33] = static_cast<u16>(samples.back());
  voice[0x30] = static_cast<u16>(position & 0xfff);
}

void Mixer::input(Voice &voice, Samples &samples) {
  if (voice[6]) {
    samples.fill(voice.signed_word(0x33));
    return;
  }
  const unsigned type = voice[0x80];
  u32 position = voice[0x30];
  if (type == 0 || type == 3) {
    const unsigned shift = type == 0 ? 1 : 2;
    position <<= shift;
    for (auto &sample : samples) {
      sample = ((position >> 16) & ((1u << shift) - 1)) ? -16384 : 16384;
      position += u32(voice[2]) << (shift - 1);
    }
    voice[0x30] = static_cast<u16>(position >> shift);
    return;
  }
  if (type == 1) {
    for (auto &sample : samples) {
      sample = truncate16(position);
      position += voice[2] >> 1;
    }
    voice[0x30] = static_cast<u16>(position);
    return;
  }
  if (type == 7 || type == 10 || type == 4 || type == 11 || type == 12) {
    const unsigned pattern = type == 4 ? 1 : type == 11 ? 2 : type == 12 ? 3 : 0;
    position <<= 6;
    for (unsigned i = 0; i < frame_samples; ++i) {
      if ((position >> 16) >= 64)
        throw std::runtime_error("DSP waveform pattern position is outside its table");
      samples[i] = patterns_[pattern * 64 + (position >> 16)];
      position = (position + (u32(voice[2]) << 5)) % (64u << 16);
      if (type == 10)
        position = ((position << 10) + u32(s32(channels_[3][i]) * voice[2])) >> 10;
    }
    voice[0x30] = static_cast<u16>(position >> 6);
    return;
  }
  std::array<s16, 4 + 0x510> raw{};
  for (unsigned i = 0; i < 4; ++i)
    raw[i] = voice.signed_word(0x3c + i);
  const unsigned count = (u32(voice[0x30]) + frame_samples * voice[2]) >> 12;
  auto destination = std::span<s16>(raw).subspan(4, count);
  switch (type) {
  case 8:
    pcm(voice, destination, false);
    break;
  case 16:
    pcm(voice, destination, true);
    break;
  case 5:
  case 9:
    afc(voice, destination);
    break;
  case 33:
    main_pcm(voice, destination);
    break;
  default:
    throw std::runtime_error("DSP voice uses an unsupported sample source");
  }
  resample(voice, raw, samples);
}

void Mixer::filter(Voice &voice, Samples &samples) {
  if (voice[0x84] & 31)
    throw std::runtime_error("DSP variable FIR voice filtering is not implemented");
  if (voice[0xa8]) {
    s32 previous_output = voice[4] ? 0 : voice.signed_word(0x68);
    s32 previous_input = voice[4] ? 0 : voice.signed_word(0x69);
    for (auto &sample : samples) {
      const s32 input = sample;
      sample = saturate((s64(input - previous_input) * voice[0xa8] >> 7) + previous_output);
      previous_output = sample;
      previous_input = input;
    }
    voice[0x68] = static_cast<u16>(previous_output);
    voice[0x69] = static_cast<u16>(previous_input);
  }
  if ((voice[0x84] & 32) && (voice[0xa4] != 0x7fff || voice[0xa5] || voice[0xa6] || voice[0xa7])) {
    s32 input_1 = voice.signed_word(0x54), input_2 = voice.signed_word(0x55);
    s32 output_1 = voice.signed_word(0x56), output_2 = voice.signed_word(0x57);
    for (auto &sample : samples) {
      const s32 input = sample;
      const s64 value =
          s64(voice.signed_word(0xa4)) * input_1 + s64(voice.signed_word(0xa5)) * input_2 +
          s64(voice.signed_word(0xa6)) * output_1 + s64(voice.signed_word(0xa7)) * output_2;
      sample = saturate(value >> 15);
      input_2 = input_1;
      input_1 = input;
      output_2 = output_1;
      output_1 = sample;
    }
    voice[0x54] = static_cast<u16>(input_1);
    voice[0x55] = static_cast<u16>(input_2);
    voice[0x56] = static_cast<u16>(output_1);
    voice[0x57] = static_cast<u16>(output_2);
  }
}

void Mixer::voice(u16 index) {
  Voice voice;
  const unsigned words = profile_.small_voice ? 0x80 : 0xc0;
  const u32 address = checked_address(voices_, u64(index) * words * 2);
  const auto bytes = memory_.range(Memory::boot_physical(address), words * 2);
  for (unsigned i = 0; i < words; ++i)
    voice[i] = (u16(bytes[2 * i]) << 8) | bytes[2 * i + 1];
  if (profile_.small_voice) {
    const auto saved = voice.words;
    std::fill(voice.words.begin() + 0x18, voice.words.begin() + 0x80, 0);
    std::copy_n(saved.begin() + 0x40, 0x40, voice.words.begin() + 0x80);
    std::copy_n(saved.begin() + 0x30, 16, voice.words.begin() + 0x58);
    std::copy_n(saved.begin() + 0x18, 0x18, voice.words.begin() + 0x30);
  }
  if (!voice[0] || voice[1])
    return;
  // The behavioral reference retains the requested history count without transferring
  // samples for this profile. Decode, mix and write back the ordinary voice state.
  Samples samples{};
  input(voice, samples);
  filter(voice, samples);
  if (voice[0x2c]) {
    if (voice[0x85]) {
      voice[0x2b] = static_cast<u16>(voice.signed_word(0x2a) / 2);
      if (!voice[0x2b])
        voice[1] = 1;
    }
    const unsigned x = (voice[0x28] >> 8) & 127, y = voice[0x28] & 127;
    const unsigned shift = profile_.louder_position ? 15 : 16;
    const s16 delta = truncate16(s32(voice.signed_word(0x2b)) - voice.signed_word(0x2a));
    const std::array<s16, 4> quadrants{truncate16(s32(sine_[x ^ 127]) * sine_[y ^ 127] >> shift),
                                       truncate16(s32(sine_[x ^ 127]) * sine_[y] >> shift),
                                       truncate16(s32(sine_[x]) * sine_[y ^ 127] >> shift),
                                       truncate16(s32(sine_[x]) * sine_[y] >> shift)};
    constexpr std::array<unsigned, 4> targets{0, 2, 1, 3};
    for (unsigned i = 0; i < 4; ++i) {
      const s16 initial = truncate16(s32(quadrants[i]) * voice.signed_word(0x2a) >> shift);
      const s16 change = truncate16(s32(static_cast<u16>(quadrants[i])) * delta >> shift);
      ramp(channels_[targets[i]], samples, initial, change);
      ramp(channels_[targets[i] + 4], samples,
           truncate16(s32(initial) * voice.signed_word(0x29) >> shift),
           truncate16(s32(change) * voice.signed_word(0x29) >> shift));
    }
    voice[0x2a] = voice[0x2b];
  } else {
    const unsigned count = profile_.four_channels ? 4 : 6;
    if (voice[0x85]) {
      bool muted = true;
      for (unsigned channel = 0; channel < count; ++channel) {
        voice[9 + channel * 4] = static_cast<u16>(voice.signed_word(10 + channel * 4) / 2);
        muted &= voice[9 + channel * 4] == 0;
      }
      if (muted)
        voice[1] = 1;
    }
    for (unsigned channel = 0; channel < count; ++channel) {
      const unsigned offset = 8 + channel * 4;
      if (!voice[offset])
        continue;
      const s16 current = voice.signed_word(offset + 2), target = voice.signed_word(offset + 1);
      const s16 delta = profile_.explicit_ramp ? target : truncate16(s32(target) - current);
      if (!current && !delta)
        continue;
      voice[offset + 2] =
          static_cast<u16>(ramp(destination(voice[offset]), samples, current, delta) >> 16);
    }
  }
  if (!voice[6])
    voice[4] = 0;
  if (profile_.small_voice) {
    const auto saved = voice.words;
    std::copy_n(saved.begin() + 0x30, 0x18, voice.words.begin() + 0x18);
    std::copy_n(saved.begin() + 0x58, 16, voice.words.begin() + 0x30);
  }
  write_words(address, std::span<const u16>(voice.words).first(words - 0x40));
}

void Mixer::finish() {
  scale(channels_[0], volume_, 12);
  scale(channels_[1], volume_, 12);
  write_words(left_, std::span<const s16>(channels_[0]));
  write_words(right_, std::span<const s16>(channels_[1]));
  left_ = checked_address(left_, 160);
  right_ = checked_address(right_, 160);
  reverb(true);
  prepared_ = false;
}

} // namespace

struct AudioProgram::Impl {
  enum class State { Waiting, Receiving, Syncing, Halted };
  Memory &memory;
  const u32 hash;
  const Profile profile;
  std::function<void(u32, bool)> send;
  std::function<void()> interrupt;
  Mixer mixer;
  State state = State::Waiting;
  unsigned expected = 0;
  std::deque<u32> commands;
  unsigned pending = 0;
  bool execute = true;
  unsigned voice_count = 0, frames = 0, frame = 0, voice = 0, voice_limit = 0;
  std::array<u16, 256> voice_masks{};
  bool second_mask = false;
  bool uploading = false, resume = false;
  unsigned upload_word = 0;
  std::array<u32, 10> upload{};

  Impl(Memory &ram, Console console, u32 program, std::function<void(u32, bool)> sender,
       std::function<void(u32, std::span<u8>)> reader, std::function<void()> notify)
      : memory(ram), hash(program), profile(profile_for(program).value()), send(std::move(sender)),
        interrupt(std::move(notify)), mixer(ram, console, profile, std::move(reader)) {
  }

  bool rendering() const {
    return frame != frames;
  }

  void acknowledge(u16 value) {
    if (profile.light) {
      send(0x80000000 | (2 * ((value >> 8) & 0x7f) + 0x62), false);
    } else {
      send(0xdcd10004, true);
      send(0xf3550000 | value, false);
    }
  }

  u32 pop() {
    if (commands.empty())
      throw std::runtime_error("DSP audio command has missing parameters");
    const u32 value = commands.front();
    commands.pop_front();
    return value;
  }

  void append(u32 value) {
    if (commands.size() >= 63)
      throw std::runtime_error("DSP audio command buffer overflow");
    commands.push_back(value);
  }

  void render() {
    while (rendering()) {
      if (!voice)
        mixer.prepare();
      while (voice < voice_count) {
        if (voice >= voice_limit)
          return;
        if (voice_masks[voice >> 4] & (1u << (15 - (voice & 15))))
          mixer.voice(static_cast<u16>(voice));
        ++voice;
      }
      if (!profile.light)
        acknowledge(static_cast<u16>(0xff00 | frame));
      mixer.finish();
      voice = voice_limit = 0;
      ++frame;
    }
    if (profile.light) {
      state = State::Waiting;
    } else {
      send(0xdcd10005, true);
      execute = false;
    }
  }

  void run() {
    if (rendering() || !execute)
      return;
    while (pending) {
      const u32 command = pop();
      if (!(command & 0x80000000))
        continue;
      --pending;
      const unsigned id = (command >> 24) & 127;
      const u16 sync = static_cast<u16>(command >> 16);
      switch (id) {
      case 0:
      case 3:
      case 10:
      case 11:
      case 15:
        acknowledge(sync);
        break;
      case 1: {
        voice_count = command & 0xffff;
        if (voice_count > 4096)
          throw std::runtime_error("DSP audio voice count exceeds synchronization capacity");
        const u32 voices = pop(), tables = pop(), afc = pop(), reverb = pop();
        mixer.configure(voices, tables, afc, reverb);
        acknowledge(sync);
        break;
      }
      case 2: {
        frames = (command >> 16) & 255;
        const u32 left = pop(), right = pop();
        if (profile.combined) {
          // The behavioral reference consumes these reserved arguments without a mixer effect.
          pop();
          pop();
        }
        mixer.output(left, right, static_cast<u16>(command), frames);
        frame = voice = 0;
        if (profile.light) {
          acknowledge(static_cast<u16>(frames));
          state = State::Syncing;
        } else if (rendering()) {
          render();
        }
        return;
      }
      case 12:
        if (profile.special_command_12)
          throw std::runtime_error("DSP audio command 12 variant is not implemented");
        acknowledge(sync);
        break;
      case 13:
        if (!profile.no_command_13)
          throw std::runtime_error("DSP audio command 13 filtering is not implemented");
        acknowledge(sync);
        break;
      case 14:
        if (!profile.main_ram_audio)
          throw std::runtime_error("DSP main-memory audio base is invalid for this program");
        mixer.main_audio_base(pop());
        acknowledge(sync);
        break;
      default:
        state = State::Halted;
        if (id < 4 || id > 9)
          throw std::runtime_error("DSP audio received an unsupported command");
        return;
      }
    }
  }

  std::optional<Switch> mail(u32 value) {
    if (uploading) {
      upload[upload_word++] = value;
      if (upload_word != upload.size())
        return std::nullopt;
      uploading = false;
      upload_word = 0;
      if ((upload[1] & 0xffff) || (upload[8] & 0xffff))
        throw std::runtime_error("DSP task data-memory upload/download is not implemented");
      BootDescriptor descriptor{upload[3], static_cast<u16>(upload[4]), static_cast<u16>(upload[5]),
                                static_cast<u16>(upload[8]), static_cast<u16>(upload[6])};
      const auto code = memory.range(Memory::boot_physical(descriptor.source), descriptor.length);
      resume = true;
      return Switch{firmware_hash(code), descriptor, true};
    }
    if (state == State::Halted)
      return std::nullopt;
    if (state == State::Receiving) {
      append(value);
      if (!--expected) {
        ++pending;
        state = State::Waiting;
        run();
      }
      return std::nullopt;
    }
    if (state == State::Syncing) {
      if (profile.light) {
        if (value)
          throw std::runtime_error("DSP light audio synchronization mail must be zero");
        voice_limit = 4096;
        voice_masks.fill(0xffff);
        render();
        if (interrupt)
          interrupt();
        return std::nullopt;
      }
      if (profile.frame_sync) {
        const unsigned index = second_mask ? 2 : 0;
        voice_masks[index] = static_cast<u16>(value >> 16);
        voice_masks[index + 1] = static_cast<u16>(value);
        if (second_mask)
          voice_limit = 0xffff;
        render();
        if (second_mask)
          state = State::Waiting;
        second_mask = !second_mask;
      } else {
        voice_limit = (((value >> 16) & 15) + 1) * 16;
        voice_masks[(value >> 16) & 255] = static_cast<u16>(value);
        render();
        state = State::Waiting;
      }
      return std::nullopt;
    }
    if (profile.light) {
      if (!(value & 0x80000000))
        throw std::runtime_error("DSP light audio command is missing its ready bit");
      const unsigned id = (value >> 24) & 127;
      if (id == 3)
        return std::nullopt;
      if (id != 0 && id != 1 && id != 2 && id != 12)
        throw std::runtime_error("DSP light audio command is not implemented");
      append(value);
      expected = id == 1 ? 4 : id == 2 ? 2 : 0;
      if (id == 12 && profile.special_command_12)
        throw std::runtime_error("DSP light audio command 12 variant is not implemented");
      if (expected) {
        state = State::Receiving;
      } else {
        ++pending;
        run();
      }
      return std::nullopt;
    }
    if (value & 0x80000000) {
      switch (value & 0xffff) {
      case 1:
        execute = true;
        run();
        uploading = true;
        state = State::Waiting;
        break;
      case 2:
        state = State::Halted;
        return Switch{0, {}, false};
      case 3:
        execute = true;
        run();
        break;
      default:
        state = State::Halted;
        break;
      }
    } else if (!(value & 0xffff)) {
      state = rendering() ? State::Syncing : State::Halted;
    } else {
      expected = value & 0xffff;
      if (expected > 63 || expected > 63 - commands.size())
        throw std::runtime_error("DSP audio command packet exceeds its buffer");
      state = State::Receiving;
    }
    return std::nullopt;
  }
};

bool AudioProgram::supports(u32 hash) {
  return profile_for(hash).has_value();
}

AudioProgram::AudioProgram(Memory &memory, Console console, u32 hash,
                           std::function<void(u32, bool)> send,
                           std::function<void(u32, std::span<u8>)> reader,
                           std::function<void()> notify)
    : impl_(std::make_unique<Impl>(memory, console, hash, std::move(send), std::move(reader),
                                   std::move(notify))) {
}

AudioProgram::~AudioProgram() = default;

void AudioProgram::initialize() {
  if (impl_->profile.light) {
    impl_->send(0x88881111, false);
  } else {
    impl_->send(0xdcd10000, true);
    impl_->send(0xf3551111, false);
  }
}

std::optional<AudioProgram::Switch> AudioProgram::handle_mail(u32 value) {
  return impl_->mail(value);
}

void AudioProgram::update() {
  if (impl_->resume) {
    impl_->resume = false;
    impl_->send(0xdcd10001, true);
  }
}

u32 AudioProgram::hash() const {
  return impl_->hash;
}

} // namespace cupid::dsp
