// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/vertex.hpp"
#include "test_support.hpp"
#include <bit>
#include <cmath>
#include <vector>

using namespace cupid;
using namespace cupid::gfx;

namespace {
struct Fixture {
  Memory memory{Console::GameCube};
  std::array<u32, 256> cp{};
  std::array<u32, 0x1058> xf{};

  Fixture() {
    cp[0x50] = 1 << 9;
    cp[0x70] = 1 | (4 << 1);
  }

  VertexDecoder decoder(unsigned vat = 0) {
    return {memory, cp, xf, vat};
  }
};

void append(std::vector<u8> &bytes, u32 value, unsigned size) {
  for (unsigned i = size; i != 0; --i)
    bytes.push_back(static_cast<u8>(value >> ((i - 1) * 8)));
}

void append_float(std::vector<u8> &bytes, float value) {
  append(bytes, std::bit_cast<u32>(value), 4);
}

void position_tests() {
  Fixture f;
  for (unsigned mode : {1u, 2u, 3u})
    for (unsigned format = 0; format < 8; ++format)
      for (unsigned components : {2u, 3u})
        for (unsigned fraction : {0u, 1u, 31u})
          for (bool dequantize : {false, true}) {
            f.cp[0x50] = mode << 9;
            f.cp[0x70] =
                (components - 2) | (format << 1) | (fraction << 4) | (u32(dequantize) << 30);
            const unsigned size = format < 2 ? 1 : format < 4 ? 2 : 4;
            std::vector<u8> attribute;
            for (unsigned i = 0; i < components; ++i)
              append(attribute, format >= 4 ? std::bit_cast<u32>(float(i + 1)) : i + 1, size);
            f.cp[0xa0] = 0x200;
            f.cp[0xb0] = 16;
            f.memory.copy_in(0x210, attribute);
            std::vector<u8> bytes;
            if (mode == 1)
              bytes = attribute;
            else
              append(bytes, 1, mode == 2 ? 1 : 2);
            const auto decoder = f.decoder();
            CHECK(decoder.vertex_size() == bytes.size());
            const auto vertex = decoder.decode(bytes);
            CHECK(!vertex.skip);
            const float scale = std::ldexp(1.0f, -int(format < 4 && dequantize ? fraction : 0));
            for (unsigned i = 0; i < components; ++i)
              CHECK(vertex.position[i] == float(i + 1) * scale);
            if (components == 2)
              CHECK(vertex.position[2] == 0);
            bytes.pop_back();
            CHECK_THROWS(decoder.decode(bytes));
          }
  // Signed boundaries and bit-preserving floating input.
  f.cp[0x50] = 1 << 9;
  f.cp[0x70] = (1 << 1) | (u32{1} << 30);
  const auto signed_bytes = std::array<u8, 2>{0x80, 0x7f};
  auto vertex = f.decoder().decode(signed_bytes);
  CHECK(vertex.position[0] == -128 && vertex.position[1] == 127);
  f.cp[0x70] = 4 << 1;
  std::vector<u8> float_bytes;
  append(float_bytes, 0x80000000, 4);
  append(float_bytes, 0x7f812345, 4);
  vertex = f.decoder().decode(float_bytes);
  CHECK(std::bit_cast<u32>(vertex.position[0]) == 0x80000000);
  CHECK(std::bit_cast<u32>(vertex.position[1]) == 0x7f812345);
  // An invalid position index skips the whole vertex without touching an invalid array base.
  for (unsigned mode : {2u, 3u}) {
    f.cp[0x50] = (mode << 9) | (3 << 13);
    f.cp[0x70] = (4 << 1) | (5 << 14);
    f.cp[0xa0] = f.cp[0xa2] = 0xfffffff0;
    std::vector<u8> bytes;
    append(bytes, mode == 2 ? 255 : 65535, mode == 2 ? 1 : 2);
    append(bytes, 123, 2);
    CHECK(f.decoder().decode(bytes).skip);
    CHECK_THROWS(f.decoder().transform(f.decoder().decode(bytes)));
  }
  f.cp[0x50] = 0;
  CHECK_THROWS(f.decoder());
}

void color_tests() {
  struct Case {
    unsigned format, size;
    u32 bits;
    Color expected;
  };

  const std::array cases{Case{0, 2, 0xf81f, {255, 0, 255, 255}},
                         Case{1, 3, 0x102030, {16, 32, 48, 255}},
                         Case{2, 4, 0x10203001, {16, 32, 48, 255}},
                         Case{3, 2, 0x1234, {17, 34, 51, 68}},
                         Case{4, 3, (1 << 18) | (2 << 12) | (3 << 6) | 4, {4, 8, 12, 16}},
                         Case{5, 4, 0x10203040, {16, 32, 48, 64}}};
  Fixture f;
  for (unsigned channel : {0u, 1u})
    for (unsigned mode : {1u, 2u, 3u})
      for (const auto &test : cases) {
        f.cp[0x50] = (1 << 9) | (mode << (13 + channel * 2));
        f.cp[0x70] = (4 << 1) | (test.format << (14 + channel * 4));
        f.cp[0xa2 + channel] = 0x400;
        f.cp[0xb2 + channel] = 4;
        std::vector<u8> color;
        append(color, test.bits, test.size);
        f.memory.copy_in(0x40c, color);
        std::vector<u8> bytes;
        append_float(bytes, 1);
        append_float(bytes, 2);
        if (mode == 1)
          bytes.insert(bytes.end(), color.begin(), color.end());
        else
          append(bytes, 3, mode == 2 ? 1 : 2);
        const auto decoder = f.decoder();
        CHECK(decoder.vertex_size() == bytes.size());
        const auto vertex = decoder.decode(bytes);
        CHECK(vertex.colors[channel] == test.expected);
        CHECK(vertex.color_present[channel] && !vertex.color_present[channel ^ 1]);
      }
  f.cp[0x50] = (1 << 9) | (1 << 13);
  f.cp[0x70] = 6 << 14;
  CHECK_THROWS(f.decoder());
}

void normal_tests() {
  Fixture f;
  for (unsigned mode : {1u, 2u, 3u})
    for (bool index3 : {false, true}) {
      f.cp[0x50] = (1 << 9) | (mode << 11);
      f.cp[0x70] = (4 << 1) | (1 << 9) | (1 << 10) | (u32(index3) << 31);
      f.cp[0xa1] = 0x600;
      f.cp[0xb1] = 16;
      std::vector<u8> normals{64, 0, 0, 0, 32, 0, 0, 0, 0xe0};
      for (unsigned index = 1; index <= 3; ++index)
        f.memory.copy_in(0x600 + index * 16, normals);
      std::vector<u8> bytes;
      append_float(bytes, 1);
      append_float(bytes, 2);
      if (mode == 1)
        bytes.insert(bytes.end(), normals.begin(), normals.end());
      else {
        append(bytes, 1, mode == 2 ? 1 : 2);
        if (index3) {
          append(bytes, 2, mode == 2 ? 1 : 2);
          append(bytes, 3, mode == 2 ? 1 : 2);
        }
      }
      CHECK(f.decoder().vertex_size() == bytes.size());
      const auto vertex = f.decoder().decode(bytes);
      CHECK(vertex.normal_count == 3);
      CHECK((vertex.normals[0] == Vector3{1, 0, 0}));
      CHECK((vertex.normals[1] == Vector3{0, 0.5f, 0}));
      CHECK((vertex.normals[2] == Vector3{0, 0, -0.5f}));
    }
}

void texture_tests() {
  Fixture f;
  for (unsigned texture = 0; texture < 8; ++texture)
    for (unsigned mode : {1u, 2u, 3u}) {
      f.cp[0x50] = 1 << 9;
      f.cp[0x60] = mode << (2 * texture);
      f.cp[0x70] = (4 << 1) | (u32{1} << 30);
      f.cp[0x80] = f.cp[0x90] = 0;
      constexpr u32 fields = 1 | (3 << 1) | (4 << 4);
      if (texture == 0)
        f.cp[0x70] |= fields << 21;
      else if (texture < 4)
        f.cp[0x80] = fields << ((texture - 1) * 9);
      else if (texture == 4) {
        f.cp[0x80] = (fields & 15) << 27;
        f.cp[0x90] = fields >> 4;
      } else
        f.cp[0x90] = fields << (5 + (texture - 5) * 9);
      f.cp[0xa4 + texture] = 0x800;
      f.cp[0xb4 + texture] = 4;
      const auto coordinates = std::array<u8, 4>{0, 32, 0xff, 0xf0};
      f.memory.copy_in(0x808, coordinates);
      std::vector<u8> bytes;
      append_float(bytes, 1);
      append_float(bytes, 2);
      if (mode == 1)
        bytes.insert(bytes.end(), coordinates.begin(), coordinates.end());
      else
        append(bytes, 2, mode == 2 ? 1 : 2);
      const auto decoder = f.decoder();
      CHECK(decoder.vertex_size() == bytes.size());
      const auto vertex = decoder.decode(bytes);
      CHECK(vertex.texcoord_present[texture]);
      CHECK(vertex.texcoords[texture][0] == 2 && vertex.texcoords[texture][1] == -1);
    }
}

void matrix_tests() {
  Fixture f;
  f.cp[0x50] |= 1 | 2 | (2 << 5);
  f.xf[0x1018] = 9 | (12 << 6) | (15 << 12) | (18 << 18) | (21 << 24);
  f.xf[0x1019] = 24 | (27 << 6) | (30 << 12) | (33 << 18);
  std::vector<u8> bytes{0xc3, 0xc6, 0xc9};
  append_float(bytes, 1);
  append_float(bytes, 2);
  append_float(bytes, -2);
  const auto decoder = f.decoder();
  CHECK(decoder.vertex_size() == 15);
  auto vertex = decoder.decode(bytes);
  CHECK(vertex.position_matrix == 3);
  CHECK(vertex.texture_matrices[0] == 6 && vertex.texture_matrices[1] == 15);
  CHECK(vertex.texture_matrices[5] == 9 && vertex.texture_matrices[7] == 33);
  const auto put = [&](unsigned address, float value) {
    f.xf[address] = std::bit_cast<u32>(value);
  };
  const std::array<float, 12> matrix{2, 0, 0, 1, 0, 3, 0, 2, 0, 0, 1, 0};
  for (unsigned i = 0; i < matrix.size(); ++i)
    put(12 + i, matrix[i]);
  const std::array<float, 6> projection{2, 0.5f, 3, -0.25f, 1, 0.5f};
  for (unsigned i = 0; i < projection.size(); ++i)
    put(0x1020 + i, projection[i]);
  vertex.normal_count = 3;
  vertex.normals[0] = {1, 0, 0};
  vertex.normals[1] = {0, 1, 0};
  vertex.normals[2] = {0, 0, 1};
  put(0x400 + 9, 2);
  put(0x400 + 13, 3);
  put(0x400 + 17, 4);
  vertex.colors[0] = {16, 32, 48, 64};
  f.xf[0x100c] = 0x10203040;
  f.xf[0x100d] = 0x50607080;
  f.xf[0x100e] = 1;
  f.xf[0x1010] = 0;
  auto transformed = decoder.transform(vertex);
  CHECK((transformed.model_position == Vector3{3, 8, -2}));
  CHECK(transformed.clip_position[0] == 5 && transformed.clip_position[1] == 24.5f);
  CHECK(transformed.clip_position[2] == -1.5f * (1.0f - 1.0e-7f));
  CHECK(transformed.clip_position[3] == 2);
  CHECK((transformed.normals[0] == Vector3{1, 0, 0}));
  CHECK((transformed.normals[1] == Vector3{0, 3, 0}));
  CHECK((transformed.normals[2] == Vector3{0, 0, 4}));
  CHECK((transformed.colors[0] == Color{16, 32, 48, 64}));
  CHECK((transformed.colors[1] == Color{80, 96, 112, 128}));
  f.xf[0x1026] = 1;
  transformed = decoder.transform(vertex);
  CHECK((transformed.clip_position == Vector4{6.5f, 23.75f, -1.5f, 1}));
  const std::array<float, 6> viewport{100, -50, 1000, 342, 342, 1000};
  for (unsigned i = 0; i < viewport.size(); ++i)
    put(0x101a + i, viewport[i]);
  CHECK((decoder.screen_position({1, 2, -1, 2}) == Vector3{392, 292, 500}));
  CHECK_THROWS(decoder.screen_position({1, 2, 3, 0}));
  f.xf[0x100e] |= 2 | (3 << 7) | (1 << 2);
  CHECK_THROWS(decoder.transform(vertex));
  f.xf[0x100e] = 1;
  f.xf[0x103f] = 1;
  f.xf[0x1040] = 7 << 4;
  CHECK_THROWS(decoder.transform(vertex));
}

void texgen_tests() {
  Fixture f;
  const auto put = [&](unsigned address, float value) {
    f.xf[address] = std::bit_cast<u32>(value);
  };
  f.xf[0x1026] = 1;
  f.xf[0x103f] = 1;
  DecodedVertex vertex;
  vertex.position = {3, 4, 5};
  vertex.normals[0] = {6, 7, 8};
  vertex.normals[1] = {9, 10, 11};
  vertex.normals[2] = {12, 13, 14};
  for (unsigned i = 0; i < 8; ++i)
    vertex.texcoords[i] = {float(i + 1), -float(i + 1)};
  vertex.texture_matrices[0] = 3;
  const std::array<float, 12> texture_matrix{2, 0, 1, 4, 0, 3, 2, 5, 0, 0, 4, 6};
  for (unsigned i = 0; i < texture_matrix.size(); ++i)
    put(12 + i, texture_matrix[i]);
  const auto decoder = f.decoder();
  for (unsigned source : {0u, 1u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u})
    for (bool abc : {false, true})
      for (bool stq : {false, true}) {
        f.xf[0x1040] = (source << 7) | (u32(abc) << 2) | (u32(stq) << 1);
        Vector3 input;
        if (source == 0)
          input = vertex.position;
        else if (source == 1)
          input = vertex.normals[0];
        else if (source == 3 || source == 4)
          input = vertex.normals[source - 2];
        else
          input = {vertex.texcoords[source - 5][0], vertex.texcoords[source - 5][1], 1};
        if (!abc)
          input[2] = 1;
        const auto result = decoder.transform(vertex);
        CHECK(result.texcoords[0][0] == input[0] * 2 + input[2] + 4);
        CHECK(result.texcoords[0][1] == input[1] * 3 + input[2] * 2 + 5);
        CHECK(result.texcoords[0][2] == (stq ? input[2] * 4 + 6 : 1));
      }
  // Post transforms operate on STQ before the special zero-Q clamp.
  f.xf[0x1040] = (5 << 7) | 2;
  f.xf[0x1012] = 1;
  f.xf[0x1050] = 6;
  const std::array<float, 12> post{1, 0, 0, 1, 0, 1, 0, 2, 0, 0, 0, 0};
  for (unsigned i = 0; i < post.size(); ++i)
    put(0x500 + 24 + i, post[i]);
  auto result = decoder.transform(vertex);
  CHECK((result.texcoords[0] == Vector3{1, 1, 0}));
  f.xf[0x1050] |= 256;
  put(0x500 + 24 + 11, 1);
  result = decoder.transform(vertex);
  const float length = std::sqrt(7.0f * 7 + 4.0f * 4 + 10.0f * 10);
  CHECK(result.texcoords[0][0] == 7 / length + 1);
  CHECK(result.texcoords[0][1] == 4 / length + 2);
  CHECK(result.texcoords[0][2] == 1);
  f.xf[0x1012] = 0;
  f.xf[0x1040] = (5 << 7) | 2;
  vertex.texcoords[0][0] = std::bit_cast<float>(u32{0x7fc12345});
  result = decoder.transform(vertex);
  CHECK(result.texcoords[0][0] == 7);
  // Color generators use the transformed color channel and bypass dual transforms.
  f.xf[0x100c] = 0xff800020;
  f.xf[0x100d] = 0x4080c0ff;
  f.xf[0x1012] = 1;
  for (unsigned type : {2u, 3u}) {
    f.xf[0x1040] = type << 4;
    result = decoder.transform(vertex);
    CHECK(result.texcoords[0][0] == (type == 2 ? 1 : 64 / 255.0f));
    CHECK(result.texcoords[0][1] == 128 / 255.0f);
    CHECK(result.texcoords[0][2] == 1);
  }
  f.xf[0x1040] = 2 << 7;
  CHECK_THROWS(decoder.transform(vertex));
  f.xf[0x1040] = 7 << 4;
  CHECK_THROWS(decoder.transform(vertex));
}

void lighting_tests() {
  Fixture f;
  const auto put = [&](unsigned address, float value) {
    f.xf[address] = std::bit_cast<u32>(value);
  };
  put(0, 1);
  put(5, 1);
  put(10, 1);
  put(0x400, 1);
  put(0x404, 1);
  put(0x408, 1);
  f.xf[0x1026] = 1;
  f.xf[0x100c] = 0x804020ff;
  f.xf[0x100a] = 0x10203040;
  f.xf[0x100e] = f.xf[0x1010] = 2;
  DecodedVertex vertex;
  vertex.normal_count = 3;
  vertex.normals = {Vector3{0, 0, 1}, Vector3{1, 0, 0}, Vector3{0, 2, 0}};
  vertex.colors[0] = {255, 128, 64, 32};
  const auto decoder = f.decoder();
  CHECK((decoder.transform(vertex).colors[0] == Color{8, 8, 6, 63}));
  // Vertex material and ambient can be selected independently for RGB and alpha.
  f.xf[0x100e] = 2 | 1 | 64;
  f.xf[0x1010] = 2 | 64;
  CHECK((decoder.transform(vertex).colors[0] == Color{255, 64, 16, 31}));
  f.xf[0x100c] = 0xffffffff;
  f.xf[0x100a] = 0;
  f.xf[0x603] = 0x80804020;
  put(0x604, 1);
  put(0x607, 1);
  put(0x60c, 2);
  put(0x60f, 1);
  // All diffuse modes agree for an aligned front light.
  for (unsigned attenuation : {0u, 2u})
    for (unsigned diffuse : {0u, 1u, 2u}) {
      f.xf[0x100e] = f.xf[0x1010] = 2 | (1 << 2) | (diffuse << 7) | (attenuation << 9);
      CHECK((decoder.transform(vertex).colors[0] == Color{128, 128, 63, 31}));
    }
  put(0x60c, -2);
  for (unsigned diffuse : {1u, 2u}) {
    f.xf[0x100e] = f.xf[0x1010] = 2 | (1 << 2) | (diffuse << 7);
    CHECK((decoder.transform(vertex).colors[0] == Color{}));
  }
  // Specular attenuation normalizes its distance polynomial when diffuse is enabled.
  put(0x60c, 2);
  put(0x604, 0.5f);
  put(0x607, 2);
  f.xf[0x100e] = f.xf[0x1010] = 2 | (1 << 2) | (1 << 9);
  CHECK((decoder.transform(vertex).colors[0] == Color{31, 31, 15, 7}));
  f.xf[0x100e] = f.xf[0x1010] = 2 | (1 << 2) | (1 << 7) | (1 << 9);
  CHECK((decoder.transform(vertex).colors[0] == Color{63, 63, 31, 15}));
  // Spotlight denominator includes distance and squared distance.
  put(0x604, 1);
  put(0x607, 0);
  put(0x608, 1);
  put(0x609, 0.5f);
  f.xf[0x100e] = f.xf[0x1010] = 2 | (1 << 2) | (3 << 9);
  CHECK((decoder.transform(vertex).colors[0] == Color{31, 31, 15, 7}));
  // A zero denominator uses the transform unit's numerator-dependent result.
  put(0x608, 0);
  put(0x609, 0);
  CHECK((decoder.transform(vertex).colors[0] == Color{128, 128, 63, 31}));
  put(0x604, 0);
  CHECK((decoder.transform(vertex).colors[0] == Color{}));
  // The high mask selects light seven, without including light zero.
  f.xf[0x673] = 0xff804020;
  put(0x67c, 2);
  f.xf[0x100e] = f.xf[0x1010] = 2 | (8 << 11);
  CHECK((decoder.transform(vertex).colors[0] == Color{255, 128, 63, 31}));
  // Non-finite integer conversion is rejected explicitly instead of invoking host UB.
  put(0x67c, 0);
  f.xf[0x100e] |= 1 << 7;
  CHECK_THROWS(decoder.transform(vertex));
  // Emboss reads the selected light and the unnormalized transformed tangent/binormal.
  put(0x67a, 3);
  put(0x67b, 4);
  put(0x67c, 0);
  f.xf[0x100e] = f.xf[0x1010] = 0;
  f.xf[0x103f] = 2;
  f.xf[0x1040] = 2 << 4;
  f.xf[0x1041] = (1 << 4) | (7 << 15);
  const auto transformed = decoder.transform(vertex);
  CHECK((transformed.texcoords[0] == Vector3{1, 1, 1}));
  CHECK(transformed.texcoords[1][0] == 1.6f);
  CHECK(transformed.texcoords[1][1] == 2.6f);
  CHECK(transformed.texcoords[1][2] == 1);
}
} // namespace

int main() {
  return run_tests([] {
    position_tests();
    color_tests();
    normal_tests();
    texture_tests();
    matrix_tests();
    texgen_tests();
    lighting_tests();
  });
}
