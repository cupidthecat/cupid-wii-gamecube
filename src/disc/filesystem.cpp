// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/disc/filesystem.hpp"
#include <algorithm>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <iconv.h>
#endif

namespace cupid::disc {
namespace {
void require(bool condition, const char *reason) {
  if (!condition)
    throw std::runtime_error(std::string("Disc filesystem: ") + reason);
}

u32 word(std::span<const u8> bytes, std::size_t offset) {
  require(offset <= bytes.size() && bytes.size() - offset >= 4, "truncated entry");
  u32 value = 0;
  for (std::size_t i = 0; i < 4; ++i)
    value = (value << 8) | bytes[offset + i];
  return value;
}

std::string decode_name(std::span<const u8> bytes) {
  std::string input(bytes.begin(), bytes.end());
  if (std::all_of(bytes.begin(), bytes.end(), [](u8 byte) { return byte < 0x80; }))
    return input;
#ifdef _WIN32
  const int wide_size = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, input.data(),
                                            static_cast<int>(input.size()), nullptr, 0);
  require(wide_size > 0, "invalid Shift-JIS name");
  std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
  require(MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, input.data(),
                              static_cast<int>(input.size()), wide.data(), wide_size) == wide_size,
          "Shift-JIS conversion failed");
  const int utf8_size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wide_size,
                                            nullptr, 0, nullptr, nullptr);
  require(utf8_size > 0, "UTF-8 name conversion failed");
  std::string output(static_cast<std::size_t>(utf8_size), '\0');
  require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wide_size, output.data(),
                              utf8_size, nullptr, nullptr) == utf8_size,
          "UTF-8 name conversion failed");
#else
  iconv_t conversion = iconv_open("UTF-8", "SJIS");
  require(conversion != reinterpret_cast<iconv_t>(-1), "Shift-JIS conversion is unavailable");
  std::string output(input.size() * 4, '\0');
  char *source = input.data();
  char *destination = output.data();
  std::size_t source_left = input.size(), destination_left = output.size();
  const auto result = iconv(conversion, &source, &source_left, &destination, &destination_left);
  iconv_close(conversion);
  require(result != static_cast<std::size_t>(-1) && source_left == 0, "invalid Shift-JIS name");
  output.resize(output.size() - destination_left);
#endif
  return output;
}

std::string normalized(std::string_view input) {
  std::string output;
  bool slash = true;
  for (unsigned char byte : input) {
    if (byte == '/') {
      if (!slash)
        output.push_back('/');
      slash = true;
    } else {
      output.push_back(static_cast<char>(byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte));
      slash = false;
    }
  }
  if (!output.empty() && output.back() == '/')
    output.pop_back();
  return output;
}
} // namespace

Filesystem::Filesystem(Image &image) : image_(&image) {
  const u64 offset = image.fst_offset(), size = image.fst_size();
  require(size >= 12 && size <= 128 * 1024 * 1024, "invalid or oversized FST size");
  require(offset <= image.game_size() && size <= image.game_size() - offset,
          "FST exceeds the game partition");
  std::vector<u8> table(static_cast<std::size_t>(size));
  image.read_game(offset, table);
  require((word(table, 0) & 0xff000000) != 0 && word(table, 4) == 0,
          "root entry is not a valid directory");
  const u32 count = word(table, 8);
  require(count > 0 && count <= 1000000 && static_cast<u64>(count) * 12 <= size,
          "invalid or oversized FST entry count");
  require(table.back() == 0, "FST name table is not terminated");

  struct Directory {
    u32 index, end;
  };

  std::vector<Directory> stack{{0, count}};
  entries_.reserve(count);
  entries_.push_back({"", true, 0, 0});
  const std::size_t names = static_cast<std::size_t>(count) * 12;
  u64 allocated_paths = 0;
  for (u32 index = 1; index < count; ++index) {
    while (!stack.empty() && index == stack.back().end)
      stack.pop_back();
    require(!stack.empty() && index < stack.back().end, "entry lies outside its parent directory");
    const std::size_t base = static_cast<std::size_t>(index) * 12;
    const u32 flags = word(table, base), data = word(table, base + 4),
              length = word(table, base + 8);
    const std::size_t name_offset = names + (flags & 0xffffff);
    require(name_offset < table.size(), "name offset is outside the FST");
    const auto start = table.begin() + static_cast<std::ptrdiff_t>(name_offset);
    const auto end = std::find(start, table.end(), 0);
    require(end != table.end() && end != start, "file name is empty or unterminated");
    const auto name =
        decode_name(std::span(table).subspan(name_offset, static_cast<std::size_t>(end - start)));
    require(name != "." && name != ".." && name.find('/') == std::string::npos &&
                name.find('\\') == std::string::npos,
            "file name contains a path separator or parent traversal");
    Entry entry{entries_[stack.back().index].path + name, (flags & 0xff000000) != 0, 0, 0};
    if (entry.directory) {
      require(data == stack.back().index, "directory has an incorrect parent index");
      require(length > index && length <= stack.back().end, "directory has an invalid end index");
      require(stack.size() < 1024, "directory nesting exceeds 1024 levels");
      entry.path.push_back('/');
      stack.push_back({index, length});
    } else {
      entry.offset = static_cast<u64>(data) << (image.header().console == Console::Wii ? 2 : 0);
      entry.size = length;
      require(entry.offset <= image.game_size() && entry.size <= image.game_size() - entry.offset,
              "file extends outside the game partition");
    }
    allocated_paths += entry.path.size();
    require(allocated_paths <= 256 * 1024 * 1024,
            "total file paths exceed the 256 MiB allocation limit");
    entries_.push_back(std::move(entry));
  }
}

std::span<const Entry> Filesystem::entries() const noexcept {
  return entries_;
}

const Entry *Filesystem::find(std::string_view path) const {
  const auto requested = normalized(path);
  const auto result = std::find_if(entries_.begin(), entries_.end(), [&](const Entry &entry) {
    return normalized(entry.path) == requested;
  });
  return result == entries_.end() ? nullptr : &*result;
}

void Filesystem::read_file(std::string_view path, u64 offset, std::span<u8> output) {
  const auto *entry = find(path);
  require(entry && !entry->directory, "file path was not found or refers to a directory");
  require(offset <= entry->size && output.size() <= entry->size - offset,
          "file read is out of bounds");
  image_->read_game(entry->offset + offset, output);
}
} // namespace cupid::disc
