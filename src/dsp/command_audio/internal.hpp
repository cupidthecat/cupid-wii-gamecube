// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/dsp/accelerator.hpp"
#include "cupid/dsp/command_audio.hpp"
#include <array>
#include <bit>
#include <span>
#include <vector>

namespace cupid::dsp {
namespace command_audio {
constexpr unsigned frame_samples = 160, voice_words = 122;
using Voice = std::array<u16, voice_words>;
using Channels = std::array<std::array<s32, frame_samples>, 9>;

inline s32 wrap32(u32 value) {
  return std::bit_cast<s32>(value);
}

inline s32 add32(s32 left, s32 right) {
  return wrap32(u32(left) + u32(right));
}

inline u32 pair(const Voice &voice, unsigned offset) {
  return (u32(voice[offset]) << 16) | voice[offset + 1];
}

inline void pair(Voice &voice, unsigned offset, u32 value) {
  voice[offset] = u16(value >> 16);
  voice[offset + 1] = u16(value);
}
} // namespace command_audio

struct CommandAudioProgram::Impl {
  Memory &memory;
  Console console;
  u32 program_hash;
  std::function<void(u32, bool, u32)> send;
  std::function<void(u32, std::span<u8>)> read_aram;
  std::array<s16, 2048> coefficients{};
  bool have_coefficients = false;
  command_audio::Channels channels{};
  std::array<u16, 512> commands{};
  enum class Phase { Size, Address, Task } phase = Phase::Size;
  u16 command_size = 0, compressor_position = 0;
  unsigned upload_index = 0;
  bool upload = false, resume = false;
  std::array<u32, 10> upload_words{};
  SampleAccelerator accelerator;
  command_audio::Voice *active_voice = nullptr;

  Impl(Memory &ram, Console mode, u32 hash, std::function<void(u32, bool, u32)> mail,
       std::function<void(u32, std::span<u8>)> aram, std::span<const s16> table);
  std::span<u8> backing(u32 address, std::size_t length);
  u16 read16(u32 address);
  s32 read32(u32 address);
  void write16(u32 address, u16 value);
  void write32(u32 address, s32 value);
  void copy_list(u32 address, u16 words);
  void execute_list();
  void initialize_buffers(u32 address);
  void download_volume(u32 address, std::array<u16, 3> volumes);
  void upload_channels(u32 address, unsigned first, unsigned count);
  void download_channels(u32 address, unsigned first, unsigned count, bool add);
  void set_stereo(u32 address, bool opposite);
  void mix_aux(unsigned group, u32 upload_address, u32 download_address);
  void mix_aux_stereo(u32 upload_address, u32 download_address);
  void compress(u16 threshold, u16 release, u32 address);
  void output(u32 stereo_address, u32 surround_address);
  u32 mix_control(u16 value) const;
  command_audio::Voice read_voice(u32 address);
  void write_voice(u32 address, const command_audio::Voice &voice);
  void process_voices(u32 address);
  void process_voice(command_audio::Voice &voice, unsigned offset);
  void decode_samples(command_audio::Voice &voice, std::span<s16, 32> samples);
};
} // namespace cupid::dsp
