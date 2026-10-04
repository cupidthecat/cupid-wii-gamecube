// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/audio/disc_stream.hpp"
#include <algorithm>
#include <stdexcept>

namespace cupid::audio {
void DiscAdpcm::reset() {
  history_ = {};
}

void DiscAdpcm::decode(std::span<const u8, block_bytes> encoded,
                       std::span<s16, block_frames * 2> stereo) {
  constexpr std::array<std::array<s32, 2>, 4> coefficients{
      {{0, 0}, {60, 0}, {115, -52}, {98, -55}}};
  for (unsigned frame = 0; frame < block_frames; ++frame) {
    for (unsigned channel = 0; channel < 2; ++channel) {
      const unsigned header = encoded[channel];
      const unsigned predictor = header >> 4;
      auto &history = history_[channel];
      s32 predicted = 0;
      if (predictor < coefficients.size())
        predicted =
            history[0] * coefficients[predictor][0] + history[1] * coefficients[predictor][1];
      predicted = std::clamp((predicted + 32) >> 6, -2097152, 2097151);
      const unsigned nibble = (encoded[frame + 4] >> (channel * 4)) & 15;
      const s32 signed_nibble = nibble < 8 ? s32(nibble) : s32(nibble) - 16;
      const s32 value = ((signed_nibble * 4096) >> (header & 15)) * 64 + predicted;
      history[1] = history[0];
      history[0] = value;
      stereo[frame * 2 + channel] = static_cast<s16>(std::clamp(value >> 6, -32768, 32767));
    }
  }
}

DiscStream::DiscStream(u64 cpu_clock, u32 rate_divisor)
    : clock_(cpu_clock),
      remaining_(cpu_clock * 6 * DiscAdpcm::block_frames * rate_divisor / 108000000) {
  if (!clock_ || !remaining_)
    throw std::invalid_argument("Invalid disc audio clock");
}

void DiscStream::reset() {
  enabled_ = streaming_ = stop_at_end_ = false;
  buffer_size_ = 0;
  position_ = current_start_ = next_start_ = 0;
  current_length_ = next_length_ = pending_blocks_ = 0;
  pending_data_.clear();
  // An already queued read completion survives the drive's internal reset.
}

void DiscStream::configure(bool enabled, u8 buffer_size) {
  enabled_ = enabled;
  buffer_size_ = buffer_size & 15;
}

u32 DiscStream::command(u8 operation, u64 start, u32 length) {
  if (!enabled_)
    return 0x52001;
  if (operation == 1) {
    streaming_ = stop_at_end_ = false;
    return 0;
  }
  if (operation != 0)
    return 0x52401;
  if (!start && !length)
    stop_at_end_ = true;
  else if (!stop_at_end_) {
    next_start_ = start;
    next_length_ = length;
    if (!streaming_) {
      current_start_ = start;
      current_length_ = length;
      position_ = start;
      decoder_.reset();
      streaming_ = true;
    }
  }
  return 0;
}

u32 DiscStream::status(u8 operation, u32 &result) const {
  if (!enabled_)
    return 0x52001;
  switch (operation) {
  case 0:
    result = streaming_;
    break;
  case 1:
    result = static_cast<u32>((position_ & ~u64(32767)) >> 2);
    break;
  case 2:
    result = static_cast<u32>(current_start_ >> 2);
    break;
  case 3:
    result = current_length_;
    break;
  default:
    return 0x52401;
  }
  return 0;
}

u64 DiscStream::next_event() const {
  return remaining_;
}

void DiscStream::advance(u64 cycles, u32 rate_divisor, bool playing) {
  if (rate_divisor != 2248 && rate_divisor != 2250 && rate_divisor != 3372 && rate_divisor != 3375)
    throw std::invalid_argument("Unsupported disc audio rate divisor");
  while (cycles >= remaining_) {
    cycles -= remaining_;
    callback(rate_divisor, playing);
  }
  remaining_ -= cycles;
}

void DiscStream::callback(u32 rate_divisor, bool playing) {
  const u32 maximum = rate_divisor >= 3000 ? 4 : 6;
  if (read_ok_) {
    std::array<s16, 6 * DiscAdpcm::block_frames * 2> pcm{};
    const auto blocks =
        std::min<std::size_t>(pending_blocks_, pending_data_.size() / DiscAdpcm::block_bytes);
    for (std::size_t block = 0; block < blocks; ++block)
      decoder_.decode(
          std::span<const u8, DiscAdpcm::block_bytes>(
              pending_data_.data() + block * DiscAdpcm::block_bytes, DiscAdpcm::block_bytes),
          std::span<s16, DiscAdpcm::block_frames * 2>(
              pcm.data() + block * DiscAdpcm::block_frames * 2, DiscAdpcm::block_frames * 2));
    if (samples && pending_blocks_)
      samples(std::span<const s16>(pcm).first(pending_blocks_ * DiscAdpcm::block_frames * 2),
              rate_divisor);
  } else if (read_error)
    read_error();
  const bool schedule_read = read_ok_ && streaming_ && playing;
  read_ok_ = true;
  pending_data_.clear();
  if (schedule_read) {
    const u64 read_start = position_;
    pending_blocks_ = 0;
    while (pending_blocks_ < maximum) {
      if (position_ >= current_start_ + current_length_) {
        position_ = current_start_ = next_start_;
        current_length_ = next_length_;
        if (stop_at_end_) {
          stop_at_end_ = streaming_ = false;
          break;
        }
        decoder_.reset();
      }
      position_ += DiscAdpcm::block_bytes;
      ++pending_blocks_;
    }
    if (pending_blocks_) {
      pending_data_.resize(pending_blocks_ * DiscAdpcm::block_bytes);
      if (!read)
        throw std::runtime_error("Disc audio reader is not connected");
      try {
        read(read_start, pending_data_);
      } catch (const std::exception &) {
        pending_data_.clear();
        read_ok_ = false;
      }
    }
  } else
    pending_blocks_ = maximum;
  // A stop reached exactly at a boundary schedules an immediate empty callback.
  remaining_ = clock_ * pending_blocks_ * DiscAdpcm::block_frames * rate_divisor / 108000000;
}
} // namespace cupid::audio
