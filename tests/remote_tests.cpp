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
  send(remote, {0x12, 2, 0x3e});
  receive(remote, 0x22);
  remote.update();
  receive(remote, 0x3e);
  remote.set_linked(false);
  send(remote, {0x15, 0});
  remote.update();
  CHECK(!remote.take_report());
}

void full_infrared() {
  Remote remote;
  RemoteState state;
  state.connected = true;
  state.buttons = 0x0810;
  state.acceleration = {515, 514, 518};
  state.infrared = {{{300, 500, 3}, {700, 600, 4}, {1023, 767, 15}, {65535, 65535, 255}}};
  remote.set_state(state);
  remote.set_linked(true);
  remote.receive_output(std::array<u8, 2>{0x13, 4});
  write_register(remote, 0x58, 0x30, 8);
  write_register(remote, 0x58, 0x33, 5);
  remote.receive_output(std::array<u8, 3>{0x12, 2, 0x3e});
  CHECK(receive(remote, 0x22)[5] == 0);
  constexpr std::array<u8, 36> full{44, 244, 0x53, 41,  113, 47,  119, 0,   0,    188, 88,  0xa4,
                                    56, 84,  64,   92,  0,   1,   255, 255, 0xbf, 112, 112, 0,
                                    0,  0,   154,  255, 255, 255, 255, 255, 255,  255, 255, 255};
  remote.update();
  auto packet = receive(remote, 0x3e);
  CHECK(packet.size() == 23 && packet[2] == 0x10 && packet[3] == 0x48 && packet[4] == 128);
  CHECK(std::equal(packet.begin() + 5, packet.end(), full.begin()));
  remote.receive_output(std::array<u8, 7>{0x17, 4, 0xb0, 0x12, 0x37, 0, 36});
  packet = receive(remote, 0x21);
  CHECK(packet[5] == 0x12 && packet[6] == 0x37);
  CHECK(std::equal(packet.begin() + 7, packet.end(), full.begin()));
  remote.update();
  packet = receive(remote, 0x21);
  CHECK(packet[6] == 0x47);
  CHECK(std::equal(packet.begin() + 7, packet.end(), full.begin() + 16));
  remote.update();
  packet = receive(remote, 0x21);
  CHECK(packet[6] == 0x57 && packet[4] == 0x30);
  CHECK(std::equal(packet.begin() + 7, packet.begin() + 11, full.begin() + 32));
  remote.receive_output(std::array<u8, 2>{0x15, 0});
  receive(remote, 0x20);
  remote.update();
  packet = receive(remote, 0x3f);
  CHECK(packet[2] == 0x30 && packet[3] == 0x08 && packet[4] == 128);
  CHECK(std::equal(packet.begin() + 5, packet.end(), full.begin() + 18));
  remote.update();
  receive(remote, 0x3e);
  write_register(remote, 0x58, 0x30, 1);
  remote.update();
  packet = receive(remote, 0x3f);
  CHECK(std::all_of(packet.begin() + 5, packet.end(), [](u8 byte) { return byte == 255; }));
  write_register(remote, 0x58, 0x30, 8);
  remote.receive_output(std::array<u8, 2>{0x13, 0});
  remote.update();
  packet = receive(remote, 0x3e);
  CHECK(std::all_of(packet.begin() + 5, packet.end(), [](u8 byte) { return byte == 255; }));
  remote.receive_output(std::array<u8, 3>{0x12, 0, 0x3f});
  remote.update();
  receive(remote, 0x3f);
  state.nunchuk.attached = true;
  remote.set_state(state);
  remote.update();
  receive(remote, 0x20);
  remote.update();
  CHECK(!remote.take_report());
  remote.set_linked(false);
  remote.update();
  CHECK(!remote.take_report());
  remote.set_linked(true);
  state.nunchuk.attached = false;
  remote.set_state(state);
  remote.update();
  receive(remote, 0x30);
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
  send(remote, {0x17, 4, 0xa4, 0, 0xfa, 0, 6});
  packet = receive(remote, 0x21);
  CHECK(packet[4] == 0x50);
  CHECK(std::equal(packet.begin() + 7, packet.begin() + 13,
                   std::array<u8, 6>{0xfe, 0xfe, 0x9a, 0x1e, 0xfe, 0xfe}.begin()));
  CHECK_THROWS(remote.set_eeprom(std::array<u8, 1>{0}));
}

