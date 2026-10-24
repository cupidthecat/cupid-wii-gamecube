// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/audio/host_mixer.hpp"
#include "cupid/platform/output.hpp"
#include "test_support.hpp"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <iostream>
#include <thread>
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

template <typename Predicate>
platform::HostAudioStats wait_audio(platform::Output &output, Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  do {
    const auto stats = output.host_audio_stats();
    if (predicate(stats))
      return stats;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (std::chrono::steady_clock::now() < deadline);
  throw std::runtime_error("SDL audio callback did not produce the expected samples");
}

audio::RemoteSpeakerPacket speaker_packet(u8 left, u8 right) {
  audio::RemoteSpeakerPacket packet;
  packet.count = 40;
  packet.rate_divisor = 3375;
  packet.left_volume = left;
  packet.right_volume = right;
  for (unsigned n = 0; n < packet.count; ++n)
    packet.mono[n] = static_cast<s16>(10000 + n * 17);
  return packet;
}

void speaker_channel(unsigned port, bool left) {
  platform::OutputOptions options;
  options.audio = options.remote_speakers = true;
  platform::Output output(options);
  CHECK(output.remote_speaker_processing());
  auto packet = speaker_packet(left ? u8{255} : u8{0}, left ? u8{0} : u8{255});
  for (unsigned n = 0; n < 26; ++n)
    output.remote_speaker(port, packet);
  const auto stats =
      wait_audio(output, [](const auto &value) { return value.nonzero_samples != 0; });
  CHECK(left ? stats.left_nonzero > 0 : stats.right_nonzero > 0);
  CHECK(left ? stats.right_nonzero == 0 : stats.left_nonzero == 0);
  CHECK(output.audio_samples() == 0 && output.audio_nonzero_samples() == 0);
  CHECK(output.audio_hash() == 14695981039346656037ULL && output.audio_peak() == 0);
  packet.count = 41;
  CHECK_THROWS(output.remote_speaker(port, packet));
  packet.count = 40;
  packet.rate_divisor = 0;
  CHECK_THROWS(output.remote_speaker(port, packet));
  output.remote_speaker(4, packet);
  std::cout << "REMOTE " << port << " left " << stats.left_nonzero << " right "
            << stats.right_nonzero << '\n';
}

void dma_order(u32 rate, bool left) {
  platform::OutputOptions options;
  options.audio = true;
  platform::Output output(options);
  std::array<s16, 2048> guest_rl{};
  for (std::size_t n = 0; n < guest_rl.size(); n += 2)
    guest_rl[n + (left ? 1 : 0)] = 16000;
  output.audio(guest_rl, rate);
  const auto stats =
      wait_audio(output, [](const auto &value) { return value.nonzero_samples != 0; });
  CHECK(left ? stats.left_nonzero > 0 : stats.right_nonzero > 0);
  CHECK(left ? stats.right_nonzero == 0 : stats.left_nonzero == 0);
  CHECK(output.audio_samples() == guest_rl.size() && output.audio_nonzero_samples() == 1024);
  CHECK(output.audio_hash() == hash_samples(guest_rl));
  CHECK_THROWS(output.audio(guest_rl, 20000));
  CHECK_THROWS(output.audio(std::span<const s16>(guest_rl).first(3), rate));
  CHECK(output.audio_samples() == guest_rl.size());
  std::cout << "DMA " << rate << " left " << stats.left_nonzero << " right " << stats.right_nonzero
            << '\n';
}

void disc_order(u32 divisor, bool left) {
  platform::OutputOptions options;
  options.audio = true;
  platform::Output output(options);
  std::array<s16, 2048> decoded_rl{};
  for (std::size_t n = 0; n < decoded_rl.size(); n += 2)
    decoded_rl[n + (left ? 1 : 0)] = 16000;
  output.disc_audio(decoded_rl, divisor, left ? u8{255} : u8{0}, left ? u8{0} : u8{255});
  const auto stats =
      wait_audio(output, [](const auto &value) { return value.nonzero_samples != 0; });
  CHECK(left ? stats.left_nonzero > 0 : stats.right_nonzero > 0);
  CHECK(left ? stats.right_nonzero == 0 : stats.left_nonzero == 0);
  CHECK(output.disc_audio_stats().samples == decoded_rl.size());
  CHECK(output.disc_audio_stats().nonzero_samples == 1024);
  CHECK(output.disc_audio_stats().hash == hash_samples(decoded_rl));
  CHECK(output.audio_samples() == 0 && output.audio_hash() == 14695981039346656037ULL);
  // Independent AI volume control must mute already queued disc samples.
  output.disc_audio_control(divisor, 0, 0);
  CHECK_THROWS(output.disc_audio_control(0, 255, 255));
}
} // namespace

#include "machine_speaker_checks.inc"

int main() {
  return run_tests([] {
    CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    machine_speaker_transport::checks();
    platform::OutputOptions invalid;
    invalid.remote_speakers = true;
    CHECK_THROWS(platform::Output(invalid));
    {
      platform::OutputOptions options;
      options.audio = true;
      platform::Output output(options);
      CHECK(!output.remote_speaker_processing());
      for (unsigned n = 0; n < 26; ++n)
        output.remote_speaker(0, speaker_packet(255, 255));
      const auto stats =
          wait_audio(output, [](const auto &value) { return value.mixed_frames >= 4096; });
      CHECK(stats.nonzero_samples == 0 && output.audio_samples() == 0);
    }
    for (unsigned port = 0; port < 4; ++port) {
      speaker_channel(port, true);
      speaker_channel(port, false);
    }
    for (u32 rate : {32000u, 48000u, 32028u, 48042u}) {
      dma_order(rate, true);
      dma_order(rate, false);
    }
    for (u32 divisor : {2248u, 2250u, 3372u, 3375u}) {
      disc_order(divisor, true);
      disc_order(divisor, false);
    }
    {
      platform::Output output({true, false, {}, {}});
      CHECK(output.host_audio_stats().mixed_frames == 0);
      output.remote_speaker(0, speaker_packet(255, 255));
      CHECK(output.audio_samples() == 0);
    }
  });
}
