// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace cupid::dsp {
using namespace command_audio;

std::span<u8> CommandAudioProgram::Impl::backing(u32 address, std::size_t length) {
  return memory.range(address & 0x3fffffff, length);
}

u16 CommandAudioProgram::Impl::read16(u32 address) {
  const auto bytes = backing(address, 2);
  return u16((u16(bytes[0]) << 8) | bytes[1]);
}

s32 CommandAudioProgram::Impl::read32(u32 address) {
  const auto bytes = backing(address, 4);
  return wrap32((u32(bytes[0]) << 24) | (u32(bytes[1]) << 16) | (u32(bytes[2]) << 8) | bytes[3]);
}

void CommandAudioProgram::Impl::write16(u32 address, u16 value) {
  auto bytes = backing(address, 2);
  bytes[0] = u8(value >> 8);
  bytes[1] = u8(value);
}

void CommandAudioProgram::Impl::write32(u32 address, s32 value) {
  auto bytes = backing(address, 4);
  const u32 word = u32(value);
  for (unsigned i = 0; i < 4; ++i)
    bytes[i] = u8(word >> (24 - 8 * i));
}

void CommandAudioProgram::Impl::copy_list(u32 address, u16 words) {
  if (words >= commands.size())
    return;
  backing(address, std::size_t(words) * 2);
  // Short uploads retain the unused tail, as the task's command buffer does.
  for (unsigned i = 0; i < words; ++i)
    commands[i] = read16(address + i * 2);
}

void CommandAudioProgram::Impl::initialize_buffers(u32 address) {
  std::array<u16, 27> init{};
  backing(address, init.size() * 2);
  for (unsigned i = 0; i < init.size(); ++i)
    init[i] = read16(address + i * 2);
  for (unsigned channel = 0; channel < channels.size(); ++channel) {
    const u32 initial = (u32(init[channel * 3]) << 16) | init[channel * 3 + 1];
    const s32 delta = s16(init[channel * 3 + 2]);
    for (unsigned i = 0; i < frame_samples; ++i)
      channels[channel][i] = initial ? wrap32(initial + u32(delta) * i) : 0;
  }
}

void CommandAudioProgram::Impl::download_volume(u32 address, std::array<u16, 3> volumes) {
  backing(address, frame_samples * 3 * 4);
  // Each bus group reads the same three-channel input, with its own volume.
  for (unsigned group = 0; group < 3; ++group)
    for (unsigned channel = 0; channel < 3; ++channel)
      for (unsigned i = 0; i < frame_samples; ++i) {
        const s64 value = s64(read32(address + (channel * frame_samples + i) * 4)) * volumes[group];
        auto &destination = channels[group * 3 + channel][i];
        destination = add32(destination, wrap32(u32(value >> 15)));
      }
}

void CommandAudioProgram::Impl::upload_channels(u32 address, unsigned first, unsigned count) {
  for (unsigned channel = first; channel < first + count; ++channel) {
    backing(address, frame_samples * 4);
    for (unsigned i = 0; i < frame_samples; ++i)
      write32(address + i * 4, channels[channel][i]);
    address += frame_samples * 4;
  }
}

void CommandAudioProgram::Impl::download_channels(u32 address, unsigned first, unsigned count,
                                                  bool add) {
  backing(address, frame_samples * count * 4);
  for (unsigned channel = 0; channel < count; ++channel)
    for (unsigned i = 0; i < frame_samples; ++i) {
      const s32 value = read32(address + (channel * frame_samples + i) * 4);
      auto &destination = channels[first + channel][i];
      destination = add ? add32(destination, value) : value;
    }
}

void CommandAudioProgram::Impl::set_stereo(u32 address, bool opposite) {
  backing(address, frame_samples * 4);
  for (unsigned i = 0; i < frame_samples; ++i) {
    const s32 value = read32(address + i * 4);
    channels[0][i] = opposite ? wrap32(0u - u32(value)) : value;
    channels[1][i] = value;
    channels[2][i] = 0;
  }
}

void CommandAudioProgram::Impl::mix_aux(unsigned group, u32 upload_address, u32 download_address) {
  if (upload_address)
    upload_channels(upload_address, 3 + group * 3, 3);
  download_channels(download_address, 0, 3, true);
}

void CommandAudioProgram::Impl::mix_aux_stereo(u32 upload_address, u32 download_address) {
  upload_channels(upload_address, 6, 2);
  backing(download_address, frame_samples * 2 * 4);
  for (unsigned channel = 0; channel < 2; ++channel)
    for (unsigned i = 0; i < frame_samples; ++i) {
      const s32 value = read32(download_address + (channel * frame_samples + i) * 4);
      channels[6 + channel][i] = value;
      channels[channel][i] = add32(channels[channel][i], value);
    }
}

