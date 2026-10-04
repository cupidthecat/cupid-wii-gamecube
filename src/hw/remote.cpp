// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/hw/remote.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace cupid::hw {
namespace {
void checksum(std::span<u8> bytes, unsigned count) {
  u8 sum = 0x55;
  for (std::size_t i = 0; i < bytes.size() - count; ++i)
    sum = u8(sum + bytes[i]);
  for (std::size_t i = bytes.size() - count; i < bytes.size(); ++i) {
    bytes[i] = sum;
    sum = u8(sum + 0x55);
  }
}

void buttons(std::vector<u8> &packet, u16 value) {
  packet.push_back(u8(value & 0x9f1f));
  packet.push_back(u8((value & 0x9f1f) >> 8));
}

[[noreturn]] void unfinished(const std::string &feature) {
  throw std::runtime_error("Unsupported Wii Remote behavior: " + feature);
}
} // namespace

Remote::Remote() {
  for (std::size_t start : {std::size_t(0x16), std::size_t(0x20)}) {
    auto calibration = std::span<u8>(eeprom_).subspan(start, 10);
    for (unsigned axis = 0; axis < 3; ++axis) {
      calibration[axis] = 0x80;
      calibration[axis + 4] = 0x9a;
    }
    checksum(calibration, 1);
  }
  for (unsigned copy = 0; copy < 2; ++copy) {
    auto calibration = std::span<u8>(eeprom_).subspan(copy * 11, 11);
    const std::array<InfraredPoint, 4> corners{
        {{0x7f, 0x5d, 0}, {0x380, 0x5d, 0}, {0x380, 0x2a2, 0}, {0x7f, 0x2a2, 0}}};
    for (unsigned pair = 0; pair < 2; ++pair) {
      const auto &a = corners[pair * 2], &b = corners[pair * 2 + 1];
      const unsigned base = pair * 5;
      calibration[base] = u8(a.x);
      calibration[base + 1] = u8(a.y);
      calibration[base + 2] =
          u8(((a.y >> 8) << 6) | ((a.x >> 8) << 4) | ((b.y >> 8) << 2) | (b.x >> 8));
      calibration[base + 3] = u8(b.x);
      calibration[base + 4] = u8(b.y);
    }
    checksum(calibration, 1);
  }
  reset_extension();
}

void Remote::reset_extension() {
  extension_.fill(u8(0));
  auto calibration = std::span<u8>(extension_).subspan(0x20, 16);
  for (unsigned axis = 0; axis < 3; ++axis) {
    calibration[axis] = 0x80;
    calibration[axis + 4] = 0xb3;
  }
  for (unsigned axis = 0; axis < 2; ++axis) {
    calibration[8 + axis * 3] = 0xe0;
    calibration[9 + axis * 3] = 0x20;
    calibration[10 + axis * 3] = 0x80;
  }
  checksum(calibration, 2);
  extension_[0xfa] = 0;
  extension_[0xfb] = 0;
  extension_[0xfc] = 0xa4;
  extension_[0xfd] = 0x20;
  extension_[0xfe] = 0;
  extension_[0xff] = 0;
}

void Remote::set_state(const RemoteState &state) {
  state_ = state;
  state_.buttons &= 0x9f1f;
  for (auto &value : state_.acceleration)
    value = std::min<u16>(value, 1023);
  for (auto &value : state_.nunchuk.acceleration)
    value = std::min<u16>(value, 1023);
}

const RemoteState &Remote::state() const {
  return state_;
}

void Remote::set_linked(bool linked) {
  linked_ = linked;
  extension_present_ = false;
  ir_enabled_ = speaker_enabled_ = speaker_muted_ = false;
  mode_ = 0x30;
  leds_ = 0;
  camera_.fill(u8(0));
  speaker_.fill(u8(0));
  speaker_decoder_.reset();
  reset_extension();
  read_.reset();
  reports_.clear();
  if (rumble)
    rumble(false);
}

bool Remote::linked() const {
  return linked_;
}

