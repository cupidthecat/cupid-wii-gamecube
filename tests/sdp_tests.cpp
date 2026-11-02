// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/hw/sdp.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>

using namespace cupid;

namespace {
u16 half(std::span<const u8> data, std::size_t offset) {
  return static_cast<u16>((u16(data[offset]) << 8) | data[offset + 1]);
}

std::vector<u8> attribute_request(u32 handle, u16 maximum, u16 first, u16 last,
                                  unsigned offset = 0) {
  std::vector<u8> bytes{4,
                        0xab,
                        0xcd,
                        0,
                        0,
                        u8(handle >> 24),
                        u8(handle >> 16),
                        u8(handle >> 8),
                        u8(handle),
                        u8(maximum >> 8),
                        u8(maximum),
                        0x35,
                        5,
                        0x0a,
                        u8(first >> 8),
                        u8(first),
                        u8(last >> 8),
                        u8(last)};
  bytes.push_back(offset ? 2 : 0);
  if (offset) {
    bytes.push_back(u8(offset >> 8));
    bytes.push_back(u8(offset));
  }
  bytes[4] = static_cast<u8>(bytes.size() - 5);
  return bytes;
}

std::vector<u8> collect(u32 handle, u16 maximum) {
  std::vector<u8> result;
  unsigned offset = 0;
  do {
    const auto response = hw::sdp_response(attribute_request(handle, maximum, 0, 0xffff, offset));
    CHECK(response[0] == 5);
    CHECK(half(response, 1) == 0xabcd);
    CHECK(half(response, 3) == response.size() - 5);
    const unsigned count = half(response, 5);
    CHECK(count <= maximum && count <= 118);
    CHECK(response.size() >= 8 + count);
    result.insert(result.end(), response.begin() + 7, response.begin() + 7 + count);
    const auto continuation = response[7 + count];
    if (continuation) {
      CHECK(continuation == 2);
      CHECK(response.size() == count + 10);
      CHECK(half(response, 8 + count) == offset + count);
      offset += count;
    } else {
      CHECK(response.size() == count + 8);
      offset = 0;
    }
  } while (offset);
  return result;
}
} // namespace

int main() {
  return run_tests([] {
    const std::array<u8, 13> search{2, 0x13, 0x57, 0, 8, 0x35, 3, 0x19, 0x11, 0x24, 0, 1, 0};
    const std::vector<u8> expected{3, 0x13, 0x57, 0, 9, 0, 1, 0, 1, 0, 1, 0, 0, 0};
    CHECK(hw::sdp_response(search) == expected);
    auto wide_search = std::vector<u8>(search.begin(), search.end());
    wide_search[5] = 0x36;
    wide_search.insert(wide_search.begin() + 6, 0);
    wide_search[4]++;
    CHECK(hw::sdp_response(wide_search) == expected);
    for (std::size_t length = 0; length < search.size(); ++length)
      CHECK_THROWS(hw::sdp_response(std::span(search).first(length)));
    auto malformed = search;
    malformed[9] = 0;
    CHECK_THROWS(hw::sdp_response(malformed));
    malformed = search;
    malformed[11] = 0;
    CHECK_THROWS(hw::sdp_response(malformed));
    malformed = search;
    malformed[0] = 6;
    CHECK_THROWS(hw::sdp_response(malformed));

    const auto hid = collect(0x10000, 118);
    CHECK(hid.size() == 463);
    CHECK(hid[0] == 0x36 && half(hid, 1) == 460);
    const auto identity = collect(0x10001, 118);
    CHECK(identity.size() == 93);
    CHECK(identity[0] == 0x36 && half(identity, 1) == 90);
    for (u16 maximum : {u16(7), u16(31), u16(117), u16(118), u16(512), u16(65535)}) {
      CHECK(collect(0x10000, maximum) == hid);
      CHECK(collect(0x10001, maximum) == identity);
    }

    auto single = attribute_request(0x10000, 512, 0, 0);
    single.resize(16);
    single[4] = 11;
    single[12] = 3;
    single[13] = 9;
    single[14] = single[15] = 0;
    single.push_back(0);
    single[4] = 12;
    const auto selected = hw::sdp_response(single);
    const std::array<u8, 11> handle_element{0x36, 0, 8, 9, 0, 0, 10, 0, 1, 0, 0};
    CHECK(half(selected, 5) == handle_element.size());
    CHECK(std::equal(handle_element.begin(), handle_element.end(), selected.begin() + 7));
    CHECK(selected.back() == 0);
    auto wide_selection = attribute_request(0x10000, 512, 0, 0);
    wide_selection[11] = 0x36;
    wide_selection.insert(wide_selection.begin() + 12, 0);
    wide_selection[4]++;
    CHECK(hw::sdp_response(wide_selection) == selected);
    auto multiple = attribute_request(0x10000, 512, 0, 0);
    const std::array<u8, 5> extra_selector{0x0a, 0x02, 0x02, 0x02, 0x02};
    multiple.insert(multiple.end() - 1, extra_selector.begin(), extra_selector.end());
    multiple[4] += 5;
    multiple[12] += 5;
    const auto multiple_result = hw::sdp_response(multiple);
    CHECK(half(multiple_result, 5) == 16);
    const std::array<u8, 5> subclass_element{9, 2, 2, 8, 4};
    CHECK(
        std::equal(subclass_element.begin(), subclass_element.end(), multiple_result.begin() + 18));
    auto duplicate = attribute_request(0x10000, 512, 0, 0);
    const std::array<u8, 5> duplicate_selector{0x0a, 0, 0, 0, 0};
    duplicate.insert(duplicate.end() - 1, duplicate_selector.begin(), duplicate_selector.end());
    duplicate[4] += 5;
    duplicate[12] += 5;
    CHECK(hw::sdp_response(duplicate) == selected);

    const auto absent = hw::sdp_response(attribute_request(0x10000, 512, 0x4000, 0x4fff));
    CHECK(half(absent, 5) == 3);
    CHECK(absent[7] == 0x36 && absent[8] == 0 && absent[9] == 0 && absent[10] == 0);
    const auto normal = attribute_request(0x10000, 118, 0, 0xffff);
    for (std::size_t length = 0; length < normal.size(); ++length)
      CHECK_THROWS(hw::sdp_response(std::span(normal).first(length)));
    CHECK_THROWS(hw::sdp_response(attribute_request(0x10002, 118, 0, 0xffff)));
    CHECK_THROWS(hw::sdp_response(attribute_request(0x10000, 6, 0, 0xffff)));
    CHECK_THROWS(hw::sdp_response(attribute_request(0x10000, 118, 5, 1)));
    CHECK_THROWS(hw::sdp_response(attribute_request(0x10000, 118, 0, 0xffff, 463)));
    auto invalid = normal;
    invalid.back() = 1;
    CHECK_THROWS(hw::sdp_response(invalid));
    invalid = normal;
    invalid[13] = 0x19;
    CHECK_THROWS(hw::sdp_response(invalid));
    invalid = normal;
    invalid.push_back(0);
    invalid[4]++;
    CHECK_THROWS(hw::sdp_response(invalid));
  });
}
