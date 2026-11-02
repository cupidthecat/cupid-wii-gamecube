// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/audio/host_mixer.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cupid::audio {
namespace {
// Symmetric Slepian/convolution window, generated from its defining eigenproblem.
constexpr std::array<u32, 128> window_half_bits{
    0x35da662bu, 0x36aa490fu, 0x373de572u, 0x37b59b4eu, 0x381e45a6u, 0x38815fe4u, 0x38c9a49fu,
    0x39175ca2u, 0x395c6a57u, 0x399c72b4u, 0x39d94f28u, 0x3a14190fu, 0x3a46874du, 0x3a831bb6u,
    0x3aaadfe3u, 0x3adc069bu, 0x3b0c19f8u, 0x3b309dd1u, 0x3b5c9166u, 0x3b888845u, 0x3ba7a856u,
    0x3bcc5071u, 0x3bf734ccu, 0x3c148afdu, 0x3c316033u, 0x3c5285cbu, 0x3c786ca5u, 0x3c91c516u,
    0x3caa2bd3u, 0x3cc5a8bau, 0x3ce47b15u, 0x3d037167u, 0x3d168ff3u, 0x3d2bb8eeu, 0x3d430b8cu,
    0x3d5ca621u, 0x3d78a5ceu, 0x3d8b9313u, 0x3d9c206eu, 0x3dae06afu, 0x3dc15047u, 0x3dd60630u,
    0x3dec2fcau, 0x3e01e957u, 0x3e0e7949u, 0x3e1bc892u, 0x3e29d6f8u, 0x3e38a325u, 0x3e482a9fu,
    0x3e5869bfu, 0x3e695bb1u, 0x3e7afa6fu, 0x3e869f66u, 0x3e901038u, 0x3e99caf5u, 0x3ea3ca5bu,
    0x3eae08aau, 0x3eb87fa6u, 0x3ec328acu, 0x3ecdfcb2u, 0x3ed8f45fu, 0x3ee40810u, 0x3eef2fedu,
    0x3efa63f4u, 0x3f02ce06u, 0x3f08680au, 0x3f0dfbf8u, 0x3f1385d1u, 0x3f1901a7u, 0x3f1e6baau,
    0x3f23c02du, 0x3f28fbabu, 0x3f2e1ad2u, 0x3f331a86u, 0x3f37f7e4u, 0x3f3cb04du, 0x3f414164u,
    0x3f45a914u, 0x3f49e590u, 0x3f4df558u, 0x3f51d737u, 0x3f558a42u, 0x3f590ddcu, 0x3f5c61aeu,
    0x3f5f85aau, 0x3f627a07u, 0x3f653f3au, 0x3f67d5f7u, 0x3f6a3f2au, 0x3f6c7bf2u, 0x3f6e8d9eu,
    0x3f7075a3u, 0x3f72359eu, 0x3f73cf47u, 0x3f754471u, 0x3f769701u, 0x3f77c8eau, 0x3f78dc27u,
    0x3f79d2bau, 0x3f7aaea1u, 0x3f7b71d7u, 0x3f7c1e4du, 0x3f7cb5e9u, 0x3f7d3a7fu, 0x3f7dadd4u,
    0x3f7e1196u, 0x3f7e675fu, 0x3f7eb0afu, 0x3f7eeeefu, 0x3f7f236fu, 0x3f7f4f62u, 0x3f7f73e6u,
    0x3f7f91fdu, 0x3f7faa90u, 0x3f7fbe72u, 0x3f7fce5eu, 0x3f7fdafau, 0x3f7fe4d6u, 0x3f7fec72u,
    0x3f7ff239u, 0x3f7ff68au, 0x3f7ff9b3u, 0x3f7ffbf5u, 0x3f7ffd87u, 0x3f7ffe95u, 0x3f7fff42u,
    0x3f7fffabu, 0x3f7fffe5u,
};

float window(unsigned position) {
  return std::bit_cast<float>(window_half_bits[position < 128 ? position : 255 - position]);
}

struct Stereo {
  float left = 0, right = 0;
};

using Granule = std::array<Stereo, 256>;

u32 increment(u32 divisor, const MixerOptions &options, double &input_rate) {
  if (!divisor || !options.output_rate || !std::isfinite(options.speed) || options.speed < 0)
    throw std::invalid_argument("Invalid host mixer rate");
  input_rate = 108000000.0 / divisor;
  if (!options.preserve_pitch && options.speed > 0 && options.speed != 1)
    input_rate *= options.speed;
  const double value = 16777216.0 * input_rate / options.output_rate;
  if (value > std::numeric_limits<s32>::max() - 0.5)
    throw std::invalid_argument("Host mixer rate step exceeds its portable range");
  return static_cast<u32>(std::lround(value));
}
} // namespace

struct SampleFifo::Impl {
  u32 divisor = 3375, phase = 0;
  unsigned cursor = 0, head = 0, tail = 0, queue_limit = 20;
  bool fading = false, looping = false;
  float fade = 1;
  Stereo gain{1, 1}, error;
  Granule incoming{}, first{}, second{};
  std::array<Granule, 256> queue{};
  u64 dropped = 0;

