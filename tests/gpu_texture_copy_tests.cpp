// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/efb_copy.hpp"
#include "cupid/gfx/hardware_renderer.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <string_view>
#include <vector>

using namespace cupid;

namespace {
u32 expanded(u32 value) {
  u32 result = 0;
  for (unsigned channel = 0; channel < 4; ++channel) {
    const u32 component = (value >> (18 - channel * 6)) & 63;
    result = (result << 8) | (component << 2) | (component >> 4);
  }
  return result;
}

void checks() {
  Memory actual(Console::GameCube), expected(Console::GameCube);
  std::vector<u8> texture_memory(1024 * 1024);
  gfx::HardwareRenderer renderer(actual, texture_memory);
  std::cout << "Device: " << renderer.device() << '\n';
  std::vector<u32> colors(640 * 528), depths(colors.size()), observed_colors(colors.size()),
      observed_depths(depths.size());
  for (unsigned pixel = 0; pixel < colors.size(); ++pixel) {
    colors[pixel] = (pixel * 1640531527u + 0x3195abd1u) & 0xffffff;
    depths[pixel] = (pixel * 2654435761u + 0xd5e731u) & 0xffffff;
  }
  unsigned count = 0;
  for (unsigned source_format = 0; source_format < 4; ++source_format)
    for (unsigned format = 0; format <= 12; ++format) {
      if (source_format == 3 &&
          (format == 2 || format == 3 || format == 4 || format == 5 || format == 7))
        continue;
      for (bool half : {false, true})
        for (unsigned intensity_flags = 0; intensity_flags < 4; ++intensity_flags)
          for (unsigned shape = 0; shape < 3; ++shape)
            for (unsigned stride_kind = 0; stride_kind < 3; ++stride_kind) {
              std::array<u32, 256> bp{};
              bp[0x43] = source_format;
              const unsigned width = shape == 0 ? 13 : shape == 1 ? 1 : 32;
              const unsigned height = shape == 0 ? 7 : shape == 1 ? 1 : 16;
              bp[0x49] = 7 | (9 << 10);
              bp[0x4a] = (width - 1) | ((height - 1) << 10);
              bp[0x4b] = 0x20000 >> 5;
              bp[0x52] = ((((format << 1) & 15) | (format >> 3)) << 3) | (half ? 1 << 9 : 0) |
                         (intensity_flags << 15);
              auto plan = gfx::plan_efb_texture(bp);
              if (shape == 2)
                bp[0x49] = (640 - plan.layout.padded_width * plan.scale) |
                           ((528 - plan.layout.padded_height * plan.scale) << 10);
              bp[0x4d] = stride_kind == 0   ? 0
                         : stride_kind == 1 ? 1
                                            : (plan.layout.row_bytes + 64) >> 5;
              const unsigned mask = count & 7;
              bp[0x40] = (mask & 4) ? 16 : 0;
              bp[0x41] = ((mask & 1) ? 8 : 0) | ((mask & 2) ? 16 : 0);
              bp[0x4f] = 0x71f4;
              bp[0x50] = 0x38d2;
              bp[0x51] = 0x815392;
              // Every profile checks copy-before-clear, while masks rotate
              // independently of format and shape across the complete matrix.
              bp[0x52] |= 1 << 11;
              plan = gfx::plan_efb_texture(bp);
              const auto &layout = plan.layout;
              const auto extent =
                  std::size_t(layout.block_rows - 1) * layout.stride + layout.row_bytes;
              auto actual_bytes = actual.range(0x20000 - 32, extent + 64);
              auto expected_bytes = expected.range(0x20000 - 32, extent + 64);
              std::fill(actual_bytes.begin(), actual_bytes.end(), 0xa5);
              std::fill(expected_bytes.begin(), expected_bytes.end(), 0xa5);
              gfx::copy_efb_texture(expected, bp,
                                    {[&](unsigned x, unsigned y) {
                                       const u32 raw = colors[y * 640 + x];
                                       return source_format == 1 ? expanded(raw) : (raw << 8) | 255;
                                     },
                                     [&](unsigned x, unsigned y) { return depths[y * 640 + x]; }});
              renderer.upload(colors, depths);
              CHECK(renderer.copy_texture(bp));
              CHECK(std::equal(actual_bytes.begin(), actual_bytes.end(), expected_bytes.begin()));
              const auto stats = renderer.synchronize(observed_colors, observed_depths);
              CHECK(stats.fragments == 0);
              const unsigned left = plan.left, top = plan.top;
              for (unsigned y = 0; y < 528; ++y)
                for (unsigned x = 0; x < 640; ++x) {
                  const auto offset = y * 640 + x;
                  const bool clear = x >= left && x < left + width && y >= top && y < top + height;
                  u32 color = colors[offset];
                  if (clear) {
                    if (source_format == 1) {
                      const u32 packed =
                          ((0xf438d271u >> 26) << 18) | (((0xf438d271u >> 18) & 63) << 12) |
                          (((0xf438d271u >> 10) & 63) << 6) | ((0xf438d271u >> 2) & 63);
                      const u32 color_mask = ((mask & 1) ? 0xffffc0u : 0) | ((mask & 2) ? 63u : 0);
                      color = (color & ~color_mask) | (packed & color_mask);
                    } else if (mask & 1)
                      color = 0xf438d2;
                  }
                  CHECK(observed_colors[offset] == color);
                  CHECK(observed_depths[offset] ==
                        (clear && (mask & 4) ? bp[0x51] : depths[offset]));
                }
              ++count;
            }
    }
  std::cout << "Verified " << count << " complete texture/RAM/clear profiles\n";
}
} // namespace

int main(int argc, char **argv) {
  const bool required = argc == 2 && std::string_view(argv[1]) == "--require-device";
  try {
    if (argc > 1 && !required)
      throw std::invalid_argument("Expected no arguments or --require-device");
    checks();
    std::cout << "All checks passed\n";
    return 0;
  } catch (const gfx::HardwareUnavailable &error) {
    if (!required) {
      std::cout << "Hardware checks skipped: " << error.what() << '\n';
      return 77;
    }
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