void write_bytes(Remote &remote, u8 slave, u8 address, std::span<const u8> bytes, u8 error = 0) {
  std::array<u8, 22> packet{};
  packet[0] = 0x16;
  packet[1] = 4;
  packet[2] = u8(slave << 1);
  packet[4] = address;
  packet[5] = u8(bytes.size());
  std::copy(bytes.begin(), bytes.end(), packet.begin() + 6);
  remote.receive_output(packet);
  CHECK(receive(remote, 0x22)[5] == error);
}

std::vector<u8> read_bytes(Remote &remote, u8 slave, u8 address, u8 count) {
  send(remote, {0x17, 4, u8(slave << 1), 0, address, 0, count});
  return receive(remote, 0x21);
}

void speaker_registers() {
  Remote remote;
  remote.set_linked(true);
  // The guest setup write uses only its declared byte, regardless of unused payload.
  write_register(remote, 0x51, 9, 1);
  CHECK(read_bytes(remote, 0x51, 9, 1)[7] == 1);
  std::array<u8, 256> expected{};
  expected[9] = 1;
  for (unsigned address = 1; address < 256; ++address) {
    for (unsigned count = 1; count <= 16; ++count) {
      std::array<u8, 16> bytes{};
      for (unsigned i = 0; i < count; ++i)
        bytes[i] = u8(address * 31 + i * 13);
      const unsigned copied = std::min(count, 256 - address);
      std::copy_n(bytes.begin(), copied, expected.begin() + address);
      write_bytes(remote, 0x51, u8(address), std::span<const u8>(bytes).first(count),
                  copied == count ? 0 : 7);
      const auto packet = read_bytes(remote, 0x51, u8(address), u8(count));
      CHECK(packet[4] == (copied == count ? u8((count - 1) << 4) : u8(0xf7)));
      CHECK(std::equal(expected.begin() + address, expected.begin() + address + copied,
                       packet.begin() + 7));
      CHECK(std::all_of(packet.begin() + 7 + copied, packet.end(),
                        [](u8 value) { return value == 0; }));
    }
  }
  remote.set_linked(false);
  remote.set_linked(true);
  const auto cleared = read_bytes(remote, 0x51, 0, 16);
  CHECK(std::all_of(cleared.begin() + 7, cleared.end(), [](u8 value) { return value == 0; }));
}

void speaker_processing() {
  Remote remote;
  remote.set_linked(true);
  const std::array<u8, 4> pcm_config{0x40, 0xb8, 0x0b, 255};
  write_bytes(remote, 0x51, 2, pcm_config);
  std::array<u8, 22> samples{0x18, 0x20, 0x80, 0xff, 0, 0x7f};
  unsigned calls = 0;
  std::vector<audio::RemoteSpeakerPacket> packets;
  remote.speaker_samples = [&](const audio::RemoteSpeakerPacket &packet) {
    ++calls;
    packets.push_back(packet);
  };
  send(remote, {0x14, 6});
  receive(remote, 0x22);
  send(remote, {0x15, 0});
  CHECK((receive(remote, 0x20)[4] & 4) != 0);
  remote.receive_output(samples);
  CHECK(calls == 0 && !remote.take_report());
  // Host processing is independent of the guest's speaker-status bit.
  send(remote, {0x14, 2});
  receive(remote, 0x22);
  remote.set_speaker_processing(true, 0.5f);
  remote.receive_output(samples);
  CHECK(calls == 1 && packets.back().count == 4 && packets.back().rate_divisor == 13500);
  CHECK(packets.back().left_volume == 128 && packets.back().right_volume == 255);
  CHECK(std::equal(packets.back().mono.begin(), packets.back().mono.begin() + 4,
                   std::array<s16, 4>{-32768, -256, 0, 32512}.begin()));
  send(remote, {0x19, 6});
  receive(remote, 0x22);
  remote.receive_output(samples);
  CHECK(calls == 1);
  // Raw I2C samples bypass the HID mute flag, and do not alter register zero.
  write_bytes(remote, 0x51, 0, std::span<const u8>(samples).subspan(2, 4));
  CHECK(calls == 2 && read_bytes(remote, 0x51, 0, 1)[7] == 0);
  samples[1] = 0xa8;
  remote.receive_output(samples);
  CHECK(calls == 2);
  remote.receive_output(std::span<const u8>(samples).first(21));
  CHECK(calls == 2);
  write_register(remote, 0x51, 2, 0);
  const std::array<u8, 1> adpcm{0x12};
  write_bytes(remote, 0x51, 0, adpcm);
  const auto first = packets.back();
  write_bytes(remote, 0x51, 0, adpcm);
  CHECK(packets.back().mono[0] != first.mono[0]);
  remote.set_linked(false);
  remote.set_linked(true);
  // Link reset clears both the register bank and decoder history, preserving host settings.
  const std::array<u8, 4> adpcm_config{0, 0xb8, 0x0b, 255};
  write_bytes(remote, 0x51, 2, adpcm_config);
  write_bytes(remote, 0x51, 0, adpcm);
  CHECK(packets.back().count == first.count && packets.back().mono == first.mono);
  remote.set_speaker_processing(false);
  write_register(remote, 0x51, 2, 0xff);
  write_bytes(remote, 0x51, 0, adpcm);
  remote.set_speaker_processing(true);
  CHECK_THROWS(write_bytes(remote, 0x51, 0, adpcm));
  remote.speaker_samples = {};
  write_register(remote, 0x51, 2, 0);
  CHECK_THROWS(write_bytes(remote, 0x51, 0, adpcm));
}

