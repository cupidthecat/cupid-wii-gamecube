// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/disc/image.hpp"
#include <string_view>
#include <vector>

namespace cupid::disc {
struct Entry {
  std::string path;
  bool directory = false;
  u64 offset = 0;
  u64 size = 0;
};

class Filesystem {
public:
  explicit Filesystem(Image &image);
  [[nodiscard]] std::span<const Entry> entries() const noexcept;
  [[nodiscard]] const Entry *find(std::string_view path) const;
  void read_file(std::string_view path, u64 offset, std::span<u8> output);

private:
  Image *image_;
  std::vector<Entry> entries_;
};
} // namespace cupid::disc
