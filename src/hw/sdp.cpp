// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/hw/sdp.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace cupid::hw {
namespace {
using Bytes = std::vector<u8>;
constexpr u32 hid_handle = 0x10000, identity_handle = 0x10001;
constexpr unsigned fragment_limit = 118;

void append(Bytes &output, std::span<const u8> bytes) {
  output.insert(output.end(), bytes.begin(), bytes.end());
}

void integer(Bytes &output, u32 value, unsigned width) {
  for (unsigned remaining = width; remaining != 0; --remaining)
    output.push_back(static_cast<u8>(value >> ((remaining - 1) * 8)));
}

Bytes number(u32 value, unsigned width) {
  Bytes bytes{static_cast<u8>(width == 1 ? 8 : width == 2 ? 9 : 10)};
  integer(bytes, value, width);
  return bytes;
}

Bytes uuid(u16 value) {
  Bytes bytes{0x19};
  integer(bytes, value, 2);
  return bytes;
}

Bytes boolean(bool value) {
  return {0x28, static_cast<u8>(value)};
}

Bytes variable(u8 type, Bytes payload, bool wide = false) {
  if (payload.size() > 65535)
    throw SdpError("SDP data element exceeds its two-byte size field");
  wide = wide || payload.size() > 255;
  Bytes bytes{static_cast<u8>((type << 3) | (wide ? 6 : 5))};
  integer(bytes, static_cast<u32>(payload.size()), wide ? 2 : 1);
  append(bytes, payload);
  return bytes;
}

Bytes sequence(std::initializer_list<Bytes> elements) {
  Bytes payload;
  for (const auto &element : elements)
    append(payload, element);
  return variable(6, std::move(payload));
}

Bytes text(std::string_view value) {
  return variable(4, Bytes(value.begin(), value.end()));
}

enum class HidItem : u8 {
  UsagePage = 0x04,
  Usage = 0x08,
  LogicalMinimum = 0x14,
  LogicalMaximum = 0x24,
  ReportSize = 0x74,
  Input = 0x80,
  ReportId = 0x84,
  Output = 0x90,
  ReportCount = 0x94,
  Collection = 0xa0,
  EndCollection = 0xc0
};

Bytes report_descriptor() {
  Bytes bytes;
  const auto item = [&](HidItem tag, unsigned value, unsigned width = 1) {
    bytes.push_back(static_cast<u8>(static_cast<u8>(tag) | width));
    for (unsigned i = 0; i < width; ++i)
      bytes.push_back(static_cast<u8>(value >> (i * 8)));
  };
  item(HidItem::UsagePage, 1);
  item(HidItem::Usage, 5);
  item(HidItem::Collection, 1);
  item(HidItem::ReportId, 0x10);
  item(HidItem::LogicalMinimum, 0);
  item(HidItem::LogicalMaximum, 255, 2);
  item(HidItem::ReportSize, 8);
  item(HidItem::ReportCount, 1);
  item(HidItem::UsagePage, 0xff00, 2);
  item(HidItem::Usage, 1);
  item(HidItem::Output, 0);
  constexpr std::array<std::pair<u8, u8>, 10> output_reports{{{0x11, 1},
                                                              {0x12, 2},
                                                              {0x13, 1},
                                                              {0x14, 1},
                                                              {0x15, 1},
                                                              {0x16, 21},
                                                              {0x17, 6},
                                                              {0x18, 21},
                                                              {0x19, 1},
                                                              {0x1a, 1}}};
  constexpr std::array<std::pair<u8, u8>, 14> input_reports{{{0x20, 6},
                                                             {0x21, 21},
                                                             {0x22, 4},
                                                             {0x30, 2},
                                                             {0x31, 5},
                                                             {0x32, 10},
                                                             {0x33, 17},
                                                             {0x34, 21},
                                                             {0x35, 21},
                                                             {0x36, 21},
                                                             {0x37, 21},
                                                             {0x3d, 21},
                                                             {0x3e, 21},
                                                             {0x3f, 21}}};
  const auto reports = [&](auto entries, HidItem direction) {
    for (auto [id, count] : entries) {
      item(HidItem::ReportId, id);
      item(HidItem::ReportCount, count);
      item(HidItem::Usage, 1);
      item(direction, 0);
    }
  };
  reports(output_reports, HidItem::Output);
  reports(input_reports, HidItem::Input);
  item(HidItem::EndCollection, 0, 0);
  return bytes;
}

Bytes protocol_list(u16 psm, u16 upper_protocol) {
  return sequence({sequence({uuid(0x0100), number(psm, 2)}), sequence({uuid(upper_protocol)})});
}

std::map<u16, Bytes> attributes(u32 handle) {
  if (handle == hid_handle)
    return {{0x0000, number(handle, 4)},
            {0x0001, sequence({uuid(0x1124)})},
            {0x0004, protocol_list(0x11, 0x11)},
            {0x0005, sequence({uuid(0x1002)})},
            {0x0006, sequence({number(0x656e, 2), number(0x006a, 2), number(0x0100, 2)})},
            {0x0009, sequence({sequence({uuid(0x1124), number(0x0100, 2)})})},
            {0x000d, sequence({protocol_list(0x13, 0x11)})},
            {0x0100, text("Nintendo RVL-CNT-01")},
            {0x0101, text("Nintendo RVL-CNT-01")},
            {0x0102, text("Nintendo")},
            {0x0200, number(0x0100, 2)},
            {0x0201, number(0x0111, 2)},
            {0x0202, number(4, 1)},
            {0x0203, number(0x33, 1)},
            {0x0204, boolean(false)},
            {0x0205, boolean(true)},
            {0x0206, sequence({sequence({number(0x22, 1), variable(4, report_descriptor())})})},
            {0x0207, sequence({sequence({number(0x0409, 2), number(0x0100, 2)})})},
            {0x0208, boolean(false)},
            {0x0209, boolean(true)},
            {0x020a, boolean(true)},
            {0x020b, number(0x0100, 2)},
            {0x020c, number(0x0c80, 2)},
            {0x020d, boolean(false)},
            {0x020e, boolean(false)}};
  if (handle == identity_handle)
    return {{0x0000, number(handle, 4)},
            {0x0001, sequence({uuid(0x1200)})},
            {0x0004, protocol_list(1, 1)},
            {0x0005, sequence({uuid(0x1002)})},
            {0x0009, sequence({sequence({uuid(0x1200), number(0x0100, 2)})})},
            {0x0200, number(0x0100, 2)},
            {0x0201, number(0x057e, 2)},
            {0x0202, number(0x0306, 2)},
            {0x0203, number(0x0600, 2)},
            {0x0204, boolean(true)},
            {0x0205, number(2, 2)}};
  throw SdpError("Unknown SDP service handle " + std::to_string(handle));
}

class Reader {
public:
  explicit Reader(std::span<const u8> bytes) : bytes_(bytes) {
  }

