// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/hw/remote.hpp"
#include "test_support.hpp"
#include <algorithm>

using namespace cupid;
using namespace cupid::hw;

namespace {
void send(Remote &remote, std::initializer_list<u8> bytes) {
  remote.receive_output(std::span<const u8>(bytes.begin(), bytes.size()));
}

std::vector<u8> receive(Remote &remote, u8 operation) {
  auto packet = remote.take_report();
  CHECK(packet.has_value());
  CHECK(packet->size() >= 2 && (*packet)[0] == 0xa1 && (*packet)[1] == operation);
  return *packet;
}

void write_register(Remote &remote, u8 slave, u16 address, u8 value) {
  std::array<u8, 22> bytes{};
  bytes[0] = 0x16;
  bytes[1] = 4;
  bytes[2] = u8(slave << 1);
  bytes[3] = u8(address >> 8);
  bytes[4] = u8(address);
  bytes[5] = 1;
  bytes[6] = value;
  remote.receive_output(bytes);
  CHECK(receive(remote, 0x22)[5] == 0);
}

void remote_reports() {
  Remote remote;
  RemoteState state;
  state.connected = true;
  state.buttons = RemoteA | RemotePlus;
  state.acceleration = {515, 514, 518};
  bool rumbling = false;
  remote.rumble = [&](bool value) { rumbling = value; };
  remote.set_state(state);
  remote.set_linked(true);
  send(remote, {0x11, 0x13});
  CHECK(rumbling);
  auto packet = receive(remote, 0x22);
  CHECK(packet == std::vector<u8>({0xa1, 0x22, 0x10, 0x08, 0x11, 0}));
  send(remote, {0x15, 0});
  packet = receive(remote, 0x20);
  CHECK(packet == std::vector<u8>({0xa1, 0x20, 0x10, 0x08, 0x10, 0, 0, 0xff}));
  send(remote, {0x12, 2, 0x31});
  receive(remote, 0x22);
  remote.update();
  packet = receive(remote, 0x31);
  CHECK(packet == std::vector<u8>({0xa1, 0x31, 0x70, 0x28, 128, 128, 129}));
  send(remote, {0x12, 2, 0xff});
  CHECK(!remote.take_report());
  remote.update();
  receive(remote, 0x31);
  send(remote, {0x13, 6});
  receive(remote, 0x22);
  write_register(remote, 0x58, 0x30, 8);
  write_register(remote, 0x58, 0x33, 1);
  state.nunchuk.attached = true;
  state.nunchuk.stick_x = 10;
  state.nunchuk.stick_y = 240;
  state.nunchuk.acceleration = {513, 514, 715};
  state.nunchuk.c = true;
  state.infrared[0] = {300, 500, 3};
  state.infrared[1] = {700, 600, 4};
  remote.set_state(state);
  remote.update();
  CHECK(receive(remote, 0x20)[4] == 0x1a);
  remote.update();
  CHECK(!remote.take_report());
  send(remote, {0x12, 2, 0x37});
  receive(remote, 0x22);
  remote.update();
  packet = receive(remote, 0x37);
  CHECK(packet.size() == 23);
  CHECK(packet[7] == 44 && packet[8] == 244 && packet[9] == 0x5a);
  CHECK(packet[10] == 188 && packet[11] == 88);
  CHECK(
      std::all_of(packet.begin() + 12, packet.begin() + 17, [](u8 byte) { return byte == 0xff; }));
  CHECK(packet[17] == 10 && packet[18] == 240);
  CHECK(packet[19] == 128 && packet[20] == 128 && packet[21] == 178);
  CHECK(packet[22] == 0xe5);
  state.nunchuk.attached = false;
  remote.set_state(state);
  remote.update();
  CHECK((receive(remote, 0x20)[4] & 2) == 0);
  send(remote, {0x12, 2, 0x32});
  receive(remote, 0x22);
  remote.update();
  packet = receive(remote, 0x32);
  CHECK(std::all_of(packet.begin() + 4, packet.end(), [](u8 byte) { return byte == 0xff; }));
  CHECK_THROWS(send(remote, {0x12, 2, 0x3e}));
  remote.set_linked(false);
  send(remote, {0x15, 0});
  remote.update();
  CHECK(!remote.take_report());
}

void remote_memory() {
  Remote remote;
  RemoteState state;
  state.connected = true;
  state.nunchuk.attached = true;
  remote.set_state(state);
  remote.set_linked(true);
  remote.update();
  receive(remote, 0x20);
  send(remote, {0x17, 0, 0, 0, 0x16, 0, 32});
  auto packet = receive(remote, 0x21);
  CHECK(packet.size() == 23 && packet[4] == 0xf0 && packet[5] == 0 && packet[6] == 0x16);
  CHECK(packet[7] == 0x80 && packet[11] == 0x9a);
  u8 checksum = 0x55;
  for (unsigned i = 0; i < 9; ++i)
    checksum = u8(checksum + packet[7 + i]);
  CHECK(packet[16] == checksum);
  send(remote, {0x17, 0, 0, 0, 0, 0, 1});
  CHECK(receive(remote, 0x22)[5] == 4);
  remote.update();
  packet = receive(remote, 0x21);
  CHECK(packet[6] == 0x26 && packet[4] == 0xf0);
  send(remote, {0x17, 0, 0, 0x17, 0x70, 0, 1});
  CHECK(receive(remote, 0x21)[4] == 0xf8);
  send(remote, {0x17, 0x0c, 0, 0, 0, 0, 1});
  CHECK(receive(remote, 0x21)[4] == 0xf6);
  send(remote, {0x17, 4, 0xa0, 0, 0, 0, 1});
  CHECK(receive(remote, 0x21)[4] == 0xf8);
  send(remote, {0x17, 4, 0xa8, 0, 0, 0, 1});
  CHECK(receive(remote, 0x21)[4] == 0xf7);
  send(remote, {0x17, 4, 0xa4, 0x12, 0xfa, 0, 6});
  packet = receive(remote, 0x21);
  CHECK(packet[4] == 0x50 && packet[5] == 0x12 && packet[6] == 0xfa);
  CHECK(std::equal(packet.begin() + 7, packet.begin() + 13,
                   std::array<u8, 6>{0, 0, 0xa4, 0x20, 0, 0}.begin()));
  write_register(remote, 0x52, 0xf0, 0xaa);
  CHECK_THROWS(send(remote, {0x17, 4, 0xa4, 0, 0xfa, 0, 6}));
  CHECK_THROWS(remote.set_eeprom(std::array<u8, 1>{0}));
}
} // namespace

int main() {
  return run_tests([] {
    remote_reports();
    remote_memory();
  });
}
