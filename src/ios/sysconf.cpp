// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/ios/sysconf.hpp"
#include <algorithm>
#include <array>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace cupid::ios {
namespace {
constexpr std::size_t file_size = 16384, footer_offset = file_size - 4;
constexpr std::array<u8, 4> header{'S', 'C', 'v', '0'}, footer{'S', 'C', 'e', 'd'};
enum class Type : u8 { Array = 1, SmallArray, Byte, Short, Long, LongLong, Boolean };

struct Entry {
  Type type;
  std::string name;
  std::vector<u8> value;
};

[[noreturn]] void invalid(std::string_view detail) {
  throw SysconfError("Invalid Wii system configuration: " + std::string(detail));
}

u16 be16(std::span<const u8> bytes, std::size_t at) {
  return u16((u16(bytes[at]) << 8) | bytes[at + 1]);
}

unsigned scalar_size(Type type) {
  switch (type) {
  case Type::Byte:
  case Type::Boolean:
    return 1;
  case Type::Short:
    return 2;
  case Type::Long:
    return 4;
  case Type::LongLong:
    return 8;
  default:
    invalid("entry type");
  }
}

std::vector<Entry> parse(std::span<const u8> bytes) {
  if (bytes.size() != file_size || !std::equal(header.begin(), header.end(), bytes.begin()) ||
      !std::equal(footer.begin(), footer.end(), bytes.begin() + footer_offset))
    invalid("size or signature");
  const std::size_t count = be16(bytes, 4), table_end = 6 + (count + 1) * 2;
  if (table_end > footer_offset)
    invalid("offset table exceeds file");
  std::vector<Entry> entries;
  entries.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    std::size_t at = be16(bytes, 6 + i * 2);
    const std::size_t limit = be16(bytes, 8 + i * 2);
    if (at < table_end || at >= limit || limit > footer_offset)
      invalid("entry offsets");
    const u8 description = bytes[at++];
    const auto type = Type(description >> 5);
    const std::size_t name_size = (description & 31) + 1;
    if (name_size > limit - at)
      invalid("entry name exceeds its range");
    std::string name(reinterpret_cast<const char *>(bytes.data() + at), name_size);
    at += name_size;
    std::size_t value_size;
    if (type == Type::Array) {
      if (limit - at < 2)
        invalid("missing array length");
      value_size = std::size_t(be16(bytes, at)) + 1;
      at += 2;
    } else if (type == Type::SmallArray) {
      if (at == limit)
        invalid("missing array length");
      value_size = std::size_t(bytes[at++]) + 1;
    } else {
      value_size = scalar_size(type);
    }
    if (value_size > limit - at)
      invalid("entry value exceeds its range");
    entries.push_back(
        {type, std::move(name), {bytes.begin() + at, bytes.begin() + at + value_size}});
  }
  if (!count) {
    const auto end = be16(bytes, 6);
    if (end < table_end || end > footer_offset)
      invalid("empty offset table");
  }
  return entries;
}

std::vector<Entry> defaults() {
  std::vector<Entry> entries;
  auto add = [&](Type type, std::string name, std::vector<u8> value) {
    entries.push_back({type, std::move(name), std::move(value)});
  };
  add(Type::Array, "BT.DINF", std::vector<u8>(1121));
  add(Type::Array, "BT.CDIF", std::vector<u8>(517));
  add(Type::Long, "BT.SENS", {0, 0, 0, 3});
  for (const auto &[name, value] : std::array<std::pair<const char *, u8>, 3>{
           {{"BT.BAR", u8(1)}, {"BT.SPKV", u8(88)}, {"BT.MOT", u8(1)}}})
    add(Type::Byte, name, {value});
  std::vector<u8> nickname(22);
  constexpr std::string_view name = "Cupid";
  for (std::size_t i = 0; i < name.size(); ++i)
    nickname[i * 2 + 1] = u8(name[i]);
  nickname.back() = u8(name.size());
  add(Type::SmallArray, "IPL.NIK", std::move(nickname));
  add(Type::Byte, "IPL.LNG", {1});
  std::vector<u8> address(4104);
  address[0] = 108;
  add(Type::Array, "IPL.SADR", std::move(address));
  std::vector<u8> parental(74);
  parental[1] = 4;
  parental[2] = 20;
  add(Type::SmallArray, "IPL.PC", std::move(parental));
  add(Type::Long, "IPL.CB", std::vector<u8>(4));
  add(Type::Byte, "IPL.AR", {1});
  add(Type::Byte, "IPL.SSV", {1});
  for (const auto &[key, value] : std::array<std::pair<const char *, u8>, 3>{
           {{"IPL.CD", u8(0)}, {"IPL.CD2", u8(0)}, {"IPL.EULA", u8(1)}}})
    add(Type::Boolean, key, {value});
  for (const auto &[key, value] : std::array<std::pair<const char *, u8>, 4>{
           {{"IPL.UPT", u8(2)}, {"IPL.PGS", u8(0)}, {"IPL.E60", u8(1)}, {"IPL.DH", u8(0)}}})
    add(Type::Byte, key, {value});
  add(Type::Long, "IPL.INC", {0, 0, 0, 8});
  add(Type::Long, "IPL.FRC", {0, 0, 0, 40});
  add(Type::SmallArray, "IPL.IDL", {0, 1});
  add(Type::Long, "NET.WCFG", {0, 0, 0, 1});
  add(Type::Long, "NET.CTPC", std::vector<u8>(4));
  add(Type::Byte, "WWW.RST", {0});
  add(Type::Boolean, "MPLS.MOVIE", {1});
  return entries;
}

