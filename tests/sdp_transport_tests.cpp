// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/memory.hpp"
#include "cupid/hw/devices.hpp"
#include "cupid/hw/remote.hpp"
#include "cupid/hw/sdp.hpp"
#include "cupid/ios/ipc.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

namespace {
using namespace cupid;
constexpr u32 ipc_base = 0x0d000000, command = 0x10000, bulk_request = 0x20000;
constexpr u32 packet_buffer = 0x18000, bulk_buffer = 0x28000;

u16 little(std::span<const u8> bytes, std::size_t position) {
  CHECK(position + 2 <= bytes.size());
  return u16(bytes[position] | (u16(bytes[position + 1]) << 8));
}

u16 big(std::span<const u8> bytes, std::size_t position) {
  CHECK(position + 2 <= bytes.size());
  return u16((u16(bytes[position]) << 8) | bytes[position + 1]);
}

void append_little(std::vector<u8> &bytes, u16 value) {
  bytes.push_back(u8(value));
  bytes.push_back(u8(value >> 8));
}

std::vector<u8> acl(u16 channel, std::span<const u8> payload) {
  std::vector<u8> bytes;
  append_little(bytes, 0x2100);
  append_little(bytes, u16(payload.size() + 4));
  append_little(bytes, u16(payload.size()));
  append_little(bytes, channel);
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

struct Controller {
  Memory memory{Console::Wii};
  hw::Devices devices{Console::Wii};
  ios::Ipc ipc{memory, devices, Console::Wii};
  u32 descriptor = 0;

  Controller() {
    devices.write(0x0c003004, 0x4000, 4);
    const std::string_view path = "/dev/usb/oh1/57e/305";
    for (std::size_t i = 0; i < path.size(); ++i)
      memory.write8(0x19000 + u32(i), u8(path[i]));
    memory.write8(0x19000 + u32(path.size()), 0);
    clear(command);
    memory.write32(command, 1);
    memory.write32(command + 12, 0x19000);
    memory.write32(command + 16, 3);
    submit(command);
    descriptor = u32(reply(command));
    hw::RemoteState state;
    state.connected = true;
    ipc.set_remote(0, state);
  }

  void clear(u32 address) {
    auto bytes = memory.range(address, 64);
    std::fill(bytes.begin(), bytes.end(), u8{});
  }

  void submit(u32 address) {
    devices.write(ipc_base, address, 4);
    devices.write(ipc_base + 4, 0x31, 4);
    ipc.advance(6000);
    CHECK((devices.read(ipc_base + 4, 4) & 3) == 2);
    devices.write(ipc_base + 4, 0x32, 4);
    devices.write(ipc_base + 0x30, 0x40000000, 4);
  }

  s32 reply(u32 address) {
    ipc.advance(100000);
    CHECK(memory.read32(address) == 8);
    CHECK(devices.read(ipc_base + 8, 4) == address);
    CHECK(devices.read(ipc_base + 4, 4) & 4);
    const auto result = static_cast<s32>(memory.read32(address + 4));
    devices.write(ipc_base + 4, 0x34, 4);
    devices.write(ipc_base + 0x30, 0x40000000, 4);
    return result;
  }

  void vectors(u32 address, u32 operation, std::initializer_list<std::pair<u32, u32>> in,
               std::initializer_list<std::pair<u32, u32>> out) {
    clear(address);
    memory.write32(address, 7);
    memory.write32(address + 8, descriptor);
    memory.write32(address + 12, operation);
    memory.write32(address + 16, u32(in.size()));
    memory.write32(address + 20, u32(out.size()));
    u32 cursor = address + 0x1000;
    memory.write32(address + 24, cursor);
    for (const auto &list : {in, out})
      for (const auto &[buffer, size] : list) {
        memory.write32(cursor, buffer);
        memory.write32(cursor + 4, size);
        cursor += 8;
      }
  }

  s32 control(std::span<const u8> bytes) {
    constexpr u32 setup = command + 0x2000;
    memory.copy_in(packet_buffer, bytes);
    memory.write8(setup, 0x20);
    memory.write8(setup + 1, 0);
    memory.write16(setup + 2, 0);
    memory.write16(setup + 4, 0);
    memory.write16(setup + 6, u16(bytes.size() << 8));
    memory.write8(setup + 8, 0);
    vectors(command, 0,
            {{setup, 1},
             {setup + 1, 1},
             {setup + 2, 2},
             {setup + 4, 2},
             {setup + 6, 2},
             {setup + 8, 1}},
            {{packet_buffer, u32(bytes.size())}});
    submit(command);
    return reply(command);
  }

  void endpoint(u32 address, u8 endpoint_number, u32 buffer, u16 length) {
    const u32 setup = address + 0x2000;
    memory.write8(setup, endpoint_number);
    memory.write16(setup + 2, length);
    vectors(address, endpoint_number == 0x81 ? 2 : 1, {{setup, 1}, {setup + 2, 2}},
            {{buffer, length}});
  }

  s32 output(std::span<const u8> bytes) {
    memory.copy_in(packet_buffer, bytes);
    endpoint(command, 2, packet_buffer, u16(bytes.size()));
    submit(command);
    return reply(command);
  }

  std::vector<u8> input(u8 endpoint_number) {
    endpoint(command, endpoint_number, packet_buffer, 512);
    submit(command);
    const s32 size = reply(command);
    CHECK(size > 0 && size <= 512);
    const auto bytes = memory.range(packet_buffer, u32(size));
    return {bytes.begin(), bytes.end()};
  }

  void tick() {
    const u64 period = devices.ticks_per_second() / 200;
    ipc.advance(period - ipc.cycles() % period);
  }

  void completed_packets() {
    tick();
    const auto event = input(0x81);
    CHECK(event.size() == 23 && event[0] == 0x13 && event[2] == 5);
    CHECK(little(event, 3) == 0x100 && little(event, 5) != 0);
  }

  void connect_sdp() {
    const std::array<u8, 16> connection{5, 4,    13,   0x11, 2, 0x19, 0x79, 0,
                                        0, 0x18, 0xcc, 0,    0, 0,    0,    0};
    CHECK(control(connection) == s32(connection.size()));
    CHECK(input(0x81) == std::vector<u8>({0x0f, 4, 0, 1, 5, 4}));
    CHECK(input(0x81) == std::vector<u8>({3, 11, 0, 0, 1, 0x11, 2, 0x19, 0x79, 0, 0, 1, 0}));
    tick();
    const std::array<u8, 8> connect{2, 0x31, 4, 0, 1, 0, 0x71, 0};
    CHECK(output(acl(1, connect)) == 0);
    CHECK(input(0x82) == acl(1, std::array<u8, 12>{3, 0x31, 8, 0, 0x40, 0, 0x71, 0, 0, 0, 0, 0}));
    completed_packets();
    CHECK(input(0x82) == acl(1, std::array<u8, 12>{4, 4, 8, 0, 0x71, 0, 0, 0, 1, 2, 185, 0}));
    const std::array<u8, 12> config{4, 0x32, 8, 0, 0x40, 0, 0, 0, 1, 2, 64, 0};
    CHECK(output(acl(1, config)) == 0);
    CHECK(input(0x82) ==
          acl(1, std::array<u8, 14>{5, 0x32, 10, 0, 0x71, 0, 0, 0, 0, 0, 1, 2, 64, 0}));
    CHECK(output(acl(1, std::array<u8, 10>{5, 4, 6, 0, 0x40, 0, 0, 0, 0, 0})) == 0);
    completed_packets();
  }

  std::vector<u8> service(std::span<const u8> request) {
    CHECK(output(acl(0x40, request)) == 0);
    completed_packets();
    const auto packet = input(0x82);
    CHECK(little(packet, 0) == 0x2100);
    CHECK(little(packet, 2) == packet.size() - 4);
    CHECK(little(packet, 4) == packet.size() - 8);
    CHECK(little(packet, 6) == 0x71);
    return {packet.begin() + 8, packet.end()};
  }
};

std::vector<u8> attributes(u32 handle, u16 maximum, u16 transaction, u16 offset = 0) {
  std::vector<u8> request{4,
                          u8(transaction >> 8),
                          u8(transaction),
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
                          0,
                          0,
                          255,
                          255,
                          u8(offset ? 2 : 0)};
  if (offset) {
    request.push_back(u8(offset >> 8));
    request.push_back(u8(offset));
  }
  request[4] = u8(request.size() - 5);
  return request;
}

void attribute_continuations(Controller &controller, u32 handle, std::size_t expected_size) {
  std::vector<u8> record;
  u16 offset = 0;
  do {
    const u16 transaction = u16(0xa000 + record.size());
    const auto request = attributes(handle, 31, transaction, offset);
    const auto response = controller.service(request);
    CHECK(response == hw::sdp_response(request));
    CHECK(response[0] == 5 && big(response, 1) == transaction);
    const u16 count = big(response, 5);
    CHECK(count > 0 && count <= 31);
    record.insert(record.end(), response.begin() + 7, response.begin() + 7 + count);
    if (response[7 + count]) {
      CHECK(response[7 + count] == 2);
      offset = big(response, 8 + count);
      CHECK(offset == record.size());
    } else
      offset = 0;
  } while (offset);
  CHECK(record.size() == expected_size && record[0] == 0x36);
  CHECK(big(record, 1) == record.size() - 3);
}

void deferred_bulk(Controller &controller) {
  const std::array<u8, 13> search{2, 0xab, 0xcd, 0, 8, 0x35, 3, 0x19, 0x11, 0x24, 0, 1, 0};
  CHECK(controller.control(std::array<u8, 3>{3, 12, 0}) == 3);
  CHECK(controller.output(acl(0x40, search)) == 0);
  auto destination = controller.memory.range(bulk_buffer, 512);
  std::fill(destination.begin(), destination.end(), u8(0x69));
  controller.endpoint(bulk_request, 0x82, bulk_buffer, 512);
  controller.submit(bulk_request);
  controller.ipc.advance(100000);
  CHECK(controller.memory.read32(bulk_request) == 7);
  CHECK(std::all_of(destination.begin(), destination.end(), [](u8 byte) { return byte == 0x69; }));
  CHECK(controller.input(0x81) == std::vector<u8>({0x0e, 4, 1, 3, 12, 0}));
  controller.tick();
  const auto size = controller.reply(bulk_request);
  CHECK(size > 8);
  const auto bytes = controller.memory.range(bulk_buffer, u32(size));
  const auto expected = acl(0x71, hw::sdp_response(search));
  CHECK(std::equal(bytes.begin(), bytes.end(), expected.begin(), expected.end()));
  const auto completion = controller.input(0x81);
  CHECK(completion[0] == 0x13);
}
} // namespace

int main() {
  return run_tests([] {
    Controller controller;
    controller.connect_sdp();
    attribute_continuations(controller, 0x10000, 463);
    attribute_continuations(controller, 0x10001, 93);
    deferred_bulk(controller);
    CHECK(controller.output(acl(0x55, std::array<u8, 1>{0})) == -4);
    bool rejected = false;
    try {
      controller.output(acl(0x40, std::array<u8, 4>{4, 0, 0, 0}));
    } catch (const BusFault &fault) {
      CHECK(fault.address == command && fault.unsupported_reason);
      CHECK(std::string_view(fault.what()).find("Bluetooth SDP") != std::string_view::npos);
      rejected = true;
    }
    CHECK(rejected);
  });
}
