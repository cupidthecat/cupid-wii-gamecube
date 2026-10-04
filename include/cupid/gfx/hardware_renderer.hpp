// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/gfx/rasterizer.hpp"
#include <memory>
#include <stdexcept>
#include <string>

namespace cupid::gfx {
class HardwareUnavailable : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct HardwareStats {
  u64 dispatches = 0, fragments = 0, fallback_rectangles = 0;
  u32 left = 0xffffffff, right = 0, top = 0xffffffff, bottom = 0;
};

// A hidden host context executes EFB coverage and fragment operations independently
// of the game-output window. CPU visibility is restored before EFB reads and copies.
class HardwareRenderer {
public:
  HardwareRenderer(Memory &memory, std::span<const u8> texture_memory);
  ~HardwareRenderer();
  HardwareRenderer(const HardwareRenderer &) = delete;
  HardwareRenderer &operator=(const HardwareRenderer &) = delete;
  bool begin(std::span<const u32> bp, const TevRegisters &colors, const TevRegisters &konst);
  bool draw(const RasterWork &work);
  void upload(std::span<const u32> colors, std::span<const u32> depths);
  HardwareStats synchronize(std::span<u32> colors, std::span<u32> depths,
                            FragmentShader *shader = nullptr);
  const std::string &device() const;
  const std::string &fallback_reason() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cupid::gfx
