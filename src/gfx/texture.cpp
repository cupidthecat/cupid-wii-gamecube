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

int wrapped(int coordinate, unsigned size, unsigned mode) {
  if (mode == 1)
    return coordinate & static_cast<int>(size - 1);
  if (mode == 2) {
    if (coordinate & static_cast<int>(size))
      coordinate = ~coordinate;
    return coordinate & static_cast<int>(size - 1);
  }
  return std::clamp(coordinate, 0, static_cast<int>(size) - 1);
}
} // namespace

TextureSampler::TextureSampler(Memory &memory, std::span<const u32> bp, std::span<const u8> tmem)
    : memory_(memory), bp_(bp), tmem_(tmem) {
  if (bp.size() < 256)
    throw VertexError("Texture register state is truncated");
}

u32 TextureSampler::reg(unsigned unit, unsigned group) const {
  if (unit >= 8)
    throw VertexError("Texture unit exceeds hardware range");
  return bp_[0x80 + (unit & 3) + ((unit & 4) << 3) + group * 4];
}

Color TextureSampler::texel(unsigned unit, unsigned x, unsigned y, unsigned mip) const {
  const u32 image = reg(unit, 2), mode1 = reg(unit, 3), mode2 = reg(unit, 4);
  const unsigned format = image >> 20, original_width = (image & 1023) + 1,
                 original_height = ((image >> 10) & 1023) + 1;
  const auto tile = layout(format);
  if (mip > 10)
    throw VertexError("Texture mip level exceeds hardware range");
  const unsigned w = ((original_width - 1) >> mip) + 1, h = ((original_height - 1) >> mip) + 1;
  if (x >= w || y >= h)
    throw VertexError("Texture coordinates are outside the selected mip");
  const bool manual = (mode1 & (1 << 21)) != 0;
  if (manual && mip)
    throw VertexError("Manually managed mip textures are not implemented");
  u64 mip_offset = 0;
  for (unsigned level = 0; level < mip; ++level) {
    const auto mw = std::max(tile.width, original_width >> level),
               mh = std::max(tile.height, original_height >> level);
    mip_offset += u64(mw) * mh * tile.bytes / (tile.width * tile.height);
  }
  const u64 block =
      (u64(y / tile.height) * ((w + tile.width - 1) / tile.width) + x / tile.width) * tile.bytes;
  const unsigned local = (y % tile.height) * tile.width + x % tile.width;
  const u64 base = (manual ? u64(mode1 & 0x7fff) * 32 : u64(reg(unit, 5)) << 5) + mip_offset;
  auto byte = [&](u64 offset, bool odd = false) -> u8 {
    const u64 address = (odd && manual ? u64(mode2 & 0x7fff) * 32 : base) + offset;
    if (manual) {
      if (address >= tmem_.size())
        throw VertexError("Texture-memory fetch is out of bounds");
      return tmem_[static_cast<std::size_t>(address)];
    }
    if (address > 0xffffffff)
      throw VertexError("Texture address wraps");
    return memory_.read8(static_cast<u32>(address));
  };
  auto half = [&](u64 offset) {
    return static_cast<u16>((u16(byte(offset)) << 8) | byte(offset + 1));
  };
  auto palette = [&](unsigned index) -> Color {
    const auto config = reg(unit, 6);
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

Color TextureSampler::sample(unsigned unit, s32 s, s32 t, s32 lod, bool linear) const {
  const u32 mode = reg(unit, 0), image = reg(unit, 2);
  const unsigned mip_mode = (mode >> 5) & 3;
  unsigned mip = lod > 0 && mip_mode ? static_cast<unsigned>(lod) >> 4 : 0;
  if (mip_mode == 1 && (lod & 15) >= 8 && lod > 0)
    ++mip;
  const bool mip_linear = mip_mode == 2 && lod > 0 && (lod & 15) != 0;
  auto sample_mip = [&](unsigned level) {
    if (level > 10)
      throw VertexError("Texture sampler mip exceeds hardware range");
    const unsigned w = ((image & 1023) >> level) + 1, h = (((image >> 10) & 1023) >> level) + 1;
    const s64 ss = (s64(s) >> level) - (linear ? 64 : 0),
              tt = (s64(t) >> level) - (linear ? 64 : 0);
    const int px = static_cast<int>(ss >> 7), py = static_cast<int>(tt >> 7);
    auto fetch = [&](int x, int y) {
      return texel(unit, static_cast<unsigned>(wrapped(x, w, mode & 3)),
                   static_cast<unsigned>(wrapped(y, h, (mode >> 2) & 3)), level);
    };
    if (!linear)
      return fetch(px, py);
    const std::array<Color, 4> colors{fetch(px, py), fetch(px + 1, py), fetch(px, py + 1),
                                      fetch(px + 1, py + 1)};
    const unsigned fx = static_cast<u32>(ss) & 127, fy = static_cast<u32>(tt) & 127;
    const std::array<unsigned, 4> weights{(128 - fx) * (128 - fy), fx * (128 - fy), (128 - fx) * fy,
                                          fx * fy};
    Color result{};
    for (unsigned channel = 0; channel < 4; ++channel) {
      unsigned sum = 0;
      for (unsigned tap = 0; tap < 4; ++tap)
        sum += colors[tap][channel] * weights[tap];
      result[channel] = static_cast<u8>(sum >> 14);
    }
    return result;
  };
  auto result = sample_mip(mip);
  if (mip_linear) {
    const auto other = sample_mip(mip + 1);
    const auto fraction = static_cast<unsigned>(lod) & 15;
    for (unsigned i = 0; i < 4; ++i)
      result[i] = static_cast<u8>((result[i] * (16 - fraction) + other[i] * fraction) >> 4);
  }
  return result;
}
} // namespace cupid::gfx
