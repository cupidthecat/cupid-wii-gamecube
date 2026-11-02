// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/disc/crypto.hpp"
#include "cupid/ios/crypto.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace cupid::ios {
namespace {
u32 little32(std::span<const u8> bytes, unsigned offset) {
  return u32(bytes[offset]) | (u32(bytes[offset + 1]) << 8) | (u32(bytes[offset + 2]) << 16) |
         (u32(bytes[offset + 3]) << 24);
}

void little32(std::span<u8> bytes, unsigned offset, u32 value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes[offset + i] = u8(value >> (i * 8));
}
} // namespace

s32 sha_ioctlv(Memory &memory, u32 command, std::span<const CryptoVector> input,
               std::span<const CryptoVector> output) {
  const auto valid = [](const CryptoVector &v) { return v.size == 0 || v.address != 0; };
  if (command > 2 || input.size() != 1 || output.size() != 2 ||
      !std::all_of(input.begin(), input.end(), valid) ||
      !std::all_of(output.begin(), output.end(), valid))
    return -4;
  auto context = memory.range(output[0].address, 28);
  std::array<u32, 5> states{};
  for (unsigned i = 0; i < states.size(); ++i)
    states[i] = little32(context, i * 4);
  u64 total = u64(little32(context, 20)) | (u64(little32(context, 24)) << 32);
  std::array<u8, 20> digest{};
  if (command == 0) {
    states = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    total = 0;
  } else {
    const unsigned retained = unsigned(total & 63);
    if (retained && (command == 2 || input[0].size >= 64 - retained))
      throw std::runtime_error(
          "IOS SHA context omits the prior partial block required by this request");
    const auto source =
        input[0].size ? memory.range(input[0].address, input[0].size) : std::span<u8>{};
    const std::vector<u8> copied(source.begin(), source.end());
    auto remaining = std::span<const u8>(copied);
    total += copied.size();
    if (!retained) {
      while (remaining.size() >= 64) {
        disc::detail::sha1_compress(remaining.first<64>(), states);
        remaining = remaining.subspan(64);
      }
    }
    if (command == 2) {
      std::array<u8, 128> tail{};
      std::copy(remaining.begin(), remaining.end(), tail.begin());
      tail[remaining.size()] = 0x80;
      const unsigned length = remaining.size() < 56 ? 64 : 128;
      const u64 bits = total * 8;
      for (unsigned i = 0; i < 8; ++i)
        tail[length - 1 - i] = u8(bits >> (i * 8));
      disc::detail::sha1_compress(std::span<const u8>(tail).first<64>(), states);
      if (length == 128)
        disc::detail::sha1_compress(std::span<const u8>(tail).subspan<64, 64>(), states);
      for (unsigned i = 0; i < digest.size(); ++i)
        digest[i] = u8(states[i / 4] >> ((3 - i % 4) * 8));
    }
  }
  // The service's opaque context uses little-endian words and a byte counter.
  // Finalization changes the state words but does not count padding bytes.
  for (unsigned i = 0; i < states.size(); ++i)
    little32(context, i * 4, states[i]);
  little32(context, 20, u32(total));
  little32(context, 24, u32(total >> 32));
  if (command == 2)
    memory.copy_in(output[1].address, digest);
  return 0;
}
} // namespace cupid::ios