void nunchuk_reset() {
  Remote remote;
  RemoteState state;
  state.connected = true;
  state.nunchuk.attached = true;
  remote.set_state(state);
  remote.set_linked(true);
  remote.update();
  receive(remote, 0x20);
  write_register(remote, 0x52, 0x40, 0x19);
  write_register(remote, 0x52, 0xf0, 0xaa);
  write_register(remote, 0x51, 9, 1);
  const auto eeprom = std::vector<u8>(remote.eeprom().begin(), remote.eeprom().end());
  remote.set_linked(false);
  remote.set_linked(true);
  remote.update();
  receive(remote, 0x20);
  CHECK(read_bytes(remote, 0x52, 0x40, 1)[7] == 0);
  CHECK(read_bytes(remote, 0x52, 0xf0, 1)[7] == 0);
  CHECK(read_bytes(remote, 0x51, 9, 1)[7] == 0);
  CHECK(std::equal(eeprom.begin(), eeprom.end(), remote.eeprom().begin()));
  write_register(remote, 0x52, 0x40, 0x19);
  write_register(remote, 0x52, 0xf0, 0xaa);
  state.nunchuk.attached = false;
  remote.set_state(state);
  remote.update();
  receive(remote, 0x20);
  state.nunchuk.attached = true;
  remote.set_state(state);
  remote.update();
  receive(remote, 0x20);
  CHECK(read_bytes(remote, 0x52, 0x40, 1)[7] == 0);
  CHECK(read_bytes(remote, 0x52, 0xf0, 1)[7] == 0);
  const auto calibration = read_bytes(remote, 0x52, 0x20, 16);
  CHECK(calibration[7] == 0x80 && calibration[11] == 0xb3 && calibration[15] == 0xe0);
}