std::span<const u8> Remote::eeprom() const {
  return eeprom_;
}

void Remote::set_eeprom(std::span<const u8> bytes) {
  if (bytes.size() != eeprom_.size())
    throw std::invalid_argument("Wii Remote EEPROM requires 5888 bytes");
  std::copy(bytes.begin(), bytes.end(), eeprom_.begin());
}

void Remote::set_speaker_processing(bool enabled, float pan) {
  speaker_processing_ = enabled;
  speaker_pan_ = pan;
}

void Remote::process_speaker(std::span<const u8> bytes) {
  const u16 divisor = u16(u16(speaker_[3]) | (u16(speaker_[4]) << 8));
  if (speaker_processing_ && divisor != 0 && !bytes.empty() && !speaker_samples)
    unfinished("host speaker output");
  const auto packet = speaker_decoder_.decode(
      {speaker_[2], divisor, speaker_[5], speaker_processing_, speaker_pan_}, bytes);
  if (packet.count != 0)
    speaker_samples(packet);
}

void Remote::ack(u8 operation, u8 error) {
  std::vector<u8> packet{0xa1, 0x22};
  buttons(packet, state_.buttons);
  packet.push_back(operation);
  packet.push_back(error);
  reports_.push_back(std::move(packet));
}

void Remote::status() {
  std::vector<u8> packet{0xa1, 0x20};
  buttons(packet, state_.buttons);
  packet.push_back(u8(leds_ | (state_.battery < 0x20 ? 1 : 0) | (state_.nunchuk.attached ? 2 : 0) |
                      (speaker_enabled_ ? 4 : 0) | (ir_enabled_ ? 8 : 0)));
  packet.insert(packet.end(), {0, 0, state_.battery});
  reports_.push_back(std::move(packet));
}

void Remote::refresh_peripherals() {
  const auto &n = state_.nunchuk;
  extension_[0] = n.stick_x;
  extension_[1] = n.stick_y;
  for (unsigned axis = 0; axis < 3; ++axis)
    extension_[axis + 2] = u8(n.acceleration[axis] >> 2);
  extension_[5] = u8((n.z ? 0 : 1) | (n.c ? 0 : 2) | ((n.acceleration[0] & 3) << 2) |
                     ((n.acceleration[1] & 3) << 4) | ((n.acceleration[2] & 3) << 6));
  std::fill(camera_.begin() + 0x37, camera_.begin() + 0x5b, u8(0xff));
  if (!ir_enabled_ || camera_[0x30] != 8)
    return;
  const u8 format = camera_[0x33];
  if (format == 1) {
    for (unsigned pair = 0; pair < 2; ++pair) {
      const auto &a = state_.infrared[pair * 2], &b = state_.infrared[pair * 2 + 1];
      const unsigned base = 0x37 + pair * 5;
      camera_[base] = u8(a.x);
      camera_[base + 1] = u8(a.y);
      camera_[base + 2] = u8((((a.y >> 8) & 3) << 6) | (((a.x >> 8) & 3) << 4) |
                             (((b.y >> 8) & 3) << 2) | ((b.x >> 8) & 3));
      camera_[base + 3] = u8(b.x);
      camera_[base + 4] = u8(b.y);
    }
  } else if (format == 3) {
    for (unsigned point = 0; point < 4; ++point) {
      const auto &p = state_.infrared[point];
      if (p.x >= 1024)
        continue;
      const unsigned base = 0x37 + point * 3;
      camera_[base] = u8(p.x);
      camera_[base + 1] = u8(p.y);
      camera_[base + 2] = u8((p.size & 15) | (((p.x >> 8) & 3) << 4) | (((p.y >> 8) & 3) << 6));
    }
  } else if (format == 5)
    unfinished("full infrared camera report");
}

