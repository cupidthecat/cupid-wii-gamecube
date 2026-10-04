// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/gfx/vertex.hpp"

namespace cupid::gfx {
class TextureSampler {
public:
  TextureSampler(Memory &memory, std::span<const u32> bp, std::span<const u8> tmem);
  Color texel(unsigned unit, unsigned x, unsigned y, unsigned mip = 0) const;
  Color sample(unsigned unit, s32 s, s32 t, s32 lod, bool linear) const;

private:
  Memory &memory_;
  std::span<const u32> bp_;
  std::span<const u8> tmem_;
  u32 reg(unsigned unit, unsigned group) const;
};
} // namespace cupid::gfx
