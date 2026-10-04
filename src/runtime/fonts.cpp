// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/fonts.hpp"
#include "cupid/runtime/boot.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace cupid::runtime {
namespace {
[[noreturn]] void invalid(const std::string &reason) {
  throw std::runtime_error("Invalid IPL font data: " + reason);
}

u32 read(std::span<const u8> bytes, std::size_t offset, unsigned size) {
  if (offset > bytes.size() || size > bytes.size() - offset)
    invalid("truncated stream");
  u32 value = 0;
  for (unsigned i = 0; i < size; ++i)
    value = (value << 8) | bytes[offset + i];
  return value;
}

u32 checksum(std::span<const u8> bytes) {
  u32 value = 0xffffffff;
  for (u8 byte : bytes) {
    value ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit)
      value = (value >> 1) ^ ((value & 1) ? 0xedb88320 : 0);
  }
  return ~value;
}

IplFontBlock font(std::span<const u8> source, unsigned type) {
  if (read(source, 0, 4) != 0x59617930)
    invalid("expected a Yay0 stream");
  const u32 size = read(source, 4, 4);
  const std::size_t links_start = read(source, 8, 4), chunks_start = read(source, 12, 4);
  if (size < 48 || size > 1024 * 1024)
    invalid("decoded size is outside the supported font range");
  if (links_start < 16 || (links_start & 3) || chunks_start < links_start ||
      ((chunks_start - links_start) & 1) || chunks_start > source.size())
    invalid("invalid mask, link, or literal offsets");

  std::vector<u8> decoded;
  decoded.reserve(size);
  std::size_t mask_position = 16, link_position = links_start, chunk_position = chunks_start;
  u32 mask = 0;
  unsigned bits = 0;
  auto literal = [&] {
    if (chunk_position >= source.size())
      invalid("literal stream is truncated");
    return source[chunk_position++];
  };
  while (decoded.size() < size) {
    if (!bits) {
      if (mask_position > links_start || links_start - mask_position < 4)
        invalid("mask stream overlaps links");
      mask = read(source, mask_position, 4);
      mask_position += 4;
      bits = 32;
    }
    if (mask & 0x80000000) {
      decoded.push_back(literal());
    } else {
      if (link_position > chunks_start || chunks_start - link_position < 2)
        invalid("link stream overlaps literals");
      const u32 token = read(source, link_position, 2);
      link_position += 2;
      const unsigned distance = (token & 4095) + 1;
      const unsigned count = (token >> 12) ? (token >> 12) + 2 : unsigned(literal()) + 18;
      if (distance > decoded.size() || count > size - decoded.size())
        invalid("back-reference exceeds decoded data");
      for (unsigned i = 0; i < count; ++i)
        decoded.push_back(decoded[decoded.size() - distance]);
    }
    mask <<= 1;
    --bits;
  }

  const auto field = [&](unsigned offset) { return read(decoded, offset, 2); };
  const u32 cell_width = field(16), cell_height = field(18);
  const u32 sheet_size = read(decoded, 20, 4), sheet_format = field(24);
  const u32 columns = field(26), rows = field(28), width = field(30), height = field(32);
  const u32 widths = field(34), image = read(decoded, 36, 4), full_size = read(decoded, 40, 4);
  if (field(0) != type || field(2) > field(4))
    invalid("font type or character range does not match its slot");
  if (sheet_format != 0)
    invalid("unsupported glyph sheet encoding");
  if (!cell_width || !cell_height || !width || !height || width > 4096 || height > 4096 ||
      (width & 7) || (height & 7) || !columns || !rows || columns * cell_width > width ||
      rows * cell_height > height)
    invalid("invalid glyph sheet geometry");
  if (sheet_size != width * height / 2 || !full_size || full_size % sheet_size || widths < 48 ||
      widths >= image || image > size || full_size / 2 != size - image || (full_size & 1))
    invalid("glyph table or image range exceeds decoded data");
  const u64 glyph_capacity = u64(columns) * rows * (full_size / sheet_size);
  if (u64(image - widths) > glyph_capacity || field(6) >= image - widths)
    invalid("glyph indices exceed the available sheets");
  if (type == 0 && image - widths != field(4) - field(2) + 1)
    invalid("Western glyph table disagrees with its character range");
  for (u32 position = widths; position < image; ++position)
    if (decoded[position] > cell_width)
      invalid("character width exceeds a glyph cell");

  IplFontBlock result;
  result.encoded_size = static_cast<u32>(std::max({mask_position, link_position, chunk_position}));
  result.decoded_size = size;
  result.decoded_crc32 = checksum(decoded);
  result.bytes.resize(source.size());
  std::copy_n(source.begin(), result.encoded_size, result.bytes.begin());
  return result;
}
} // namespace

IplFonts validate_ipl_fonts(std::span<const u8> container) {
  if (container.size() != 2 * 1024 * 1024)
    invalid("font container must contain exactly 2097152 bytes");
  IplFonts result;
  result.shift_jis = font(container.subspan(0x1aff00, 0x4d000), 2);
  result.western = font(container.subspan(0x1fcf00, 0x3100), 0);
  result.container_crc32 = checksum(container);
  return result;
}

IplFonts load_ipl_fonts(const std::filesystem::path &path) {
  return validate_ipl_fonts(read_file(path, 2 * 1024 * 1024));
}
} // namespace cupid::runtime
