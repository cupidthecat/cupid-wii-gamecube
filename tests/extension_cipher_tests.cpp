// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/hw/extension_cipher.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <string_view>

namespace {
using namespace cupid;
using namespace cupid::hw;

template <std::size_t Size> consteval std::array<u8, Size> bytes(std::string_view text) {
  if (text.size() != Size * 2)
    throw "Wrong fixture size";
  std::array<u8, Size> result{};
  auto digit = [](char value) -> unsigned {
    if (value >= '0' && value <= '9')
      return unsigned(value - '0');
    if (value >= 'a' && value <= 'f')
      return unsigned(value - 'a') + 10;
    throw "Invalid fixture digit";
  };
  for (std::size_t i = 0; i < Size; ++i)
    result[i] = u8((digit(text[i * 2]) << 4) | digit(text[i * 2 + 1]));
  return result;
}

struct Fixture {
  std::array<u8, 16> record;
  ExtensionKey key;
  std::array<u8, 32> encrypted;
};

// Authored inputs and fixed results from the pinned compiled behavioral reference.
constexpr std::array<Fixture, 10> fixtures{
    {{bytes<16>("00000000000000000000000000000000"),
      {bytes<8>("1717171717171717"), bytes<8>("1717171717171717")},
      bytes<32>("fefdfcfbfaf9f8e7e6e5e4e3e2e1e0efeeedecebeae9e817161514131211101f")},
     {bytes<16>("a5000000000000000000000000000000"),
      {bytes<8>("1717171717bc1717"), bytes<8>("171717bc17171717")},
      bytes<32>("fefdfc50fa5ef8e7e6e5e448e246e0efeeedec40ea4ee817161514b81276101f")},
     {bytes<16>("0000000000000000000000000000005a"),
      {bytes<8>("171717ae17171717"), bytes<8>("ae17171717171717")},
      bytes<32>("47fdfc42faf9f8e75fe5e44ae2e1e0ef57edec72eae9e817af15147a1211101f")},
     {bytes<16>("9988776655443322110020d96682fbdc"),
      {bytes<8>("1c484b1d2ced4016"), bytes<8>("eb064fcafca4a004")},
      bytes<32>("0fbff82c24bc66f507c7f0241c846efd1fcf883c148c760517d780340c947e0d")},
     {bytes<16>("99887766554433221100edd07606b949"),
      {bytes<8>("ada9f306b13ee6cd"), bytes<8>("666e00b45494bdac")},
      bytes<32>("35360f4907539d963d0e17b10f5b95ee05061fb937438de60d1e27a13f4b85fe")},
     {bytes<16>("9988776655443322110083794a103b27"),
      {bytes<8>("763c6066657f4cb3"), bytes<8>("4956b4cfce17844b")},
      bytes<32>("c393165251913e1fdb9b1e6a69994617d383066261814e2feb8b0e7a79895627")},
     {bytes<16>("998877665544332211008bee541deb33"),
      {bytes<8>("e29c82a2f1a581f0"), bytes<8>("7097b8db67b5b740")},
      bytes<32>("6ef238ba74d5325756fa30b27cdd3a5f5ee228aa44c5226746ea20a24ccd2a6f")},
     {bytes<16>("998877665544332211006d3cdfdcf818"),
      {bytes<8>("788a01d476af1d11"), bytes<8>("747ae96c06298252")},
      bytes<32>("fc0de843887f6ba4e405e05b907773acecfdf853984f7b54d4f5f02ba047835c")},
     {bytes<16>("99887766554433221100de2c30bca8d4"),
      {bytes<8>("59dd09644fd70c12"), bytes<8>("a0926153a0aa5170")},
      bytes<32>("07b698cc1584ab850fbe60f41d9c538d17a668fc65945b751fae70e46dec437d")},
     {bytes<16>("998877665544332211007b752b3ae098"),
      {bytes<8>("c0b63602bdc395c4"), bytes<8>("5a544b6910fda0a2")},
      bytes<32>("1a1f876857bfd1e112079f605fb7d9e90a0f977847af21f10237af704fa729f9")}}};

void fixed_vectors() {
  for (const auto &fixture : fixtures) {
    const auto key = make_extension_key(fixture.record);
    CHECK(key == fixture.key);
    std::array<u8, 32> data{};
    for (unsigned i = 0; i < data.size(); ++i)
      data[i] = u8(i);
    key.encrypt(data, 0x20);
    CHECK(data == fixture.encrypted);
    key.decrypt(data, 0x20);
    for (unsigned i = 0; i < data.size(); ++i)
      CHECK(data[i] == i);
  }
}

void phases_chunks_and_wrap() {
  for (const auto &fixture : fixtures) {
    for (unsigned phase = 0; phase < 8; ++phase) {
      std::array<u8, 257> plain{};
      for (unsigned i = 0; i < plain.size(); ++i)
        plain[i] = u8(i);
      auto whole = plain, split = plain;
      const u32 address = 0xfffffff8 + phase;
      fixture.key.encrypt(whole, address);
      fixture.key.encrypt(std::span(split).first(13), address);
      fixture.key.encrypt(std::span(split).subspan(13), address + 13);
      CHECK(whole == split);
      for (unsigned i = 0; i < 8; ++i) {
        std::array<u8, 1> individual{plain[i]};
        fixture.key.encrypt(individual, address + i);
        CHECK(individual[0] == whole[i]);
      }
      fixture.key.decrypt(whole, address);
      fixture.key.decrypt(std::span(split).first(127), address);
      fixture.key.decrypt(std::span(split).subspan(127), address + 127);
      CHECK(whole == plain && split == plain);
      const auto unchanged = fixture.key;
      fixture.key.encrypt({}, address);
      fixture.key.decrypt({}, address);
      CHECK(fixture.key == unchanged);
    }
  }
}

void requested_span_and_immutable_material() {
  const auto record = fixtures[1].record;
  const auto key = make_extension_key(record);
  CHECK(record == fixtures[1].record);
  std::array<u8, 32> response{};
  // A bus read may supply four bytes followed by a zero-filled unread suffix.
  std::copy_n(fixtures[1].encrypted.begin(), 4, response.begin());
  key.encrypt(response, 0xfc);
  CHECK(std::all_of(response.begin() + 4, response.end(), [](u8 byte) { return byte != 0; }));
  key.decrypt(response, 0xfc);
  CHECK(std::equal(response.begin(), response.begin() + 4, fixtures[1].encrypted.begin()));
  CHECK(std::all_of(response.begin() + 4, response.end(), [](u8 byte) { return byte == 0; }));
  CHECK(key == fixtures[1].key);
}
} // namespace

int main() {
  return run_tests([] {
    fixed_vectors();
    phases_chunks_and_wrap();
    requested_span_and_immutable_material();
  });
}
