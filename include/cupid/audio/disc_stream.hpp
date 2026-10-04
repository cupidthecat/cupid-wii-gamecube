// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/types.hpp"
#include <array>
#include <functional>
#include <span>
#include <vector>

namespace cupid::audio {
class DiscAdpcm {
public:
  static constexpr unsigned block_bytes = 32, block_frames = 28;
  void reset();
  void decode(std::span<const u8, block_bytes> encoded, std::span<s16, block_frames * 2> stereo);

private:
  std::array<std::array<s32, 2>, 2> history_{};
};

// Disc audio has its own clock and sample queue, independent of DSP audio DMA.
class DiscStream {
public:
  explicit DiscStream(u64 cpu_clock, u32 rate_divisor);
  void reset();
  void configure(bool enabled, u8 buffer_size);
  u32 command(u8 operation, u64 start, u32 length);
  u32 status(u8 operation, u32 &result) const;
  void advance(u64 cycles, u32 rate_divisor, bool playing);
  u64 next_event() const;
  std::function<void(u64, std::span<u8>)> read;
  std::function<void(std::span<const s16>, u32)> samples;
  std::function<void()> read_error;

private:
  void callback(u32 rate_divisor, bool playing);
  DiscAdpcm decoder_;
  u64 clock_, remaining_;
  bool enabled_ = false, streaming_ = false, stop_at_end_ = false, read_ok_ = true;
  u8 buffer_size_ = 0;
  u64 position_ = 0, current_start_ = 0, next_start_ = 0;
  u32 current_length_ = 0, next_length_ = 0, pending_blocks_ = 6;
  std::vector<u8> pending_data_;
};
} // namespace cupid::audio
