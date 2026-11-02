// SPDX-License-Identifier: GPL-2.0-or-later
#include "Core/HW/StreamADPCM.h"
#include "cupid/audio/disc_stream.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

void check_disc_stream_schedule();

int main() {
  try {
    check_disc_stream_schedule();
    StreamADPCM::ADPCMDecoder reference;
    cupid::audio::DiscAdpcm observed;
    std::array<u8, 32> block{};
    std::array<s16, 56> expected{}, actual{};
    u64 random = 71, blocks = 0, hash = 14695981039346656037ULL;
    for (unsigned headers = 0; headers < 65536; ++headers) {
      reference.ResetFilter();
      observed.reset();
      for (unsigned continuation = 0; continuation < 16; ++continuation) {
        block[0] = u8(headers);
        block[1] = u8(headers >> 8);
        for (unsigned n = 2; n < 32; ++n) {
          random = random * 6364136223846793005ULL + 1;
          block[n] = u8(random >> 32);
        }
        reference.DecodeBlock(expected.data(), block.data());
        observed.decode(block, actual);
        if (actual != expected)
          throw std::runtime_error("Disc ADPCM mismatch at headers " + std::to_string(headers) +
                                   " continuation " + std::to_string(continuation));
        ++blocks;
        for (s16 sample : actual) {
          const u16 bits = static_cast<u16>(sample);
          hash = (hash ^ (bits >> 8)) * 1099511628211ULL;
          hash = (hash ^ (bits & 255)) * 1099511628211ULL;
        }
      }
    }
    std::cout << blocks << " exact decoded blocks, " << blocks * 56 << " sample values, hash "
              << hash << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
