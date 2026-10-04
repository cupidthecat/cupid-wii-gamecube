// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/disc/image.hpp"
#include "cupid/disc/crypto.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>
#include <zstd.h>
#if CUPID_HAVE_BZIP2
#include <bzlib.h>
#endif
#if CUPID_HAVE_LZMA
#include <lzma.h>
#endif

namespace cupid::disc {
namespace {
constexpr u64 sector_bytes = 0x8000;
constexpr u64 plaintext_bytes = 0x7c00;
constexpr u64 hash_bytes = 0x400;
constexpr u64 hash_group_bytes = 64 * sector_bytes;
constexpr u64 plain_group_bytes = 64 * plaintext_bytes;
constexpr u64 allocation_limit = 256 * 1024 * 1024;

[[noreturn]] void invalid(const std::string &reason) {
  throw std::runtime_error("Disc image: " + reason);
}

void require(bool condition, const std::string &reason) {
  if (!condition)
    invalid(reason);
}

void range(u64 offset, u64 length, u64 limit, const char *description) {
  require(offset <= limit && length <= limit - offset,
          std::string(description) + " is out of bounds");
}

u16 be16(std::span<const u8> bytes, std::size_t offset) {
  range(offset, 2, bytes.size(), "16-bit field");
  return static_cast<u16>((static_cast<u16>(bytes[offset]) << 8) | bytes[offset + 1]);
}

u32 be32(std::span<const u8> bytes, std::size_t offset) {
  range(offset, 4, bytes.size(), "32-bit field");
  u32 value = 0;
  for (std::size_t i = 0; i < 4; ++i)
    value = (value << 8) | bytes[offset + i];
  return value;
}

u64 be64(std::span<const u8> bytes, std::size_t offset) {
  return (static_cast<u64>(be32(bytes, offset)) << 32) | be32(bytes, offset + 4);
}

bool digest_equal(std::span<const u8> input, std::span<const u8> expected) {
  const auto actual = detail::sha1(input);
  return expected.size() == actual.size() &&
         std::equal(actual.begin(), actual.end(), expected.begin());
}

struct Exception {
  u16 offset;
  detail::Digest digest;
};

struct Segment {
  u64 start, size;
  u32 first_group, groups;
};

struct Partition {
  std::array<u8, 16> key{};
  std::array<Segment, 2> segments{};
};

struct Group {
  u64 offset;
  u32 size, packed;
  bool compressed;
};

struct Decoded {
  std::vector<u8> data;
  std::vector<std::vector<Exception>> exceptions;
};

class PaddingGenerator {
public:
  explicit PaddingGenerator(std::span<const u8> seed) {
    require(seed.size() == 68, "invalid packed padding seed");
    for (std::size_t i = 0; i < 17; ++i)
      state_[i] = be32(seed, i * 4);
    for (std::size_t i = 17; i < state_.size(); ++i)
      state_[i] = (state_[i - 17] << 23) ^ (state_[i - 16] >> 9) ^ state_[i - 1];
    for (int warmup = 0; warmup < 4; ++warmup)
      advance();
  }

  u8 next() {
    if (position_ == state_.size() * 4) {
      advance();
      position_ = 0;
    }
    constexpr std::array<unsigned, 4> shifts{24, 18, 8, 0};
    const u8 result = static_cast<u8>(state_[position_ / 4] >> shifts[position_ % 4]);
    ++position_;
    return result;
  }

  void skip(u64 bytes) {
    while (bytes--)
      (void)next();
  }

private:
  std::array<u32, 521> state_{};
  std::size_t position_ = 0;

  void advance() {
    for (std::size_t i = 0; i < state_.size(); ++i)
      state_[i] ^= state_[(i + state_.size() - 32) % state_.size()];
  }
};

std::vector<u8> unpack(std::span<const u8> input, std::size_t expected, u64 offset) {
  std::vector<u8> result;
  result.reserve(expected);
  std::size_t cursor = 0;
  while (cursor < input.size()) {
    const u32 tag = be32(input, cursor);
    cursor += 4;
    const std::size_t count = tag & 0x7fffffff;
    range(result.size(), count, expected, "packed output");
    if (tag & 0x80000000) {
      range(cursor, 68, input.size(), "packed padding seed");
      PaddingGenerator generator(input.subspan(cursor, 68));
      generator.skip((offset + result.size()) % sector_bytes);
      cursor += 68;
      for (std::size_t i = 0; i < count; ++i)
        result.push_back(generator.next());
    } else {
      range(cursor, count, input.size(), "packed literal");
      result.insert(result.end(), input.begin() + static_cast<std::ptrdiff_t>(cursor),
                    input.begin() + static_cast<std::ptrdiff_t>(cursor + count));
      cursor += count;
    }
  }
  require(result.size() == expected, "packed data expands to an incorrect size");
  return result;
}
} // namespace

class Image::Impl {
public:
  explicit Impl(const std::filesystem::path &path) : file_(path, std::ios::binary) {
    require(file_.is_open(), "cannot open input file");
    file_.seekg(0, std::ios::end);
    const auto end = file_.tellg();
    require(end >= 0, "cannot determine file size");
    stored_size_ = static_cast<u64>(end);
    require(stored_size_ >= 0x80, "truncated disc header");
    const auto magic = physical(0, 4);
    rvz_ = magic == std::vector<u8>{'R', 'V', 'Z', 1};
    container_ = rvz_ || magic == std::vector<u8>{'W', 'I', 'A', 1};
    if (container_)
      initialize_container();
    else
      logical_size_ = stored_size_;
    std::array<u8, 0x80> bytes{};
    read(0, bytes);
    if (be32(bytes, 0x18) == 0x5d1c9ea3)
      metadata_.console = Console::Wii;
    else
      require(be32(bytes, 0x1c) == 0xc2339f3d, "unrecognized disc magic");
    if (container_)
      require(disc_type_ == (metadata_.console == Console::Wii ? 2u : 1u) || disc_type_ == 0,
              "container console does not match disc header");
    metadata_.id.assign(reinterpret_cast<const char *>(bytes.data()), 6);
    metadata_.disc_number = bytes[6];
    metadata_.revision = bytes[7];
    const auto title_end = std::find(bytes.begin() + 0x20, bytes.end(), 0);
    metadata_.title.assign(bytes.begin() + 0x20, title_end);
    game_size_ = logical_size_;
    has_hashes_ = bytes[0x60] == 0;
    encrypted_ = bytes[0x61] == 0;
    if (metadata_.console == Console::Wii)
      initialize_game_partition();
  }

