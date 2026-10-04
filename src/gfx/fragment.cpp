// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/fragment.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace cupid::gfx {
namespace {
s32 fixed_coordinate(float value) {
  if (!std::isfinite(value))
    throw VertexError("Non-finite texture coordinate");
  const double truncated = std::trunc(static_cast<double>(value) * 128);
  const double modulo = std::fmod(truncated, 16777216.0);
  const auto bits = static_cast<u32>(static_cast<s64>(modulo)) & 0xffffff;
  return static_cast<s32>(bits ^ 0x800000) - 0x800000;
}

s32 signed_field(u32 value, unsigned bits) {
  const u32 sign = 1u << (bits - 1);
  return static_cast<s32>((value & ((sign << 1) - 1)) ^ sign) - static_cast<s32>(sign);
}

s32 coordinate_bits(s64 value) {
  return static_cast<s32>((static_cast<u32>(value) & 0xffffff) ^ 0x800000) - 0x800000;
}

struct Filtering {
  s32 lod;
  bool linear;
};

Filtering filtering(std::span<const u32> bp, const FragmentInputs &input, unsigned unit,
                    unsigned coordinate) {
  const unsigned address = 0x80 + (unit & 3) + ((unit & 4) << 3);
  const u32 mode = bp[address], mode1 = bp[address + 4];
  const auto &dx = input.dx[coordinate];
  const auto &dy = input.dy[coordinate];
  const auto delta = [mode](float x, float y) {
    return (mode & 256) ? std::abs(x) + std::abs(y) : std::max(std::abs(x), std::abs(y));
  };
  const float size = std::max(delta(dx[0], dy[0]), delta(dx[1], dy[1]));
  if (!std::isfinite(size))
    throw VertexError("Non-finite texture derivative");
  const auto bits = std::bit_cast<u32>(size);
  s32 lod = static_cast<s32>((bits & 0x7f800000) >> 19) - 2032 +
            static_cast<s32>((bits & 0x7fffff) >> 19);
  lod += signed_field(mode >> 9, 8) >> 1;
  const bool linear = lod > 0 ? (mode & 128) != 0 : (mode & 16) != 0;
  if (lod > static_cast<s32>((mode1 >> 8) & 255))
    lod = static_cast<s32>((mode1 >> 8) & 255);
  else if (lod < static_cast<s32>(mode1 & 255))
    lod = static_cast<s32>(mode1 & 255);
  return {lod, linear};
}

s32 indirect_wrap(s32 value, unsigned mode) {
  if (mode == 0)
    return value;
  if (mode == 6)
    return 0;
  if (mode > 6)
    throw VertexError("Reserved indirect texture wrap mode");
  return value & ((1 << (16 - mode)) - 1);
}

float fog_float(u32 value) {
  const u32 bits =
      ((value & 0x80000) << 12) | (((value >> 11) & 255) << 23) | ((value & 2047) << 12);
  return std::bit_cast<float>(bits);
}

s32 arithmetic(u32 control, s32 a, s32 b, s32 c, s32 d, bool alpha) {
  const unsigned bias = (control >> 16) & 3, scale = (control >> 20) & 3;
  const bool subtract = (control & (1 << 18)) != 0;
  const s32 multiplier = scale == 3 ? 1 : 1 << scale;
  const s32 fraction = (c & 255) + ((c & 255) >> 7);
  s32 mix = ((a & 255) * (256 - fraction) + (b & 255) * fraction) * multiplier;
  mix += scale == 3 ? 0 : subtract ? 127 : 128;
  if (alpha && subtract)
    mix = (-mix) >> 8;
  else {
    mix >>= 8;
    if (subtract)
      mix = -mix;
  }
  const s32 offset = bias == 1 ? 128 : bias == 2 ? -128 : 0;
  s32 result = (d + offset) * multiplier + mix;
  if (scale == 3)
    result >>= 1;
  return result;
}
} // namespace

bool compare(unsigned mode, u32 value, u32 reference) {
  switch (mode & 7) {
  case 0:
    return false;
  case 1:
    return value < reference;
  case 2:
    return value == reference;
  case 3:
    return value <= reference;
  case 4:
    return value > reference;
  case 5:
    return value != reference;
  case 6:
    return value >= reference;
  default:
    return true;
  }
}

FragmentShader::FragmentShader(Memory &memory, std::span<const u32> bp, const TevRegisters &colors,
                               const TevRegisters &konst, std::span<const u8> tmem)
    : sampler_(memory, bp, tmem), bp_(bp), colors_(colors), konst_(konst) {
}

