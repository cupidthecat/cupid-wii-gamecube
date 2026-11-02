// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/audio/remote_speaker.hpp"
#include <algorithm>
#include <cmath>

namespace cupid::audio {
namespace {
int adaptation(unsigned magnitude) {
  switch (magnitude) {
  case 4:
    return 307;
  case 5:
    return 409;
  case 6:
    return 512;
  case 7:
    return 614;
  default:
    return 230;
  }
}
} // namespace

s16 RemoteSpeakerDecoder::nibble(u8 value) {
  const int magnitude = (value & 7) * 2 + 1;
  const int direction = value & 8 ? -1 : 1;
  state_.predictor =
      std::clamp(state_.predictor + state_.step * direction * magnitude / 8, -32768, 32767);
  state_.step = std::clamp(state_.step * adaptation(value & 7) / 256, 127, 24576);
  return static_cast<s16>(state_.predictor);
}

RemoteSpeakerPacket RemoteSpeakerDecoder::decode(const RemoteSpeakerConfig &config,
                                                 std::span<const u8> bytes) {
  if (bytes.size() > 20)
    throw RemoteSpeakerError("Wii Remote speaker packet exceeds twenty bytes");
  RemoteSpeakerPacket packet;
  if (!config.enabled || !config.divisor || bytes.empty())
    return packet;
  if (config.format != 0 && config.format != 0x40)
    throw RemoteSpeakerError("Unsupported Wii Remote speaker sample format");
  if (std::isnan(config.pan))
    throw RemoteSpeakerError("Invalid Wii Remote speaker pan");

  const bool pcm = config.format == 0x40;
  const unsigned volume_divisor = std::max<unsigned>(pcm ? 255 : 127, config.volume);
  const double volume = float(config.volume) * 256.f / float(volume_divisor);
  const float pan = std::clamp(config.pan, -1.f, 1.f);
  packet.left_volume = u8(std::min(u32(std::min(1.f - pan, 1.f) * volume), 255u));
  packet.right_volume = u8(std::min(u32(std::min(1.f + pan, 1.f) * volume), 255u));
  const u32 sample_rate = (pcm ? 12000000u : 6000000u) / config.divisor;
  packet.rate_divisor = 108000000u / (sample_rate * 2);
  for (u8 byte : bytes) {
    if (pcm) {
      const int sample = byte < 128 ? byte : int(byte) - 256;
      packet.mono[packet.count++] = static_cast<s16>(sample * 256);
    } else {
      packet.mono[packet.count++] = nibble(byte >> 4);
      packet.mono[packet.count++] = nibble(byte & 15);
    }
  }
  return packet;
}

void RemoteSpeakerDecoder::reset() {
  state_ = {};
}

RemoteSpeakerState RemoteSpeakerDecoder::state() const {
  return state_;
}
} // namespace cupid::audio