  void read(u64 offset, std::span<u8> output) {
    range(offset, output.size(), logical_size_, "disc read");
    if (!container_) {
      physical_read(offset, output);
      return;
    }
    if (offset < disc_header_.size()) {
      const auto count =
          static_cast<std::size_t>(std::min<u64>(output.size(), disc_header_.size() - offset));
      std::copy_n(disc_header_.begin() + static_cast<std::ptrdiff_t>(offset), count,
                  output.begin());
      offset += count;
      output = output.subspan(count);
    }
    while (!output.empty()) {
      const auto raw = std::find_if(raw_.begin(), raw_.end(), [offset](const Segment &item) {
        return offset >= item.start && offset - item.start < item.size;
      });
      if (raw != raw_.end()) {
        const auto count = static_cast<std::size_t>(
            std::min<u64>(output.size(), raw->size - (offset - raw->start)));
        read_segment(*raw, offset, output.first(count), false, 0);
        offset += count;
        output = output.subspan(count);
        continue;
      }
      bool found = false;
      for (std::size_t i = 0; i < partitions_.size(); ++i) {
        for (const auto &segment : partitions_[i].segments) {
          if (offset < segment.start || offset - segment.start >= segment.size)
            continue;
          const auto count = static_cast<std::size_t>(
              std::min<u64>(output.size(), segment.size - (offset - segment.start)));
          read_encrypted(i, offset, output.first(count));
          offset += count;
          output = output.subspan(count);
          found = true;
          break;
        }
        if (found)
          break;
      }
      require(found, "disc read crosses a missing data range");
    }
  }

  void read_game(u64 offset, std::span<u8> output) {
    range(offset, output.size(), game_size_, "game partition read");
    if (metadata_.console == Console::GameCube) {
      read(offset, output);
      return;
    }
    require(game_data_offset_.has_value(), "Wii disc has no game partition");
    if (game_partition_) {
      read_plain_partition(*game_partition_, offset, output);
      return;
    }
    require(!encrypted_ || output.empty(), "encrypted raw Wii ISO requires a partition title key");
    if (!has_hashes_) {
      read(*game_data_offset_ + offset, output);
      return;
    }
    while (!output.empty()) {
      const u64 in_sector = offset % plaintext_bytes;
      const auto count =
          static_cast<std::size_t>(std::min<u64>(output.size(), plaintext_bytes - in_sector));
      read(*game_data_offset_ + offset / plaintext_bytes * sector_bytes + hash_bytes + in_sector,
           output.first(count));
      offset += count;
      output = output.subspan(count);
    }
  }

  u64 boot_field(u64 offset) {
    std::array<u8, 4> value{};
    read_game(offset, value);
    return static_cast<u64>(be32(value, 0)) << (metadata_.console == Console::Wii ? 2 : 0);
  }

  u64 partition_offset() const noexcept {
    return game_partition_offset_.value_or(0);
  }

  std::optional<u32> required_ios() {
    if (!game_partition_offset_)
      return std::nullopt;
    std::array<u8, 8> descriptor{};
    read(*game_partition_offset_ + 0x2a4, descriptor);
    const u64 tmd_size = be32(descriptor, 0);
    const u64 tmd_offset = static_cast<u64>(be32(descriptor, 4)) * 4;
    std::array<u8, 4> signature{};
    read(*game_partition_offset_ + tmd_offset, signature);
    const u32 type = be32(signature, 0);
    const u64 body = type == 0x10000 ? 0x240 : type == 0x10001 ? 0x140 : type == 0x10002 ? 0x80 : 0;
    require(body != 0, "unsupported Wii TMD signature type");
    require(tmd_size >= body + 0x4c, "Wii TMD is too small for its IOS field");
    std::array<u8, 8> version{};
    read(*game_partition_offset_ + tmd_offset + body + 0x44, version);
    require(be32(version, 0) == 1, "Wii TMD has an invalid IOS title ID");
    return be32(version, 4);
  }

