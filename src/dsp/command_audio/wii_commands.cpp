// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace cupid::dsp {
using namespace command_audio;

namespace {
std::array<u16, 96> volume_ramp(u16 before, u16 after) {
  std::array<u16, 96> result{};
  float current = before;
  const float increment = float(int(after) - int(before)) / float(result.size());
  for (auto &value : result) {
    current += increment;
    value = u16(current);
  }
  return result;
}
} // namespace

void CommandAudioProgram::Impl::initialize_wii_buffers(u32 address) {
  backing(address, 120);
  for (unsigned channel = 0; channel < wii_channels.size(); ++channel) {
    const u32 initial = u32(read32(address + channel * 6));
    const s32 delta = s16(read16(address + channel * 6 + 4));
    const unsigned count = channel < 12 ? 96 : 18;
    for (unsigned i = 0; i < count; ++i)
      wii_channels[channel][i] = initial ? wrap32(initial + u32(delta) * i) : 0;
  }
}

void CommandAudioProgram::Impl::wii_add_stereo(u32 address, unsigned mode) {
  backing(address, (mode == 3 ? 192 : 96) * 4);
  for (unsigned i = 0; i < 96; ++i) {
    const u32 first = u32(read32(address + i * 4));
    const u32 second = mode == 3 ? u32(read32(address + (96 + i) * 4)) : first;
    wii_channels[0][i] = add32(wii_channels[0][i], wrap32(mode == 2 ? 0u - first : first));
    wii_channels[1][i] = add32(wii_channels[1][i], wrap32(mode != 1 ? 0u - second : second));
  }
}

void CommandAudioProgram::Impl::wii_mix_aux(unsigned group, u32 upload, u32 download, u16 volume) {
  const auto ramp = volume_ramp(last_aux_volumes[group], volume);
  last_aux_volumes[group] = volume;
  if (upload) {
    backing(upload, 3 * 96 * 4);
    for (unsigned channel = 0; channel < 3; ++channel)
      for (unsigned i = 0; i < 96; ++i)
        write32(upload + (channel * 96 + i) * 4, wii_channels[3 + group * 3 + channel][i]);
  }
  backing(download, 3 * 96 * 4);
  for (unsigned channel = 0; channel < 3; ++channel)
    for (unsigned i = 0; i < 96; ++i) {
      const s64 value = s64(read32(download + (channel * 96 + i) * 4)) * ramp[i];
      wii_channels[channel][i] = add32(wii_channels[channel][i], wrap32(u32(value >> 15)));
    }
}

void CommandAudioProgram::Impl::wii_upload_mix(unsigned group, const std::array<u32, 6> &addresses,
                                               u16 volume) {
  backing(addresses[0], 3 * 96 * 4);
  for (unsigned channel = 0; channel < 3; ++channel)
    for (unsigned i = 0; i < 96; ++i)
      write32(addresses[0] + (channel * 96 + i) * 4, wii_channels[3 + group * 3 + channel][i]);
  backing(addresses[1], 96 * 4);
  for (unsigned i = 0; i < 96; ++i)
    write32(addresses[1] + i * 4, wii_channels[10 + group][i]);
  const auto ramp = volume_ramp(last_aux_volumes[group], volume);
  last_aux_volumes[group] = volume;
  for (unsigned channel = 0; channel < 4; ++channel) {
    backing(addresses[2 + channel], 96 * 4);
    auto &temporary = wii_channels[3 + group * 3];
    // The uploaded bus's left channel is also the download scratch buffer.
    for (unsigned i = 0; i < 96; ++i)
      temporary[i] = read32(addresses[2 + channel] + i * 4);
    auto &destination = wii_channels[channel == 3 ? 9 : channel];
    for (unsigned i = 0; i < 96; ++i)
      destination[i] = add32(destination[i], wrap32(u32((s64(temporary[i]) * ramp[i]) >> 15)));
  }
}

void CommandAudioProgram::Impl::wii_output(u32 stereo, u32 surround, u16 volume, bool extra) {
  const auto ramp = volume_ramp(last_main_volume, volume);
  last_main_volume = volume;
  backing(surround, (extra ? 192 : 96) * 4);
  for (unsigned i = 0; i < 96; ++i) {
    write32(surround + i * 4, wii_channels[2][i]);
    if (extra)
      write32(surround + (96 + i) * 4, wii_channels[9][i]);
  }
  backing(stereo, 96 * 4);
  for (unsigned i = 0; i < 96; ++i) {
    for (unsigned channel = 0; channel < 2; ++channel)
      wii_channels[channel][i] =
          s32(std::clamp((s64(wii_channels[channel][i]) * ramp[i]) >> 15, s64(-32768), s64(32767)));
    write16(stereo + i * 4, u16(wii_channels[1][i]));
    write16(stereo + i * 4 + 2, u16(wii_channels[0][i]));
  }
  send(0xdcd10004, true, 0);
}

void CommandAudioProgram::Impl::wii_output_remotes(const std::array<u32, 4> &addresses) {
  for (unsigned remote = 0; remote < 4; ++remote) {
    backing(addresses[remote], 18 * 2);
    for (unsigned i = 0; i < 18; ++i)
      write16(addresses[remote] + i * 2,
              u16(std::clamp(wii_channels[12 + remote * 2][i], s32(-32768), s32(32767))));
  }
}

void CommandAudioProgram::Impl::execute_wii_list() {
  unsigned cursor = 0;
  u32 voice_address = 0;
  const auto word = [&]() -> u16 {
    if (cursor == commands.size())
      throw std::runtime_error("Wii DSP command list has no bounded terminator");
    return commands[cursor++];
  };
  const auto address = [&] {
    const u32 high = word();
    return (high << 16) | word();
  };
  while (cursor < commands.size()) {
    unsigned command = word();
    if (old_wii && command == 4) {
      voice_address = address();
      continue;
    }
    if (old_wii && command > 4)
      --command;
    switch (command) {
    case 0:
      initialize_wii_buffers(address());
      break;
    case 1:
    case 2:
    case 3:
      wii_add_stereo(address(), command);
      break;
    case 4:
      process_wii_voices(old_wii ? voice_address : address());
      break;
    case 5:
    case 6:
    case 7: {
      const u16 volume = word();
      const u32 upload = address(), download = address();
      wii_mix_aux(command - 5, upload, download, volume);
      break;
    }
    case 8:
    case 9: {
      const u16 volume = word();
      std::array<u32, 6> addresses{};
      for (auto &value : addresses)
        value = address();
      wii_upload_mix(command - 8, addresses, volume);
      break;
    }
    case 10: {
      const u16 threshold = word(), release = word();
      compress_buffers(wii_channels[0], wii_channels[1], threshold, release, address());
      break;
    }
    case 11:
    case 12: {
      const u16 volume = old_wii || program_hash == 0xd9c4bf34 ? u16(0x8000) : word();
      const u32 surround = address(), stereo = address();
      wii_output(stereo, surround, volume, command == 12);
      break;
    }
    case 13: {
      std::array<u32, 4> addresses{};
      for (auto &value : addresses)
        value = address();
      wii_output_remotes(addresses);
      break;
    }
    case 14:
      return;
    default:
      // This family skips an unknown opcode and continues with the next word.
      break;
    }
  }
  throw std::runtime_error("Wii DSP command list has no bounded terminator");
}
} // namespace cupid::dsp
