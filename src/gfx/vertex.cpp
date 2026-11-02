// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/vertex.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <string>

namespace cupid::gfx {
namespace {
unsigned scalar_size(unsigned format) {
  return format < 2 ? 1 : format < 4 ? 2 : 4;
}

unsigned color_size(unsigned format) {
  constexpr std::array<unsigned, 6> sizes{2, 3, 4, 2, 3, 4};
  if (format >= sizes.size())
    throw VertexError("Reserved vertex color format " + std::to_string(format));
  return sizes[format];
}

class Reader {
public:
  explicit Reader(std::span<const u8> data) : data_(data) {
  }

  u32 read(unsigned bytes) {
    const auto part = take(bytes);
    u32 result = 0;
    for (u8 byte : part)
      result = (result << 8) | byte;
    return result;
  }

  std::span<const u8> take(std::size_t bytes) {
    if (bytes > data_.size())
      throw VertexError("Incomplete vertex attribute");
    const auto result = data_.first(bytes);
    data_ = data_.subspan(bytes);
    return result;
  }

private:
  std::span<const u8> data_;
};

float scalar(Reader &reader, unsigned format, unsigned fraction) {
  const u32 bits = reader.read(scalar_size(format));
  if (format >= 4)
    return std::bit_cast<float>(bits);
  float value;
  if (format == 0)
    value = static_cast<float>(bits);
  else if (format == 1)
    value = static_cast<float>(std::bit_cast<s8>(static_cast<u8>(bits)));
  else if (format == 2)
    value = static_cast<float>(bits);
  else
    value = static_cast<float>(std::bit_cast<s16>(static_cast<u16>(bits)));
  return value * std::ldexp(1.0f, -static_cast<int>(fraction));
}

Color unpack_color(Reader &reader, unsigned format) {
  const u32 value = reader.read(color_size(format));
  const auto expand = [](unsigned bits, unsigned count) {
    const unsigned high = bits << (8 - count);
    return static_cast<u8>(high | (bits >> (2 * count - 8)));
  };
  switch (format) {
  case 0:
    return {expand((value >> 11) & 31, 5), expand((value >> 5) & 63, 6), expand(value & 31, 5),
            255};
  case 1:
    return {static_cast<u8>(value >> 16), static_cast<u8>(value >> 8), static_cast<u8>(value), 255};
  case 2:
    return {static_cast<u8>(value >> 24), static_cast<u8>(value >> 16), static_cast<u8>(value >> 8),
            255};
  case 3:
    return {expand((value >> 12) & 15, 4), expand((value >> 8) & 15, 4),
            expand((value >> 4) & 15, 4), expand(value & 15, 4)};
  case 4:
    return {expand((value >> 18) & 63, 6), expand((value >> 12) & 63, 6),
            expand((value >> 6) & 63, 6), expand(value & 63, 6)};
  case 5:
    return {static_cast<u8>(value >> 24), static_cast<u8>(value >> 16), static_cast<u8>(value >> 8),
            static_cast<u8>(value)};
  default:
    throw VertexError("Reserved vertex color format");
  }
}

Vector3 normalized(Vector3 value) {
  const float length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
  for (auto &component : value)
    component /= length;
  return value;
}

float dot(const Vector3 &a, const Vector3 &b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Color register_color(u32 value) {
  return {static_cast<u8>(value >> 24), static_cast<u8>(value >> 16), static_cast<u8>(value >> 8),
          static_cast<u8>(value)};
}
} // namespace

VertexDecoder::VertexDecoder(Memory &memory, std::span<const u32> cp, std::span<const u32> xf,
                             unsigned vat_index)
    : memory_(memory), cp_(cp), xf_(xf) {
  if (cp.size() < 0xc0 || xf.size() < 0x1058 || vat_index >= 8)
    throw VertexError("Vertex decoder requires complete CP/XF state and a VAT index below eight");
  descriptor_low_ = cp[0x50];
  const u32 a = cp[0x70 + vat_index], b = cp[0x80 + vat_index], c = cp[0x90 + vat_index];
  const bool dequantize = (a >> 30) & 1;
  position_ = {(descriptor_low_ >> 9) & 3, (a >> 1) & 7, 2 + (a & 1), (a >> 4) & 31};
  if (!position_.mode)
    throw VertexError("GX primitive has no position attribute");
  normal_ = {(descriptor_low_ >> 11) & 3, (a >> 10) & 7, (a & 0x200) ? 9u : 3u, 0};
  normal_index3_ = (a >> 31) && normal_.components == 9;
  constexpr std::array<unsigned, 8> normal_fraction{7, 6, 15, 14, 0, 0, 0, 0};
  normal_.fraction = normal_fraction[normal_.format];
  for (unsigned i = 0; i < 2; ++i)
    colors_[i] = {(descriptor_low_ >> (13 + 2 * i)) & 3, (a >> (14 + 4 * i)) & 7, 4, 0};
  const std::array<u32, 8> fields{
      a >> 21, b, b >> 9, b >> 18, ((b >> 27) & 15) | ((c & 31) << 4), c >> 5, c >> 14, c >> 23};
  for (unsigned i = 0; i < 8; ++i)
    textures_[i] = {(cp[0x60] >> (2 * i)) & 3, (fields[i] >> 1) & 7, 1 + (fields[i] & 1),
                    (fields[i] >> 4) & 31};
  if (!dequantize) {
    position_.fraction = 0;
    for (auto &texture : textures_)
      texture.fraction = 0;
  }
  size_ = std::popcount(descriptor_low_ & 0x1ff);
  const auto add = [&](const Attribute &attribute, bool color = false, bool indices3 = false) {
    if (attribute.mode == 1)
      size_ += color ? color_size(attribute.format)
                     : scalar_size(attribute.format) * attribute.components;
    else if (attribute.mode >= 2) {
      if (color)
        (void)color_size(attribute.format);
      size_ += (attribute.mode == 2 ? 1 : 2) * (indices3 ? 3 : 1);
    }
  };
  add(position_);
  add(normal_, false, normal_index3_);
  for (const auto &color : colors_)
    add(color, true);
  for (const auto &texture : textures_)
    add(texture);
}

DecodedVertex VertexDecoder::decode(std::span<const u8> bytes) const {
  if (bytes.size() < size_)
    throw VertexError("Incomplete GX vertex: expected " + std::to_string(size_) + " bytes");
  Reader input(bytes.first(size_));
  DecodedVertex result;
  result.position_matrix = static_cast<u8>(xf_[0x1018] & 63);
  for (unsigned i = 0; i < 8; ++i) {
    const unsigned index = i < 4 ? 0x1018 : 0x1019;
    const unsigned shift = i < 4 ? 6 + i * 6 : (i - 4) * 6;
    result.texture_matrices[i] = static_cast<u8>((xf_[index] >> shift) & 63);
  }
  if (descriptor_low_ & 1)
    result.position_matrix = static_cast<u8>(input.read(1) & 63);
  for (unsigned i = 0; i < 8; ++i)
    if (descriptor_low_ & (2u << i))
      result.texture_matrices[i] = static_cast<u8>(input.read(1) & 63);
  const auto source = [&](const Attribute &attribute, unsigned array, unsigned components,
                          unsigned component_offset = 0) -> std::span<const u8> {
    const unsigned count = components * scalar_size(attribute.format);
    if (attribute.mode == 1)
      return input.take(count);
    const u32 index = input.read(attribute.mode == 2 ? 1 : 2);
    if (array == 0 && index == (attribute.mode == 2 ? 255u : 65535u)) {
      result.skip = true;
      return {};
    }
    const u64 address = u64(cp_[0xa0 + array]) + u64(index) * (cp_[0xb0 + array] & 255) +
                        component_offset * scalar_size(attribute.format);
    if (address > std::numeric_limits<u32>::max())
      throw VertexError("Indexed vertex array address exceeds the physical address space");
    return memory_.range(static_cast<u32>(address), count);
  };
  const auto read_vector = [&](const Attribute &attribute, unsigned array, std::span<float> output,
                               unsigned component_offset = 0) {
    Reader data(source(attribute, array, static_cast<unsigned>(output.size()), component_offset));
    if (result.skip)
      return;
    for (float &component : output)
      component = scalar(data, attribute.format, attribute.fraction);
  };
  read_vector(position_, 0, std::span<float>(result.position).first(position_.components));
  if (result.skip)
    return result;
  if (normal_.mode) {
    result.normal_count = normal_.components / 3;
    if (normal_.mode >= 2 && normal_index3_) {
      for (unsigned i = 0; i < 3; ++i)
        read_vector(normal_, 1, result.normals[i], i * 3);
    } else {
      Reader data(source(normal_, 1, normal_.components));
      for (unsigned i = 0; i < result.normal_count; ++i)
        for (auto &component : result.normals[i])
          component = scalar(data, normal_.format, normal_.fraction);
    }
  }
  for (unsigned i = 0; i < 2; ++i) {
    const auto &attribute = colors_[i];
    result.color_present[i] = attribute.mode != 0;
    if (!attribute.mode)
      continue;
    std::span<const u8> data;
    const unsigned count = color_size(attribute.format);
    if (attribute.mode == 1)
      data = input.take(count);
    else {
      const u32 index = input.read(attribute.mode == 2 ? 1 : 2);
      const u64 address = u64(cp_[0xa2 + i]) + u64(index) * (cp_[0xb2 + i] & 255);
      if (address > std::numeric_limits<u32>::max())
        throw VertexError("Indexed color array address exceeds the physical address space");
      data = memory_.range(static_cast<u32>(address), count);
    }
    Reader color(data);
    result.colors[i] = unpack_color(color, attribute.format);
  }
  for (unsigned i = 0; i < 8; ++i) {
    const auto &attribute = textures_[i];
    result.texcoord_present[i] = attribute.mode != 0;
    if (attribute.mode)
      read_vector(attribute, 4 + i,
                  std::span<float>(result.texcoords[i]).first(attribute.components));
  }
  return result;
}

TransformedVertex VertexDecoder::transform(const DecodedVertex &vertex) const {
  if (vertex.skip)
    throw VertexError("A skipped indexed vertex cannot enter the transform unit");
  const auto value = [&](unsigned address) { return std::bit_cast<float>(xf_[address]); };
  const auto matrix = [&](const Vector3 &input, unsigned base, bool translation,
                          unsigned rows = 3) {
    Vector3 output{0, 0, 1};
    const unsigned stride = translation ? 4 : 3;
    for (unsigned row = 0; row < rows; ++row) {
      const unsigned address = base + row * stride;
      output[row] =
          value(address) * input[0] + value(address + 1) * input[1] + value(address + 2) * input[2];
      if (translation)
        output[row] += value(address + 3);
    }
    return output;
  };
  TransformedVertex result;
  result.model_position = matrix(vertex.position, vertex.position_matrix * 4, true);
  const auto &p = result.model_position;
  if (xf_[0x1026] == 0) {
    result.clip_position = {value(0x1020) * p[0] + value(0x1021) * p[2],
                            value(0x1022) * p[1] + value(0x1023) * p[2],
                            (value(0x1024) * p[2] + value(0x1025)) * (1.0f - 1.0e-7f), -p[2]};
  } else if (xf_[0x1026] == 1) {
    result.clip_position = {value(0x1020) * p[0] + value(0x1021),
                            value(0x1022) * p[1] + value(0x1023),
                            value(0x1024) * p[2] + value(0x1025), 1};
  } else
    throw VertexError("Reserved XF projection type");
  for (unsigned i = 0; i < vertex.normal_count; ++i)
    result.normals[i] = matrix(vertex.normals[i], 0x400 + (vertex.position_matrix & 31) * 3, false);
  if (vertex.normal_count)
    result.normals[0] = normalized(result.normals[0]);
  const auto light_vector = [&](unsigned light, unsigned offset) {
    const unsigned address = 0x600 + light * 16 + offset;
    return Vector3{value(address), value(address + 1), value(address + 2)};
  };
  const auto light_factor = [&](unsigned light, u32 control) {
    const Vector3 light_position = light_vector(light, 10);
    Vector3 direction;
    for (unsigned i = 0; i < 3; ++i)
      direction[i] = light_position[i] - result.model_position[i];
    const unsigned attenuation = (control >> 9) & 3, diffuse = (control >> 7) & 3;
    const Vector3 &normal = result.normals[0];
    float factor = 1;
    if (attenuation == 0 || attenuation == 2) {
      direction = normalized(direction);
      if (direction == Vector3{})
        direction = normal;
    } else {
      const Vector3 cosine = light_vector(light, 4);
      Vector3 distance = light_vector(light, 7), polynomial;
      float numerator;
      if (attenuation == 1) {
        direction = normalized(direction);
        const float angle =
            dot(direction, normal) >= 0 ? std::max(0.0f, dot(light_vector(light, 13), normal)) : 0;
        polynomial = {1, angle, angle * angle};
        if (diffuse)
          distance = normalized(distance);
        numerator = std::max(0.0f, dot(polynomial, cosine));
      } else {
        const float squared_length = dot(direction, direction);
        const float length = std::sqrt(squared_length);
        for (float &component : direction)
          component /= length;
        const float angle = std::max(0.0f, dot(direction, light_vector(light, 13)));
        polynomial = {1, length, squared_length};
        numerator = std::max(0.0f, cosine[0] + cosine[1] * angle + cosine[2] * angle * angle);
      }
      const float denominator = dot(polynomial, distance);
      factor = denominator == 0 ? (numerator > 0 ? 1.0f : 0.0f) : numerator / denominator;
    }
    if (diffuse == 1)
      factor *= dot(direction, normal);
    else if (diffuse == 2)
      factor *= std::max(0.0f, dot(direction, normal));
    else if (diffuse == 3)
      throw VertexError("Reserved XF diffuse attenuation function");
    return factor;
  };
  for (unsigned i = 0; i < 2; ++i) {
    const u32 color_control = xf_[0x100e + i], alpha_control = xf_[0x1010 + i];
    const Color material = register_color(xf_[0x100c + i]);
    result.colors[i] = color_control & 1 ? vertex.colors[i] : material;
    result.colors[i][3] = alpha_control & 1 ? vertex.colors[i][3] : material[3];
    const Color ambient = register_color(xf_[0x100a + i]);
    for (unsigned group = 0; group < 2; ++group) {
      const u32 control = group == 0 ? color_control : alpha_control;
      if (!(control & 2))
        continue;
      const Color &source_ambient = control & 64 ? vertex.colors[i] : ambient;
      Vector4 accumulator;
      for (unsigned component = 0; component < 4; ++component)
        accumulator[component] = source_ambient[component];
      const unsigned mask = ((control >> 2) & 15) | (((control >> 11) & 15) << 4);
      const unsigned first = group == 0 ? 0 : 3, end = group == 0 ? 3 : 4;
      for (unsigned light = 0; light < 8; ++light) {
        if (!(mask & (1u << light)))
          continue;
        const float factor = light_factor(light, control);
        const Color light_color = register_color(xf_[0x603 + light * 16]);
        for (unsigned component = first; component < end; ++component)
          accumulator[component] += light_color[component] * factor;
      }
      for (unsigned component = first; component < end; ++component) {
        if (!std::isfinite(accumulator[component]))
          throw VertexError("Non-finite XF lighting accumulation has no defined integer result");
        const unsigned intensity =
            static_cast<unsigned>(std::clamp(accumulator[component], 0.0f, 255.0f));
        result.colors[i][component] =
            static_cast<u8>((result.colors[i][component] * (intensity + (intensity >> 7))) >> 8);
      }
    }
  }
  const unsigned textures = xf_[0x103f] & 15;
  if (textures > 8)
    throw VertexError("XF texture generator count exceeds eight");
  for (unsigned i = 0; i < textures; ++i) {
    const u32 control = xf_[0x1040 + i];
    const unsigned type = (control >> 4) & 7;
    if (type == 2 || type == 3) {
      if (control & 4)
        throw VertexError("Color texture generation requires the AB11 input form");
      const auto &color = result.colors[type - 2];
      result.texcoords[i] = {color[0] / 255.0f, color[1] / 255.0f, 1};
      continue;
    }
    if (type == 1) {
      const unsigned source = (control >> 12) & 7, light = (control >> 15) & 7;
      Vector3 direction = light_vector(light, 10);
      for (unsigned component = 0; component < 3; ++component)
        direction[component] -= result.model_position[component];
      direction = normalized(direction);
      result.texcoords[i] = result.texcoords[source];
      result.texcoords[i][0] += dot(direction, result.normals[1]);
      result.texcoords[i][1] += dot(direction, result.normals[2]);
      continue;
    }
    if (type != 0)
      throw VertexError("Reserved XF texture generation type");
    Vector3 input;
    const unsigned source = (control >> 7) & 31;
    if (source == 0)
      input = vertex.position;
    else if (source == 1)
      input = vertex.normals[0];
    else if (source == 3 || source == 4)
      input = vertex.normals[source - 2];
    else if (source >= 5 && source <= 12) {
      const auto &coordinate = vertex.texcoords[source - 5];
      input = {coordinate[0], coordinate[1], 1};
    } else
      throw VertexError("Reserved regular XF texture generator source row");
    for (auto &component : input)
      if (std::isnan(component))
        component = 1;
    if (!(control & 4))
      input[2] = 1;
    result.texcoords[i] =
        matrix(input, vertex.texture_matrices[i] * 4, true, (control & 2) ? 3 : 2);
    if (xf_[0x1012] & 1) {
      const u32 post = xf_[0x1050 + i];
      input = result.texcoords[i];
      if (post & 256)
        input = normalized(input);
      result.texcoords[i] = matrix(input, 0x500 + (post & 63) * 4, true);
    }
    auto &coordinate = result.texcoords[i];
    if (coordinate[2] == 0) {
      coordinate[0] = std::clamp(coordinate[0] / 2, -1.0f, 1.0f);
      coordinate[1] = std::clamp(coordinate[1] / 2, -1.0f, 1.0f);
    }
  }
  return result;
}

Vector3 VertexDecoder::screen_position(const Vector4 &clip) const {
  if (clip[3] == 0 || !std::isfinite(clip[3]))
    throw VertexError("Viewport division requires a finite nonzero clip W");
  Vector3 result;
  const float inverse_w = 1.0f / clip[3];
  for (unsigned i = 0; i < 3; ++i)
    result[i] = clip[i] * inverse_w * std::bit_cast<float>(xf_[0x101a + i]) +
                std::bit_cast<float>(xf_[0x101d + i]);
  return result;
}
} // namespace cupid::gfx
