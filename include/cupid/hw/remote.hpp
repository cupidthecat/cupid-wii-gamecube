// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/types.hpp"
#include <array>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace cupid::hw {
enum RemoteButton : u16 {
  RemoteLeft = 0x0001,
  RemoteRight = 0x0002,
  RemoteDown = 0x0004,
  RemoteUp = 0x0008,
  RemotePlus = 0x0010,
  RemoteTwo = 0x0100,
  RemoteOne = 0x0200,
  RemoteB = 0x0400,
  RemoteA = 0x0800,
  RemoteMinus = 0x1000,
  RemoteHome = 0x8000
};

struct InfraredPoint {
  u16 x = 0xffff, y = 0xffff;
  u8 size = 0xff;
};

struct NunchukState {
  bool attached = false;
  u8 stick_x = 128, stick_y = 128;
  std::array<u16, 3> acceleration{512, 512, 716};
  bool c = false, z = false;
};

struct RemoteState {
  bool connected = false;
  u16 buttons = 0;
  std::array<u16, 3> acceleration{512, 512, 616};
  std::array<InfraredPoint, 4> infrared{};
  NunchukState nunchuk;
  u8 battery = 0xff;
};

class Remote {
public:
  Remote();
  void set_state(const RemoteState &state);
  const RemoteState &state() const;
  void set_linked(bool linked);
  bool linked() const;
  void receive_output(std::span<const u8> report);
  void update();
  // Packets include the HID input header 0xa1 and report ID.
  std::optional<std::vector<u8>> take_report();
  std::span<const u8> eeprom() const;
  void set_eeprom(std::span<const u8> bytes);
  std::function<void(bool)> rumble;

private:
  struct Read {
    u8 space, slave;
    u16 address, remaining;
  };

  RemoteState state_;
  bool linked_ = false, extension_present_ = false, ir_enabled_ = false;
  bool speaker_enabled_ = false, speaker_muted_ = false;
  u8 mode_ = 0x30, leds_ = 0;
  std::array<u8, 0x1700> eeprom_{};
  std::array<u8, 256> extension_{}, camera_{};
  std::optional<Read> read_;
  std::deque<std::vector<u8>> reports_;
  void ack(u8 operation, u8 error = 0);
  void status();
  void refresh_peripherals();
  void process_read();
  u8 memory_read(u8 space, u8 slave, u16 address, std::span<u8> bytes);
  u8 memory_write(u8 space, u8 slave, u16 address, std::span<const u8> bytes);
  std::vector<u8> data_report() const;
};
} // namespace cupid::hw