u8 Remote::memory_read(u8 space, u8 slave, u16 address, std::span<u8> bytes) {
  refresh_peripherals();
  if (space == 0) {
    if (std::size_t(address) + bytes.size() > eeprom_.size())
      return 8;
    std::copy_n(eeprom_.begin() + address, bytes.size(), bytes.begin());
    return 0;
  }
  if (space > 2)
    return 6;
  if (slave == 0x50)
    return 8;
  if (slave == 0x52 && state_.nunchuk.attached) {
    if (extension_[0xf0] == 0xaa)
      unfinished("encrypted Nunchuk memory");
    const std::size_t offset = u8(address);
    if (bytes.size() > extension_.size() - offset)
      return 7;
    std::copy_n(extension_.begin() + offset, bytes.size(), bytes.begin());
    return 0;
  }
  if (slave == 0x58 && ir_enabled_) {
    const std::size_t offset = u8(address);
    if (bytes.size() > camera_.size() - offset)
      return 7;
    std::copy_n(camera_.begin() + offset, bytes.size(), bytes.begin());
    return 0;
  }
  if (slave == 0x51) {
    const std::size_t offset = u8(address);
    const auto count = std::min(bytes.size(), speaker_.size() - offset);
    std::copy_n(speaker_.begin() + offset, count, bytes.begin());
    return count == bytes.size() ? 0 : 7;
  }
  return 7;
}

u8 Remote::memory_write(u8 space, u8 slave, u16 address, std::span<const u8> bytes) {
  if (space == 0) {
    if (std::size_t(address) + bytes.size() > eeprom_.size())
      return 8;
    std::copy(bytes.begin(), bytes.end(), eeprom_.begin() + address);
    return 0;
  }
  if (space > 2)
    return 6;
  if (slave == 0x50)
    return 8;
  const std::size_t offset = u8(address);
  if (slave == 0x52 && state_.nunchuk.attached) {
    if (bytes.size() > extension_.size() - offset)
      return 7;
    std::copy(bytes.begin(), bytes.end(), extension_.begin() + offset);
    return 0;
  }
  if (slave == 0x58 && ir_enabled_) {
    if (bytes.size() > camera_.size() - offset)
      return 7;
    std::copy(bytes.begin(), bytes.end(), camera_.begin() + offset);
    return 0;
  }
  if (slave == 0x51) {
    if (offset == 0) {
      process_speaker(bytes);
      return 0;
    }
    const auto count = std::min(bytes.size(), speaker_.size() - offset);
    std::copy_n(bytes.begin(), count, speaker_.begin() + offset);
    return count == bytes.size() ? 0 : 7;
  }
  return 7;
}

void Remote::process_read() {
  if (!read_ || read_->remaining == 0)
    return;
  const unsigned amount = std::min<unsigned>(16, read_->remaining);
  std::vector<u8> packet{0xa1, 0x21};
  buttons(packet, state_.buttons);
  packet.resize(23, u8(0));
  packet[5] = u8(read_->address >> 8);
  packet[6] = u8(read_->address);
  u8 error = 0;
  if (read_->space == 0 && std::size_t(read_->address) + read_->remaining > eeprom_.size())
    error = 8;
  else
    error = memory_read(read_->space, read_->slave, read_->address,
                        std::span<u8>(packet).subspan(7, amount));
  packet[4] = error != 0 ? u8(0xf0 | error) : u8((amount - 1) << 4);
  reports_.push_back(std::move(packet));
  if (error != 0 || amount == read_->remaining)
    read_.reset();
  else {
    read_->address = u16(read_->address + amount);
    read_->remaining = u16(read_->remaining - amount);
  }
}