void CommandAudioProgram::Impl::compress(u16 threshold, u16 release, u32 address) {
  compress_buffers(channels[0], channels[1], threshold, release, address);
}

void CommandAudioProgram::Impl::compress_buffers(std::span<s32> left, std::span<s32> right,
                                                 u16 threshold, u16 release, u32 address) {
  const unsigned count = unsigned(left.size());
  const std::array<std::span<s32>, 2> buffers{left, right};
  bool triggered = false;
  for (unsigned i = 0; i < count && !triggered; ++i)
    for (unsigned channel = 0; channel < 2 && !triggered; ++channel) {
      const s32 value = buffers[channel][i];
      if (value == std::numeric_limits<s32>::min())
        throw std::runtime_error("DSP compressor magnitude has no defined source result");
      triggered |= value > threshold || value < -s32(threshold);
    }
  unsigned entry;
  if (triggered) {
    entry = compressor_position;
    compressor_position = release;
  } else if (compressor_position) {
    entry = 11 + --compressor_position;
  } else {
    return;
  }
  address += entry * count * 2;
  backing(address, count * 2);
  for (unsigned i = 0; i < count; ++i) {
    const u16 coefficient = read16(address + i * 2);
    for (unsigned channel = 0; channel < 2; ++channel)
      buffers[channel][i] = wrap32(u32((s64(buffers[channel][i]) * coefficient) >> 15));
  }
}

void CommandAudioProgram::Impl::output(u32 stereo_address, u32 surround_address) {
  upload_channels(surround_address, 2, 1);
  backing(stereo_address, frame_samples * 4);
  for (unsigned i = 0; i < frame_samples; ++i) {
    write16(stereo_address + i * 4, u16(std::clamp(channels[1][i], s32(-32768), s32(32767))));
    write16(stereo_address + i * 4 + 2, u16(std::clamp(channels[0][i], s32(-32768), s32(32767))));
  }
}

void CommandAudioProgram::Impl::execute_list() {
  unsigned cursor = 0;
  u32 voice_address = 0;
  const auto word = [&]() -> u16 {
    if (cursor == commands.size())
      throw std::runtime_error("DSP audio command list has no bounded terminator");
    return commands[cursor++];
  };
  const auto address = [&]() {
    const u32 high = word();
    return (high << 16) | word();
  };
  for (unsigned budget = 0; budget < 1048576; ++budget) {
    const u16 command = word();
    switch (command) {
    case 0:
      initialize_buffers(address());
      break;
    case 1: {
      const u32 source = address();
      std::array<u16, 3> volumes{};
      for (auto &volume : volumes)
        volume = word();
      download_volume(source, volumes);
      break;
    }
    case 2:
      voice_address = address();
      break;
    case 3:
      process_voices(voice_address);
      break;
    case 4:
    case 5: {
      const u32 upload_address = address(), download_address = address();
      mix_aux(command - 4, upload_address, download_address);
      break;
    }
    case 6:
      upload_channels(address(), 0, 3);
      break;
    case 7:
      set_stereo(address(), false);
      break;
    case 8:
      // This profile consumes ten words but defines no corresponding effect.
      for (unsigned i = 0; i < 10; ++i)
        (void)word();
      break;
    case 9:
      mix_aux(1, 0, address());
      break;
    case 10:
    case 11:
    case 12:
      break;
    case 13: {
      const u32 source = address();
      const u16 size = word();
      copy_list(source, size);
      cursor = 0;
      break;
    }
    case 14: {
      const u32 surround = address(), stereo = address();
      output(stereo, surround);
      break;
    }
    case 15:
      return;
    case 16: {
      const u32 upload_address = address(), download_address = address();
      mix_aux_stereo(upload_address, download_address);
      break;
    }
    case 17:
      set_stereo(address(), true);
      break;
    case 18: {
      if (program_hash == 0x4e8a8b21)
        throw std::runtime_error("DSP profile has no defined compressor command");
      const u16 threshold = word(), release = word();
      compress(threshold, release, address());
      break;
    }
    case 19: {
      std::array<u32, 6> locations{};
      for (auto &location : locations)
        location = address();
      upload_channels(locations[0], 3, 3);
      upload_channels(locations[1], 8, 1);
      for (unsigned channel = 0; channel < 4; ++channel)
        download_channels(locations[channel + 2], channel < 2 ? channel : channel + 4, 1, true);
      break;
    }
    default:
      // Unknown opcodes terminate this command list in the selected profiles.
      return;
    }
  }
  throw std::runtime_error("DSP audio command chain exceeds its bounded execution limit");
}
} // namespace cupid::dsp