void FragmentShader::validate() const {
  if ((bp_[0] & 15) > 8 || ((bp_[0] >> 4) & 7) > 2)
    throw VertexError("Invalid GX texture or color channel count");
  if (((bp_[0] >> 16) & 7) > 4)
    throw VertexError("Indirect texture stage count exceeds four");
  if ((bp_[0xf5] & 3) == 3 || ((bp_[0xf5] >> 2) & 3) == 3)
    throw VertexError("Reserved depth-texture encoding or operation");
  if (bp_[0] & (1 << 9))
    throw VertexError("Multisample coverage is not implemented");
}

std::optional<Color> FragmentShader::shade(const FragmentInputs &input, u32 *depth) {
  validate();
  auto registers = colors_;
  unsigned last_color = 0, last_alpha = 0;
  const unsigned stages = ((bp_[0] >> 10) & 15) + 1, texgens = bp_[0] & 15;
  const auto swap = [&](Color value, unsigned table) {
    const auto a = bp_[0xf6 + table * 2], b = bp_[0xf7 + table * 2];
    return Color{value[a & 3], value[(a >> 2) & 3], value[b & 3], value[(b >> 2) & 3]};
  };
  const auto konst = [&](unsigned selection, unsigned channel) -> s32 {
    if (selection < 8)
      return std::array<s32, 8>{255, 223, 191, 159, 128, 96, 64, 32}[selection];
    if (selection < 12)
      return 0;
    if (selection < 16)
      return channel == 3 ? 0 : konst_[selection - 12][channel];
    return konst_[selection & 3][(selection - 16) / 4];
  };
  std::array<std::array<s32, 2>, 8> coordinates{};
  for (unsigned i = 0; i < texgens; ++i)
    for (unsigned c = 0; c < 2; ++c)
      coordinates[i][c] = fixed_coordinate(input.uv[i][c]);
  for (unsigned stage = 0; stage < ((bp_[0] >> 16) & 7); ++stage) {
    const u32 order = bp_[0x27] >> (stage * 6);
    const unsigned unit = order & 7, selected = (order >> 3) & 7;
    const unsigned coordinate = selected < texgens ? selected : 0;
    const u32 scale = bp_[0x25 + stage / 2] >> ((stage & 1) * 8);
    const auto filter = filtering(bp_, input, unit, coordinate);
    indirect_textures_[stage] = sampler_.sample(unit, coordinates[coordinate][0] >> (scale & 15),
                                                coordinates[coordinate][1] >> ((scale >> 4) & 15),
                                                filter.lod, filter.linear);
  }
  for (unsigned stage = 0; stage < stages; ++stage) {
    const u32 order = bp_[0x28 + stage / 2] >> ((stage & 1) * 12), cc = bp_[0xc0 + stage * 2],
              ac = bp_[0xc1 + stage * 2];
    const unsigned coordinate = ((order >> 3) & 7) < texgens ? ((order >> 3) & 7) : 0,
                   unit = order & 7;
    const auto &map = indirect_textures_[bp_[0x10 + stage] & 3];
    const u32 indirect = bp_[0x10 + stage];
    const unsigned format = (indirect >> 2) & 3, bump = (indirect >> 7) & 3;
    const unsigned shift = std::array<unsigned, 4>{0, 3, 4, 5}[format];
    const std::array<u8, 3> channels{map[3], map[2], map[1]};
    u8 alpha_bump = bump ? channels[bump - 1] : 0;
    alpha_bump =
        static_cast<u8>(format == 0 ? alpha_bump & 248 : unsigned(alpha_bump) << (8 - shift));
    std::array<s32, 3> values{};
    for (unsigned i = 0; i < 3; ++i)
      values[i] = (channels[i] >> shift) + ((indirect & (1 << (4 + i))) ? (format ? 1 : -128) : 0);
    std::array<s64, 2> translation{};
    const unsigned matrix = (indirect >> 9) & 3, matrix_id = (indirect >> 11) & 3;
    if ((!matrix && matrix_id) || matrix_id == 3)
      throw VertexError("Invalid indirect texture matrix selection");
    if (matrix) {
      const unsigned base = 6 + (matrix - 1) * 3;
      const int scale = int(((bp_[base] >> 22) & 3) | (((bp_[base + 1] >> 22) & 3) << 2) |
                            (((bp_[base + 2] >> 22) & 1) << 4));
      for (unsigned row = 0; row < 2; ++row) {
        if (matrix_id == 0) {
          for (unsigned col = 0; col < 3; ++col)
            translation[row] += s64(signed_field(bp_[base + col] >> (row * 11), 11)) * values[col];
          translation[row] >>= 3;
        } else
          translation[row] = s64(coordinates[coordinate][row]) * values[matrix_id - 1] / 256;
        const int exponent = 17 - scale;
        if (exponent >= 0)
          translation[row] >>= exponent;
        else
          translation[row] *= s64{1} << -exponent;
      }
    }
    for (unsigned i = 0; i < 2; ++i) {
      s64 result = indirect_wrap(coordinates[coordinate][i], (indirect >> (13 + i * 3)) & 7) +
                   translation[i];
      if (indirect & (1 << 20))
        result += texture_coordinates_[i];
      texture_coordinates_[i] = coordinate_bits(result);
    }
    if (order & 64) {
      if (texgens) {
        const auto filter = filtering(bp_, input, unit, coordinate);
        raw_texture_ = sampler_.sample(unit, texture_coordinates_[0], texture_coordinates_[1],
                                       filter.lod, filter.linear);
      } else
        raw_texture_ = Color{};
      texture_ = swap(raw_texture_, (ac >> 2) & 3);
    }
    const auto tex = texture_;
    const unsigned color_channel = (order >> 7) & 7;
    Color raster{};
    if (color_channel < 2)
      raster = swap(input.colors[color_channel], ac & 3);
    else if (color_channel == 5 || color_channel == 6)
      raster.fill(color_channel == 5 ? alpha_bump
                                     : static_cast<u8>(alpha_bump | (alpha_bump >> 5)));
    else if (color_channel != 7)
      throw VertexError("Invalid TEV raster color channel");
    const u32 selections = bp_[0xf6 + stage / 2] >> ((stage & 1) * 10);
    const unsigned kc = (selections >> 4) & 31, ka = (selections >> 9) & 31;
    const auto color_arg = [&](unsigned argument, unsigned channel) -> s32 {
      if (argument < 8)
        return registers[argument / 2][argument & 1 ? 3 : channel];
      if (argument == 8)
        return tex[channel];
      if (argument == 9)
        return tex[3];
      if (argument == 10)
        return raster[channel];
      if (argument == 11)
        return raster[3];
      if (argument == 12)
        return 255;
      if (argument == 13)
        return 128;
      if (argument == 14)
        return konst(kc, channel);
      return 0;
    };
    const auto alpha_arg = [&](unsigned argument) -> s32 {
      if (argument < 4)
        return registers[argument][3];
      if (argument == 4)
        return tex[3];
      if (argument == 5)
        return raster[3];
      if (argument == 6)
        return konst(ka, 3);
      return 0;
    };
    std::array<TevColor, 4> args{};
    for (unsigned operand = 0; operand < 4; ++operand) {
      for (unsigned channel = 0; channel < 3; ++channel)
        args[operand][channel] = color_arg((cc >> (12 - operand * 4)) & 15, channel);
      args[operand][3] = alpha_arg((ac >> (13 - operand * 3)) & 7);
      if (operand < 3)
        for (auto &component : args[operand])
          component &= 255;
    }
    const auto compare_inputs = [&](unsigned mode, unsigned channel) {
      u32 a = 0, b = 0;
      if (mode == 3) {
        a = static_cast<u32>(args[0][channel]);
        b = static_cast<u32>(args[1][channel]);
      } else
        for (unsigned i = 0; i <= mode; ++i) {
          a |= static_cast<u32>(args[0][i]) << (i * 8);
          b |= static_cast<u32>(args[1][i]) << (i * 8);
        }
      return (cc & (1 << 18)) ? a == b : a > b;
    };
    last_color = (cc >> 22) & 3;
    last_alpha = (ac >> 22) & 3;
    for (unsigned channel = 0; channel < 4; ++channel) {
      const u32 control = channel == 3 ? ac : cc;
      s32 value;
      if (((control >> 16) & 3) == 3) {
        const unsigned mode = (control >> 20) & 3;
        bool pass;
        if (channel < 3)
          pass = compare_inputs(mode, channel);
        else {
          u32 a = 0, b = 0;
          if (mode == 3) {
            a = static_cast<u32>(args[0][3]);
            b = static_cast<u32>(args[1][3]);
          } else
            for (unsigned i = 0; i <= mode; ++i) {
              a |= static_cast<u32>(args[0][i]) << (i * 8);
              b |= static_cast<u32>(args[1][i]) << (i * 8);
            }
          pass = (ac & (1 << 18)) ? a == b : a > b;
        }
        value = args[3][channel] + (pass ? args[2][channel] : 0);
      } else
        value = arithmetic(control, args[0][channel], args[1][channel], args[2][channel],
                           args[3][channel], channel == 3);
      registers[channel == 3 ? last_alpha : last_color][channel] =
          std::clamp(value, (control & (1 << 19)) ? 0 : -1024, (control & (1 << 19)) ? 255 : 1023);
    }
  }
  Color result{};
  for (unsigned i = 0; i < 4; ++i)
    result[i] = static_cast<u8>(registers[i == 3 ? last_alpha : last_color][i]);
  const u32 test = bp_[0xf3];
  const bool a = compare((test >> 16) & 7, result[3], test & 255),
             b = compare((test >> 19) & 7, result[3], (test >> 8) & 255);
  const unsigned logic = test >> 22;
  const bool pass = logic == 0 ? a && b : logic == 1 ? a || b : logic == 2 ? a != b : a == b;
  if (!pass)
    return std::nullopt;
  u32 z = depth ? *depth : 0;
  if ((bp_[0xf5] >> 2) & 3) {
    u32 texture_depth = bp_[0xf4];
    switch (bp_[0xf5] & 3) {
    case 0:
      texture_depth += raw_texture_[3];
      break;
    case 1:
      texture_depth += (u32(raw_texture_[3]) << 8) | raw_texture_[0];
      break;
    case 2:
      texture_depth += (u32(raw_texture_[0]) << 16) | (u32(raw_texture_[1]) << 8) | raw_texture_[2];
      break;
    default:
      throw VertexError("Reserved depth-texture encoding");
    }
    if (((bp_[0xf5] >> 2) & 3) == 1)
      texture_depth += z;
    z = texture_depth & 0xffffff;
  }
  const unsigned fog_type = (bp_[0xf1] >> 21) & 7;
  if (fog_type) {
    float fog_a = fog_float(bp_[0xee]), fog_c = fog_float(bp_[0xf1]);
    if (((bp_[0xee] >> 11) & 255) == 255 && ((bp_[0xf1] >> 11) & 255) == 255) {
      fog_a = 0;
      fog_c = (!(bp_[0xee] & 0x80000) && !(bp_[0xf1] & 0x80000))
                  ? -std::numeric_limits<float>::infinity()
                  : std::numeric_limits<float>::infinity();
    }
    float distance;
    if (bp_[0xf1] & (1 << 20))
      distance = fog_a * (static_cast<float>(z) / 16777215.0f);
    else {
      if (bp_[0xf0] >= 32)
        throw VertexError("Fog depth shift exceeds the defined range");
      const s32 denominator = static_cast<s32>(bp_[0xef] - (z >> bp_[0xf0]));
      distance = fog_a * 16777215.0f / static_cast<float>(denominator);
    }
    if (bp_[0xe8] & 1024) {
      if (!std::isfinite(input.viewport_width) || input.viewport_width == 0)
        throw VertexError("Fog range adjustment requires a finite nonzero viewport width");
      const float offset =
          (static_cast<float>(input.x) - float(int(bp_[0xe8] & 1023) - 342)) / input.viewport_width;
      const float index = std::clamp(9.0f - std::abs(offset) * 9.0f, 0.0f, 9.0f);
      const unsigned lower = static_cast<unsigned>(index), upper = std::min(9u, lower + 1);
      const auto coefficient = [&](unsigned i) {
        return float((bp_[0xe9 + i / 2] >> ((i & 1) ? 0 : 12)) & 4095) / 64;
      };
      const float fraction = index - static_cast<float>(lower);
      const float k = coefficient(lower) * (1 - fraction) + coefficient(upper) * fraction;
      distance *= std::sqrt(offset * offset + k * k) / k;
    }
    distance -= fog_c;
    if (std::isnan(distance))
      throw VertexError("Fog computation produced an undefined NaN");
    float fog = std::clamp(distance, 0.0f, 1.0f);
    if (fog_type >= 4) {
      if (fog_type >= 6)
        fog = 1 - fog;
      const float exponent = fog_type & 1 ? -8.0f * fog * fog : -8.0f * fog;
      fog = std::pow(2.0f, exponent);
      if (fog_type < 6)
        fog = 1 - fog;
    }
    const u32 factor = static_cast<u32>(fog * 256);
    for (unsigned channel = 0; channel < 3; ++channel) {
      const u32 component = (bp_[0xf2] >> (16 - channel * 8)) & 255;
      result[channel] =
          static_cast<u8>((result[channel] * (256 - factor) + component * factor) >> 8);
    }
  }
  if (depth)
    *depth = z;
  return result;
}
} // namespace cupid::gfx
