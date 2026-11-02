// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/disc/crypto.hpp"
#include "cupid/ios/crypto.hpp"
#include <algorithm>
#include <array>
#include <vector>

namespace cupid::ios {
s32 aes_ioctlv(Memory &memory, u32 command, std::span<const CryptoVector> input,
               std::span<const CryptoVector> output) {
  const auto valid = [](const CryptoVector &v) { return v.size == 0 || v.address != 0; };
  if (!std::all_of(input.begin(), input.end(), valid) ||
      !std::all_of(output.begin(), output.end(), valid))
    return -4;
  if (command == 0) {
    if (input.size() != 1 || output.size() != 1)
      return -4;
    if (!input[0].size)
      return 0;
    const auto source = memory.range(input[0].address, input[0].size);
    const std::vector<u8> copied(source.begin(), source.end());
    // This command copies the input length, independent of the output vector's
    // advertised length. Snapshotting also preserves overlapping copies.
    memory.copy_in(output[0].address, copied);
    return 0;
  }
  if ((command != 2 && command != 3) || input.size() != 2 || output.size() != 2)
    return -4;
  if (input[1].size != 16 || (input[1].address & 3) || output[1].size != 16 ||
      (output[1].address & 3))
    return -4;
  const auto source =
      input[0].size ? memory.range(input[0].address, input[0].size) : std::span<u8>{};
  auto destination =
      output[0].size ? memory.range(output[0].address, output[0].size) : std::span<u8>{};
  std::vector<u8> plaintext(source.begin(), source.end()), transformed(destination.size());
  std::array<u8, 16> key{}, iv{};
  const auto key_bytes = memory.range(input[1].address, 16);
  const auto iv_bytes = memory.range(output[1].address, 16);
  std::copy(key_bytes.begin(), key_bytes.end(), key.begin());
  std::copy(iv_bytes.begin(), iv_bytes.end(), iv.begin());
  const std::size_t count = std::min(plaintext.size(), transformed.size());
  if (count != 0 && count % 16 == 0) {
    const disc::detail::Aes128 aes(key);
    const auto in = std::span<const u8>(plaintext).first(count);
    const auto out = std::span<u8>(transformed).first(count);
    if (command == 2) {
      aes.encrypt_cbc(in, out, iv);
      std::copy_n(out.end() - 16, 16, iv.begin());
    } else {
      aes.decrypt_cbc(in, out, iv);
      std::copy_n(in.end() - 16, 16, iv.begin());
    }
  }
  // The selected service ignores the primitive's non-block-length failure:
  // output stays zero and the IV stays unchanged, while the request succeeds.
  std::copy(transformed.begin(), transformed.end(), destination.begin());
  memory.copy_in(output[1].address, iv);
  return 0;
}
} // namespace cupid::ios