  std::optional<PartitionMetadata> wii_metadata() {
    if (!game_partition_offset_)
      return std::nullopt;
    PartitionMetadata result;
    result.partition_offset = *game_partition_offset_;
    std::array<u8, 28> descriptor{};
    read(result.partition_offset + 0x2a4, descriptor);
    const u64 tmd_size = be32(descriptor, 0),
              tmd_relative = static_cast<u64>(be32(descriptor, 4)) * 4;
    const u64 cert_size = be32(descriptor, 8),
              cert_relative = static_cast<u64>(be32(descriptor, 12)) * 4;
    const u64 h3_relative = static_cast<u64>(be32(descriptor, 16)) * 4;
    const u64 data_relative = static_cast<u64>(be32(descriptor, 20)) * 4;
    result.data_offset = result.partition_offset + data_relative;
    result.data_size = static_cast<u64>(be32(descriptor, 24)) * 4;
    result.h3_offset = result.partition_offset + h3_relative;
    require(tmd_size >= 0x1e4 && tmd_size <= 0x49e4, "Wii TMD has an invalid size");
    require(cert_size <= 4 * 1024 * 1024, "Wii certificate chain is too large");
    range(0, 0x2a4, data_relative, "Wii partition ticket");
    range(tmd_relative, tmd_size, data_relative, "Wii partition TMD");
    range(cert_relative, cert_size, data_relative, "Wii partition certificate chain");
    result.ticket.resize(0x2a4);
    read(result.partition_offset, result.ticket);
    result.tmd.resize(static_cast<std::size_t>(tmd_size));
    read(result.partition_offset + tmd_relative, result.tmd);
    result.certificate_chain.resize(static_cast<std::size_t>(cert_size));
    read(result.partition_offset + cert_relative, result.certificate_chain);
    require(be32(result.ticket, 0) == 0x10001 && be32(result.tmd, 0) == 0x10001,
            "Wii ticket and TMD require RSA-2048 layout");
    require(result.ticket[0x1bc] == 0,
            "version-one Wii tickets are unsupported in disc partitions");
    require(be32(result.tmd, 0x184) == 1, "Wii TMD has an invalid IOS title ID");
    result.ios = be32(result.tmd, 0x188);
    result.title_id = be64(result.tmd, 0x18c);
    require(be64(result.ticket, 0x1dc) == result.title_id, "Wii ticket and TMD title IDs differ");
    result.title_version = be16(result.tmd, 0x1dc);
    const u16 count = be16(result.tmd, 0x1de);
    result.boot_index = be16(result.tmd, 0x1e0);
    range(0x1e4, static_cast<u64>(count) * 36, result.tmd.size(), "Wii TMD content records");
    result.contents.reserve(count);
    for (u16 index = 0; index < count; ++index) {
      const std::size_t offset = 0x1e4 + static_cast<std::size_t>(index) * 36;
      Content content{be32(result.tmd, offset),
                      be16(result.tmd, offset + 4),
                      be16(result.tmd, offset + 6),
                      be64(result.tmd, offset + 8),
                      {}};
      std::copy_n(result.tmd.begin() + static_cast<std::ptrdiff_t>(offset + 16), 20,
                  content.sha1.begin());
      result.contents.push_back(content);
    }
    return result;
  }

  u64 logical_size_ = 0, game_size_ = 0;
  Header metadata_;
  bool container_ = false, rvz_ = false;
  u32 codec_ = 0;

private:
  std::ifstream file_;
  u64 stored_size_ = 0;
  u32 chunk_size_ = 0, disc_type_ = 0;
  std::array<u8, 0x80> disc_header_{};
  std::vector<u8> properties_;
  std::vector<Segment> raw_;
  std::vector<Partition> partitions_;
  std::vector<Group> groups_;
  std::optional<std::size_t> game_partition_, cached_group_, encrypted_partition_;
  std::optional<u64> game_data_offset_, game_partition_offset_, encrypted_group_;
  bool has_hashes_ = true, encrypted_ = true;
  u64 cached_output_size_ = 0, cached_data_offset_ = 0;
  bool cached_partition_ = false;
  Decoded cached_;
  std::vector<u8> encrypted_cache_;

  void physical_read(u64 offset, std::span<u8> output) {
    range(offset, output.size(), stored_size_, "container read");
    if (output.empty())
      return;
    require(offset <= static_cast<u64>(std::numeric_limits<std::streamoff>::max()),
            "file offset exceeds stream limits");
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    file_.read(reinterpret_cast<char *>(output.data()),
               static_cast<std::streamsize>(output.size()));
    require(static_cast<std::size_t>(file_.gcount()) == output.size(), "input file read failed");
  }

  std::vector<u8> physical(u64 offset, u64 size) {
    require(size <= allocation_limit, "metadata or chunk exceeds the 256 MiB allocation limit");
    range(offset, size, stored_size_, "container range");
    std::vector<u8> result(static_cast<std::size_t>(size));
    physical_read(offset, result);
    return result;
  }

