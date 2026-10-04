// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/gfx/texture.hpp"
#include <optional>

namespace cupid::gfx {
using TevColor = std::array<s32, 4>;
using TevRegisters = std::array<TevColor, 4>;

struct FragmentInputs {
  std::array<Color, 2> colors{};
  std::array<std::array<float, 2>, 8> uv{}, dx{}, dy{};
  unsigned x = 0;
  float viewport_width = 1;
};

struct FragmentTextureState {
  Color texture, raw;
  std::array<Color, 4> indirect;
  std::array<s32, 2> coordinates;
  bool operator==(const FragmentTextureState &) const = default;
};

class FragmentShader {
public:
  FragmentShader(Memory &memory, std::span<const u32> bp, const TevRegisters &colors,
                 const TevRegisters &konst, std::span<const u8> tmem);
  void validate() const;
  std::optional<Color> shade(const FragmentInputs &input, u32 *depth = nullptr);

  void restore_texture_state(Color texture, Color raw) {
    texture_ = texture;
    raw_texture_ = raw;
  }

  void restore_indirect_coordinates(std::array<s32, 2> coordinates) {
    texture_coordinates_ = coordinates;
  }

  void restore_indirect_texture(unsigned stage, Color value) {
    indirect_textures_[stage] = value;
  }

  FragmentTextureState texture_state() const {
    return {texture_, raw_texture_, indirect_textures_, texture_coordinates_};
  }

private:
  TextureSampler sampler_;
  std::span<const u32> bp_;
  const TevRegisters &colors_;
  const TevRegisters &konst_;
  Color texture_{};
  Color raw_texture_{};
  std::array<Color, 4> indirect_textures_{};
  std::array<s32, 2> texture_coordinates_{};
};

bool compare(unsigned mode, u32 value, u32 reference);
// Quantized fog interpolation weight shared with hardware bucket construction.
u32 fog_factor(float distance, unsigned type);
} // namespace cupid::gfx
