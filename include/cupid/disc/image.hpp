// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "cupid/core/types.hpp"

namespace cupid::disc {

struct Header {
  Console console = Console::GameCube;
  std::string id;
  std::string title;
  u8 disc_number = 0;
  u8 revision = 0;
};

struct Content {
  u32 id = 0;
  u16 index = 0;
  u16 type = 0;
  u64 size = 0;
  std::array<u8, 20> sha1{};
};

struct PartitionMetadata {
  u64 partition_offset = 0;
  u64 data_offset = 0;
  u64 data_size = 0;
  u64 h3_offset = 0;
  u64 title_id = 0;
  u32 ios = 0;
  u16 title_version = 0;
  u16 boot_index = 0;
  std::vector<u8> ticket;
  std::vector<u8> tmd;
  std::vector<u8> certificate_chain;
  std::vector<Content> contents;
};

// Throws std::runtime_error for invalid images, corrupt data, and unsupported codecs.
// A reader owns its file and chunk cache; concurrent calls require external locking.
class Image {
public:
  explicit Image(const std::filesystem::path &path);
  ~Image();
  Image(Image &&) noexcept;
  Image &operator=(Image &&) noexcept;
  Image(const Image &) = delete;
  Image &operator=(const Image &) = delete;

  [[nodiscard]] u64 size() const noexcept;
  [[nodiscard]] u64 game_size() const noexcept;
  [[nodiscard]] const Header &header() const noexcept;
  [[nodiscard]] std::string format() const;
  [[nodiscard]] std::string compression() const;
  // Reads outside the logical image throw std::out_of_range before changing output.
  void read(u64 offset, std::span<u8> output);
  // Wii offsets refer to plaintext in the game partition, with sector hashes removed.
  void read_game(u64 offset, std::span<u8> output);
  [[nodiscard]] u64 main_dol_offset();
  [[nodiscard]] u64 fst_offset();
  [[nodiscard]] u64 fst_size();
  [[nodiscard]] u64 game_partition_offset() const noexcept;
  [[nodiscard]] std::optional<u32> required_ios();
  [[nodiscard]] std::optional<PartitionMetadata> wii_metadata();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace cupid::disc