  std::vector<u8> decompress(std::span<const u8> input, u32 codec, u64 limit) {
    require(limit <= allocation_limit, "decompressed block exceeds the 256 MiB allocation limit");
    if (codec == 0) {
      require(input.size() <= limit, "uncompressed block is too large");
      return {input.begin(), input.end()};
    }
    std::vector<u8> output(static_cast<std::size_t>(limit));
    if (codec == 5) {
      const auto frame = ZSTD_findFrameCompressedSize(input.data(), input.size());
      require(!ZSTD_isError(frame) && frame == input.size(),
              "invalid or trailing Zstandard frame data");
      const auto written =
          ZSTD_decompress(output.data(), output.size(), input.data(), input.size());
      require(!ZSTD_isError(written), "Zstandard decompression failed");
      output.resize(written);
      return output;
    }
#if CUPID_HAVE_BZIP2
    if (codec == 2) {
      bz_stream stream{};
      require(BZ2_bzDecompressInit(&stream, 0, 0) == BZ_OK, "bzip2 initialization failed");
      stream.next_in = reinterpret_cast<char *>(const_cast<u8 *>(input.data()));
      stream.avail_in = static_cast<unsigned>(input.size());
      stream.next_out = reinterpret_cast<char *>(output.data());
      stream.avail_out = static_cast<unsigned>(output.size());
      const int result = BZ2_bzDecompress(&stream);
      const auto remaining_in = stream.avail_in, remaining_out = stream.avail_out;
      BZ2_bzDecompressEnd(&stream);
      require(result == BZ_STREAM_END && remaining_in == 0,
              "invalid, oversized, or trailing bzip2 data");
      output.resize(output.size() - remaining_out);
      return output;
    }
#endif
#if CUPID_HAVE_LZMA
    if (codec == 3 || codec == 4) {
      lzma_options_lzma options{};
      if (codec == 3) {
        require(properties_.size() == 5 && properties_[0] < 225, "invalid LZMA properties");
        options.lc = properties_[0] % 9;
        options.lp = (properties_[0] / 9) % 5;
        options.pb = properties_[0] / 45;
        for (unsigned i = 0; i < 4; ++i)
          options.dict_size |= static_cast<u32>(properties_[i + 1]) << (i * 8);
      } else {
        require(properties_.size() == 1 && properties_[0] <= 40, "invalid LZMA2 properties");
        const u8 prop = properties_[0];
        options.dict_size = prop == 40 ? 0xffffffffu : (2u | (prop & 1u)) << (prop / 2 + 11);
      }
      std::array<lzma_filter, 2> filters{
          {{codec == 3 ? LZMA_FILTER_LZMA1 : LZMA_FILTER_LZMA2, &options},
           {LZMA_VLI_UNKNOWN, nullptr}}};
      require(lzma_raw_decoder_memusage(filters.data()) <= allocation_limit,
              "LZMA dictionary exceeds the 256 MiB limit");
      std::size_t in_position = 0, out_position = 0;
      const auto result =
          lzma_raw_buffer_decode(filters.data(), nullptr, input.data(), &in_position, input.size(),
                                 output.data(), &out_position, output.size());
      require(result == LZMA_OK && in_position == input.size(),
              "invalid, oversized, or trailing LZMA data");
      output.resize(out_position);
      return output;
    }
#endif
    invalid("compression codec " + std::to_string(codec) + " is unavailable in this build");
  }

