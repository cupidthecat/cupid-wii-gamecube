// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/types.hpp"
#include <memory>
#include <span>

namespace cupid::audio {
struct MixerOptions {
  u32 output_rate = 48000;
  u32 buffer_ms = 80;
  float speed = 1;
  bool preserve_pitch = false;
  bool fill_gaps = true;
  bool running = true;
};

// Calls on one queue must be serialized by the output backend.
class SampleFifo {
public:
  explicit SampleFifo(u32 rate_divisor = 3375);
  ~SampleFifo();
  SampleFifo(const SampleFifo &) = delete;
  SampleFifo &operator=(const SampleFifo &) = delete;
  void set_rate_divisor(u32 divisor);
  void set_volume(u8 left, u8 right);
  void push_stereo(std::span<const s16> samples);
  void push_mono(std::span<const s16> samples);
  void mix(std::span<s16> samples, const MixerOptions &options);
  u64 dropped_granules() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cupid::audio
