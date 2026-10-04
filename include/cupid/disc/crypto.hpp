// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/types.hpp"
#include <array>
#include <span>

namespace cupid::disc::detail {
using Digest = std::array<u8, 20>;
Digest sha1(std::span<const u8> bytes);

class Aes128 {
public:
  explicit Aes128(std::span<const u8, 16> key);
  void encrypt_cbc(std::span<const u8> input, std::span<u8> output,
                   std::span<const u8, 16> iv) const;
  void decrypt_cbc(std::span<const u8> input, std::span<u8> output,
                   std::span<const u8, 16> iv) const;

private:
  std::array<u8, 176> keys_{};
  void block(std::array<u8, 16> &state, bool decrypt) const;
};
} // namespace cupid::disc::detail