  void initialize_container() {
    const auto first = physical(0, 0x48);
    const u32 version = be32(first, 4), compatible = be32(first, 8);
    require(compatible <= 0x01000000 && version >= (rvz_ ? 0x00030000u : 0x00080000u),
            "unsupported container version");
    require(digest_equal(std::span(first).first(0x34), std::span(first).subspan(0x34, 20)),
            "header checksum mismatch");
    require(be64(first, 0x2c) == stored_size_, "declared container file size is incorrect");
    logical_size_ = be64(first, 0x24);
    require(logical_size_ >= 0x80, "invalid logical disc size");
    const u32 header_size = be32(first, 0xc);
    require(header_size >= 0xd5, "truncated secondary header");
    auto header = physical(0x48, header_size);
    require(digest_equal(header, std::span(first).subspan(0x10, 20)),
            "secondary header checksum mismatch");
    const u8 property_size = header[0xd4];
    require(property_size <= 7 && 0xd5u + property_size <= header.size(),
            "invalid compressor property size");
    properties_.assign(header.begin() + 0xd5, header.begin() + 0xd5 + property_size);
    disc_type_ = be32(header, 0);
    codec_ = be32(header, 4);
    require(codec_ <= (rvz_ ? 5u : 4u) && !(rvz_ && codec_ == 1),
            "unsupported container compression codec");
    require(codec_ != 3 || (properties_.size() == 5 && properties_[0] < 225),
            "invalid LZMA properties");
    require(codec_ != 4 || (properties_.size() == 1 && properties_[0] <= 40),
            "invalid LZMA2 properties");
#if !CUPID_HAVE_BZIP2
    require(codec_ != 2, "bzip2 support is unavailable in this build");
#endif
#if !CUPID_HAVE_LZMA
    require(codec_ != 3 && codec_ != 4, "LZMA support is unavailable in this build");
#endif
    chunk_size_ = be32(header, 0xc);
    const bool power_of_two = chunk_size_ && !(chunk_size_ & (chunk_size_ - 1));
    require(chunk_size_ != 0 && chunk_size_ <= allocation_limit &&
                ((rvz_ && chunk_size_ >= sector_bytes && power_of_two) ||
                 chunk_size_ % hash_group_bytes == 0),
            "invalid or oversized chunk size");
    std::copy_n(header.begin() + 0x10, disc_header_.size(), disc_header_.begin());
    const u32 partition_count = be32(header, 0x90), stride = be32(header, 0x94);
    require(partition_count <= 65536 && (!partition_count || stride > 0),
            "invalid partition table dimensions");
    const auto partition_table =
        physical(be64(header, 0x98), static_cast<u64>(partition_count) * stride);
    require(digest_equal(partition_table, std::span(header).subspan(0xa0, 20)),
            "partition table checksum mismatch");
    for (u32 i = 0; i < partition_count; ++i) {
      std::array<u8, 0x30> bytes{};
      std::copy_n(partition_table.begin() +
                      static_cast<std::ptrdiff_t>(static_cast<u64>(i) * stride),
                  std::min<std::size_t>(stride, bytes.size()), bytes.begin());
      Partition partition;
      std::copy_n(bytes.begin(), 16, partition.key.begin());
      for (std::size_t j = 0; j < 2; ++j) {
        const std::size_t base = 16 + j * 16;
        partition.segments[j] = {static_cast<u64>(be32(bytes, base)) * sector_bytes,
                                 static_cast<u64>(be32(bytes, base + 4)) * sector_bytes,
                                 be32(bytes, base + 8), be32(bytes, base + 12)};
      }
      require(!partition.segments[1].size ||
                  partition.segments[1].start >=
                      partition.segments[0].start + partition.segments[0].size,
              "partition segments overlap or are reversed");
      partitions_.push_back(partition);
    }
    const u32 raw_count = be32(header, 0xb4), group_count = be32(header, 0xc4);
    const auto raw_table =
        table(be64(header, 0xb8), be32(header, 0xc0), static_cast<u64>(raw_count) * 24);
    for (u32 i = 0; i < raw_count; ++i) {
      const std::size_t base = static_cast<std::size_t>(i) * 24;
      raw_.push_back({be64(raw_table, base), be64(raw_table, base + 8), be32(raw_table, base + 16),
                      be32(raw_table, base + 20)});
    }
    const u32 group_stride = rvz_ ? 12 : 8;
    const auto group_table =
        table(be64(header, 0xc8), be32(header, 0xd0), static_cast<u64>(group_count) * group_stride);
    for (u32 i = 0; i < group_count; ++i) {
      const std::size_t base = static_cast<std::size_t>(i) * group_stride;
      const u32 tagged_size = be32(group_table, base + 4);
      Group group{static_cast<u64>(be32(group_table, base)) * 4,
                  rvz_ ? tagged_size & 0x7fffffff : tagged_size,
                  rvz_ ? be32(group_table, base + 8) : 0, !rvz_ || (tagged_size & 0x80000000) != 0};
      range(group.offset, group.size, stored_size_, "group data");
      require(group.size <= allocation_limit && group.packed <= allocation_limit,
              "oversized group data");
      groups_.push_back(group);
    }
    std::vector<std::pair<u64, u64>> occupied;
    auto validate_segment = [&](const Segment &segment, bool partition) {
      range(segment.start, segment.size, logical_size_, "disc data range");
      range(segment.first_group, segment.groups, groups_.size(), "group table reference");
      if (!segment.size)
        return;
      const u64 expanded = segment.size + (partition ? 0 : segment.start % sector_bytes);
      require(segment.groups == (expanded - 1) / chunk_size_ + 1,
              "data range has an incorrect group count");
      occupied.emplace_back(segment.start, segment.start + segment.size);
    };
    for (const auto &segment : raw_)
      validate_segment(segment, false);
    for (const auto &partition : partitions_)
      for (const auto &segment : partition.segments)
        validate_segment(segment, true);
    std::sort(occupied.begin(), occupied.end());
    for (std::size_t i = 1; i < occupied.size(); ++i)
      require(occupied[i - 1].second <= occupied[i].first, "disc data ranges overlap");
  }

  std::vector<u8> table(u64 offset, u32 stored, u64 expected) {
    const auto input = physical(offset, stored);
    auto result = codec_ == 1 ? purge(input, expected, {}) : decompress(input, codec_, expected);
    require(result.size() == expected, "metadata table has an incorrect decoded size");
    return result;
  }

