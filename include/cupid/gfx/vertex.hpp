// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/memory.hpp"
#include <array>
#include <span>
#include <stdexcept>

namespace cupid::gfx {
class VertexError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

using Vector3 = std::array<float, 3>;
using Vector4 = std::array<float, 4>;
using Color = std::array<u8, 4>;

struct DecodedVertex {
  Vector3 position{};
  std::array<Vector3, 3> normals{};
  std::array<Color, 2> colors{};
  std::array<std::array<float, 2>, 8> texcoords{};
  std::array<bool, 2> color_present{};
  std::array<bool, 8> texcoord_present{};
  u8 position_matrix = 0;
  std::array<u8, 8> texture_matrices{};
  unsigned normal_count = 0;
  bool skip = false;
};

struct TransformedVertex {
  Vector3 model_position{};
  Vector4 clip_position{};
  std::array<Vector3, 3> normals{};
  std::array<Color, 2> colors{};
  std::array<Vector3, 8> texcoords{};
};

// CP and XF state use their hardware register addresses as span indices.
// The caller owns primitive assembly and homogeneous clipping.
class VertexDecoder {
public:
  VertexDecoder(Memory &memory, std::span<const u32> cp, std::span<const u32> xf,
                unsigned vat_index);

  std::size_t vertex_size() const {
    return size_;
  }

  DecodedVertex decode(std::span<const u8> bytes) const;
  TransformedVertex transform(const DecodedVertex &vertex) const;
  Vector3 screen_position(const Vector4 &clip) const;

private:
  struct Attribute {
    unsigned mode = 0, format = 0, components = 0, fraction = 0;
  };

  Memory &memory_;
  std::span<const u32> cp_, xf_;
  u32 descriptor_low_ = 0;
  Attribute position_{}, normal_{};
  std::array<Attribute, 2> colors_{};
  std::array<Attribute, 8> textures_{};
  bool normal_index3_ = false;
  std::size_t size_ = 0;
};
} // namespace cupid::gfx
