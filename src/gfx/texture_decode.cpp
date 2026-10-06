// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/texture.hpp"
#include <algorithm>

namespace cupid::gfx {
namespace {
struct Layout {
  unsigned width, height, bytes;
};

Layout layout(unsigned format) {
  switch (format) {
  case 0:
  case 8:
  case 14:
    return {8, 8, 32};
  case 1:
  case 2:
  case 9:
    return {8, 4, 32};
  case 3:
  case 4:
  case 5:
  case 10:
    return {4, 4, 32};
  case 6:
    return {4, 4, 64};
  default:
    throw VertexError("Reserved texture encoding");
  }
}

u8 expand(unsigned value, unsigned bits) {
  if (bits == 3)
    return static_cast<u8>((value << 5) | (value << 2) | (value >> 1));
  return static_cast<u8>((value << (8 - bits)) | (value >> (2 * bits - 8)));
}

Color rgb565(u16 value) {
  return {expand(value >> 11, 5), expand((value >> 5) & 63, 6), expand(value & 31, 5), 255};
}

Color rgb5a3(u16 value) {
  if (value & 0x8000)
    return {expand((value >> 10) & 31, 5), expand((value >> 5) & 31, 5), expand(value & 31, 5),
            255};
  return {expand((value >> 8) & 15, 4), expand((value >> 4) & 15, 4), expand(value & 15, 4),
          expand((value >> 12) & 7, 3)};
}

} // namespace

struct TextureSampler::DecodeLayout {
  unsigned format, width, height;
  Layout tile;
  bool manual;
  u64 mip_offset, base, odd_base;
  u32 palette;
};

TextureSampler::DecodeLayout TextureSampler::decode_layout(unsigned unit, unsigned mip) const {
  if (unit >= 8)
    throw VertexError("Texture unit exceeds hardware range");
  const auto image_register = [&](unsigned group) {
    return bp_[0x80 + (unit & 3) + ((unit & 4) << 3) + group * 4];
  };
  const u32 image = image_register(2), mode1 = image_register(3), mode2 = image_register(4);
  const unsigned format = image >> 20, original_width = (image & 1023) + 1,
                 original_height = ((image >> 10) & 1023) + 1;
  const auto tile = layout(format);
  if (mip > 10)
    throw VertexError("Texture mip level exceeds hardware range");
  u64 mip_offset = 0;
  for (unsigned level = 0; level < mip; ++level) {
    const auto mw = std::max(tile.width, original_width >> level),
               mh = std::max(tile.height, original_height >> level);
    mip_offset += u64(mw) * mh * tile.bytes / (tile.width * tile.height);
  }
  const bool manual = (mode1 & (1 << 21)) != 0;
  const u64 base = (manual ? u64(mode1 & 0x7fff) * 32 : u64(image_register(5)) << 5) + mip_offset;
  return {format,
          ((original_width - 1) >> mip) + 1,
          ((original_height - 1) >> mip) + 1,
          tile,
          manual,
          mip_offset,
          base,
          u64(mode2 & 0x7fff) * 32,
          image_register(6)};
}

void TextureSampler::decode(unsigned unit, unsigned mip, std::span<const u8> source,
                            std::span<u32> destination) const {
  if (mip > 10)
    throw VertexError("Texture mip level exceeds hardware range");
  const auto image = decode_layout(unit, mip);
  const unsigned width = image.width, height = image.height;
  if (destination.size() != std::size_t(width) * height)
    throw VertexError("Texture decode destination has the wrong extent");
  if (image.manual && mip)
    throw VertexError("Manually managed mip textures are not implemented");
  const auto &tile = image.tile;
  u64 block = 0;
  for (unsigned by = 0; by < height; by += tile.height)
    for (unsigned bx = 0; bx < width; bx += tile.width, block += tile.bytes)
      for (unsigned y = 0; y < std::min(tile.height, height - by); ++y)
        for (unsigned x = 0; x < std::min(tile.width, width - bx); ++x) {
          const auto color = decode_texel(image, block, y * tile.width + x, bx + x, by + y, source);
          u32 value = 0;
          for (u8 component : color)
            value = (value << 8) | component;
          destination[std::size_t(by + y) * width + bx + x] = value;
        }
}

Color TextureSampler::decode_texel(const DecodeLayout &image, u64 block, unsigned local, unsigned x,
                                   unsigned y, std::span<const u8> source) const {
  const unsigned format = image.format;
  const bool manual = image.manual;
  auto byte = [&](u64 offset, bool odd = false) -> u8 {
    const u64 address = (odd && manual ? image.odd_base : image.base) + offset;
    if (manual) {
      if (address >= tmem_.size())
        throw VertexError("Texture-memory fetch is out of bounds");
      return tmem_[static_cast<std::size_t>(address)];
    }
    if (address > 0xffffffff)
      throw VertexError("Texture address wraps");
    const auto index = image.mip_offset + offset;
    if (index >= source.size())
      throw VertexError("Texture decode source is out of bounds");
    return source[static_cast<std::size_t>(index)];
  };
  auto half = [&](u64 offset) {
    return static_cast<u16>((u16(byte(offset)) << 8) | byte(offset + 1));
  };
  auto palette = [&](unsigned index) -> Color {
    const auto config = image.palette;
    const std::size_t offset = (config & 1023) * 512 + index * 2;
    if (offset >= tmem_.size() || tmem_.size() - offset < 2)
      throw VertexError("Texture palette index is out of bounds");
    const u16 value = static_cast<u16>((u16(tmem_[offset]) << 8) | tmem_[offset + 1]);
    switch ((config >> 10) & 3) {
    case 0: {
      const auto intensity = static_cast<u8>(value);
      return {intensity, intensity, intensity, static_cast<u8>(value >> 8)};
    }
    case 1:
      return rgb565(value);
    case 2:
      return rgb5a3(value);
    default:
      throw VertexError("Reserved texture palette encoding");
    }
  };
  switch (format) {
  case 0:
  case 8: {
    const auto value = (byte(block + local / 2) >> ((local & 1) ? 0 : 4)) & 15;
    if (format == 8)
      return palette(value);
    const u8 intensity = expand(value, 4);
    return {intensity, intensity, intensity, intensity};
  }
  case 1:
  case 2:
  case 9: {
    const u8 value = byte(block + local);
    if (format == 9)
      return palette(value);
    const auto intensity = format == 1 ? value : expand(value & 15, 4),
               alpha = format == 1 ? value : expand(value >> 4, 4);
    return {intensity, intensity, intensity, alpha};
  }
  case 3: {
    const u16 value = half(block + local * 2);
    const u8 intensity = static_cast<u8>(value);
    return {intensity, intensity, intensity, static_cast<u8>(value >> 8)};
  }
  case 4:
    return rgb565(half(block + local * 2));
  case 5:
    return rgb5a3(half(block + local * 2));
  case 10:
    return palette(half(block + local * 2) & 0x3fff);
  case 6: {
    const auto offset = manual ? block / 2 : block;
    return {byte(offset + local * 2 + 1), byte(offset + local * 2 + (manual ? 0 : 32), true),
            byte(offset + local * 2 + (manual ? 1 : 33), true), byte(offset + local * 2)};
  }
  case 14: {
    const u64 offset = block + ((y & 4) ? 16 : 0) + ((x & 4) ? 8 : 0);
    const u16 a = half(offset), b = half(offset + 2);
    const auto ca = rgb565(a), cb = rgb565(b);
    const unsigned select = (byte(offset + 4 + (y & 3)) >> (6 - (x & 3) * 2)) & 3;
    if (select < 2)
      return select == 0 ? ca : cb;
    Color result{};
    for (unsigned channel = 0; channel < 3; ++channel) {
      const unsigned value = a > b ? (select == 2 ? ca[channel] * 5 + cb[channel] * 3
                                                  : ca[channel] * 3 + cb[channel] * 5)
                                   : ca[channel] + cb[channel];
      result[channel] = static_cast<u8>(a > b ? value >> 3 : value >> 1);
    }
    result[3] = (a <= b && select == 3) ? 0 : 255;
    return result;
  }
  default:
    throw VertexError("Texture decoder has no selected encoding");
  }
}

} // namespace cupid::gfx
