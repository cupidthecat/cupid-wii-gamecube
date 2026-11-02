// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/runtime/boot.hpp"

namespace cupid::runtime {
// Loads an absolute ELF32 PowerPC executable. All ranges are checked before RAM is changed.
BootInfo load_elf(Memory &memory, std::span<const u8> file);
} // namespace cupid::runtime