  std::vector<u8> purge(std::span<const u8> input, u64 expected, std::span<const u8> prefix) {
    require(input.size() >= 20 && expected <= allocation_limit, "invalid PURGE block");
    std::vector<u8> hashed(prefix.begin(), prefix.end());
    hashed.insert(hashed.end(), input.begin(), input.end() - 20);
    require(digest_equal(hashed, input.last(20)), "PURGE checksum mismatch");
    std::vector<u8> output(static_cast<std::size_t>(expected));
    std::size_t cursor = 0;
    u64 last_end = 0;
    const auto segments = input.first(input.size() - 20);
    while (cursor < segments.size()) {
      const u32 offset = be32(segments, cursor), size = be32(segments, cursor + 4);
      cursor += 8;
      require(offset >= last_end, "PURGE segments overlap or are reversed");
      range(offset, size, expected, "PURGE output");
      range(cursor, size, segments.size(), "PURGE literal");
      std::copy_n(segments.begin() + static_cast<std::ptrdiff_t>(cursor), size,
                  output.begin() + offset);
      cursor += size;
      last_end = static_cast<u64>(offset) + size;
    }
    return output;
  }

  void initialize_game_partition() {
    std::array<u8, 32> directory{};
    read(0x40000, directory);
    for (std::size_t group = 0; group < 4; ++group) {
      const u32 count = be32(directory, group * 8);
      require(count <= 65536, "Wii partition directory is too large");
      const u64 table_offset = static_cast<u64>(be32(directory, group * 8 + 4)) * 4;
      range(table_offset, static_cast<u64>(count) * 8, logical_size_, "Wii partition directory");
      for (u32 index = 0; index < count; ++index) {
        std::array<u8, 8> entry{};
        read(table_offset + static_cast<u64>(index) * 8, entry);
        if (be32(entry, 4) != 0 || game_data_offset_)
          continue;
        const u64 partition_offset = static_cast<u64>(be32(entry, 0)) * 4;
        std::array<u8, 8> data_info{};
        range(partition_offset, 0x2c0, logical_size_, "Wii partition header");
        read(partition_offset + 0x2b8, data_info);
        const u64 relative_offset = static_cast<u64>(be32(data_info, 0)) * 4;
        const u64 raw_size = static_cast<u64>(be32(data_info, 4)) * 4;
        range(partition_offset, relative_offset, logical_size_, "Wii partition data offset");
        const u64 data_offset = partition_offset + relative_offset;
        range(data_offset, raw_size, logical_size_, "Wii partition data");
        game_data_offset_ = data_offset;
        game_partition_offset_ = partition_offset;
        game_size_ = has_hashes_ ? raw_size / sector_bytes * plaintext_bytes : raw_size;
        if (container_) {
          for (std::size_t i = 0; i < partitions_.size(); ++i) {
            if (partitions_[i].segments[0].start == data_offset) {
              game_partition_ = i;
              const auto &segments = partitions_[i].segments;
              const u64 end = segments[1].size ? segments[1].start + segments[1].size
                                               : segments[0].start + segments[0].size;
              const u64 available = (end - data_offset) / sector_bytes * plaintext_bytes;
              require(game_size_ <= available, "Wii game partition is truncated in the container");
              break;
            }
          }
          if (has_hashes_ && encrypted_)
            require(game_partition_.has_value(),
                    "Wii game partition has no matching plaintext data");
        }
      }
    }
    if (!game_data_offset_)
      game_size_ = 0;
  }

  Decoded decode(std::size_t index, u64 expected, u32 exception_lists, u64 data_offset) {
    const auto &group = groups_.at(index);
    Decoded result;
    result.exceptions.resize(exception_lists);
    if (!group.size) {
      result.data.resize(static_cast<std::size_t>(expected));
      return result;
    }
    const u32 codec = group.compressed ? codec_ : 0;
    const bool compressed_exceptions = codec >= 2;
    auto input = physical(group.offset, group.size);
    const u64 payload_size = group.packed ? group.packed : expected;
    std::vector<u8> decoded;
    if (compressed_exceptions)
      decoded = decompress(input, codec,
                           payload_size + static_cast<u64>(exception_lists) * (2 + 3328 * 22));
    else
      decoded = std::move(input);
    std::size_t cursor = 0;
    for (auto &list : result.exceptions) {
      const u16 count = be16(decoded, cursor);
      cursor += 2;
      if (compressed_exceptions)
        require(count <= 3328, "too many compressed hash exceptions");
      range(cursor, static_cast<u64>(count) * 22, decoded.size(), "hash exceptions");
      list.reserve(count);
      for (u16 i = 0; i < count; ++i) {
        Exception exception{be16(decoded, cursor), {}};
        require(exception.offset % hash_bytes + 20 <= hash_bytes,
                "hash exception crosses a sector hash boundary");
        std::copy_n(decoded.begin() + static_cast<std::ptrdiff_t>(cursor + 2), 20,
                    exception.digest.begin());
        list.push_back(exception);
        cursor += 22;
      }
    }
    if (!compressed_exceptions && exception_lists) {
      cursor = (cursor + 3) & ~std::size_t{3};
      range(cursor, 0, decoded.size(), "hash exception padding");
    }
    std::span<const u8> payload = std::span(decoded).subspan(cursor);
    if (codec == 1) {
      auto purged = purge(payload, payload_size, std::span(decoded).first(cursor));
      decoded = std::move(purged);
      payload = decoded;
    }
    require(payload.size() == payload_size, "group has an incorrect decoded size");
    if (group.packed)
      result.data = unpack(payload, static_cast<std::size_t>(expected), data_offset);
    else
      result.data.assign(payload.begin(), payload.end());
    return result;
  }