  void push(s16 left, s16 right) {
    incoming[cursor] = {float(left), float(right)};
    cursor = (cursor + 1) & 255;
    if (cursor & 127)
      return;
    const unsigned next = (head + 1) & 255;
    if (next == tail) {
      ++dropped;
      return;
    }
    for (unsigned n = 0; n < 256; ++n) {
      const auto sample = incoming[(cursor + n) & 255];
      const float weight = window(n);
      queue[head][n] = {sample.left * weight, sample.right * weight};
    }
    head = next;
    fading = looping = false;
  }

  bool take(Granule &destination, const MixerOptions &options) {
    unsigned selected = tail;
    if (((head - selected) & 255) > queue_limit)
      selected = (head - (queue_limit / 2 + 1)) & 255;
    unsigned next = (selected + 1) & 255;
    if (next == head) {
      if (options.fill_gaps && options.running) {
        next = (head - (std::max(2u, queue_limit / 2) - 1)) & 255;
        fading = looping;
        looping = true;
      } else {
        destination.fill({});
        fading = looping = false;
        return false;
      }
    }
    destination = queue[selected];
    tail = next;
    return fading;
  }
};

SampleFifo::SampleFifo(u32 rate_divisor) : impl_(std::make_unique<Impl>()) {
  set_rate_divisor(rate_divisor);
}

SampleFifo::~SampleFifo() = default;

void SampleFifo::set_rate_divisor(u32 divisor) {
  if (!divisor)
    throw std::invalid_argument("Host mixer rate divisor must be nonzero");
  impl_->divisor = divisor;
}

void SampleFifo::set_volume(u8 left, u8 right) {
  impl_->gain = {float(unsigned(left) + (left >> 7)) / 256,
                 float(unsigned(right) + (right >> 7)) / 256};
}

void SampleFifo::push_stereo(std::span<const s16> samples) {
  if (samples.size() & 1)
    throw std::invalid_argument("Host mixer stereo input has an incomplete frame");
  for (std::size_t n = 0; n < samples.size(); n += 2)
    impl_->push(samples[n], samples[n + 1]);
}

void SampleFifo::push_mono(std::span<const s16> samples) {
  for (s16 sample : samples)
    impl_->push(sample, sample);
}

void SampleFifo::mix(std::span<s16> samples, const MixerOptions &options) {
  if (samples.size() & 1)
    throw std::invalid_argument("Host mixer stereo output has an incomplete frame");
  double input_rate = 0;
  const u32 step = increment(impl_->divisor, options, input_rate);
  const auto buffered = static_cast<u64>(std::llround(options.buffer_ms * input_rate / 1000.0));
  impl_->queue_limit = static_cast<unsigned>(std::clamp<u64>(buffered / 128, 4, 256));
  const float rise = static_cast<float>(-std::expm1(-1.0 / (options.output_rate * 0.008)));
  const float fall = static_cast<float>(-std::expm1(-1.0 / (options.output_rate * 0.064)));
  bool fade_audio = impl_->fading;
  for (std::size_t n = 0; n < samples.size(); n += 2) {
    impl_->phase += step;
    const u32 back_phase = impl_->phase + 0x80000000u;
    if (impl_->phase < step)
      fade_audio = impl_->take(impl_->first, options);
    else if (back_phase < step)
      fade_audio = impl_->take(impl_->second, options);
    const unsigned front = impl_->phase >> 24, back = back_phase >> 24;
    const float t = float(impl_->phase & 0xffffffu) / 16777216.f;
    const float squared = t * t, cubed = squared * t;
    const std::array<float, 6> coefficient{
        (t - 2 * squared + cubed) / 12,      (-8 * t + 15 * squared - 7 * cubed) / 12,
        (3 - 7 * squared + 4 * cubed) / 3,   (2 * t + 5 * squared - 4 * cubed) / 3,
        (-t - 6 * squared + 7 * cubed) / 12, (squared - cubed) / 12};
    Stereo filtered;
    for (unsigned tap = 0; tap < 6; ++tap) {
      const auto a = impl_->first[(front + tap - 2) & 255];
      const auto b = impl_->second[(back + tap - 2) & 255];
      const Stereo term{(a.left + b.left) * coefficient[tap],
                        (a.right + b.right) * coefficient[tap]};
      if (!tap)
        filtered = term;
      else {
        filtered.left += term.left;
        filtered.right += term.right;
      }
    }
    impl_->fade += (fade_audio ? fall : rise) * ((fade_audio ? 0.f : 1.f) - impl_->fade);
    const auto quantize = [&](float value, float gain, float &error, s16 &destination) {
      const float mixed = (value * gain) * impl_->fade + (destination - error);
      destination = static_cast<s16>(std::clamp<long>(std::lround(mixed), -32768, 32767));
      error = std::clamp(destination - mixed, -1.f, 1.f);
    };
    quantize(filtered.left, impl_->gain.left, impl_->error.left, samples[n]);
    quantize(filtered.right, impl_->gain.right, impl_->error.right, samples[n + 1]);
  }
}

u64 SampleFifo::dropped_granules() const {
  return impl_->dropped;
}
} // namespace cupid::audio