  u32 integer(unsigned width) {
    if (width > bytes_.size() - position_)
      throw SdpError("Truncated SDP field");
    u32 value = 0;
    for (unsigned i = 0; i < width; ++i)
      value = (value << 8) | bytes_[position_++];
    return value;
  }

  std::span<const u8> sequence() {
    const auto type = integer(1);
    if (type != 0x35 && type != 0x36)
      throw SdpError("SDP request requires a sequence element");
    const auto length = integer(type == 0x35 ? 1 : 2);
    if (length > bytes_.size() - position_)
      throw SdpError("Truncated SDP sequence");
    const auto payload = bytes_.subspan(position_, length);
    position_ += length;
    return payload;
  }

  unsigned continuation() {
    const auto length = integer(1);
    if (length != 0 && length != 2)
      throw SdpError("Unsupported SDP continuation length");
    return length ? integer(2) : 0;
  }

  bool empty() const {
    return position_ == bytes_.size();
  }

private:
  std::span<const u8> bytes_;
  std::size_t position_ = 0;
};

Bytes pdu(u8 operation, u16 transaction, Bytes payload) {
  if (payload.size() > 65535)
    throw SdpError("SDP response exceeds its parameter-length field");
  Bytes reply{operation};
  integer(reply, transaction, 2);
  integer(reply, static_cast<u32>(payload.size()), 2);
  append(reply, payload);
  return reply;
}
} // namespace

std::vector<u8> sdp_response(std::span<const u8> request) {
  Reader input(request);
  const auto operation = input.integer(1);
  const auto transaction = static_cast<u16>(input.integer(2));
  const auto parameter_size = input.integer(2);
  if (parameter_size != request.size() - 5)
    throw SdpError("SDP parameter length does not match the PDU");
  if (operation == 2) {
    Reader pattern(input.sequence());
    if (pattern.integer(1) != 0x19 || pattern.integer(2) != 0x1124 || !pattern.empty())
      throw SdpError("Unsupported SDP service search pattern");
    if (input.integer(2) == 0)
      throw SdpError("SDP service search requires a nonzero record limit");
    if (input.continuation() != 0 || !input.empty())
      throw SdpError("Invalid SDP service search continuation");
    Bytes payload;
    integer(payload, 1, 2);
    integer(payload, 1, 2);
    integer(payload, hid_handle, 4);
    payload.push_back(0);
    return pdu(3, transaction, std::move(payload));
  }
  if (operation != 4)
    throw SdpError("Unsupported SDP operation " + std::to_string(operation));
  const auto handle = input.integer(4), maximum = input.integer(2);
  if (maximum < 7)
    throw SdpError("SDP attribute byte limit must be at least seven");
  Reader selection(input.sequence());
  std::vector<std::pair<u16, u16>> ranges;
  while (!selection.empty()) {
    const auto type = selection.integer(1);
    u16 first, last;
    if (type == 9)
      first = last = static_cast<u16>(selection.integer(2));
    else if (type == 10) {
      first = static_cast<u16>(selection.integer(2));
      last = static_cast<u16>(selection.integer(2));
    } else
      throw SdpError("Unsupported SDP attribute selector type");
    if (first > last)
      throw SdpError("SDP attribute range is reversed");
    ranges.emplace_back(first, last);
  }
  if (ranges.empty())
    throw SdpError("SDP attribute selector is empty");
  const auto offset = input.continuation();
  if (!input.empty())
    throw SdpError("Trailing bytes after SDP continuation");
  Bytes selected;
  for (const auto &[id, value] : attributes(handle))
    if (std::ranges::any_of(
            ranges, [id](const auto &range) { return id >= range.first && id <= range.second; })) {
      append(selected, number(id, 2));
      append(selected, value);
    }
  const auto encoded = variable(6, std::move(selected), true);
  if (offset >= encoded.size())
    throw SdpError("SDP continuation lies outside the selected attributes");
  const auto amount = std::min<std::size_t>({maximum, fragment_limit, encoded.size() - offset});
  Bytes payload;
  integer(payload, static_cast<u32>(amount), 2);
  append(payload, std::span<const u8>(encoded).subspan(offset, amount));
  const auto next = offset + amount;
  payload.push_back(next == encoded.size() ? 0 : 2);
  if (next != encoded.size())
    integer(payload, static_cast<u32>(next), 2);
  return pdu(5, transaction, std::move(payload));
}
} // namespace cupid::hw
