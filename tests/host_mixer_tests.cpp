// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/audio/host_mixer.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <vector>

namespace {
using namespace cupid;

u64 hash_samples(std::span<const s16> values) {
  u64 result = 14695981039346656037ULL;
  for (s16 value : values) {
    const auto bits = static_cast<u16>(value);
    result = (result ^ (bits >> 8)) * 1099511628211ULL;
    result = (result ^ (bits & 255)) * 1099511628211ULL;
  }
  return result;
}

void fixed_fifo_vectors() {
  constexpr std::array<u64, 3> expected{8960202406781947320ULL, 9276470734982418956ULL,
                                        13970842107419246131ULL};
  for (unsigned scenario = 0; scenario < expected.size(); ++scenario) {
    audio::SampleFifo fifo(scenario == 1 ? 36000u : 3375u);
    fifo.set_volume(scenario == 2 ? u8{0} : u8{137}, 233);
    const unsigned count = scenario == 2 ? 40000u : 1024u;
    std::vector<s16> input(count);
    for (unsigned n = 0; n < count; ++n)
      input[n] = std::bit_cast<s16>(static_cast<u16>(n * 12345u + 677u));
    fifo.push_mono(input);
    audio::MixerOptions options;
    options.fill_gaps = scenario != 2;
    std::array<s16, 8192> output{};
    fifo.mix(output, options);
    CHECK(hash_samples(output) == expected[scenario]);
    CHECK(fifo.dropped_granules() == (scenario == 2 ? 57u : 0u));
    if (scenario == 2) {
      for (unsigned n = 0; n < 12; ++n) {
        output.fill(0);
        fifo.mix(output, options);
      }
      CHECK(hash_samples(output) == 11248824735641314085ULL);
      CHECK(std::all_of(output.begin(), output.end(), [](s16 sample) { return sample == 0; }));
    }
  }
  CHECK_THROWS(audio::SampleFifo(0));
  audio::SampleFifo fifo;
  const std::array<s16, 3> odd{1, 2, 3};
  CHECK_THROWS(fifo.push_stereo(odd));
  std::array<s16, 3> odd_output{};
  CHECK_THROWS(fifo.mix(odd_output, {}));
  CHECK_THROWS(fifo.set_rate_divisor(0));
  std::array<s16, 16> output{};
  audio::MixerOptions invalid;
  invalid.output_rate = 0;
  CHECK_THROWS(fifo.mix(output, invalid));
  invalid = {};
  invalid.speed = -1;
  CHECK_THROWS(fifo.mix(output, invalid));
}

void empty_callback_burst() {
  // Executed target output: this startup sequence discards all 12 incoming granules.
  audio::SampleFifo fifo(36000);
  fifo.set_rate_divisor(13500);
  audio::MixerOptions options;
  std::array<s16, 2048> output{};
  fifo.mix(output, options);
  const std::vector<s16> input(1600, s16{16384});
  fifo.push_mono(input);
  CHECK(fifo.dropped_granules() == 12);
  for (unsigned remaining = 144000; remaining;) {
    const unsigned frames = std::min(remaining, 1024u);
    output.fill(0);
    const auto block = std::span<s16>(output).first(frames * 2);
    fifo.mix(block, options);
    CHECK(std::all_of(block.begin(), block.end(), [](s16 sample) { return sample == 0; }));
    remaining -= frames;
  }
}

} // namespace

int main() {
  return run_tests([] {
    fixed_fifo_vectors();
    empty_callback_burst();
  });
}