void Remote::receive_output(std::span<const u8> report) {
  if (!linked_ || report.size() < 2)
    return;
  const u8 operation = report[0], flags = report[1];
  if (rumble)
    rumble((flags & 1) != 0);
  switch (operation) {
  case 0x10:
    return;
  case 0x11:
    leds_ = flags & 0xf0;
    break;
  case 0x12:
    if (report.size() < 3)
      return;
    if ((report[2] < 0x30 || report[2] > 0x37) && report[2] != 0x3d && report[2] != 0x3e &&
        report[2] != 0x3f)
      return;
    if (report[2] == 0x3e || report[2] == 0x3f)
      unfinished("interleaved acceleration and infrared report");
    mode_ = report[2];
    break;
  case 0x13:
    ir_enabled_ = (flags & 4) != 0;
    break;
  case 0x14:
    speaker_enabled_ = (flags & 4) != 0;
    break;
  case 0x15:
    status();
    return;
  case 0x16: {
    if (report.size() < 22 || report[5] == 0 || report[5] > 16)
      return;
    const u16 address = u16((u16(report[3]) << 8) | report[4]);
    const u8 error =
        memory_write((flags >> 2) & 3, report[2] >> 1, address, report.subspan(6, report[5]));
    ack(operation, error);
    return;
  }
  case 0x17:
    if (report.size() < 7)
      return;
    if (read_) {
      ack(operation, 4);
      return;
    }
    read_ = Read{u8((flags >> 2) & 3), u8(report[2] >> 1), u16((u16(report[3]) << 8) | report[4]),
                 u16((u16(report[5]) << 8) | report[6])};
    process_read();
    if (read_ && read_->remaining == 0)
      read_.reset();
    return;
  case 0x18:
    if (report.size() < 22 || (flags >> 3) == 0 || (flags >> 3) > 20)
      return;
    if (!speaker_muted_)
      process_speaker(report.subspan(2, flags >> 3));
    return;
  case 0x19:
    speaker_muted_ = (flags & 4) != 0;
    break;
  case 0x1a:
    break;
  default:
    unfinished("output report " + std::to_string(operation));
  }
  if (flags & 2)
    ack(operation);
}

std::vector<u8> Remote::data_report() const {
  const bool core = mode_ != 0x3d;
  const bool acceleration = mode_ == 0x31 || mode_ == 0x33 || mode_ == 0x35 || mode_ == 0x37;
  const unsigned ir_bytes = mode_ == 0x33 ? 12 : (mode_ == 0x36 || mode_ == 0x37 ? 10 : 0);
  const unsigned extension_bytes = mode_ == 0x32   ? 8
                                   : mode_ == 0x34 ? 19
                                   : mode_ == 0x35 ? 16
                                   : mode_ == 0x36 ? 9
                                   : mode_ == 0x37 ? 6
                                   : mode_ == 0x3d ? 21
                                                   : 0;
  std::vector<u8> packet{0xa1, mode_};
  if (core)
    buttons(packet, state_.buttons);
  if (acceleration) {
    const auto &a = state_.acceleration;
    packet[2] |= u8((a[0] & 3) << 5);
    // The behavioral reference's two-bit assignment drops the Z low bit.
    packet[3] |= u8(((a[1] >> 1) & 1) << 5);
    for (u16 value : a)
      packet.push_back(u8(value >> 2));
  }
  if (ir_bytes != 0)
    packet.insert(packet.end(), camera_.begin() + 0x37, camera_.begin() + 0x37 + ir_bytes);
  if (extension_bytes != 0) {
    if (state_.nunchuk.attached) {
      if (extension_[0xf0] == 0xaa)
        unfinished("encrypted Nunchuk report");
      packet.insert(packet.end(), extension_.begin(), extension_.begin() + extension_bytes);
    } else
      packet.insert(packet.end(), extension_bytes, u8(0xff));
  }
  return packet;
}

void Remote::update() {
  if (!linked_)
    return;
  refresh_peripherals();
  if (extension_present_ != state_.nunchuk.attached) {
    extension_present_ = state_.nunchuk.attached;
    if (extension_present_)
      reset_extension();
    mode_ = 0;
    status();
    return;
  }
  if (read_) {
    process_read();
    return;
  }
  if (mode_ != 0)
    reports_.push_back(data_report());
}

std::optional<std::vector<u8>> Remote::take_report() {
  if (reports_.empty())
    return std::nullopt;
  auto packet = std::move(reports_.front());
  reports_.pop_front();
  return packet;
}
} // namespace cupid::hw
