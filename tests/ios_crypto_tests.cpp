// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/disc/crypto.hpp"
#include "cupid/ios/crypto.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <vector>
using namespace cupid;

namespace {
using V = ios::CryptoVector;
constexpr std::array<u8, 16> plaintext{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                       0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
constexpr std::array<u8, 16> encrypted{0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                       0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
constexpr std::array<u8, 20> abc_hash{0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                                      0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};

void aes_buffers() {
  Memory memory(Console::Wii);
  for (unsigned i = 0; i < 16; ++i)
    memory.write8(0x4000 + i, u8(i));
  memory.copy_in(0x3000, plaintext);
  std::array<V, 2> input{{{0x3000, 16}, {0x4000, 16}}}, output{{{0x5000, 32}, {0x4100, 16}}};
  CHECK(ios::aes_ioctlv(memory, 2, input, output) == 0);
  CHECK(std::equal(encrypted.begin(), encrypted.end(), memory.range(0x5000, 16).begin()));
  CHECK(std::equal(encrypted.begin(), encrypted.end(), memory.range(0x4100, 16).begin()));
  CHECK(std::all_of(memory.range(0x5010, 16).begin(), memory.range(0x5010, 16).end(),
                    [](u8 b) { return b == 0; }));
  input[0] = {0x5000, 16};
  output[0] = {0x6000, 16};
  memory.copy_in(0x4100, std::array<u8, 16>{});
  CHECK(ios::aes_ioctlv(memory, 3, input, output) == 0);
  CHECK(std::equal(plaintext.begin(), plaintext.end(), memory.range(0x6000, 16).begin()));
  CHECK(std::equal(encrypted.begin(), encrypted.end(), memory.range(0x4100, 16).begin()));
  input[0] = {0x3000, 17};
  output[0] = {0x5000, 32};
  CHECK(ios::aes_ioctlv(memory, 2, input, output) == 0);
  CHECK(std::all_of(memory.range(0x5000, 32).begin(), memory.range(0x5000, 32).end(),
                    [](u8 b) { return b == 0; }));
  CHECK(std::equal(encrypted.begin(), encrypted.end(), memory.range(0x4100, 16).begin()));
  input[1].address = 0x4001;
  CHECK(ios::aes_ioctlv(memory, 2, input, output) == -4);
  input[1] = {0, 16};
  CHECK(ios::aes_ioctlv(memory, 2, input, output) == -4);
  input[1] = {0x4000, 16};
  input[0] = {0xffffffff, 0};
  output[0] = {0xfffffffe, 0};
  CHECK(ios::aes_ioctlv(memory, 2, input, output) == 0);
  const std::array<V, 1> copy_in{{{0x3000, 16}}}, copy_out{{{0x3003, 1}}};
  CHECK(ios::aes_ioctlv(memory, 0, copy_in, copy_out) == 0);
  CHECK(std::equal(plaintext.begin(), plaintext.end(), memory.range(0x3003, 16).begin()));
  CHECK(ios::aes_ioctlv(memory, 0, std::array<V, 1>{{{0xffffffff, 0}}},
                        std::array<V, 1>{{{0xfffffffe, 0}}}) == 0);
  CHECK(ios::aes_ioctlv(memory, 1, input, output) == -4);
}

void parallel_cbc_boundaries() {
  const std::array<u8, 16> key{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  const std::array<u8, 16> iv{15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
  const disc::detail::Aes128 aes(key);
  for (unsigned length : {0u, 16u, 112u, 128u, 144u, 240u, 256u, 272u, 1024u, 32768u}) {
    std::vector<u8> original(length + 3), ciphertext(length + 3), inplace(length + 3);
    for (unsigned i = 0; i < original.size(); ++i)
      original[i] = u8(i * 73 + i / 256);
    ciphertext = original;
    aes.encrypt_cbc(std::span<const u8>(original).subspan(1, length),
                    std::span<u8>(ciphertext).subspan(1, length), iv);
    inplace = ciphertext;
    auto buffer = std::span<u8>(inplace).subspan(1, length);
    aes.decrypt_cbc(buffer, buffer, iv);
    CHECK(inplace == original);
    std::vector<u8> separate = original;
    aes.decrypt_cbc(std::span<const u8>(ciphertext).subspan(1, length),
                    std::span<u8>(separate).subspan(1, length), iv);
    CHECK(separate == original);
  }
}

void sha_contexts() {
  Memory memory(Console::Wii);
  constexpr u32 context = 0x2001, hash = 0x2100, data = 0x3000;
  std::array<V, 1> input{{{0xffffffff, 123}}};
  std::array<V, 2> output{{{context, 0}, {hash, 20}}};
  CHECK(ios::sha_ioctlv(memory, 0, input, output) == 0);
  CHECK(memory.read32(context) == 0x01234567 && memory.read32(context + 4) == 0x89abcdef);
  memory.copy_in(data, std::array<u8, 3>{'a', 'b', 'c'});
  input[0] = {data, 3};
  CHECK(ios::sha_ioctlv(memory, 2, input, output) == 0);
  CHECK(std::equal(abc_hash.begin(), abc_hash.end(), memory.range(hash, 20).begin()));
  CHECK(memory.read32(context) == 0x363e99a9 && memory.read32(context + 20) == 0x03000000);
  CHECK(ios::sha_ioctlv(memory, 0, input, output) == 0);
  std::array<u8, 64> zeros{};
  memory.copy_in(data, zeros);
  input[0] = {data, 64};
  CHECK(ios::sha_ioctlv(memory, 1, input, output) == 0);
  CHECK(memory.read32(context + 20) == 0x40000000);
  memory.copy_in(0x2200, memory.range(context, 28));
  output[0].address = 0x2200;
  memory.copy_in(data, std::array<u8, 3>{'a', 'b', 'c'});
  input[0] = {data, 3};
  CHECK(ios::sha_ioctlv(memory, 2, input, output) == 0);
  std::vector<u8> combined(64);
  combined.insert(combined.end(), {'a', 'b', 'c'});
  const auto expected = disc::detail::sha1(combined);
  CHECK(std::equal(expected.begin(), expected.end(), memory.range(hash, 20).begin()));
  CHECK(memory.read32(0x2214) == 0x43000000);
  CHECK(ios::sha_ioctlv(memory, 0, input, output) == 0);
  CHECK(ios::sha_ioctlv(memory, 1, input, output) == 0);
  input[0].size = 4;
  CHECK(ios::sha_ioctlv(memory, 1, input, output) == 0);
  const auto before = memory.range(0x2200, 28);
  const std::vector<u8> saved(before.begin(), before.end());
  input[0] = {0xffffffff, 0};
  CHECK_THROWS(ios::sha_ioctlv(memory, 2, input, output));
  CHECK(std::equal(saved.begin(), saved.end(), memory.range(0x2200, 28).begin()));
  input[0] = {data, 57};
  CHECK_THROWS(ios::sha_ioctlv(memory, 1, input, output));
  CHECK(ios::sha_ioctlv(memory, 0, input, output) == 0);
  // Increment the opaque low byte count through its 32-bit carry boundary.
  memory.write32(0x2214, 0xc0ffffff);
  memory.write32(0x2218, 0x07000000);
  input[0].size = 64;
  CHECK(ios::sha_ioctlv(memory, 1, input, output) == 0);
  CHECK(memory.read32(0x2214) == 0 && memory.read32(0x2218) == 0x08000000);
  CHECK(ios::sha_ioctlv(memory, 15, input, output) == -4);
  CHECK(ios::sha_ioctlv(memory, 0, {}, output) == -4);
  input[0] = {0, 1};
  CHECK(ios::sha_ioctlv(memory, 0, input, output) == -4);
}
} // namespace

int main() {
  return run_tests([] {
    aes_buffers();
    sha_contexts();
    parallel_cbc_boundaries();
  });
}
