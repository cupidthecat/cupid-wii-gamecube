// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/memory.hpp"
#include "cupid/hw/devices.hpp"
#include "cupid/hw/remote.hpp"
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace cupid::platform {
struct Frame {
  u32 width = 0, height = 0;
  std::vector<u8> rgba;
};

Frame decode_xfb(const Memory &memory, const hw::Xfb &xfb);
u64 frame_hash(std::span<const u8> data);

enum class DisplayAspect { Standard, Widescreen, Pixels };

struct OutputOptions {
  bool headless = true;
  bool audio = false;
  std::filesystem::path frame_dump;
  std::filesystem::path input_config;
  DisplayAspect display_aspect = DisplayAspect::Standard;
  bool remote_speakers = false;
};

struct HostAudioStats {
  u64 mixed_frames = 0, nonzero_samples = 0;
  u64 left_nonzero = 0, right_nonzero = 0, dropped_granules = 0;
};

struct DiscAudioStats {
  u64 samples = 0, nonzero_samples = 0, hash = 14695981039346656037ULL;
  u32 peak = 0;
};

class Output {
public:
  explicit Output(const OutputOptions &options);
  ~Output();
  Output(const Output &) = delete;
  Output &operator=(const Output &) = delete;
  bool poll(hw::Devices &devices, bool sample_input = true);
  const hw::RemoteState &remote_state() const;
  bool rumble(unsigned port, bool enabled);
  void present(const Memory &memory, const hw::Xfb &xfb);
  void audio(std::span<const s16> samples, u32 sample_rate);
  void disc_audio(std::span<const s16> samples, u32 rate_divisor, u8 left, u8 right);
  void disc_audio_control(u32 rate_divisor, u8 left, u8 right);
  DiscAudioStats disc_audio_stats() const;
  void remote_speaker(unsigned port, const audio::RemoteSpeakerPacket &packet);
  bool remote_speaker_processing() const;
  HostAudioStats host_audio_stats() const;
  u64 presented_frames() const;
  u64 last_frame_hash() const;
  u64 audio_samples() const;
  u64 audio_nonzero_samples() const;
  u32 audio_peak() const;
  u64 audio_hash() const;
  void save_frame() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cupid::platform