  const Decoded &chunk(std::size_t index, u64 expected, bool partition, u64 data_offset) {
    if (!cached_group_ || *cached_group_ != index || cached_output_size_ != expected ||
        cached_data_offset_ != data_offset || cached_partition_ != partition) {
      cached_group_.reset();
      cached_ =
          decode(index, expected,
                 partition ? std::max<u32>(1, chunk_size_ / static_cast<u32>(hash_group_bytes)) : 0,
                 data_offset);
      cached_output_size_ = expected;
      cached_data_offset_ = data_offset;
      cached_partition_ = partition;
      cached_group_ = index;
    }
    return cached_;
  }

  void read_segment(const Segment &segment, u64 offset, std::span<u8> output, bool partition,
                    u64 base) {
    const u64 sector = partition ? plaintext_bytes : sector_bytes;
    const u64 chunk_bytes = partition ? chunk_size_ / sector_bytes * plaintext_bytes : chunk_size_;
    const u64 skipped = segment.start % sector;
    const u64 aligned_start = segment.start - skipped;
    const u64 expanded = segment.size + skipped;
    while (!output.empty()) {
      require(offset >= aligned_start, "read precedes data segment");
      const u64 relative = offset - aligned_start;
      range(relative, 1, expanded, "data segment read");
      const u64 index = relative / chunk_bytes;
      require(index < segment.groups, "missing data group");
      const u64 within = relative % chunk_bytes;
      const u64 expected = std::min(chunk_bytes, expanded - index * chunk_bytes);
      const auto count = static_cast<std::size_t>(std::min<u64>(output.size(), expected - within));
      const auto &decoded = chunk(static_cast<std::size_t>(segment.first_group + index), expected,
                                  partition, index * chunk_bytes + base);
      std::copy_n(decoded.data.begin() + static_cast<std::ptrdiff_t>(within), count,
                  output.begin());
      offset += count;
      output = output.subspan(count);
    }
  }

  void read_plain_partition(std::size_t index, u64 offset, std::span<u8> output) {
    const auto &partition = partitions_.at(index);
    const u64 first = partition.segments[0].start;
    for (const auto &physical_segment : partition.segments) {
      if (output.empty())
        return;
      if (!physical_segment.size)
        continue;
      const Segment segment{(physical_segment.start - first) / sector_bytes * plaintext_bytes,
                            physical_segment.size / sector_bytes * plaintext_bytes,
                            physical_segment.first_group, physical_segment.groups};
      if (offset >= segment.start + segment.size)
        continue;
      require(offset >= segment.start, "game partition read crosses a missing segment");
      const auto count = static_cast<std::size_t>(
          std::min<u64>(output.size(), segment.size - (offset - segment.start)));
      read_segment(segment, offset, output.first(count), true, 0);
      offset += count;
      output = output.subspan(count);
    }
    require(output.empty(), "game partition read exceeds stored data");
  }