std::vector<u8> serialize(const std::vector<Entry> &entries) {
  std::vector<u8> result(file_size);
  std::copy(header.begin(), header.end(), result.begin());
  std::copy(footer.begin(), footer.end(), result.begin() + footer_offset);
  auto put16 = [&](std::size_t offset, std::size_t value) {
    result[offset] = u8(value >> 8);
    result[offset + 1] = u8(value);
  };
  if (entries.size() > (footer_offset - 8) / 2)
    invalid("too many entries");
  put16(4, entries.size());
  std::size_t cursor = 6 + (entries.size() + 1) * 2;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto &entry = entries[i];
    if (entry.name.empty() || entry.name.size() > 32)
      invalid("entry name length");
    const unsigned prefix = entry.type == Type::Array ? 2 : entry.type == Type::SmallArray ? 1 : 0;
    if (entry.value.empty() || (prefix == 1 && entry.value.size() > 256) ||
        (prefix == 2 && entry.value.size() > 65536) ||
        (!prefix && entry.value.size() != scalar_size(entry.type)))
      invalid("entry value length");
    const auto size = 1 + entry.name.size() + prefix + entry.value.size();
    if (cursor > footer_offset || size > footer_offset - cursor)
      invalid("entries exceed file");
    put16(6 + i * 2, cursor);
    result[cursor++] = u8((u8(entry.type) << 5) | (entry.name.size() - 1));
    std::copy(entry.name.begin(), entry.name.end(), result.begin() + cursor);
    cursor += entry.name.size();
    if (prefix == 2)
      put16(cursor, entry.value.size() - 1);
    else if (prefix == 1)
      result[cursor] = u8(entry.value.size() - 1);
    cursor += prefix;
    std::copy(entry.value.begin(), entry.value.end(), result.begin() + cursor);
    cursor += entry.value.size();
  }
  put16(6 + entries.size() * 2, cursor);
  return result;
}
} // namespace

std::vector<u8> update_bluetooth_peers(std::span<const u8> existing) {
  auto entries = existing.empty() ? defaults() : parse(existing);
  auto found = std::find_if(entries.begin(), entries.end(),
                            [](const Entry &entry) { return entry.name == "BT.DINF"; });
  if (found == entries.end()) {
    entries.push_back({Type::Array, "BT.DINF", {}});
    found = std::prev(entries.end());
  }
  if (found->type != Type::Array)
    invalid("BT.DINF type");
  found->value.assign(1121, 0);
  found->value[0] = 5;
  for (unsigned peer = 0; peer < 5; ++peer) {
    const std::array<u8, 6> address{u8(peer), 0, 121, 25, 2, 17};
    const std::string_view name = peer == 4 ? "Nintendo RVL-WBC-01" : "Nintendo RVL-CNT-01";
    for (unsigned slot : {peer, 10 + peer}) {
      const auto at = 1 + slot * 70;
      std::copy(address.begin(), address.end(), found->value.begin() + at);
      std::copy(name.begin(), name.end(), found->value.begin() + at + 6);
    }
  }
  return serialize(entries);
}
} // namespace cupid::ios
