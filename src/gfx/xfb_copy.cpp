// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/xfb_copy.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cupid::gfx {
namespace {
struct Yuv {
  int y, u, v;
};

[[noreturn]] void unsupported(const char *reason) {
  throw std::runtime_error(std::string("Unsupported XFB copy: ") + reason);
}

Yuv filtered_color(const std::array<u32, 3> &colors, const std::array<unsigned, 3> &weights,
                   float gamma_inverse) {
  std::array<int, 3> rgb{};
  for (unsigned channel = 0; channel < 3; ++channel) {
    unsigned sum = 0;
    for (unsigned tap = 0; tap < 3; ++tap)
      sum += ((colors[tap] >> (24 - channel * 8)) & 255) * weights[tap];
    const float normalized = static_cast<float>(std::min(255u, sum >> 6)) / 255.0f;
    rgb[channel] =
        static_cast<int>(std::clamp(std::pow(normalized, gamma_inverse) * 255.0f, 0.0f, 255.0f));
  }
  const auto rounded = [](int value) { return (value >> 8) + ((value >> 7) & 1); };
  return {rounded(66 * rgb[0] + 129 * rgb[1] + 25 * rgb[2]),
          rounded(-38 * rgb[0] - 74 * rgb[1] + 112 * rgb[2]),
          rounded(112 * rgb[0] - 94 * rgb[1] - 18 * rgb[2])};
}
} // namespace

XfbCopyPlan plan_efb_xfb(std::span<const u32> bp) {
  if (bp.size() < 256)
    throw std::invalid_argument("XFB copy requires 256 BP registers");
  const u32 control = bp[0x52];
  if (!(control & (1 << 14)))
    unsupported("texture target passed to the XFB encoder");
  if ((bp[0x43] & 7) > 3)
    unsupported("source pixel format");
  const unsigned left = bp[0x49] & 1023, top = (bp[0x49] >> 10) & 1023;
  if (left != 0)
    unsupported("nonzero horizontal origin has undefined intermediate indexing");
  if (top >= 528)
    unsupported("source rectangle does not touch the EFB");
  XfbCopyPlan plan;
  auto &layout = plan.layout;
  plan.top = top;
  layout.destination = (bp[0x4b] & 0xffffff) << 5;
  layout.width = std::min(640u, (bp[0x4a] & 1023) + 1);
  const unsigned raw_height_minus_one = (bp[0x4a] >> 10) & 1023;
  const unsigned source_height = std::min(raw_height_minus_one + 1, 528 - top);
  plan.source_height = source_height;
  if (layout.width & 1)
    unsupported("odd source width");
  layout.stride = (bp[0x4d] & 0xffffff) << 5;
  if (!layout.stride)
    unsupported("zero stride requires caller suppression");
  const float scale_register = static_cast<float>(bp[0x4e] & 0xffffff);
  if (scale_register == 0)
    unsupported("zero vertical scale");
  layout.y_scale = control & (1 << 10) ? 256.0f / scale_register : scale_register / 256.0f;
  const float encoded_height = static_cast<float>(source_height) * layout.y_scale;
  const float copied_height = 1.0f + static_cast<float>(raw_height_minus_one) * layout.y_scale;
  if (!std::isfinite(encoded_height) || !std::isfinite(copied_height) || encoded_height < 1 ||
      encoded_height >= static_cast<float>(std::numeric_limits<int>::max()) || copied_height < 1 ||
      copied_height >= static_cast<float>(std::numeric_limits<int>::max()))
    unsupported("vertical scale produces an undefined image extent");
  layout.encoded_height = static_cast<unsigned>(encoded_height);
  layout.height = static_cast<unsigned>(copied_height);
  layout.row_bytes = ((layout.width + 15) & ~15u) * 2;
  layout.copied_row_bytes = std::min(layout.row_bytes, layout.stride);
  const u64 extent = u64(layout.height - 1) * layout.stride + layout.copied_row_bytes;
  const u64 encoded_bytes = u64(layout.width) * layout.encoded_height * 2;
  if (extent > encoded_bytes)
    unsupported("staging readback would expose unwritten encoded bytes");
  const u64 last_row = (extent - 1) / (layout.width * 2);
  const double ratio = static_cast<double>(source_height) / layout.encoded_height;
  plan.row_ratio = ratio;
  if (std::round(static_cast<double>(last_row) * ratio) >= source_height)
    unsupported("nearest-row selection reaches unwritten source data");
  if (u64(layout.destination) + extent > u64{1} << 32)
    unsupported("destination address wraps");
  std::array<unsigned, 7> coefficients{};
  for (unsigned tap = 0; tap < coefficients.size(); ++tap)
    coefficients[tap] = (bp[tap < 4 ? 0x53 : 0x54] >> ((tap < 4 ? tap : tap - 4) * 6)) & 63;
  plan.weights = {coefficients[0] + coefficients[1],
                  coefficients[2] + coefficients[3] + coefficients[4],
                  coefficients[5] + coefficients[6]};
  plan.gamma = (control >> 7) & 3;
  return plan;
}