  void reconstruct(std::size_t index, u64 group_index) {
    const auto &partition = partitions_.at(index);
    const u64 first = partition.segments[0].start;
    const auto &tail = partition.segments[1].size ? partition.segments[1] : partition.segments[0];
    const u64 size = (tail.start + tail.size - first) / sector_bytes * plaintext_bytes;
    const u64 start = group_index * plain_group_bytes;
    std::vector<u8> plain(static_cast<std::size_t>(plain_group_bytes));
    if (start < size)
      read_plain_partition(index, start,
                           std::span(plain).first(static_cast<std::size_t>(
                               std::min(plain_group_bytes, size - start))));
    std::vector<u8> hashes(64 * static_cast<std::size_t>(hash_bytes));
    for (std::size_t sector = 0; sector < 64; ++sector) {
      for (std::size_t block = 0; block < 31; ++block) {
        const auto hash =
            detail::sha1(std::span(plain).subspan(sector * plaintext_bytes + block * 0x400, 0x400));
        std::copy(hash.begin(), hash.end(),
                  hashes.begin() + static_cast<std::ptrdiff_t>(sector * hash_bytes + block * 20));
      }
      const auto hash = detail::sha1(std::span(hashes).subspan(sector * hash_bytes, 31 * 20));
      const std::size_t base = (sector / 8) * 8;
      for (std::size_t copy = base; copy < base + 8; ++copy)
        std::copy(hash.begin(), hash.end(),
                  hashes.begin() +
                      static_cast<std::ptrdiff_t>(copy * hash_bytes + 0x280 + (sector % 8) * 20));
    }
    for (std::size_t subgroup = 0; subgroup < 8; ++subgroup) {
      const auto hash =
          detail::sha1(std::span(hashes).subspan(subgroup * 8 * hash_bytes + 0x280, 8 * 20));
      for (std::size_t copy = 0; copy < 64; ++copy)
        std::copy(hash.begin(), hash.end(),
                  hashes.begin() +
                      static_cast<std::ptrdiff_t>(copy * hash_bytes + 0x340 + subgroup * 20));
    }
    // Exception coordinates are relative to each chunk's hash stream. Large chunks contain one list
    // per 64 sectors.
    for (const auto &segment : partition.segments) {
      if (!segment.size)
        continue;
      const u64 segment_start = (segment.start - first) / sector_bytes * plaintext_bytes;
      const u64 segment_size = segment.size / sector_bytes * plaintext_bytes;
      const u64 chunk_bytes = chunk_size_ / sector_bytes * plaintext_bytes;
      const u64 end = std::min(start + plain_group_bytes, segment_start + segment_size);
      if (end <= segment_start || end <= start)
        continue;
      const u64 begin = std::max(start, segment_start);
      for (u64 local = (begin - segment_start) / chunk_bytes; local < segment.groups; ++local) {
        const u64 chunk_start = segment_start + local * chunk_bytes;
        if (chunk_start >= end)
          break;
        const u64 expected = std::min(chunk_bytes, segment_size - local * chunk_bytes);
        const auto &decoded = chunk(static_cast<std::size_t>(segment.first_group + local), expected,
                                    true, local * chunk_bytes);
        for (std::size_t list = 0; list < decoded.exceptions.size(); ++list) {
          const u64 list_start = chunk_start + list * plain_group_bytes;
          if (list_start >= start + plain_group_bytes || list_start + plain_group_bytes <= start)
            continue;
          for (const auto &exception : decoded.exceptions[list]) {
            const u64 sector = list_start / plaintext_bytes + exception.offset / hash_bytes;
            require(sector >= group_index * 64 && sector < (group_index + 1) * 64,
                    "hash exception lies outside its reconstructed group");
            const u64 where =
                (sector - group_index * 64) * hash_bytes + exception.offset % hash_bytes;
            std::copy(exception.digest.begin(), exception.digest.end(),
                      hashes.begin() + static_cast<std::ptrdiff_t>(where));
          }
        }
      }
    }
    encrypted_cache_.resize(static_cast<std::size_t>(hash_group_bytes));
    const detail::Aes128 aes(partition.key);
    const std::array<u8, 16> zero{};
    for (std::size_t sector = 0; sector < 64; ++sector) {
      auto output = std::span(encrypted_cache_).subspan(sector * sector_bytes, sector_bytes);
      aes.encrypt_cbc(std::span(hashes).subspan(sector * hash_bytes, hash_bytes),
                      output.first(hash_bytes), zero);
      std::array<u8, 16> iv{};
      std::copy_n(output.begin() + 0x3d0, 16, iv.begin());
      aes.encrypt_cbc(std::span(plain).subspan(sector * plaintext_bytes, plaintext_bytes),
                      output.subspan(hash_bytes), iv);
    }
  }

  void read_encrypted(std::size_t index, u64 offset, std::span<u8> output) {
    const u64 first = partitions_.at(index).segments[0].start;
    while (!output.empty()) {
      const u64 relative = offset - first;
      const u64 group = relative / hash_group_bytes;
      if (!encrypted_partition_ || *encrypted_partition_ != index || !encrypted_group_ ||
          *encrypted_group_ != group) {
        encrypted_partition_.reset();
        encrypted_group_.reset();
        reconstruct(index, group);
        encrypted_partition_ = index;
        encrypted_group_ = group;
      }
      const u64 within = relative % hash_group_bytes;
      const auto count =
          static_cast<std::size_t>(std::min<u64>(output.size(), hash_group_bytes - within));
      std::copy_n(encrypted_cache_.begin() + static_cast<std::ptrdiff_t>(within), count,
                  output.begin());
      offset += count;
      output = output.subspan(count);
    }
  }
};

Image::Image(const std::filesystem::path &path) : impl_(std::make_unique<Impl>(path)) {
}

Image::~Image() = default;
Image::Image(Image &&) noexcept = default;
Image &Image::operator=(Image &&) noexcept = default;

u64 Image::size() const noexcept {
  return impl_->logical_size_;
}

u64 Image::game_size() const noexcept {
  return impl_->game_size_;
}

const Header &Image::header() const noexcept {
  return impl_->metadata_;
}

std::string Image::format() const {
  return impl_->container_ ? (impl_->rvz_ ? "RVZ" : "WIA") : "ISO";
}

std::string Image::compression() const {
  constexpr std::array<const char *, 6> names{"none", "purge", "bzip2", "lzma", "lzma2", "zstd"};
  return names.at(impl_->codec_);
}

void Image::read(u64 offset, std::span<u8> output) {
  if (offset > size() || output.size() > size() - offset)
    throw std::out_of_range("Disc read exceeds logical image size");
  impl_->read(offset, output);
}

void Image::read_game(u64 offset, std::span<u8> output) {
  if (offset > game_size() || output.size() > game_size() - offset)
    throw std::out_of_range("Game partition read exceeds logical data size");
  impl_->read_game(offset, output);
}

u64 Image::main_dol_offset() {
  return impl_->boot_field(0x420);
}

u64 Image::fst_offset() {
  return impl_->boot_field(0x424);
}

u64 Image::fst_size() {
  return impl_->boot_field(0x428);
}

u64 Image::game_partition_offset() const noexcept {
  return impl_->partition_offset();
}

std::optional<u32> Image::required_ios() {
  return impl_->required_ios();
}

std::optional<PartitionMetadata> Image::wii_metadata() {
  return impl_->wii_metadata();
}
} // namespace cupid::disc
