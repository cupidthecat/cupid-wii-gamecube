// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/types.hpp"
#include <filesystem>
#include <span>
#include <vector>

namespace cupid::runtime {
struct IplFontBlock {
  std::vector<u8> bytes;
  u32 encoded_size = 0, decoded_size = 0, decoded_crc32 = 0;
};

struct IplFonts {
  IplFontBlock shift_jis, western;
  u32 container_crc32 = 0;
};

IplFonts validate_ipl_fonts(std::span<const u8> container);
IplFonts load_ipl_fonts(const std::filesystem::path &path);
} // namespace cupid::runtime
