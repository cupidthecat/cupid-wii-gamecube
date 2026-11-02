// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/efb_copy.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace cupid::gfx {
namespace {
using Channels = std::array<unsigned, 4>;

[[noreturn]] void unsupported(const std::string &reason) {
  throw std::runtime_error("Unsupported EFB texture copy: " + reason);
}

unsigned intensity(const Channels &color) {
  return (4096 + 66 * color[0] + 129 * color[1] + 25 * color[2]) >> 8;
}

Channels sample(const EfbCopySource &source, unsigned format, unsigned scale, unsigned x,
                unsigned y) {
  Channels values{};
  for (unsigned dy = 0; dy < scale; ++dy) {
    for (unsigned dx = 0; dx < scale; ++dx) {
      if (format == 3) {
        const u32 depth = source.depth(x + dx, y + dy);
        for (unsigned channel = 0; channel < 3; ++channel)
          values[channel] += (depth >> (16 - channel * 8)) & 255;
      } else {
        const u32 color = source.color(x + dx, y + dy);
        for (unsigned channel = 0; channel < 4; ++channel) {
          unsigned component = (color >> (24 - channel * 8)) & 255;
          if (format == 1)
            component >>= 2;
          values[channel] += component;
        }
      }
    }
  }
  if (format == 1) {
    for (unsigned &component : values)
      component = scale == 2 ? component + (component >> 6) : (component << 2) | (component >> 4);
  } else {
    if (scale == 2)
      for (unsigned &component : values)
        component >>= 2;
    values[3] = 255;
  }
  return values;
}

void encode_pixel(std::span<u8> block, unsigned pixel, unsigned copy_format, const Channels &color,
                  bool convert_intensity, bool depth_half) {
  const unsigned red = convert_intensity ? intensity(color) : color[0];
  const unsigned green = color[1], blue = color[2], alpha = color[3];
  auto pair = [&](unsigned first, unsigned second) {
    block[pixel * 2] = static_cast<u8>(first);
    block[pixel * 2 + 1] = static_cast<u8>(second);
  };
  auto word = [&](unsigned value) { pair(value >> 8, value & 255); };
  switch (copy_format) {
  case 0: {
    const unsigned shift = (pixel & 1) ? 0 : 4;
    block[pixel / 2] |= static_cast<u8>((red >> 4) << shift);
    break;
  }
  case 1:
  case 8:
    block[pixel] = static_cast<u8>(red);
    break;
  case 2:
    block[pixel] = static_cast<u8>((alpha & 240) | (red >> 4));
    break;
  case 3:
    pair(alpha, red);
    break;
  case 4:
    word(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
    break;
  case 5:
    if (alpha >= 224)
      word(0x8000 | ((red >> 3) << 10) | ((green >> 3) << 5) | (blue >> 3));
    else
      word(((alpha >> 5) << 12) | ((red >> 4) << 8) | ((green >> 4) << 4) | (blue >> 4));
    break;
  case 6:
    pair(alpha, depth_half ? blue : red);
    block[32 + pixel * 2] = static_cast<u8>(green);
    block[33 + pixel * 2] = static_cast<u8>(depth_half ? red : blue);
    break;
  case 7:
    block[pixel] = static_cast<u8>(alpha);
    break;
  case 9:
    block[pixel] = static_cast<u8>(green);
    break;
  case 10:
    block[pixel] = static_cast<u8>(blue);
    break;
  case 11:
    pair(green, depth_half ? blue : red);
    break;
  case 12:
    pair(depth_half ? red : blue, green);
    break;
  default:
    unsupported("undefined tile format " + std::to_string(copy_format));
  }
}
} // namespace

EfbCopyPlan plan_efb_texture(std::span<const u32> bp) {
  if (bp.size() < 256)
    throw std::invalid_argument("EFB texture copy requires 256 BP registers");
  const u32 control = bp[0x52];
  if (control & (1 << 14))
    unsupported("XFB target passed to the texture encoder");
  const unsigned source_format = bp[0x43] & 7;
  if (source_format > 3)
    unsupported("source pixel format " + std::to_string(source_format));
  const unsigned encoded_format = (control >> 3) & 15;
  const unsigned copy_format = (encoded_format >> 1) | ((encoded_format & 1) << 3);
  if (copy_format > 12)
    unsupported("undefined tile format " + std::to_string(copy_format));
  const bool depth = source_format == 3;
  if (depth && (copy_format == 2 || copy_format == 3 || copy_format == 4 || copy_format == 5 ||
                copy_format == 7))
    unsupported("depth tile format " + std::to_string(copy_format));
  const unsigned scale = (control & (1 << 9)) ? 2 : 1;
  const unsigned block_width = copy_format <= 2 || (copy_format >= 7 && copy_format <= 10) ? 8 : 4;
  const unsigned block_height = copy_format == 0 ? 8 : 4;
  const unsigned block_bytes = copy_format == 6 ? 64 : 32;
  EfbCopyLayout layout;
  layout.destination = (bp[0x4b] & 0xffffff) << 5;
  layout.width = (bp[0x4a] & 1023) / scale + 1;
  layout.height = ((bp[0x4a] >> 10) & 1023) / scale + 1;
  const unsigned columns = (layout.width + block_width - 1) / block_width;
  layout.block_rows = (layout.height + block_height - 1) / block_height;
  layout.padded_width = columns * block_width;
  layout.padded_height = layout.block_rows * block_height;
  layout.row_bytes = columns * block_bytes;
  layout.stride = (bp[0x4d] & 0xffffff) << 5;
  const unsigned left = bp[0x49] & 1023, top = (bp[0x49] >> 10) & 1023;
  if (left + layout.padded_width * scale > 640 || top + layout.padded_height * scale > 528)
    unsupported("padded source footprint extends outside the 640 by 528 EFB");
  const u64 extent = u64(layout.block_rows - 1) * layout.stride + layout.row_bytes;
  if (u64(layout.destination) + extent > u64{1} << 32)
    unsupported("destination address wraps");
  EfbCopyPlan plan;
  plan.layout = layout;
  plan.source_format = source_format;
  plan.copy_format = copy_format;
  plan.scale = scale;
  plan.block_width = block_width;
  plan.block_height = block_height;
  plan.block_bytes = block_bytes;
  plan.columns = columns;
  plan.left = left;
  plan.top = top;
  plan.convert_intensity = !depth && (control & (1 << 15)) && (control & (1 << 16)) &&
                           (copy_format <= 3 || copy_format == 8);
  return plan;
}

EfbCopyLayout copy_efb_texture(Memory &memory, std::span<const u32> bp,
                               const EfbCopySource &source) {
  if (bp.size() < 256)
    throw std::invalid_argument("EFB texture copy requires 256 BP registers");
  const u32 control = bp[0x52];
  if (control & (1 << 14))
    unsupported("XFB target passed to the texture encoder");
  const unsigned source_format = bp[0x43] & 7;
  if (source_format > 3)
    unsupported("source pixel format " + std::to_string(source_format));
  const unsigned encoded_format = (control >> 3) & 15;
  const unsigned copy_format = (encoded_format >> 1) | ((encoded_format & 1) << 3);
  if (copy_format > 12)
    unsupported("undefined tile format " + std::to_string(copy_format));
  const bool depth = source_format == 3;
  if (depth && (copy_format == 2 || copy_format == 3 || copy_format == 4 || copy_format == 5 ||
                copy_format == 7))
    unsupported("depth tile format " + std::to_string(copy_format));
  if (!(depth ? bool(source.depth) : bool(source.color)))
    throw std::invalid_argument("EFB texture copy has no selected source callback");
  const auto plan = plan_efb_texture(bp);
  const auto &layout = plan.layout;
  const unsigned scale = plan.scale, block_width = plan.block_width;
  const unsigned block_height = plan.block_height, block_bytes = plan.block_bytes;
  const unsigned columns = plan.columns, left = plan.left, top = plan.top;
  const u64 extent = u64(layout.block_rows - 1) * layout.stride + layout.row_bytes;
  auto destination = memory.range(layout.destination, static_cast<std::size_t>(extent));
  std::vector<u8> encoded(static_cast<std::size_t>(layout.block_rows) * layout.row_bytes);
  const bool convert_intensity = !depth && (control & (1 << 15)) && (control & (1 << 16)) &&
                                 (copy_format <= 3 || copy_format == 8);
  for (unsigned row = 0; row < layout.block_rows; ++row) {
    for (unsigned column = 0; column < columns; ++column) {
      auto block = std::span<u8>(encoded).subspan(
          static_cast<std::size_t>(row) * layout.row_bytes + column * block_bytes, block_bytes);
      for (unsigned y = 0; y < block_height; ++y) {
        for (unsigned x = 0; x < block_width; ++x) {
          const auto color =
              sample(source, source_format, scale, left + (column * block_width + x) * scale,
                     top + (row * block_height + y) * scale);
          encode_pixel(block, y * block_width + x, copy_format, color, convert_intensity,
                       depth && scale == 2);
        }
      }
    }
  }
  for (unsigned row = 0; row < layout.block_rows; ++row)
    std::copy_n(encoded.begin() + static_cast<std::size_t>(row) * layout.row_bytes,
                layout.row_bytes,
                destination.begin() + static_cast<std::size_t>(row) * layout.stride);
  return layout;
}
} // namespace cupid::gfx
