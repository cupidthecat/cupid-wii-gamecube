// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/disc/crypto.hpp"
#include "cupid/disc/filesystem.hpp"
#include "cupid/disc/image.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>
#include <zstd.h>
#if CUPID_HAVE_BZIP2
#include <bzlib.h>
#endif
#if CUPID_HAVE_LZMA
#include <lzma.h>
#endif

using namespace cupid;
using Bytes = std::vector<u8>;

namespace {
void put16(Bytes &bytes, std::size_t offset, u16 value) {
  bytes.at(offset) = static_cast<u8>(value >> 8);
  bytes.at(offset + 1) = static_cast<u8>(value);
}

void put32(Bytes &bytes, std::size_t offset, u32 value) {
  for (std::size_t i = 0; i < 4; ++i)
    bytes.at(offset + i) = static_cast<u8>(value >> ((3 - i) * 8));
}

void put64(Bytes &bytes, std::size_t offset, u64 value) {
  put32(bytes, offset, static_cast<u32>(value >> 32));
  put32(bytes, offset + 4, static_cast<u32>(value));
}

void append32(Bytes &bytes, u32 value) {
  const auto at = bytes.size();
  bytes.resize(at + 4);
  put32(bytes, at, value);
}

void copy_hash(Bytes &destination, std::size_t offset, std::span<const u8> input) {
  const auto hash = disc::detail::sha1(input);
  std::copy(hash.begin(), hash.end(), destination.begin() + static_cast<std::ptrdiff_t>(offset));
}

void seal(Bytes &bytes) {
  copy_hash(bytes, 0x10, std::span(bytes).subspan(0x48, 0xdc));
  put64(bytes, 0x2c, bytes.size());
  copy_hash(bytes, 0x34, std::span(bytes).first(0x34));
}

class TemporaryFile {
public:
  explicit TemporaryFile(std::span<const u8> bytes) {
    static unsigned sequence = 0;
    path = std::filesystem::temp_directory_path() /
           ("cupid-disc-test-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
            std::to_string(sequence++) + ".bin");
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    CHECK(stream.good());
  }

  ~TemporaryFile() {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }

