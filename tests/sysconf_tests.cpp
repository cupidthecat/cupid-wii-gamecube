// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/ios/sysconf.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <string>

namespace {
using namespace cupid;

struct Record {
  u8 type;
  std::string name;
  std::vector<u8> value;
};

u16 number(std::span<const u8> bytes, std::size_t at) {
  return u16((u16(bytes[at]) << 8) | bytes[at + 1]);
}

void number(std::span<u8> bytes, std::size_t at, u16 value) {
  bytes[at] = u8(value >> 8);
  bytes[at + 1] = u8(value);
}

std::vector<u8> fixture(std::span<const Record> records) {
  std::vector<u8> bytes(16384);
  std::copy_n("SCv0", 4, bytes.begin());
  std::copy_n("SCed", 4, bytes.end() - 4);
  number(bytes, 4, u16(records.size()));
  std::size_t at = 8 + records.size() * 2;
  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto &record = records[i];
    number(bytes, 6 + i * 2, u16(at));
    bytes[at++] = u8((record.type << 5) | (record.name.size() - 1));
    std::copy(record.name.begin(), record.name.end(), bytes.begin() + at);
    at += record.name.size();
    if (record.type == 1) {
      number(bytes, at, u16(record.value.size() - 1));
      at += 2;
    } else if (record.type == 2) {
      bytes[at++] = u8(record.value.size() - 1);
    }
    std::copy(record.value.begin(), record.value.end(), bytes.begin() + at);
    at += record.value.size();
  }
  CHECK(at < 16380);
  number(bytes, 6 + records.size() * 2, u16(at));
  return bytes;
}

std::vector<Record> inspect(std::span<const u8> bytes) {
  CHECK(bytes.size() == 16384);
  CHECK(std::equal(bytes.begin(), bytes.begin() + 4, "SCv0"));
  CHECK(std::equal(bytes.end() - 4, bytes.end(), "SCed"));
  std::vector<Record> records;
  for (unsigned i = 0; i < number(bytes, 4); ++i) {
    std::size_t at = number(bytes, 6 + i * 2);
    const u8 description = bytes[at++], type = description >> 5;
    const unsigned name_size = (description & 31) + 1;
    std::string name(reinterpret_cast<const char *>(bytes.data() + at), name_size);
    at += name_size;
    const unsigned size = type == 1   ? unsigned(number(bytes, at)) + 1
                          : type == 2 ? unsigned(bytes[at]) + 1
                          : type == 4 ? 2
                          : type == 5 ? 4
                          : type == 6 ? 8
                                      : 1;
    at += type == 1 ? 2 : type == 2 ? 1 : 0;
    records.push_back({type, std::move(name), {bytes.begin() + at, bytes.begin() + at + size}});
  }
  return records;
}

const Record &lookup(const std::vector<Record> &records, const std::string &name) {
  const auto found = std::find_if(records.begin(), records.end(),
                                  [&](const Record &record) { return record.name == name; });
  CHECK(found != records.end());
  return *found;
}

void check_peers(const Record &entry) {
  CHECK(entry.type == 1 && entry.value.size() == 1121 && entry.value[0] == 5);
  for (unsigned peer = 0; peer < 5; ++peer) {
    const std::array<u8, 6> address{u8(peer), 0, 0x79, 0x19, 2, 0x11};
    const std::string name = peer == 4 ? "Nintendo RVL-WBC-01" : "Nintendo RVL-CNT-01";
    for (unsigned slot : {peer, 10 + peer}) {
      const auto at = 1 + slot * 70;
      CHECK(std::equal(address.begin(), address.end(), entry.value.begin() + at));
      CHECK(std::equal(name.begin(), name.end(), entry.value.begin() + at + 6));
      CHECK(std::all_of(entry.value.begin() + at + 6 + name.size(), entry.value.begin() + at + 70,
                        [](u8 value) { return value == 0; }));
    }
  }
  CHECK(std::all_of(entry.value.begin() + 1 + 5 * 70, entry.value.begin() + 1 + 10 * 70,
                    [](u8 value) { return value == 0; }));
  CHECK(std::all_of(entry.value.begin() + 1 + 15 * 70, entry.value.end(),
                    [](u8 value) { return value == 0; }));
}

