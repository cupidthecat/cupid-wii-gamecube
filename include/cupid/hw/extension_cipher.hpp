// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/types.hpp"
#include <array>
#include <span>

namespace cupid::hw {
struct ExtensionKey {
  std::array<u8, 8> subtraction{}, xor_mask{};
  void encrypt(std::span<u8> data, u32 address) const;
  void decrypt(std::span<u8> data, u32 address) const;
  bool operator==(const ExtensionKey &) const = default;
};

ExtensionKey make_extension_key(std::span<const u8, 16> register_key_data);
} // namespace cupid::hw
