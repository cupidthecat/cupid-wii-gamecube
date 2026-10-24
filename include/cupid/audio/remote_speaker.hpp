// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/types.hpp"
#include <array>
#include <span>
#include <stdexcept>

namespace cupid::audio {
struct RemoteSpeakerConfig {
  u8 format = 0;
  u16 divisor = 0;
  u8 volume = 0;
  bool enabled = true;
  float pan = 0;
};

struct RemoteSpeakerPacket {
  std::array<s16, 40> mono{};
  u8 count = 0;
  u32 rate_divisor = 0;
  u8 left_volume = 0, right_volume = 0;
};

struct RemoteSpeakerState {
  s32 predictor = 0, step = 127;
  bool operator==(const RemoteSpeakerState &) const = default;
};

class RemoteSpeakerError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class RemoteSpeakerDecoder {
public:
  RemoteSpeakerPacket decode(const RemoteSpeakerConfig &config, std::span<const u8> bytes);
  void reset();
  RemoteSpeakerState state() const;

private:
  RemoteSpeakerState state_;
  s16 nibble(u8 value);
};
} // namespace cupid::audio