void initial_configuration() {
  const auto bytes = ios::update_bluetooth_peers({});
  const auto records = inspect(bytes);
  CHECK(records.size() == 27);
  check_peers(lookup(records, "BT.DINF"));
  CHECK(lookup(records, "BT.CDIF").value == std::vector<u8>(517));
  CHECK(lookup(records, "BT.SENS").value == std::vector<u8>({0, 0, 0, 3}));
  CHECK(lookup(records, "IPL.LNG").value == std::vector<u8>({1}));
  CHECK(lookup(records, "IPL.SADR").value.size() == 4104);
  CHECK(lookup(records, "IPL.SADR").value.front() == 108);
  const std::vector<u8> nickname{0, 'C', 0, 'u', 0, 'p', 0, 'i', 0, 'd', 0,
                                 0, 0,   0, 0,   0, 0,   0, 0,   0, 0,   5};
  CHECK(lookup(records, "IPL.NIK").value == nickname);
  CHECK(ios::update_bluetooth_peers(bytes) == bytes);
}

void preserve_entries() {
  std::vector<u8> small(256), large(2049);
  for (std::size_t i = 0; i < small.size(); ++i)
    small[i] = u8(i);
  for (std::size_t i = 0; i < large.size(); ++i)
    large[i] = u8(i * 19);
  const std::array<Record, 8> original{{{2, "CUSTOM.ARRAY", small},
                                        {1, "BT.DINF", std::vector<u8>(1121, 0xa5)},
                                        {1, "CUSTOM.LARGE", large},
                                        {3, "CUSTOM.BYTE", {0xde}},
                                        {4, "CUSTOM.SHORT", {0x12, 0x34}},
                                        {5, "CUSTOM.LONG", {0xfe, 0xdc, 0xba, 0x98}},
                                        {6, "CUSTOM.WIDE", {0, 1, 2, 3, 4, 5, 6, 7}},
                                        {7, "CUSTOM.BOOL", {1}}}};
  const auto before = fixture(original);
  const auto result = ios::update_bluetooth_peers(before);
  const auto after = inspect(result);
  CHECK(before == fixture(original) && after.size() == original.size());
  for (unsigned i = 0; i < original.size(); ++i) {
    CHECK(after[i].name == original[i].name && after[i].type == original[i].type);
    if (original[i].name != "BT.DINF")
      CHECK(after[i].value == original[i].value);
  }
  check_peers(after[1]);
  CHECK(ios::update_bluetooth_peers(result) == result);
  const std::array<Record, 1> missing{{{3, "IPL.LNG", {4}}}};
  const auto added = inspect(ios::update_bluetooth_peers(fixture(missing)));
  CHECK(added.size() == 2 && added[0].name == "IPL.LNG" && added[0].value[0] == 4);
  check_peers(added[1]);
  check_peers(inspect(ios::update_bluetooth_peers(fixture({})))[0]);
}

void malformed_configurations() {
  auto fails = [](const std::vector<u8> &bytes) {
    const auto preserved = bytes;
    bool rejected = false;
    try {
      ios::update_bluetooth_peers(bytes);
    } catch (const ios::SysconfError &) {
      rejected = true;
    }
    CHECK(rejected && bytes == preserved);
  };
  const auto valid = ios::update_bluetooth_peers({});
  fails(std::vector<u8>(16384));
  fails(std::vector<u8>(valid.begin(), valid.end() - 1));
  auto bytes = valid;
  bytes[0] = 'X';
  fails(bytes);
  bytes = valid;
  bytes.back() = 0;
  fails(bytes);
  bytes = valid;
  number(bytes, 4, 0xffff);
  fails(bytes);
  bytes = valid;
  number(bytes, 6, 6);
  fails(bytes);
  bytes = valid;
  number(bytes, 8, number(bytes, 6));
  fails(bytes);
  bytes = valid;
  number(bytes, 8, 0xffff);
  fails(bytes);
  bytes = valid;
  bytes[number(bytes, 6)] &= 31;
  fails(bytes);
  bytes = valid;
  const auto first = number(bytes, 6);
  number(bytes, first + 8, 0xffff);
  fails(bytes);
  const std::array<Record, 1> wrong_type{{{3, "BT.DINF", {0}}}};
  fails(fixture(wrong_type));
  const std::array<Record, 1> full{{{1, "CUSTOM.LARGE", std::vector<u8>(15500)}}};
  fails(fixture(full));
}
} // namespace

int main() {
  return run_tests([] {
    initial_configuration();
    preserve_entries();
    malformed_configurations();
  });
}
