// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/fonts.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>

namespace {
using namespace cupid;

void put(std::span<u8> bytes, std::size_t offset, u32 value, unsigned size = 4) {
  for (unsigned i = 0; i < size; ++i)
    bytes[offset + i] = static_cast<u8>(value >> ((size - i - 1) * 8));
}

std::vector<u8> font(unsigned type, unsigned dimension, bool back_reference) {
  const unsigned image_bytes = dimension * dimension / 4;
  std::vector<u8> decoded(49 + image_bytes);
  put(decoded, 0, type, 2);
  put(decoded, 2, type ? 0x8140 : 32, 2);
  put(decoded, 4, type ? 0x8140 : 32, 2);
  put(decoded, 8, dimension, 2);
  put(decoded, 12, dimension, 2);
  put(decoded, 14, dimension, 2);
  put(decoded, 16, dimension, 2);
  put(decoded, 18, dimension, 2);
  put(decoded, 20, dimension * dimension / 2);
  put(decoded, 26, 1, 2);
  put(decoded, 28, 1, 2);
  put(decoded, 30, dimension, 2);
  put(decoded, 32, dimension, 2);
  put(decoded, 34, 48, 2);
  put(decoded, 36, 49);
  put(decoded, 40, dimension * dimension / 2);
  decoded[48] = static_cast<u8>(dimension);

  const unsigned literals = back_reference ? 50 : static_cast<unsigned>(decoded.size());
  const unsigned decisions = literals + unsigned(back_reference);
  const unsigned mask_size = ((decisions + 31) / 32) * 4;
  const unsigned links = 16 + mask_size, chunks = links + (back_reference ? 2 : 0);
  const bool extended = back_reference && image_bytes - 1 > 17;
  std::vector<u8> encoded(chunks + literals + unsigned(extended));
  put(encoded, 0, 0x59617930);
  put(encoded, 4, static_cast<u32>(decoded.size()));
  put(encoded, 8, links);
  put(encoded, 12, chunks);
  for (unsigned i = 0; i < literals; ++i)
    encoded[16 + i / 8] |= static_cast<u8>(0x80 >> (i % 8));
  std::copy_n(decoded.begin(), literals, encoded.begin() + chunks);
  if (back_reference) {
    put(encoded, links, extended ? 0 : (image_bytes - 3) << 12, 2);
    if (extended)
      encoded.back() = static_cast<u8>(image_bytes - 19);
  }
  return encoded;
}

std::vector<u8> container(std::span<const u8> jis, std::span<const u8> western) {
  std::vector<u8> result(2 * 1024 * 1024, 0xa5);
  std::copy(jis.begin(), jis.end(), result.begin() + 0x1aff00);
  std::copy(western.begin(), western.end(), result.begin() + 0x1fcf00);
  return result;
}
} // namespace

int main() {
  return run_tests([] {
    const auto jis = font(2, 8, false), western = font(0, 8, false);
    const auto literal = runtime::validate_ipl_fonts(container(jis, western));
    CHECK(literal.shift_jis.encoded_size == jis.size());
    CHECK(literal.western.encoded_size == western.size());
    CHECK(literal.shift_jis.decoded_size == 65 && literal.western.decoded_size == 65);
    CHECK(literal.shift_jis.bytes.size() == 0x4d000);
    CHECK(literal.western.bytes.size() == 0x3100);
    CHECK(std::equal(western.begin(), western.end(), literal.western.bytes.begin()));
    CHECK(std::all_of(literal.western.bytes.begin() + western.size(), literal.western.bytes.end(),
                      [](u8 byte) { return byte == 0; }));
    const auto short_font = font(0, 8, true);
    const auto short_decoded = runtime::validate_ipl_fonts(container(jis, short_font));
    CHECK(short_decoded.western.decoded_crc32 == literal.western.decoded_crc32);
    CHECK(short_decoded.western.encoded_size == short_font.size());
    const auto extended_font = font(0, 16, true);
    const auto extended_decoded = runtime::validate_ipl_fonts(container(jis, extended_font));
    const auto large_literal = runtime::validate_ipl_fonts(container(jis, font(0, 16, false)));
    CHECK(extended_decoded.western.decoded_crc32 == large_literal.western.decoded_crc32);
    CHECK(extended_decoded.western.decoded_size == 113);
    CHECK(extended_decoded.western.encoded_size == extended_font.size());

    CHECK_THROWS(runtime::validate_ipl_fonts(std::array<u8, 128>{}));
    auto bad = container(jis, western);
    bad[0x1fcf00] = 0;
    CHECK_THROWS(runtime::validate_ipl_fonts(bad));
    auto broken = western;
    put(broken, 4, 1024 * 1024 + 1);
    CHECK_THROWS(runtime::validate_ipl_fonts(container(jis, broken)));
    broken = western;
    put(broken, 8, 16);
    CHECK_THROWS(runtime::validate_ipl_fonts(container(jis, broken)));
    broken = western;
    put(broken, 12, 0x4000);
    CHECK_THROWS(runtime::validate_ipl_fonts(container(jis, broken)));
    broken = western;
    broken[16] = 0;
    CHECK_THROWS(runtime::validate_ipl_fonts(container(jis, broken)));
    broken = short_font;
    put(broken, 24, 0xe0ff, 2);
    CHECK_THROWS(runtime::validate_ipl_fonts(container(jis, broken)));
    broken = short_font;
    put(broken, 24, 0xf000, 2);
    CHECK_THROWS(runtime::validate_ipl_fonts(container(jis, broken)));
    broken = western;
    // The literal fixture's glyph header begins after its three mask words.
    const unsigned glyph_header = 28;
    put(broken, glyph_header, 2, 2);
    CHECK_THROWS(runtime::validate_ipl_fonts(container(jis, broken)));
    broken = western;
    put(broken, glyph_header + 20, 0, 4);
    CHECK_THROWS(runtime::validate_ipl_fonts(container(jis, broken)));
    broken = western;
    put(broken, glyph_header + 36, 0xffff, 4);
    CHECK_THROWS(runtime::validate_ipl_fonts(container(jis, broken)));
    broken = western;
    broken[glyph_header + 48] = 9;
    CHECK_THROWS(runtime::validate_ipl_fonts(container(jis, broken)));
  });
}
