// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/platform/output.hpp"
#include "test_support.hpp"
#include <array>

int main() {
  return run_tests([] {
    using namespace cupid;
    platform::Output output({true, false, {}, {}});
    CHECK(output.audio_samples() == 0 && output.audio_nonzero_samples() == 0);
    CHECK(output.audio_peak() == 0 && output.audio_hash() == 0xcbf29ce484222325ULL);
    const std::array<s16, 8> pcm{0, 0, 32767, -32768, 8192, -8192, 0, 0};
    for (u32 rate : {32000u, 48000u, 32028u, 48042u})
      output.audio(pcm, rate);
    CHECK(output.audio_samples() == 32 && output.audio_nonzero_samples() == 16);
    CHECK(output.audio_peak() == 32768 && output.audio_hash() == 0xd9168ca25cee7afdULL);
    output.audio({}, 48000);
    CHECK_THROWS(output.audio(pcm, 20000));
    CHECK_THROWS(output.audio(std::span<const s16>(pcm).first(3), 48000));
    CHECK(output.audio_samples() == 32 && output.audio_nonzero_samples() == 16);
    CHECK(output.audio_peak() == 32768 && output.audio_hash() == 0xd9168ca25cee7afdULL);
  });
}
