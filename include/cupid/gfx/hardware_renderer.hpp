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
  // Snapshot bytes count complete comparison spans, not host memory traffic.
  u64 texture_decodes = 0, texture_uploads = 0, texture_snapshot_bytes = 0;
  // Host-to-device texture bytes include the packed mip-offset table.
  u64 texture_upload_bytes = 0;
  u64 xfb_copies = 0, xfb_readback_bytes = 0, efb_readbacks = 0, efb_readback_bytes = 0;
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
  // Encodes RAM-visible XFB bytes before an optional GPU EFB clear.
  bool copy_xfb(std::span<const u32> bp);
  bool copy_texture(std::span<const u32> bp);
  // Retire earlier bounds before replacing one BP bounding-box pair.
  void reset_bounding_box(bool vertical);
  HardwareStats synchronize(std::span<u32> colors, std::span<u32> depths,
                            FragmentShader *shader = nullptr);
  const std::string &device() const;
  const std::string &fallback_reason() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cupid::gfx
