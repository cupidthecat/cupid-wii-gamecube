// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/audio/remote_speaker.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <limits>

namespace {
using namespace cupid;
using namespace cupid::audio;

void known_decoding() {
  RemoteSpeakerDecoder decoder;
  const std::array<u8, 5> pcm{0, 1, 127, 128, 255};
  const auto packet = decoder.decode({0x40, 2000, 255}, pcm);
  const std::array<s16, 5> expected{0, 256, 32512, -32768, -256};
  CHECK(packet.count == 5 && std::equal(expected.begin(), expected.end(), packet.mono.begin()));
  CHECK(packet.rate_divisor == 9000 && packet.left_volume == 255 && packet.right_volume == 255);
  CHECK(decoder.state() == RemoteSpeakerState{});
  const std::array<u8, 2> codes{0x70, 0xf8};
  const auto adpcm = decoder.decode({0, 1000, 127}, codes);
  const std::array<s16, 4> expanded{238, 276, -235, -316};
  CHECK(adpcm.count == 4 && std::equal(expanded.begin(), expanded.end(), adpcm.mono.begin()));
  CHECK(adpcm.rate_divisor == 9000 && decoder.state().predictor == -316 &&
        decoder.state().step == 587);
  decoder.reset();
  CHECK(decoder.state() == RemoteSpeakerState{});
}

void state_and_clipping() {
  std::array<u8, 20> bytes;
  for (unsigned i = 0; i < bytes.size(); ++i)
    bytes[i] = u8(i * 37);
  RemoteSpeakerDecoder whole, split;
  const auto packet = whole.decode({0, 1234, 0}, bytes);
  const auto first = split.decode({0, 1234, 0}, std::span(bytes).first(7));
  const auto second = split.decode({0, 1234, 0}, std::span(bytes).subspan(7));
  CHECK(packet.count == 40 && packet.left_volume == 0 && packet.right_volume == 0);
  CHECK(std::equal(first.mono.begin(), first.mono.begin() + first.count, packet.mono.begin()));
  CHECK(std::equal(second.mono.begin(), second.mono.begin() + second.count,
                   packet.mono.begin() + first.count));
  CHECK(whole.state() == split.state());
  bytes.fill(0x77);
  whole.reset();
  const auto high = whole.decode({0, 1000, 255}, bytes);
  CHECK(high.mono[39] == 32767 && whole.state().step == 24576);
  bytes.fill(0xff);
  const auto low = whole.decode({0, 1000, 127}, bytes);
  CHECK(low.mono[39] == -32768 && whole.state().step == 24576);
}

void suppression_and_errors() {
  RemoteSpeakerDecoder decoder;
  const std::array<u8, 1> codes{0x77};
  CHECK(decoder.decode({0, 1000, 127, false}, codes).count == 0);
  CHECK(decoder.decode({0, 0, 127}, codes).count == 0);
  CHECK(decoder.decode({0, 1000, 127}, {}).count == 0);
  CHECK(decoder.state() == RemoteSpeakerState{});
  const auto preserved = decoder.state();
  auto invalid = [&](RemoteSpeakerConfig config, std::span<const u8> bytes) {
    bool rejected = false;
    try {
      decoder.decode(config, bytes);
    } catch (const RemoteSpeakerError &) {
      rejected = true;
    }
    CHECK(rejected && decoder.state() == preserved);
  };
  invalid({1, 1000, 127}, codes);
  invalid({0, 1000, 127, true, std::numeric_limits<float>::quiet_NaN()}, codes);
  const std::array<u8, 21> oversized{};
  invalid({0, 1000, 127}, oversized);
}

void gain_and_rates() {
  RemoteSpeakerDecoder decoder;
  const std::array<u8, 1> bytes{0};
  for (u8 format : {u8(0), u8(0x40)}) {
    for (u16 divisor : {u16(1), u16(137), u16(65535)}) {
      const auto packet = decoder.decode({format, divisor, 64, true, 2}, bytes);
      const u32 rate = (format ? 12000000u : 6000000u) / divisor;
      CHECK(packet.rate_divisor == 108000000u / (rate * 2));
      CHECK(packet.left_volume == 0 && packet.right_volume == (format ? 64 : 129));
    }
  }
  const auto left = decoder.decode({0, 1000, 255, true, -2}, bytes);
  CHECK(left.left_volume == 255 && left.right_volume == 0);
}
} // namespace

int main() {
  return run_tests([] {
    known_decoding();
    state_and_clipping();
    suppression_and_errors();
    gain_and_rates();
  });
}