void encrypted_bank() {
  Remote remote;
  RemoteState state;
  state.connected = true;
  state.nunchuk.attached = true;
  remote.set_state(state);
  remote.set_linked(true);
  remote.update();
  receive(remote, 0x20);
  write_register(remote, 0x52, 0xf0, 0xaa);
  const auto partial = read_bytes(remote, 0x52, 0xff, 2);
  CHECK(partial[4] == 0xf7 && partial[7] == 0xfe && partial[8] == 0xfe);
  CHECK(std::all_of(partial.begin() + 9, partial.end(), [](u8 value) { return value == 0; }));
  send(remote, {0x12, 2, 0x32});
  receive(remote, 0x22);
  remote.update();
  const auto report = receive(remote, 0x32);
  CHECK(report ==
        std::vector<u8>({0xa1, 0x32, 0, 0, 0x7e, 0x7e, 0x7e, 0x7e, 0x8b, 0xfb, 0xfe, 0xfe}));
  write_register(remote, 0x52, 0xf0, 0x55);
  const auto clear = read_bytes(remote, 0x52, 0x20, 16);
  CHECK(clear[7] == 0x80 && clear[11] == 0xb3);
  write_register(remote, 0x52, 0xf0, 0xaa);
  send(remote, {0x17, 4, 0xa4, 0x12, 0x20, 0, 32});
  auto cipher = receive(remote, 0x21);
  CHECK(cipher[5] == 0x12 && cipher[6] == 0x20);
  for (unsigned i = 7; i < 23; ++i)
    CHECK(cipher[i] == u8(u8(clear[i] - 0x17) ^ 0x17));
  remote.update();
  cipher = receive(remote, 0x21);
  CHECK(cipher[5] == 0x12 && cipher[6] == 0x30);
  CHECK(std::all_of(cipher.begin() + 7, cipher.end(), [](u8 value) { return value == 0xfe; }));
  // A write starting outside the key area still dirties a key byte in its transferred prefix.
  write_bytes(remote, 0x52, 0x3f, std::array<u8, 2>{0xe1, 0xa5});
  cipher = read_bytes(remote, 0x52, 0x20, 16);
  const std::array<u8, 8> nonce_subtraction{0x17, 0x17, 0x17, 0x17, 0x17, 0xbc, 0x17, 0x17};
  const std::array<u8, 8> nonce_mask{0x17, 0x17, 0x17, 0xbc, 0x17, 0x17, 0x17, 0x17};
  for (unsigned i = 0; i < 16; ++i)
    CHECK(cipher[7 + i] == u8(u8(clear[7 + i] - nonce_subtraction[i & 7]) ^ nonce_mask[i & 7]));
  write_bytes(remote, 0x52, 0x40, std::array<u8, 16>{});
  write_register(remote, 0x52, 0x4f, 0x5a);
  cipher = read_bytes(remote, 0x52, 0x20, 16);
  const std::array<u8, 8> answer_subtraction{0x17, 0x17, 0x17, 0xae, 0x17, 0x17, 0x17, 0x17};
  const std::array<u8, 8> answer_mask{0xae, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17};
  for (unsigned i = 0; i < 16; ++i)
    CHECK(cipher[7 + i] == u8(u8(clear[7 + i] - answer_subtraction[i & 7]) ^ answer_mask[i & 7]));
  remote.set_linked(false);
  remote.set_linked(true);
  remote.update();
  receive(remote, 0x20);
  write_register(remote, 0x52, 0xf0, 0xaa);
  cipher = read_bytes(remote, 0x52, 0x20, 16);
  for (unsigned i = 7; i < 23; ++i)
    CHECK(cipher[i] == u8(u8(clear[i] - 0x17) ^ 0x17));
}

void retained_read_slave() {
  Remote remote;
  remote.set_linked(true);
  write_bytes(remote, 0x50, 9, std::array<u8, 1>{1}, 7);
  CHECK(read_bytes(remote, 0x50, 0, 1)[4] == 0xf8);
  write_bytes(remote, 0x51, 9, std::array<u8, 1>{1}, 8);
  CHECK(read_bytes(remote, 0x51, 9, 1)[7] == 0);
  write_bytes(remote, 0x51, 9, std::array<u8, 1>{1});
  CHECK(read_bytes(remote, 0x51, 9, 1)[7] == 1);
  send(remote, {0x17, 4, 0xa0, 0, 0, 0, 0});
  CHECK(!remote.take_report());
  write_bytes(remote, 0x51, 9, std::array<u8, 1>{2}, 8);
  remote.set_linked(false);
  remote.set_linked(true);
  write_bytes(remote, 0x51, 9, std::array<u8, 1>{2});
  send(remote, {0x13, 6});
  receive(remote, 0x22);
  write_bytes(remote, 0x58, 0xff, std::array<u8, 2>{0x42, 0x99}, 7);
  const auto camera = read_bytes(remote, 0x58, 0xff, 2);
  CHECK(camera[4] == 0xf7 && camera[7] == 0x42 && camera[8] == 0);
}

} // namespace

int main() {
  return run_tests([] {
    remote_reports();
    full_infrared();
    remote_memory();
    speaker_registers();
    speaker_processing();
    nunchuk_reset();
    encrypted_bank();
    retained_read_slave();
  });
}
