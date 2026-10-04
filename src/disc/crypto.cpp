// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/disc/crypto.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <utility>
#if defined(_M_X64) || defined(__x86_64__)
#include <wmmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#define CUPID_AES_TARGET
#else
#include <cpuid.h>
#define CUPID_AES_TARGET __attribute__((target("aes,sse2,no-avx")))
#endif
#endif

namespace cupid::disc::detail {
namespace {
using ShaState = std::array<u32, 5>;

template <std::size_t Round> void sha_round(std::array<u32, 16> &words, ShaState &work) {
  constexpr std::size_t slot = Round % 16;
  if constexpr (Round >= 16)
    words[slot] = std::rotl(words[(Round - 3) % 16] ^ words[(Round - 8) % 16] ^
                                words[(Round - 14) % 16] ^ words[slot],
                            1);
  u32 function, constant;
  if constexpr (Round < 20) {
    function = work[3] ^ (work[1] & (work[2] ^ work[3]));
    constant = 0x5a827999;
  } else if constexpr (Round < 40) {
    function = work[1] ^ work[2] ^ work[3];
    constant = 0x6ed9eba1;
  } else if constexpr (Round < 60) {
    function = (work[1] & work[2]) | (work[3] & (work[1] | work[2]));
    constant = 0x8f1bbcdc;
  } else {
    function = work[1] ^ work[2] ^ work[3];
    constant = 0xca62c1d6;
  }
  const u32 next = std::rotl(work[0], 5) + function + work[4] + constant + words[slot];
  work = {next, work[0], std::rotl(work[1], 30), work[2], work[3]};
}

template <std::size_t... Rounds>
void sha_rounds(std::array<u32, 16> &words, ShaState &work, std::index_sequence<Rounds...>) {
  (sha_round<Rounds>(words, work), ...);
}

void sha_block(std::span<const u8, 64> bytes, ShaState &state) {
  std::array<u32, 16> words{};
  for (std::size_t i = 0; i < words.size(); ++i) {
    const auto offset = i * 4;
    words[i] = (static_cast<u32>(bytes[offset]) << 24) |
               (static_cast<u32>(bytes[offset + 1]) << 16) |
               (static_cast<u32>(bytes[offset + 2]) << 8) | bytes[offset + 3];
  }
  auto work = state;
  sha_rounds(words, work, std::make_index_sequence<80>{});
  for (std::size_t i = 0; i < state.size(); ++i)
    state[i] += work[i];
}
} // namespace

Digest sha1(std::span<const u8> bytes) {
  const u64 bit_count = static_cast<u64>(bytes.size()) * 8;
  ShaState state{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
  while (bytes.size() >= 64) {
    sha_block(bytes.first<64>(), state);
    bytes = bytes.subspan(64);
  }
  // Only the final one or two blocks need padding or a copy of the input.
  std::array<u8, 128> tail{};
  std::copy(bytes.begin(), bytes.end(), tail.begin());
  tail[bytes.size()] = 0x80;
  const std::size_t padded_size = bytes.size() < 56 ? 64 : 128;
  for (std::size_t i = 0; i < 8; ++i)
    tail[padded_size - 1 - i] = static_cast<u8>(bit_count >> (i * 8));
  sha_block(std::span<const u8>(tail).first<64>(), state);
  if (padded_size == 128)
    sha_block(std::span<const u8>(tail).subspan<64, 64>(), state);
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

#if defined(_M_X64) || defined(__x86_64__)
bool aes_instructions_available() {
  static const bool available = [] {
#if defined(_MSC_VER)
    int registers[4]{};
    __cpuid(registers, 1);
    return (static_cast<unsigned>(registers[2]) & (1U << 25)) != 0;
#else
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    return __get_cpuid(1, &eax, &ebx, &ecx, &edx) && (ecx & (1U << 25)) != 0;
#endif
  }();
  return available;
}

CUPID_AES_TARGET
void encrypt_with_instructions(std::span<const u8> input, std::span<u8> output,
                               std::span<const u8, 16> iv, std::span<const u8, 176> keys) {
  __m128i rounds[11];
  for (std::size_t i = 0; i < 11; ++i)
    rounds[i] = _mm_loadu_si128(reinterpret_cast<const __m128i *>(keys.data() + i * 16));
  __m128i chain = _mm_loadu_si128(reinterpret_cast<const __m128i *>(iv.data()));
  for (std::size_t offset = 0; offset < input.size(); offset += 16) {
    __m128i state = _mm_loadu_si128(reinterpret_cast<const __m128i *>(input.data() + offset));
    state = _mm_xor_si128(_mm_xor_si128(state, chain), rounds[0]);
    for (std::size_t round = 1; round < 10; ++round)
      state = _mm_aesenc_si128(state, rounds[round]);
    chain = _mm_aesenclast_si128(state, rounds[10]);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(output.data() + offset), chain);
  }
}

CUPID_AES_TARGET
void decrypt_with_instructions(std::span<const u8> input, std::span<u8> output,
                               std::span<const u8, 16> iv, std::span<const u8, 176> keys) {
  __m128i rounds[11];
  rounds[0] = _mm_loadu_si128(reinterpret_cast<const __m128i *>(keys.data() + 160));
  rounds[10] = _mm_loadu_si128(reinterpret_cast<const __m128i *>(keys.data()));
  for (std::size_t i = 1; i < 10; ++i)
    rounds[i] = _mm_aesimc_si128(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(keys.data() + (10 - i) * 16)));
  __m128i chain = _mm_loadu_si128(reinterpret_cast<const __m128i *>(iv.data()));
  for (std::size_t offset = 0; offset < input.size(); offset += 16) {
    const __m128i ciphertext =
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(input.data() + offset));
    __m128i state = _mm_xor_si128(ciphertext, rounds[0]);
    for (std::size_t round = 1; round < 10; ++round)
      state = _mm_aesdec_si128(state, rounds[round]);
    state = _mm_xor_si128(_mm_aesdeclast_si128(state, rounds[10]), chain);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(output.data() + offset), state);
    chain = ciphertext;
  }
}
#endif

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
#if defined(_M_X64) || defined(__x86_64__)
  if (aes_instructions_available()) {
    encrypt_with_instructions(input, output, iv, keys_);
    return;
  }
#endif
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
#if defined(_M_X64) || defined(__x86_64__)
  if (aes_instructions_available()) {
    decrypt_with_instructions(input, output, iv, keys_);
    return;
  }
#endif
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
