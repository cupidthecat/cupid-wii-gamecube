// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/disc/crypto.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <vector>

namespace cupid::disc::detail {
Digest sha1(std::span<const u8> bytes) {
  const u64 bit_count = static_cast<u64>(bytes.size()) * 8;
  std::vector<u8> padded(bytes.begin(), bytes.end());
  padded.push_back(0x80);
  while (padded.size() % 64 != 56)
    padded.push_back(0);
  for (int shift = 56; shift >= 0; shift -= 8)
    padded.push_back(static_cast<u8>(bit_count >> shift));
  std::array<u32, 5> state{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
  for (std::size_t start = 0; start < padded.size(); start += 64) {
    std::array<u32, 80> schedule{};
    for (std::size_t i = 0; i < 16; ++i) {
      for (std::size_t j = 0; j < 4; ++j)
        schedule[i] = (schedule[i] << 8) | padded[start + i * 4 + j];
    }
    for (std::size_t i = 16; i < schedule.size(); ++i)
      schedule[i] =
          std::rotl(schedule[i - 3] ^ schedule[i - 8] ^ schedule[i - 14] ^ schedule[i - 16], 1);
    auto work = state;
    for (std::size_t i = 0; i < schedule.size(); ++i) {
      u32 f, constant;
      if (i < 20) {
        f = (work[1] & work[2]) | (~work[1] & work[3]);
        constant = 0x5a827999;
      } else if (i < 40) {
        f = work[1] ^ work[2] ^ work[3];
        constant = 0x6ed9eba1;
      } else if (i < 60) {
        f = (work[1] & work[2]) | (work[1] & work[3]) | (work[2] & work[3]);
        constant = 0x8f1bbcdc;
      } else {
        f = work[1] ^ work[2] ^ work[3];
        constant = 0xca62c1d6;
      }
      const u32 next = std::rotl(work[0], 5) + f + work[4] + constant + schedule[i];
      work = {next, work[0], std::rotl(work[1], 30), work[2], work[3]};
    }
    for (std::size_t i = 0; i < state.size(); ++i)
      state[i] += work[i];
  }
  Digest result{};
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = static_cast<u8>(state[i / 4] >> ((3 - i % 4) * 8));
  return result;
}

namespace {
u8 multiply(u8 a, u8 b) {
  u8 result = 0;
  while (b) {
    if (b & 1)
      result ^= a;
    a = static_cast<u8>((a << 1) ^ ((a & 0x80) ? 0x1b : 0));
    b >>= 1;
  }
  return result;
}

struct Substitutions {
  std::array<u8, 256> forward{}, inverse{};

  Substitutions() {
    for (unsigned value = 0; value < 256; ++value) {
      u8 reciprocal = 0;
      if (value) {
        reciprocal = 1;
        u8 power = static_cast<u8>(value);
        unsigned exponent = 254;
        while (exponent) {
          if (exponent & 1)
            reciprocal = multiply(reciprocal, power);
          power = multiply(power, power);
          exponent >>= 1;
        }
      }
      const u8 transformed =
          static_cast<u8>(reciprocal ^ std::rotl(reciprocal, 1) ^ std::rotl(reciprocal, 2) ^
                          std::rotl(reciprocal, 3) ^ std::rotl(reciprocal, 4) ^ 0x63);
      forward[value] = transformed;
      inverse[transformed] = static_cast<u8>(value);
    }
  }
};

const Substitutions &substitutions() {
  static const Substitutions value;
  return value;
}
} // namespace

Aes128::Aes128(std::span<const u8, 16> key) {
  std::copy(key.begin(), key.end(), keys_.begin());
  u8 round_constant = 1;
  for (std::size_t offset = 16; offset < keys_.size(); offset += 4) {
    std::array<u8, 4> word{};
    std::copy_n(keys_.begin() + static_cast<std::ptrdiff_t>(offset - 4), 4, word.begin());
    if (offset % 16 == 0) {
      std::rotate(word.begin(), word.begin() + 1, word.end());
      for (u8 &value : word)
        value = substitutions().forward[value];
      word[0] ^= round_constant;
      round_constant = multiply(round_constant, 2);
    }
    for (std::size_t i = 0; i < 4; ++i)
      keys_[offset + i] = keys_[offset + i - 16] ^ word[i];
  }
}

void Aes128::block(std::array<u8, 16> &state, bool decrypt) const {
  auto add_key = [&](int round) {
    for (std::size_t i = 0; i < 16; ++i)
      state[i] ^= keys_[static_cast<std::size_t>(round) * 16 + i];
  };
  auto substitute = [&] {
    const auto &table = decrypt ? substitutions().inverse : substitutions().forward;
    for (u8 &value : state)
      value = table[value];
  };
  auto shift = [&] {
    auto original = state;
    for (std::size_t row = 1; row < 4; ++row)
      for (std::size_t column = 0; column < 4; ++column)
        state[column * 4 + row] = original[((column + (decrypt ? 4 - row : row)) % 4) * 4 + row];
  };
  auto mix = [&] {
    const std::array<u8, 4> coefficients =
        decrypt ? std::array<u8, 4>{14, 11, 13, 9} : std::array<u8, 4>{2, 3, 1, 1};
    for (std::size_t column = 0; column < 4; ++column) {
      std::array<u8, 4> original{};
      std::copy_n(state.begin() + static_cast<std::ptrdiff_t>(column * 4), 4, original.begin());
      for (std::size_t row = 0; row < 4; ++row) {
        u8 value = 0;
        for (std::size_t i = 0; i < 4; ++i)
          value ^= multiply(original[(row + i) % 4], coefficients[i]);
        state[column * 4 + row] = value;
      }
    }
  };
  add_key(decrypt ? 10 : 0);
  for (int step = 1; step <= 10; ++step) {
    if (decrypt) {
      shift();
      substitute();
      add_key(10 - step);
      if (step != 10)
        mix();
    } else {
      substitute();
      shift();
      if (step != 10)
        mix();
      add_key(step);
    }
  }
}

void Aes128::encrypt_cbc(std::span<const u8> input, std::span<u8> output,
                         std::span<const u8, 16> iv) const {
  if (input.size() != output.size() || input.size() % 16)
    throw std::runtime_error("AES-CBC requires equal block-aligned buffers");
  std::array<u8, 16> chain{};
  std::copy(iv.begin(), iv.end(), chain.begin());
  for (std::size_t offset = 0; offset < input.size(); offset += 16) {
    for (std::size_t i = 0; i < 16; ++i)
      chain[i] ^= input[offset + i];
    block(chain, false);
    std::copy(chain.begin(), chain.end(), output.begin() + static_cast<std::ptrdiff_t>(offset));
  }
}

void Aes128::decrypt_cbc(std::span<const u8> input, std::span<u8> output,
                         std::span<const u8, 16> iv) const {
  if (input.size() != output.size() || input.size() % 16)
    throw std::runtime_error("AES-CBC requires equal block-aligned buffers");
  std::array<u8, 16> chain{};
  std::copy(iv.begin(), iv.end(), chain.begin());
  for (std::size_t offset = 0; offset < input.size(); offset += 16) {
    std::array<u8, 16> ciphertext{}, decoded{};
    std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(offset), 16, ciphertext.begin());
    decoded = ciphertext;
    block(decoded, true);
    for (std::size_t i = 0; i < 16; ++i)
      output[offset + i] = decoded[i] ^ chain[i];
    chain = ciphertext;
  }
}
} // namespace cupid::disc::detail