  std::filesystem::path path;
};

Bytes header(bool wii, std::size_t size) {
  Bytes bytes(size);
  const std::array<u8, 6> id = wii ? std::array<u8, 6>{'R', 'T', 'S', 'E', '0', '1'}
                                   : std::array<u8, 6>{'G', 'T', 'S', 'E', '0', '1'};
  std::copy(id.begin(), id.end(), bytes.begin());
  put32(bytes, wii ? 0x18 : 0x1c, wii ? 0x5d1c9ea3 : 0xc2339f3d);
  const std::string title = "Synthetic disc fixture";
  std::copy(title.begin(), title.end(), bytes.begin() + 0x20);
  if (size > 0x430) {
    put32(bytes, 0x420, wii ? 0x200 : 0x800);
    put32(bytes, 0x424, wii ? 0x300 : 0xc00);
    put32(bytes, 0x428, wii ? 0x40 : 0x100);
  }
  return bytes;
}

Bytes properties(u32 codec) {
  if (codec == 3)
    return {93, 0, 0, 1, 0};
  if (codec == 4)
    return {8};
  return {};
}

Bytes compress(std::span<const u8> bytes, u32 codec) {
  if (!codec)
    return {bytes.begin(), bytes.end()};
  if (codec == 1) {
    Bytes result;
    append32(result, 0);
    append32(result, static_cast<u32>(bytes.size()));
    result.insert(result.end(), bytes.begin(), bytes.end());
    const auto hash = disc::detail::sha1(result);
    result.insert(result.end(), hash.begin(), hash.end());
    return result;
  }
  if (codec == 5) {
    Bytes result(ZSTD_compressBound(bytes.size()));
    const auto size = ZSTD_compress(result.data(), result.size(), bytes.data(), bytes.size(), 3);
    CHECK(!ZSTD_isError(size));
    result.resize(size);
    return result;
  }
#if CUPID_HAVE_BZIP2
  if (codec == 2) {
    Bytes result(bytes.size() * 2 + 1024);
    unsigned size = static_cast<unsigned>(result.size());
    CHECK(BZ2_bzBuffToBuffCompress(reinterpret_cast<char *>(result.data()), &size,
                                   reinterpret_cast<char *>(const_cast<u8 *>(bytes.data())),
                                   static_cast<unsigned>(bytes.size()), 5, 0, 0) == BZ_OK);
    result.resize(size);
    return result;
  }
#endif
#if CUPID_HAVE_LZMA
  if (codec == 3 || codec == 4) {
    lzma_options_lzma options{};
    CHECK(!lzma_lzma_preset(&options, 3));
    options.dict_size = 65536;
    std::array<lzma_filter, 2> filters{
        {{codec == 3 ? LZMA_FILTER_LZMA1 : LZMA_FILTER_LZMA2, &options},
         {LZMA_VLI_UNKNOWN, nullptr}}};
    Bytes result(bytes.size() * 2 + 1024);
    std::size_t size = 0;
    CHECK(lzma_raw_buffer_encode(filters.data(), nullptr, bytes.data(), bytes.size(), result.data(),
                                 &size, result.size()) == LZMA_OK);
    result.resize(size);
    return result;
  }
#endif
  throw std::runtime_error("test compressor unavailable");
}

struct Fixture {
  Bytes file, raw, plain;
  std::vector<std::size_t> group_offsets;
  std::size_t groups_offset = 0;
};

Bytes filesystem_table(bool wii) {
  Bytes table(0x100);
  const std::array<std::array<u32, 3>, 6> entries{{{0x01000000, 0, 6},
                                                   {0x01000000, 0, 4},
                                                   {4, wii ? 0x800u : 0x2000u, 5},
                                                   {0x0100000d, 1, 4},
                                                   {19, wii ? 0x840u : 0x2100u, 4},
                                                   {28, wii ? 0x880u : 0x2200u, 0}}};
  for (std::size_t entry = 0; entry < entries.size(); ++entry)
    for (std::size_t field = 0; field < 3; ++field)
      put32(table, entry * 12 + field * 4, entries[entry][field]);
  const Bytes names{'D', 'i', 'r',  0,    'C',  'a',  's',  'e',  '.', 'b', 'i', 'n', 0,
                    'E', 'm', 'p',  't',  'y',  0,    'r',  'o',  'o', 't', '.', 'b', 'i',
                    'n', 0,   0x83, 0x65, 0x83, 0x58, 0x83, 0x67, '.', 'b', 'i', 'n', 0};
  std::copy(names.begin(), names.end(), table.begin() + 72);
  return table;
}

Fixture fixture(u32 codec, bool wii = false, bool packed = false, bool zero_last = false,
                bool exception = false, bool filesystem = false, u32 sectors = 8, u32 split = 4,
                u32 requested_chunk = 0) {
  const bool wia = codec == 1;
  const u32 chunk = requested_chunk ? requested_chunk : (wia ? 0x200000 : 0x8000);
  Fixture result;
  result.raw = header(wii, wii ? 0x60000 : (wia ? 0x208000 : 0x28000));
  for (std::size_t i = 0x480; i < result.raw.size(); ++i)
    result.raw[i] = static_cast<u8>((i * 37 + i / 31) % 251);
  if (wii) {
    std::fill(result.raw.begin() + 0x40000, result.raw.end(), 0);
    put32(result.raw, 0x40000, 1);
    put32(result.raw, 0x40004, 0x40020 / 4);
    put32(result.raw, 0x40020, 0x50000 / 4);
    put32(result.raw, 0x40024, 0);
    put32(result.raw, 0x502b8, 0x10000 / 4);
    put32(result.raw, 0x502bc, sectors * 0x8000 / 4);
    put32(result.raw, 0x502a4, 0x208);
    put32(result.raw, 0x502a8, 0x2c0 / 4);
    put32(result.raw, 0x502c0, 0x10001);
    put64(result.raw, 0x502c0 + 0x184, 0x0000000100000031);
    put32(result.raw, 0x50000, 0x10001);
    put64(result.raw, 0x501dc, 0x0001000054535445);
    put64(result.raw, 0x502c0 + 0x18c, 0x0001000054535445);
    put16(result.raw, 0x502c0 + 0x1dc, 17);
    put16(result.raw, 0x502c0 + 0x1de, 1);
    put16(result.raw, 0x502c0 + 0x1e0, 2);
    put32(result.raw, 0x502c0 + 0x1e4, 7);
    put16(result.raw, 0x502c0 + 0x1e8, 2);
    put16(result.raw, 0x502c0 + 0x1ea, 1);
    put64(result.raw, 0x502c0 + 0x1ec, sectors * 0x7c00);
    put32(result.raw, 0x502ac, 0x20);
    put32(result.raw, 0x502b0, 0x500 / 4);
    std::fill(result.raw.begin() + 0x50500, result.raw.begin() + 0x50520, 0x55);
    result.plain = header(true, sectors * 0x7c00);
    for (std::size_t i = 0x480; i < result.plain.size(); ++i)
      result.plain[i] = static_cast<u8>((i * 13 + i / 7) % 253);
  }
  if (filesystem) {
    auto &game = wii ? result.plain : result.raw;
    const auto table = filesystem_table(wii);
    std::copy(table.begin(), table.end(), game.begin() + 0xc00);
    const std::array<u8, 5> content{'H', 'e', 'l', 'l', 'o'};
    std::copy(content.begin(), content.end(), game.begin() + 0x2000);
  }
  result.file.resize(0x124);
  result.file[0] = wia ? 'W' : 'R';
  result.file[1] = wia ? 'I' : 'V';
  result.file[2] = wia ? 'A' : 'Z';
  result.file[3] = 1;
  put32(result.file, 4, 0x1000000);
  put32(result.file, 8, wia ? 0x1000000 : 0x30000);
  put32(result.file, 0xc, 0xdc);
  put64(result.file, 0x24, wii ? 0x60000 + sectors * 0x8000 : result.raw.size());
  const std::size_t h = 0x48;
  put32(result.file, h, wii ? 2 : 1);
  put32(result.file, h + 4, codec);
  put32(result.file, h + 12, chunk);
  std::copy_n(result.raw.begin(), 0x80, result.file.begin() + h + 0x10);
  const auto props = properties(codec);
  result.file[h + 0xd4] = static_cast<u8>(props.size());
  std::copy(props.begin(), props.end(), result.file.begin() + h + 0xd5);
  Bytes partitions(wii ? 0x30 : 0);
  const u32 raw_groups = static_cast<u32>((result.raw.size() + chunk - 1) / chunk);
  const u32 sectors_per_chunk = chunk / 0x8000;
  const u32 first_groups = (split + sectors_per_chunk - 1) / sectors_per_chunk;
  const u32 second_groups = (sectors - split + sectors_per_chunk - 1) / sectors_per_chunk;
  if (wii) {
    for (std::size_t i = 0; i < 16; ++i)
      partitions[i] = static_cast<u8>(i);
    for (std::size_t segment = 0; segment < 2; ++segment) {
      const std::size_t base = 16 + segment * 16;
      put32(partitions, base, 0x60000 / 0x8000 + (segment ? split : 0));
      put32(partitions, base + 4, segment ? sectors - split : split);
      put32(partitions, base + 8, raw_groups + (segment ? first_groups : 0));
      put32(partitions, base + 12, segment ? second_groups : first_groups);
    }
  }
  put32(result.file, h + 0x90, wii ? 1 : 0);
  put32(result.file, h + 0x94, 0x30);
  put64(result.file, h + 0x98, result.file.size());
  copy_hash(result.file, h + 0xa0, partitions);
  result.file.insert(result.file.end(), partitions.begin(), partitions.end());
  Bytes raw_table(24);
  put64(raw_table, 0, 0x80);
  put64(raw_table, 8, result.raw.size() - 0x80);
  put32(raw_table, 20, raw_groups);
  auto encoded = compress(raw_table, codec);
  put32(result.file, h + 0xb4, 1);
  put64(result.file, h + 0xb8, result.file.size());
  put32(result.file, h + 0xc0, static_cast<u32>(encoded.size()));
  result.file.insert(result.file.end(), encoded.begin(), encoded.end());
  const u32 total_groups = raw_groups + (wii ? first_groups + second_groups : 0);
  Bytes groups(static_cast<std::size_t>(total_groups) * (wia ? 8 : 12));
  for (u32 index = 0; index < total_groups; ++index) {
    const bool partition = index >= raw_groups;
    const u32 local = partition ? index - raw_groups : index;
    const u32 start_sector = local < first_groups
                                 ? local * sectors_per_chunk
                                 : split + (local - first_groups) * sectors_per_chunk;
    const u32 end_sector = local < first_groups ? split : sectors;
    const std::size_t start =
        partition ? start_sector * 0x7c00 : static_cast<std::size_t>(local) * chunk;
    const auto &input_source = partition ? result.plain : result.raw;
    const std::size_t count = partition
                                  ? std::min(sectors_per_chunk, end_sector - start_sector) * 0x7c00
                                  : std::min<std::size_t>(chunk, input_source.size() - start);
    const auto data = std::span(input_source).subspan(start, count);
    const bool compressed = codec && (wia || index % 2 == 0 || partition);
    Bytes payload;
    if (partition) {
      if (chunk > 0x8000) {
        for (u32 list = 0; list < std::max(1u, chunk / 0x200000); ++list) {
          const u32 available = static_cast<u32>(data.size() / 0x7c00);
          const bool present = exception && list * 64 < available;
          Bytes list_payload(present ? 46 : 2);
          put16(list_payload, 0, present ? 2 : 0);
          if (present) {
            const u32 target = std::min(63u, available - list * 64 - 1);
            put16(list_payload, 0 + 2, static_cast<u16>(target * 0x400 + 0x3d0));
            put16(list_payload, 0 + 24, static_cast<u16>(target * 0x400 + 0x3d8));
            for (unsigned i = 0; i < 20; ++i) {
              list_payload[4 + i] = static_cast<u8>(0xa0 + list * 3 + i);
              list_payload[26 + i] = static_cast<u8>(0xc0 + list * 3 + i);
            }
          }
          payload.insert(payload.end(), list_payload.begin(), list_payload.end());
        }
      } else {
        payload.resize(exception && local == 0 ? 24 : 2);
        if (exception && local == 0) {
          put16(payload, 0, 1);
          put16(payload, 2, 0x280);
          for (std::size_t i = 4; i < 24; ++i)
            payload[i] = static_cast<u8>(0xa0 + i);
        }
      }
      if (!compressed || codec == 1)
        while (payload.size() % 4)
          payload.push_back(0);
    }
    const auto exception_prefix = payload.size();
    u32 packed_size = 0;
    if (packed) {
      append32(payload, static_cast<u32>(data.size()));
      payload.insert(payload.end(), data.begin(), data.end());
      packed_size = static_cast<u32>(data.size() + 4);
    } else
      payload.insert(payload.end(), data.begin(), data.end());
    auto stored = compress(payload, compressed ? codec : 0);
    if (partition && codec == 1) {
      auto body = compress(std::span(payload).subspan(exception_prefix), 1);
      body.resize(body.size() - 20);
      stored.assign(payload.begin(),
                    payload.begin() + static_cast<std::ptrdiff_t>(exception_prefix));
      stored.insert(stored.end(), body.begin(), body.end());
      const auto digest = disc::detail::sha1(stored);
      stored.insert(stored.end(), digest.begin(), digest.end());
    }

    while (result.file.size() % 4)
      result.file.push_back(0);
    const std::size_t base = static_cast<std::size_t>(index) * (wia ? 8 : 12);
    result.group_offsets.push_back(result.file.size());
    put32(groups, base, static_cast<u32>(result.file.size() / 4));
    put32(groups, base + 4,
          zero_last && index + 1 == total_groups
              ? 0
              : static_cast<u32>(stored.size()) | (!wia && compressed ? 0x80000000 : 0));
    if (!wia)
      put32(groups, base + 8, packed_size);
    result.file.insert(result.file.end(), stored.begin(), stored.end());
  }
  result.groups_offset = result.file.size();
  encoded = compress(groups, codec);
  put32(result.file, h + 0xc4, total_groups);
  put64(result.file, h + 0xc8, result.file.size());
  put32(result.file, h + 0xd0, static_cast<u32>(encoded.size()));
  result.file.insert(result.file.end(), encoded.begin(), encoded.end());
  seal(result.file);
  return result;
}

void crypto_tests() {
  const std::array<u8, 20> empty{0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
                                 0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09};
  CHECK(disc::detail::sha1({}) == empty);
  const std::array<u8, 3> abc{'a', 'b', 'c'};
  const std::array<u8, 20> digest{0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                                  0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
  CHECK(disc::detail::sha1(abc) == digest);
  const std::array<u8, 16> key{0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                               0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
  const std::array<u8, 16> iv{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  // NIST SP 800-38A F.2.1 and F.2.2 cover all four CBC blocks.
  const std::array<u8, 64> plain{0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e,
                                 0x11, 0x73, 0x93, 0x17, 0x2a, 0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03,
                                 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51, 0x30,
                                 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19,
                                 0x1a, 0x0a, 0x52, 0xef, 0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b,
                                 0x17, 0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10};
  const std::array<u8, 64> cipher{0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46, 0xce, 0xe9, 0x8e,
                                  0x9b, 0x12, 0xe9, 0x19, 0x7d, 0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72,
                                  0x19, 0xee, 0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2, 0x73,
                                  0xbe, 0xd6, 0xb8, 0xe3, 0xc1, 0x74, 0x3b, 0x71, 0x16, 0xe6, 0x9e,
                                  0x22, 0x22, 0x95, 0x16, 0x3f, 0xf1, 0xca, 0xa1, 0x68, 0x1f, 0xac,
                                  0x09, 0x12, 0x0e, 0xca, 0x30, 0x75, 0x86, 0xe1, 0xa7};
  const disc::detail::Aes128 aes(key);
  std::array<u8, 64> output{};
  aes.encrypt_cbc(plain, output, iv);
  CHECK(output == cipher);
  aes.decrypt_cbc(cipher, output, iv);
  CHECK(output == plain);
  aes.encrypt_cbc(output, output, iv);
  CHECK(output == cipher);
  aes.decrypt_cbc(output, output, iv);
  CHECK(output == plain);
  for (std::size_t alignment = 1; alignment < 16; ++alignment) {
    std::array<u8, 96> input{}, unaligned{};
    std::copy(plain.begin(), plain.end(), input.begin() + static_cast<std::ptrdiff_t>(alignment));
    unaligned.fill(0xa5);
    const auto before = input;
    aes.encrypt_cbc(std::span(input).subspan(alignment, plain.size()),
                    std::span(unaligned).subspan(alignment, plain.size()), iv);
    CHECK(input == before);
    CHECK(std::equal(cipher.begin(), cipher.end(),
                     unaligned.begin() + static_cast<std::ptrdiff_t>(alignment)));
    aes.decrypt_cbc(std::span(unaligned).subspan(alignment, plain.size()),
                    std::span(unaligned).subspan(alignment, plain.size()), iv);
    CHECK(std::equal(plain.begin(), plain.end(),
                     unaligned.begin() + static_cast<std::ptrdiff_t>(alignment)));
    CHECK(std::all_of(unaligned.begin(), unaligned.begin() + static_cast<std::ptrdiff_t>(alignment),
                      [](u8 value) { return value == 0xa5; }));
    CHECK(std::all_of(unaligned.begin() + static_cast<std::ptrdiff_t>(alignment + plain.size()),
                      unaligned.end(), [](u8 value) { return value == 0xa5; }));
  }
  std::copy(iv.begin(), iv.end(), output.begin());
  aes.encrypt_cbc(plain, output, std::span<const u8, 16>(output.data(), 16));
  CHECK(output == cipher);
  std::copy(iv.begin(), iv.end(), output.begin());
  aes.decrypt_cbc(cipher, output, std::span<const u8, 16>(output.data(), 16));
  CHECK(output == plain);
  aes.encrypt_cbc(std::span(plain).first(0), std::span(output).first(0), iv);
  aes.decrypt_cbc(std::span(cipher).first(0), std::span(output).first(0), iv);
  CHECK(output == plain);
  for (const auto &sizes :
       {std::pair<std::size_t, std::size_t>{15, 15}, {16, 32}, {32, 16}, {0, 16}, {16, 0}}) {
    output.fill(0xa5);
    const auto before = output;
    CHECK_THROWS(aes.encrypt_cbc(std::span(plain).first(sizes.first),
                                 std::span(output).first(sizes.second), iv));
    CHECK(output == before);
    CHECK_THROWS(aes.decrypt_cbc(std::span(cipher).first(sizes.first),
                                 std::span(output).first(sizes.second), iv));
    CHECK(output == before);
  }
}

void raw_tests() {
  auto bytes = header(false, 0x1000);
  TemporaryFile file(bytes);
  disc::Image image(file.path);
  CHECK(image.format() == "ISO");
  CHECK(!image.wii_metadata().has_value());
  CHECK(image.header().console == Console::GameCube);
  CHECK(image.header().id == "GTSE01");
  CHECK(image.main_dol_offset() == 0x800);
  CHECK(image.fst_offset() == 0xc00);
  CHECK(image.fst_size() == 0x100);
  Bytes output(bytes.size());
  image.read(0, output);
  CHECK(output == bytes);
  image.read(image.size(), {});
  CHECK_THROWS(image.read(std::numeric_limits<u64>::max(), std::span(output).first(1)));
  CHECK_THROWS(image.read(image.size(), std::span(output).first(1)));
  bytes[0x1c] = 0;
  TemporaryFile invalid_file(bytes);
  CHECK_THROWS(disc::Image(invalid_file.path));
}

void read_bounds_tests() {
  const auto check = [](disc::Image &image) {
    std::array<u8, 32> output;
    output.fill(0xa5);
    for (bool game : {false, true}) {
      const u64 size = game ? image.game_size() : image.size();
      for (const u64 offset :
           {size - 1, size, std::numeric_limits<u64>::max(), u64(0x460a0000) << 2}) {
        bool out_of_range = false;
        try {
          if (game)
            image.read_game(offset, output);
          else
            image.read(offset, output);
        } catch (const std::out_of_range &) {
          out_of_range = true;
        } catch (const std::exception &) {
        }
        CHECK(out_of_range);
        CHECK(std::all_of(output.begin(), output.end(), [](u8 byte) { return byte == 0xa5; }));
      }
      if (game)
        image.read_game(size, {});
      else
        image.read(size, {});
    }
  };
  TemporaryFile raw(header(false, 0x1000));
  disc::Image iso(raw.path);
  check(iso);
  for (bool wii : {false, true}) {
    TemporaryFile packed(fixture(5, wii).file);
    disc::Image image(packed.path);
    check(image);
  }
}

void container_tests(u32 codec) {
  for (bool packed : {false, true}) {
    if (codec == 1 && packed)
      continue;
    const auto sample = fixture(codec, false, packed);
    TemporaryFile file(sample.file);
    disc::Image image(file.path);
    CHECK(image.format() == (codec == 1 ? "WIA" : "RVZ"));
    CHECK(image.size() == sample.raw.size());
    Bytes output(sample.raw.size());
    image.read(0, output);
    CHECK(output == sample.raw);
    for (u64 offset :
         std::array<u64, 5>{0, 0x7f, 0x7fff, 0xffff, static_cast<u64>(sample.raw.size() - 20)}) {
      const auto count = static_cast<std::size_t>(std::min<u64>(100, sample.raw.size() - offset));
      output.resize(count);
      image.read(offset, output);
      CHECK(std::equal(output.begin(), output.end(),
                       sample.raw.begin() + static_cast<std::ptrdiff_t>(offset)));
    }
  }
}

void wii_tests(u32 codec) {
  const auto sample = fixture(codec, true, true, false, true);
  TemporaryFile file(sample.file);
  disc::Image image(file.path);
  CHECK(image.header().console == Console::Wii);
  CHECK(image.game_partition_offset() == 0x50000);
  CHECK(image.required_ios() == 49);
  const auto metadata = image.wii_metadata();
  CHECK(metadata.has_value());
  CHECK(metadata->title_id == 0x0001000054535445);
  CHECK(metadata->ios == 49);
  CHECK(metadata->title_version == 17);
  CHECK(metadata->boot_index == 2);
  CHECK(metadata->ticket.size() == 0x2a4);
  CHECK(metadata->tmd.size() == 0x208);
  CHECK(metadata->contents.size() == 1);
  CHECK(metadata->contents[0].id == 7);
  CHECK(metadata->contents[0].index == 2);
  CHECK(metadata->contents[0].size == 8 * 0x7c00);
  CHECK(metadata->certificate_chain == Bytes(0x20, 0x55));
  CHECK(image.main_dol_offset() == 0x800);
  CHECK(image.game_size() == sample.plain.size());
  Bytes output(sample.plain.size());
  image.read_game(0, output);
  CHECK(output == sample.plain);
  output.resize(80);
  image.read_game(4 * 0x7c00 - 17, output);
  CHECK(std::equal(output.begin(), output.end(), sample.plain.begin() + 4 * 0x7c00 - 17));
  output.resize(0x40000);
  image.read(0x60000, output);
  std::array<u8, 16> key{};
  for (std::size_t i = 0; i < key.size(); ++i)
    key[i] = static_cast<u8>(i);
  disc::detail::Aes128 aes(key);
  const std::array<u8, 16> zero{};
  Bytes hashes(0x400), plaintext(0x7c00);
  for (std::size_t sector = 0; sector < 8; ++sector) {
    const auto block = std::span(output).subspan(sector * 0x8000, 0x8000);
    aes.decrypt_cbc(block.first(0x400), hashes, zero);
    std::array<u8, 16> iv{};
    std::copy_n(block.begin() + 0x3d0, 16, iv.begin());
    aes.decrypt_cbc(block.subspan(0x400), plaintext, iv);
    CHECK(std::equal(plaintext.begin(), plaintext.end(),
                     sample.plain.begin() + static_cast<std::ptrdiff_t>(sector * 0x7c00)));
    const auto expected =
        disc::detail::sha1(std::span(sample.plain).subspan(sector * 0x7c00, 0x400));
    CHECK(std::equal(expected.begin(), expected.end(), hashes.begin()));
    if (sector == 0)
      for (std::size_t i = 0; i < 20; ++i)
        CHECK(hashes[0x280 + i] == 0xa4 + i);
  }
}

void wii_sector_cache_tests() {
  for (u32 codec : {0u, 2u, 3u, 4u, 5u}) {
    const auto sample = fixture(codec, true, true, false, true, false, 130, 64);
    TemporaryFile file(sample.file);
    disc::Image image(file.path);
    Bytes encrypted(130 * 0x8000);
    image.read(0x60000, encrypted);
    Bytes output, plain(0x7c00), hashes(0x400);
    output.resize(80);
    image.read(0x60000 - 17, output);
    CHECK(std::equal(output.begin(), output.begin() + 17, sample.raw.end() - 17));
    CHECK(std::equal(output.begin() + 17, output.end(), encrypted.begin()));
    std::array<u8, 16> key{};
    for (std::size_t i = 0; i < key.size(); ++i)
      key[i] = static_cast<u8>(i);
    const disc::detail::Aes128 aes(key);
    const std::array<u8, 16> zero{};
    for (u64 sector : std::array<u64, 12>{129, 64, 63, 0, 127, 1, 128, 65, 62, 64, 129, 63}) {
      const u64 offset = sector * 0x8000;
      output.resize(0x8000);
      image.read(0x60000 + offset, output);
      CHECK(std::equal(output.begin(), output.end(), encrypted.begin() + offset));
      aes.decrypt_cbc(std::span(output).first(0x400), hashes, zero);
      std::array<u8, 16> iv{};
      std::copy_n(output.begin() + 0x3d0, 16, iv.begin());
      aes.decrypt_cbc(std::span(output).subspan(0x400), plain, iv);
      CHECK(std::equal(plain.begin(), plain.end(), sample.plain.begin() + sector * 0x7c00));
      const auto expected = disc::detail::sha1(std::span(plain).first(0x400));
      CHECK(std::equal(expected.begin(), expected.end(), hashes.begin()));

      // Plaintext reads replace the decoded chunk without invalidating reconstructed sectors.
      image.read_game(((sector + 31) % 130) * 0x7c00, plain);
      for (u64 within : {u64{0}, u64{0x3cf}, u64{0x3ff}, u64{0x400}, u64{0x7fff}}) {
        const auto count = std::min<u64>(37, encrypted.size() - offset - within);
        output.resize(static_cast<std::size_t>(count));
        image.read(0x60000 + offset + within, output);
        CHECK(std::equal(output.begin(), output.end(), encrypted.begin() + offset + within));
      }
    }
  }
}

void wii_future_sector_exceptions() {
  const auto make = [](u32 sector, u16 offset) {
    auto sample = fixture(0, true, false, false, false, false, 130, 64);
    Bytes payload(24);
    put16(payload, 0, 1);
    put16(payload, 2, offset);
    for (std::size_t i = 4; i < payload.size(); ++i)
      payload[i] = static_cast<u8>(0x80 + i);
    const auto plain = std::span(sample.plain).subspan(sector * 0x7c00, 0x7c00);
    payload.insert(payload.end(), plain.begin(), plain.end());
    while (sample.file.size() % 4)
      sample.file.push_back(0);
    const u32 raw_groups = static_cast<u32>(sample.raw.size() / 0x8000);
    const auto group = sample.groups_offset + (raw_groups + sector) * 12;
    put32(sample.file, group, static_cast<u32>(sample.file.size() / 4));
    put32(sample.file, group + 4, static_cast<u32>(payload.size()));
    sample.file.insert(sample.file.end(), payload.begin(), payload.end());
    seal(sample.file);
    return sample;
  };
  {
    const auto sample = make(63, 0x3d0);
    TemporaryFile file(sample.file);
    disc::Image image(file.path);
    Bytes output(0x8000), hashes(0x400), plain(0x7c00);
    image.read(0x60000, output);
    image.read(0x60000 + 63 * 0x8000, output);
    std::array<u8, 16> key{};
    for (std::size_t i = 0; i < key.size(); ++i)
      key[i] = static_cast<u8>(i);
    const disc::detail::Aes128 aes(key);
    const std::array<u8, 16> zero{};
    aes.decrypt_cbc(std::span(output).first(0x400), hashes, zero);
    for (std::size_t i = 0; i < 20; ++i)
      CHECK(hashes[0x3d0 + i] == 0x84 + i);
    std::array<u8, 16> iv{};
    std::copy_n(output.begin() + 0x3d0, 16, iv.begin());
    aes.decrypt_cbc(std::span(output).subspan(0x400), plain, iv);
    CHECK(std::equal(plain.begin(), plain.end(), sample.plain.begin() + 63 * 0x7c00));
  }
  for (u32 sector : {63u, 64u}) {
    const auto sample = make(sector, 0x3f0);
    TemporaryFile file(sample.file);
    disc::Image image(file.path);
    Bytes output(37, 0xa5), plain(0x7c00);
    if (sector == 64)
      image.read(0x60000, output);
    output.assign(37, 0xa5);
    for (unsigned retry = 0; retry < 2; ++retry) {
      CHECK_THROWS(image.read(0x60000 + (sector / 64) * 0x200000, output));
      CHECK(output == Bytes(37, 0xa5));
      image.read_game(0, plain);
      CHECK(std::equal(plain.begin(), plain.end(), sample.plain.begin()));
    }
  }
}

void wii_partition_cache_tests() {
  auto sample = fixture(0, true, false, false, true, false, 130, 64);
  constexpr u64 second = 0x60000 + 130 * 0x8000;
  Bytes partitions(sample.file.begin() + 0x124, sample.file.begin() + 0x154);
  partitions.insert(partitions.end(), sample.file.begin() + 0x124, sample.file.begin() + 0x154);
  std::array<u8, 16> key{};
  for (std::size_t i = 0; i < key.size(); ++i)
    key[i] = partitions[0x30 + i] = static_cast<u8>(i ^ 0x5a);
  put32(partitions, 0x30 + 16, second / 0x8000);
  put32(partitions, 0x30 + 32, second / 0x8000 + 64);
  put32(sample.file, 0x48 + 0x90, 2);
  put64(sample.file, 0x48 + 0x98, sample.file.size());
  copy_hash(sample.file, 0x48 + 0xa0, partitions);
  sample.file.insert(sample.file.end(), partitions.begin(), partitions.end());
  put64(sample.file, 0x24, second + 130 * 0x8000);
  seal(sample.file);
  TemporaryFile file(sample.file);
  disc::Image image(file.path);
  Bytes first(0x8000), other(0x8000), output(0x8000), plain(0x7c00);
  image.read(0x60000 + 63 * 0x8000, first);
  image.read(second + 63 * 0x8000, other);
  CHECK(other != first);
  const disc::detail::Aes128 aes(key);
  std::array<u8, 16> iv{};
  std::copy_n(other.begin() + 0x3d0, 16, iv.begin());
  aes.decrypt_cbc(std::span(other).subspan(0x400), plain, iv);
  CHECK(std::equal(plain.begin(), plain.end(), sample.plain.begin() + 63 * 0x7c00));
  for (unsigned retry = 0; retry < 2; ++retry) {
    image.read_game(0, plain);
    image.read(0x60000 + 63 * 0x8000, output);
    CHECK(output == first);
    image.read(second + 63 * 0x8000, output);
    CHECK(output == other);
  }
  Bytes left(37), right(37), crossing(74);
  image.read(second - 37, left);
  image.read(second, right);
  image.read(second - 37, crossing);
  CHECK(std::equal(left.begin(), left.end(), crossing.begin()));
  CHECK(std::equal(right.begin(), right.end(), crossing.begin() + 37));
}

void wii_chunk_exception_tests() {
  for (u32 codec : {0u, 1u, 2u, 3u, 4u, 5u}) {
    const auto sample = fixture(codec, true, false, false, true, false, 194, 128, 0x400000);
    TemporaryFile file(sample.file);
    disc::Image image(file.path);
    Bytes encrypted(194 * 0x8000), output(0x8000), plain(0x7c00), hashes(0x400);
    image.read(0x60000, encrypted);
    std::array<u8, 16> key{};
    for (std::size_t i = 0; i < key.size(); ++i)
      key[i] = static_cast<u8>(i);
    const disc::detail::Aes128 aes(key);
    const std::array<u8, 16> zero{};
    for (u64 sector : {63ull, 193ull, 64ull, 127ull, 128ull, 191ull, 0ull, 192ull, 63ull, 193ull}) {
      image.read_game(((sector + 31) % 194) * 0x7c00, plain);
      image.read(0x60000 + sector * 0x8000, output);
      CHECK(std::equal(output.begin(), output.end(), encrypted.begin() + sector * 0x8000));
      aes.decrypt_cbc(std::span(output).first(0x400), hashes, zero);
      std::array<u8, 16> iv{};
      std::copy_n(output.begin() + 0x3d0, 16, iv.begin());
      aes.decrypt_cbc(std::span(output).subspan(0x400), plain, iv);
      CHECK(std::equal(plain.begin(), plain.end(), sample.plain.begin() + sector * 0x7c00));
      if (sector == 63 || sector == 127 || sector == 191 || sector == 193) {
        const u64 list = (sector % 128) / 64;
        for (std::size_t i = 0; i < 8; ++i)
          CHECK(hashes[0x3d0 + i] == 0xa0 + list * 3 + i);
        for (std::size_t i = 0; i < 20; ++i)
          CHECK(hashes[0x3d8 + i] == 0xc0 + list * 3 + i);
      }
    }
  }
}

void wii_exception_error_priority_tests() {
  for (bool later_bad : {false, true}) {
    auto sample = fixture(0, true, false, false, false, false, 130, 64);
    const auto replace = [&](u32 sector, u16 offset) {
      Bytes payload(24);
      put16(payload, 0, 1);
      put16(payload, 2, offset);
      for (std::size_t i = 4; i < payload.size(); ++i)
        payload[i] = static_cast<u8>(0x90 + i);
      const auto plain = std::span(sample.plain).subspan(sector * 0x7c00, 0x7c00);
      payload.insert(payload.end(), plain.begin(), plain.end());
      while (sample.file.size() % 4)
        sample.file.push_back(0);
      const auto row = sample.groups_offset + (sample.raw.size() / 0x8000 + sector) * 12;
      put32(sample.file, row, static_cast<u32>(sample.file.size() / 4));
      put32(sample.file, row + 4, static_cast<u32>(payload.size()));
      sample.file.insert(sample.file.end(), payload.begin(), payload.end());
    };
    replace(4, 63 * 0x400 + 0x280);
    if (later_bad)
      replace(63, 0x3f0);
    seal(sample.file);
    TemporaryFile file(sample.file);
    disc::Image image(file.path);
    const std::string expected =
        later_bad ? "Disc image: hash exception crosses a sector hash boundary"
                  : "Disc image: hash exception lies outside its reconstructed group";
    for (unsigned retry = 0; retry < 3; ++retry) {
      Bytes valid(37);
      image.read(0x60000 + 64 * 0x8000, valid);
      Bytes output(37, 0xa5);
      std::string reason;
      try {
        image.read(0x60000, output);
      } catch (const std::runtime_error &error) {
        reason = error.what();
      }
      CHECK(reason == expected);
      CHECK(output == Bytes(37, 0xa5));
      Bytes plain(0x7c00);
      image.read_game(0, plain);
      CHECK(std::equal(plain.begin(), plain.end(), sample.plain.begin()));
    }
  }
}

void malformed_tests() {
  const auto sample = fixture(0);
  for (std::size_t offset : {std::size_t{0x10}, std::size_t{0x34}, std::size_t{0x48}}) {
    auto bytes = sample.file;
    bytes[offset] ^= 1;
    TemporaryFile file(bytes);
    CHECK_THROWS(disc::Image(file.path));
  }
  for (const auto &change : std::array<std::pair<std::size_t, u32>, 5>{
           {{0x54, 0}, {0x54, 0x9000}, {0x4c, 1}, {0x10c, 0xffffffff}, {0xfc, 0xffffffff}}}) {
    auto bytes = sample.file;
    put32(bytes, change.first, change.second);
    seal(bytes);
    TemporaryFile file(bytes);
    CHECK_THROWS(disc::Image(file.path));
  }
  auto truncated = sample.file;
  truncated.resize(0x90);
  TemporaryFile short_file(truncated);
  CHECK_THROWS(disc::Image(short_file.path));
  auto bytes = sample.file;
  put32(bytes, sample.groups_offset + 4, 1); // Too-short data group: fail when it is decoded.
  TemporaryFile file(bytes);
  disc::Image image(file.path);
  Bytes output(200);
  CHECK_THROWS(image.read(0x80, output));
  bytes = sample.file;
  put32(bytes, sample.groups_offset, 0xffffffff);
  TemporaryFile outside(bytes);
  CHECK_THROWS(disc::Image(outside.path));
  const auto zero = fixture(0, false, false, true);
  TemporaryFile zero_file(zero.file);
  disc::Image zeros(zero_file.path);
  output.resize(100);
  zeros.read(zeros.size() - 100, output);
  CHECK(std::all_of(output.begin(), output.end(), [](u8 v) { return v == 0; }));
  auto compressed = fixture(5);
  compressed.file[compressed.group_offsets[0]] ^= 0xff;
  TemporaryFile corrupt(compressed.file);
  disc::Image corrupt_image(corrupt.path);
  CHECK_THROWS(corrupt_image.read(0x80, output));
}

void padding_tests() {
  auto sample = fixture(0);
  constexpr std::size_t start = 0x20000;
  Bytes packet;
  append32(packet, 18);
  packet.insert(packet.end(), sample.raw.begin() + start, sample.raw.begin() + start + 18);
  append32(packet, 0x80000000u | (0x8000 - 18));
  for (unsigned i = 0; i < 68; ++i)
    packet.push_back(static_cast<u8>(i));
  while (sample.file.size() % 4)
    sample.file.push_back(0);
  const std::size_t payload = sample.file.size();
  const std::size_t group = sample.groups_offset + 4 * 12;
  put32(sample.file, group, static_cast<u32>(payload / 4));
  put32(sample.file, group + 4, static_cast<u32>(packet.size()));
  put32(sample.file, group + 8, static_cast<u32>(packet.size()));
  sample.file.insert(sample.file.end(), packet.begin(), packet.end());
  seal(sample.file);
  TemporaryFile file(sample.file);
  disc::Image image(file.path);
  Bytes output(0x8000 - 18);
  image.read(start + 18, output);
  // Behavioral reference: LaggedFibonacciGenerator::SetSeed/Forward/GetBytes, pinned in
  // docs/disc-reference.md.
  const disc::detail::Digest expected{0x12, 0xda, 0x0f, 0x2a, 0x40, 0x26, 0x48, 0x48, 0x5e, 0x77,
                                      0x02, 0x43, 0xaf, 0x4c, 0xdc, 0xc5, 0x3b, 0x12, 0x9e, 0xc4};
  CHECK(disc::detail::sha1(output) == expected);
  auto invalid_size = sample.file;
  put32(invalid_size, payload + 22, 0x80008000);
  TemporaryFile too_large(invalid_size);
  disc::Image malformed(too_large.path);
  CHECK_THROWS(malformed.read(start, output));
  auto truncated = sample.file;
  put32(truncated, group + 4, static_cast<u32>(packet.size() - 1));
  put32(truncated, group + 8, static_cast<u32>(packet.size() - 1));
  TemporaryFile no_seed(truncated);
  disc::Image missing(no_seed.path);
  CHECK_THROWS(missing.read(start, output));
}

void raw_wii_tests() {
  const auto sample = fixture(0, true);
  auto raw = sample.raw;
  raw.resize(0xa0000);
  raw[0x61] = 1;
  for (std::size_t sector = 0; sector < 8; ++sector)
    std::copy_n(sample.plain.begin() + static_cast<std::ptrdiff_t>(sector * 0x7c00), 0x7c00,
                raw.begin() + static_cast<std::ptrdiff_t>(0x60000 + sector * 0x8000 + 0x400));
  TemporaryFile hashed(raw);
  disc::Image image(hashed.path);
  Bytes output(sample.plain.size());
  image.read_game(0, output);
  CHECK(output == sample.plain);
  raw[0x60] = 1;
  put32(raw, 0x502bc, static_cast<u32>(sample.plain.size() / 4));
  std::copy(sample.plain.begin(), sample.plain.end(), raw.begin() + 0x60000);
  TemporaryFile unhashed(raw);
  disc::Image plain(unhashed.path);
  plain.read_game(0, output);
  CHECK(output == sample.plain);
  raw[0x60] = 0;
  raw[0x61] = 0;
  put32(raw, 0x502bc, 0x40000 / 4);
  TemporaryFile encrypted(raw);
  disc::Image protected_disc(encrypted.path);
  CHECK_THROWS(protected_disc.read_game(0, output));
  auto fail_metadata = [&](Bytes bytes) {
    bytes.resize(0xa0000);
    TemporaryFile file(bytes);
    disc::Image malformed(file.path);
    CHECK_THROWS(malformed.wii_metadata());
  };
  for (const auto &change :
       std::array<std::pair<std::size_t, u32>, 5>{{{0x50000, 0},
                                                   {0x502a4, 0xffffffff},
                                                   {0x502ac, 0x500000},
                                                   {0x502c0 + 0x18c, 0},
                                                   {0x502c0 + 0x1dc, 0xffffffff}}}) {
    auto bytes = sample.raw;
    put32(bytes, change.first, change.second);
    fail_metadata(bytes);
  }
  auto versioned = sample.raw;
  versioned[0x501bc] = 1;
  fail_metadata(versioned);
}

void filesystem_tests() {
  for (bool wii : {false, true}) {
    const auto sample = fixture(wii ? 5 : 0, wii, false, false, false, true);
    TemporaryFile file(sample.file);
    disc::Image image(file.path);
    disc::Filesystem fs(image);
    CHECK(fs.entries().size() == 6);
    CHECK(fs.find("///")->directory);
    CHECK(fs.find("//dIR///case.BIN/")->size == 5);
    CHECK(fs.find("dir/empty")->directory);
    CHECK(fs.find("not-present") == nullptr);
    CHECK(fs.find("root.bin")->offset == 0x2100);
    CHECK(fs.find("\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88.bin") != nullptr);
    Bytes content(5);
    fs.read_file("Dir/Case.bin", 0, content);
    CHECK(content == Bytes({'H', 'e', 'l', 'l', 'o'}));
    fs.read_file("Dir/Case.bin", 5, {});
    CHECK_THROWS(fs.read_file("Dir/Case.bin", 1, content));
    CHECK_THROWS(fs.read_file("Dir", 0, content));
    CHECK_THROWS(fs.read_file("missing", 0, content));
  }
  const auto table = filesystem_table(false);
  auto fail = [&](Bytes malformed) {
    auto iso = header(false, 0x4000);
    std::copy(malformed.begin(), malformed.end(), iso.begin() + 0xc00);
    TemporaryFile file(iso);
    disc::Image image(file.path);
    CHECK_THROWS(disc::Filesystem(image));
  };
  for (const auto &change : std::array<std::pair<std::size_t, u32>, 6>{
           {{0, 0}, {8, 0}, {12 + 4, 99}, {12 + 8, 1}, {24, 0xffffff}, {24 + 4, 0xffffffff}}}) {
    auto malformed = table;
    put32(malformed, change.first, change.second);
    fail(malformed);
  }
  auto malformed = table;
  malformed.back() = 1;
  fail(malformed);
  malformed = table;
  malformed[72] = '.';
  malformed[73] = '.';
  malformed[74] = 0;
  fail(malformed);
}
} // namespace

int main(int argc, char **argv) {
  return run_tests([&] {
    crypto_tests();
    raw_tests();
    read_bounds_tests();
    for (u32 codec : {0u, 1u, 2u, 3u, 4u, 5u})
      container_tests(codec);
    wii_tests(0);
    wii_tests(5);
    wii_sector_cache_tests();
    wii_future_sector_exceptions();
    wii_partition_cache_tests();
    wii_chunk_exception_tests();
    wii_exception_error_priority_tests();
    malformed_tests();
    padding_tests();
    raw_wii_tests();
    filesystem_tests();
    for (int argument = 1; argument < argc; ++argument) {
      disc::Image image(argv[argument]);
      Bytes data(0x440);
      image.read_game(0, data);
      CHECK(image.main_dol_offset() > 0);
      CHECK(image.fst_offset() > 0);
      CHECK(image.fst_size() > 0);
      std::cout << image.header().id << " " << image.header().title << " " << image.format() << " "
                << image.compression() << " disc=" << image.size() << " game=" << image.game_size()
                << " dol=" << image.main_dol_offset() << "\n";
      data.resize(0x100);
      image.read_game(image.main_dol_offset(), data);
      disc::Filesystem filesystem(image);
      CHECK(filesystem.entries().size() > 1);
      const auto *banner = filesystem.find("opening.bnr");
      CHECK(banner != nullptr && !banner->directory);
      data.resize(static_cast<std::size_t>(std::min<u64>(banner->size, 4096)));
      filesystem.read_file("opening.bnr", 0, data);
      std::cout << "files=" << filesystem.entries().size() << " banner-bytes=" << data.size()
                << "\n";
      if (image.header().console == Console::Wii)
        CHECK(image.required_ios().has_value() && image.wii_metadata().has_value());
    }
  });
}
