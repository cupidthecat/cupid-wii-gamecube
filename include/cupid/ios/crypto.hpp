// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/memory.hpp"
#include <span>

namespace cupid::ios {
struct CryptoVector {
  u32 address, size;
};

s32 aes_ioctlv(Memory &memory, u32 command, std::span<const CryptoVector> input,
               std::span<const CryptoVector> output);
s32 sha_ioctlv(Memory &memory, u32 command, std::span<const CryptoVector> input,
               std::span<const CryptoVector> output);
} // namespace cupid::ios