const std::array<std::array<u32, 256>, 4> &xfb_gamma_tables() {
  static const auto tables = [] {
    std::array<std::array<u32, 256>, 4> result{};
    for (unsigned gamma = 0; gamma < 4; ++gamma) {
      const float inverse = 1.0f / std::array<float, 4>{1.0f, 1.7f, 2.2f, 2.2f}[gamma];
      for (unsigned value = 0; value < 256; ++value) {
        const float normalized = static_cast<float>(value) / 255.0f;
        result[gamma][value] =
            static_cast<u32>(std::clamp(std::pow(normalized, inverse) * 255.0f, 0.0f, 255.0f));
      }
    }
    return result;
  }();
  return tables;
}

XfbCopyLayout copy_efb_xfb(Memory &memory, std::span<const u32> bp,
                           const std::function<u32(unsigned, unsigned)> &color) {
  if (bp.size() < 256 || !color)
    throw std::invalid_argument("XFB copy requires BP state and a color source");
  const auto plan = plan_efb_xfb(bp);
  const auto &layout = plan.layout;
  const u64 encoded_row_bytes = u64(layout.width) * 2;
  if (encoded_row_bytes == 0)
    throw std::logic_error("XFB planner produced an empty encoded row");
  const u32 control = bp[0x52];
  const unsigned top = plan.top, source_height = plan.source_height;
  const auto &weights = plan.weights;
  const double ratio = plan.row_ratio;
  const u64 extent = u64(layout.height - 1) * layout.stride + layout.copied_row_bytes;
  auto destination = memory.range(layout.destination, static_cast<std::size_t>(extent));
  const float gamma_inverse =
      1.0f / std::array<float, 4>{1.0f, 1.7f, 2.2f, 2.2f}[(control >> 7) & 3];
  std::vector<Yuv> scanline(layout.width);
  std::vector<u8> source(static_cast<std::size_t>(layout.width) * source_height * 2);
  for (unsigned row = 0; row < source_height; ++row) {
    const unsigned y = top + row;
    const unsigned previous = y ? std::max((control & 1) ? top : 0u, y - 1) : 0;
    const unsigned next = std::min((control & 2) ? top + source_height - 1 : 527, y + 1);
    for (unsigned x = 0; x < layout.width; ++x)
      scanline[x] =
          filtered_color({color(x, previous), color(x, y), color(x, next)}, weights, gamma_inverse);
    for (unsigned x = 0; x < layout.width; x += 2) {
      const auto &before = scanline[x ? x - 1 : 0];
      const auto &first = scanline[x], &second = scanline[x + 1];
      const auto offset = (static_cast<std::size_t>(row) * layout.width + x) * 2;
      source[offset] = static_cast<u8>(first.y + 16);
      source[offset + 1] = static_cast<u8>(128 + ((before.u + 2 * first.u + second.u) >> 2));
      source[offset + 2] = static_cast<u8>(second.y + 16);
      source[offset + 3] = static_cast<u8>(128 + ((before.v + 2 * first.v + second.v) >> 2));
    }
  }
  std::vector<u8> staged(static_cast<std::size_t>(layout.height) * layout.copied_row_bytes);
  for (unsigned row = 0; row < layout.height; ++row) {
    unsigned copied = 0;
    while (copied < layout.copied_row_bytes) {
      const u64 encoded_offset = u64(row) * layout.stride + copied;
      const u64 encoded_row = encoded_offset / encoded_row_bytes;
      const unsigned column = static_cast<unsigned>(encoded_offset % encoded_row_bytes);
      const unsigned count = std::min(layout.copied_row_bytes - copied,
                                      static_cast<unsigned>(encoded_row_bytes) - column);
      // A compact encoded row selects one nearest source row. Copy its
      // contiguous bytes together, including rows split by destination stride.
      const unsigned source_row =
          static_cast<unsigned>(std::round(static_cast<double>(encoded_row) * ratio));
      const auto source_offset = u64(source_row) * encoded_row_bytes + column;
      std::copy_n(source.begin() + static_cast<std::size_t>(source_offset), count,
                  staged.begin() + static_cast<std::size_t>(row) * layout.copied_row_bytes +
                      copied);
      copied += count;
    }
  }
  for (unsigned row = 0; row < layout.height; ++row)
    std::copy_n(staged.begin() + static_cast<std::size_t>(row) * layout.copied_row_bytes,
                layout.copied_row_bytes,
                destination.begin() + static_cast<std::size_t>(row) * layout.stride);
  return layout;
}
} // namespace cupid::gfx
