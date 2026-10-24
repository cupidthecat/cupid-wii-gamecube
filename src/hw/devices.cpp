// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/hw/devices.hpp"
#include "cupid/core/bus.hpp"
#include "cupid/hw/period.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cupid::hw {
namespace {
constexpr u32 pi_base = 0x0c003000, vi_base = 0x0c002000;
constexpr u32 di_base = 0x0c006000, si_base = 0x0c006400;
constexpr u32 exi_base = 0x0c006800, ai_base = 0x0c006c00, dsp_base = 0x0c005000;
constexpr u32 mi_base = 0x0c004000;
constexpr u32 interrupt_di = 4, interrupt_si = 8, interrupt_exi = 16;
constexpr u32 interrupt_ai = 32, interrupt_dsp = 64, interrupt_vi = 256;

bool wii_window(u32 address) {
  const u32 region = address & 0xffff0000u;
  return region == 0x0d000000 || region == 0x0d800000;
}

u32 pair(const std::array<u16, 0x3b> &registers, u32 offset) {
  return (u32(registers[offset / 2]) << 16) | registers[offset / 2 + 1];
}

void set_pair(std::array<u16, 0x3b> &registers, u32 offset, u32 value) {
  registers[offset / 2] = u16(value >> 16);
  registers[offset / 2 + 1] = u16(value);
}

u32 buffer_word(std::span<const u8> data) {
  u32 result = 0;
  for (u8 byte : data)
    result = (result << 8) | byte;
  return result;
}

void put_word(std::span<u8> data, u32 value) {
  for (std::size_t i = data.size(); i != 0; --i) {
    data[i - 1] = u8(value);
    value >>= 8;
  }
}

void card_checksums(std::span<u8> block, std::size_t first, std::size_t last,
                    std::size_t destination) {
  u16 sum = 0, inverse = 0;
  for (std::size_t i = first; i < last; i += 2) {
    const u16 value = u16((u16(block[i]) << 8) | block[i + 1]);
    sum = u16(sum + value);
    inverse = u16(inverse + (value ^ 0xffff));
  }
  put_word(block.subspan(destination, 2), sum == 0xffff ? 0 : sum);
  put_word(block.subspan(destination + 2, 2), inverse == 0xffff ? 0 : inverse);
}

[[noreturn]] void invalid(u32 address, bool write, const char *message) {
  throw BusFault(address, write, message);
}

[[noreturn]] void unavailable(u32 address, bool write, const char *message) {
  throw BusFault(address, write, message, true);
}
} // namespace

Devices::Devices(Console console)
    : console_(console), clock_(console == Console::Wii ? 729000000 : 486000000),
      disc_stream_(clock_, console == Console::Wii ? 2250 : 2248),
      aram_(console == Console::GameCube ? 16u * 1024 * 1024 : 0) {
  audio_period_ = clock_ * 8 * (console_ == Console::Wii ? 3375u : 3372u) / 108000000;
  pi_[0] = 0x10100;
  pi_[10] = 0x1ff;
  pi_[12] = 0x02492492;
  vi_[0] = 6;
  vi_[1] = 1;
  set_pair(vi_, 4, (71u << 24) | (105u << 16) | 429u);
  set_pair(vi_, 8, (373u << 17) | (162u << 7) | 64u);
  set_pair(vi_, 0x0c, (5u << 16) | 502u);
  set_pair(vi_, 0x10, (4u << 16) | 503u);
  set_pair(vi_, 0x14, ((520u << 5) | 12u) * 0x10001u);
  set_pair(vi_, 0x18, ((519u << 5) | 13u) * 0x10001u);
  set_pair(vi_, 0x30, 0x10000000u | (263u << 16) | 430u);
  set_pair(vi_, 0x34, 0x10000000u | (1u << 16) | 1u);
  vi_[0x48 / 2] = (40u << 8) | 40u;
  vi_[0x6c / 2] = 1;
  update_video_timing();
  di_[9] = 1;
  exi_[0][0] = 0x800;
  exi_[1][0] = 0x880;
  dsp_[0x0a / 2] = 4;
  dsp_[0x16 / 2] = 1;
  dsp_[0x1a / 2] = 156;
  sram_[5] = 0x2c;
  sram_[6] = 0xff;
  sram_[7] = 0xd0;
  sram_[23] = 0x2c;
  sram_[62] = sram_[63] = 0xff;
  disc_stream_.read = [this](u64 position, std::span<u8> bytes) {
    if (!disc_read)
      throw std::runtime_error("Disc audio issued without an inserted disc");
    disc_read(position, bytes);
  };
  disc_stream_.samples = [this](std::span<const s16> samples, u32 divisor) {
    if (disc_audio_samples)
      disc_audio_samples(samples, divisor, u8(ai_[1]), u8(ai_[1] >> 8));
  };
  disc_stream_.read_error = [this] { di_error_ = 0x31100; };
}

u32 Devices::canonical(u32 address) const {
  if (console_ == Console::Wii && wii_window(address)) {
    const u32 offset = address & 0xffff;
    if (offset >= 0x6000 && offset < 0x7000)
      return 0x0c000000 | offset;
  }
  return address;
}

bool Devices::handles(u32 address) const {
  if (console_ == Console::Wii && wii_window(address))
    return true;
  address = canonical(address);
  return address >= 0x0c000000 && address < 0x0c010000;
}

bool Devices::external_interrupt_pending() const {
  return (pi_[0] & pi_[1]) != 0;
}

void Devices::set_interrupt(u32 cause, bool active) {
  if (active)
    pi_[0] |= cause;
  else
    pi_[0] &= ~cause;
}

void Devices::dsp_interrupt() {
  dsp_[5] |= 0x80;
  update_interrupts(interrupt_dsp);
}

void Devices::reset_disc(bool spinup) {
  flush_elapsed();
  drive_state_ = !disc_read ? DriveState::CoverOpened
                 : spinup   ? DriveState::NoId
                            : DriveState::Changed;
  disc_stream_.reset();
  di_error_ = 0;
  rebuild_deadline();
}

void Devices::prepare_disc_boot(bool streaming, u8 buffer_size) {
  flush_elapsed();
  next_deadline_ = 0;
  drive_state_ = DriveState::ReadyNoReads;
  disc_stream_.configure(streaming, buffer_size ? buffer_size : 10);
}

u32 Devices::disc_audio_divisor() const {
  return (console_ == Console::Wii ? 2250u : 2248u) * ((ai_[0] & 2) ? 2u : 3u) / 2;
}

bool Devices::disc_read_ready() {
  const u32 error = !disc_read                            ? 0x23a00
                    : drive_state_ == DriveState::Changed ? 0x62800
                    : drive_state_ == DriveState::Stopped ? 0x20400
                    : drive_state_ == DriveState::NoId    ? 0x20401
                                                          : 0;
  if (!error)
    return true;
  di_error_ = error;
  di_failed_ = true;
  return false;
}

u32 Devices::disc_status_error() const {
  const u32 state = !disc_read && drive_state_ <= DriveState::ReadyNoReads
                        ? u32(DriveState::NoMedium)
                        : u32(drive_state_);
  return di_error_ | ((state ? state - 1 : 0) << 24);
}

void Devices::disc_id_read() {
  if (drive_state_ == DriveState::NoId)
    drive_state_ = DriveState::ReadyNoReads;
  else if (drive_state_ == DriveState::ReadyNoReads)
    drive_state_ = DriveState::Ready;
}

DiscCommandResult Devices::disc_immediate(u32 command, u32 argument1, u32 argument2) {
  flush_elapsed();
  next_deadline_ = 0;
  const u8 opcode = u8(command >> 24);
  if (opcode != 0xab && (opcode < 0xe0 || opcode > 0xe4))
    throw std::invalid_argument("Command is not an immediate disc operation");
  di_[2] = command;
  di_[3] = argument1;
  di_[4] = argument2;
  di_failed_ = false;
  if (opcode != 0xe0)
    di_error_ = 0;
  if (opcode == 0xe0) {
    di_[8] = disc_status_error();
    di_error_ = 0;
  } else if (opcode == 0xe3) {
    if (drive_state_ == DriveState::Ready || drive_state_ == DriveState::ReadyNoReads ||
        drive_state_ == DriveState::NoId)
      drive_state_ = DriveState::Stopped;
  } else if (opcode != 0xab && disc_read_ready()) {
    u32 error = 0;
    if (opcode == 0xe4) {
      if (drive_state_ == DriveState::Ready)
        error = 0x52402;
      else
        disc_stream_.configure((command & 0x10000) != 0, u8(command & 15));
    } else {
      const u8 operation = u8(command >> 16);
      if (opcode == 0xe1) {
        u32 streaming = 0;
        // An enabled stream operation consumes this period before subcommand validation.
        if (!disc_stream_.status(0, streaming))
          drive_state_ = DriveState::Ready;
      }
      error = opcode == 0xe1 ? disc_stream_.command(operation, u64(argument1) << 2, argument2)
                             : disc_stream_.status(operation, di_[8]);
    }
    if (error) {
      di_failed_ = true;
      di_error_ = error;
    }
  }
  return {di_failed_, di_[8], disc_status_error()};
}

u32 Devices::read(u32 address, unsigned width) {
  flush_elapsed();
  if (console_ == Console::Wii && wii_window(address) && (address & 0xffff) < 0x200) {
    if (width != 4 || (address & 3) != 0)
      invalid(address, false, "Invalid IPC access size or alignment");
    if (!ipc_read)
      unavailable(address, false, "Wii IPC MMIO is not connected");
    return ipc_read(address);
  }
  const u32 physical = canonical(address);
  if ((width != 1 && width != 2 && width != 4) || (physical & (width - 1)) != 0)
    invalid(address, false, "Invalid MMIO access size or alignment");
  const u32 page = physical & 0xfffff000;
  if (page == 0x0c000000 || page == 0x0c001000) {
    if (!graphics_read)
      unavailable(address, false, "Graphics MMIO is not connected");
    return graphics_read(physical, width);
  }
  if (page == vi_base || page == dsp_base || page == mi_base) {
    if (width == 4)
      return (u32(read_half(physical)) << 16) | read_half(physical + 2);
    if (width == 2)
      return read_half(physical);
    if (page == vi_base)
      return (read_half(physical & ~1u) >> ((1 - (physical & 1)) * 8)) & 255;
    invalid(address, false, "Device byte read size is invalid");
  }
  if (page == pi_base && width == 2)
    return (read_word(physical & ~3u) >> ((2 - (physical & 2)) * 8)) & 65535;
  if (physical >= si_base + 0x80 && physical < si_base + 0x100 && width != 1)
    return buffer_word(std::span<const u8>(si_buffer_).subspan(physical - si_base - 0x80, width));
  if (width != 4)
    invalid(address, false, "Register requires a 32-bit read");
  return read_word(physical);
}

void Devices::write(u32 address, u32 value, unsigned width) {
  flush_elapsed();
  next_deadline_ = 0;
  if (console_ == Console::Wii && wii_window(address) && (address & 0xffff) < 0x200) {
    if (width != 4 || (address & 3) != 0)
      invalid(address, true, "Invalid IPC access size or alignment");
    if (!ipc_write)
      unavailable(address, true, "Wii IPC MMIO is not connected");
    ipc_write(address, value);
    return;
  }
  const u32 physical = canonical(address);
  if ((width != 1 && width != 2 && width != 4) || (physical & (width - 1)) != 0)
    invalid(address, true, "Invalid MMIO access size or alignment");
  const u32 page = physical & 0xfffff000;
  if (page == 0x0c000000 || page == 0x0c001000) {
    if (!graphics_write)
      unavailable(address, true, "Graphics MMIO is not connected");
    graphics_write(physical, value, width);
    return;
  }
  if (page == vi_base || page == dsp_base || page == mi_base) {
    if (width == 4) {
      write_half(physical, u16(value >> 16));
      write_half(physical + 2, u16(value));
      return;
    }
    if (width == 2) {
      write_half(physical, u16(value));
      return;
    }
    invalid(address, true, "Device byte writes are unsupported");
  }
  if (physical >= si_base + 0x80 && physical < si_base + 0x100 && width != 1) {
    put_word(std::span<u8>(si_buffer_).subspan(physical - si_base - 0x80, width), value);
    return;
  }
  if (width != 4)
    invalid(address, true, "Register requires a 32-bit write");
  write_word(physical, value);
}

u32 Devices::read_word(u32 address) {
  if (address >= pi_base && address <= pi_base + 0x30) {
    const unsigned index = (address - pi_base) / 4;
    if (index == 2 || index == 6)
      invalid(address, false, "Write-only or unmapped PI register");
    if (index == 11)
      return 0x246500b1;
    return pi_[index];
  }
  if (address >= di_base && address < di_base + 0x28)
    return di_[(address - di_base) / 4];
  if (address >= ai_base && address < ai_base + 0x10)
    return ai_[(address - ai_base) / 4];
  if (address >= si_base && address < si_base + 0x30) {
    const unsigned channel = (address - si_base) / 12, index = ((address - si_base) % 12) / 4;
    if (index != 0) {
      si_status_ &= ~(0x20000000u >> (channel * 8));
      update_interrupts(interrupt_si);
    }
    return si_channels_[channel][index];
  }
  if (address == si_base + 0x30)
    return si_poll_;
  if (address == si_base + 0x34)
    return si_csr_;
  if (address == si_base + 0x38)
    return si_status_;
  if (address == si_base + 0x3c)
    return si_clock_;
  if (address >= exi_base && address < exi_base + 60)
    return exi_[(address - exi_base) / 20][((address - exi_base) % 20) / 4];
  unavailable(address, false, "Unimplemented hardware register read");
}

void Devices::write_word(u32 address, u32 value) {
  if (address >= pi_base && address <= pi_base + 0x30) {
    const unsigned index = (address - pi_base) / 4;
    switch (index) {
    case 0:
      pi_[0] &= ~value;
      return;
    case 1:
      pi_[1] = value;
      return;
    case 3:
    case 4:
    case 5:
      pi_[index] = value & (console_ == Console::Wii ? 0x1fffffe0 : 0x03ffffe0);
      return;
    case 6:
      if ((value & 1) != 0) {
        if (!fifo_reset)
          unavailable(address, true, "FIFO reset is not connected");
        fifo_reset();
      }
      return;
    case 7:
      pi_[index] = value & 7;
      return;
    case 9:
      pi_[index] = value;
      if (console_ == Console::GameCube && (value & 4) == 0)
        reset_disc(true);
      return;
    case 10:
      pi_[index] = value & 0x3ff;
      return;
    case 12:
      pi_[index] = value & 0x07ffffff;
      return;
    default:
      invalid(address, true, "Read-only or unmapped PI register");
    }
  }
  if (address >= di_base && address < di_base + 0x28) {
    const unsigned index = (address - di_base) / 4;
    switch (index) {
    case 0:
      if (value & 1)
        unavailable(address, true, "DI break behavior is not implemented");
      di_[0] = ((di_[0] & 0x54) & ~value) | (value & 0x2a);
      break;
    case 1:
      di_[1] = ((di_[1] & 5) & ~(value & 4)) | (value & 2);
      break;
    case 5:
      di_[5] = value & (console_ == Console::Wii ? 0xffffffe0 : 0x03ffffe0);
      break;
    case 6:
      di_[6] = value & 0xffffffe0;
      break;
    case 7:
      if (di_remaining_ != 0)
        unavailable(address, true, "DI command replaced before completion");
      di_[7] = value & 7;
      if (value & 1)
        start_disc_command();
      break;
    case 9:
      invalid(address, true, "Read-only DI configuration register");
    default:
      di_[index] = value;
      break;
    }
    update_interrupts(interrupt_di);
    return;
  }
  if (address >= ai_base && address < ai_base + 0x10) {
    const unsigned index = (address - ai_base) / 4;
    if (index == 0) {
      if ((ai_[0] ^ value) & 3)
        ai_remainder_ = 0;
      ai_[0] = (ai_[0] & 8 & ~value) | (value & 0x57);
      if (value & 0x20) {
        ai_[2] = 0;
        ai_remainder_ = 0;
      }
    } else {
      ai_[index] = value;
      if (index == 2)
        ai_remainder_ = 0;
    }
    if (disc_audio_control && index <= 1)
      disc_audio_control(disc_audio_divisor(), u8(ai_[1]), u8(ai_[1] >> 8));
    update_interrupts(interrupt_ai);
    return;
  }
  if (address >= si_base && address < si_base + 0x30) {
    si_channels_[(address - si_base) / 12][((address - si_base) % 12) / 4] = value;
    return;
  }
  if (address == si_base + 0x30) {
    si_poll_ = value;
    return;
  }
  if (address == si_base + 0x34) {
    constexpr u32 mutable_bits = 0x487f7f06;
    si_csr_ = (si_csr_ & ~mutable_bits) | (value & mutable_bits);
    si_csr_ &= ~(value & 0x90000000);
    if (value & 1) {
      si_csr_ |= 1;
      controller_transfer();
    }
    update_interrupts(interrupt_si);
    return;
  }
  if (address == si_base + 0x38) {
    si_status_ &= ~(value & 0x0f0f0f0f);
    if (value & 0x80000000) {
      for (unsigned port = 0; port < 4; ++port) {
        const u32 command = si_channels_[port][0];
        if ((command >> 16 & 255) == 0x40) {
          if ((si_poll_ & (0x80u >> port)) == 0)
            pad_modes_[port] = u8(command >> 8) & 7;
          if (rumble && pads_[port].connected)
            rumble(port, (command & 255) == 1);
        } else if ((command >> 16 & 255) != 0) {
          unavailable(address, true, "Unsupported controller direct command");
        }
      }
      si_status_ &= ~0x90909090u;
    }
    update_interrupts(interrupt_si);
    return;
  }
  if (address == si_base + 0x3c) {
    si_clock_ = value;
    return;
  }
  if (address >= exi_base && address < exi_base + 60) {
    const unsigned channel = (address - exi_base) / 20, index = ((address - exi_base) % 20) / 4;
    auto &registers = exi_[channel];
    if (index == 0) {
      const u32 mutable_bits = 0x3f5u | (channel < 2 ? 0x400u : 0u) | (channel == 0 ? 0x2000u : 0u);
      const u32 previous_cs = registers[0] & 0x380;
      registers[0] = ((registers[0] & ~mutable_bits) | (value & mutable_bits)) &
                     ~(value & (channel < 2 ? 0x80au : 0xau));
      const u32 current_cs = registers[0] & 0x380;
      const u32 changed_cs = previous_cs ^ current_cs;
      if (channel == 0 && changed_cs == 0x100 && current_cs != 0) {
        ipl_command_ = 0;
        ipl_command_bytes_ = 0;
        ipl_cursor_ = 0;
      }
      if (channel < cards_.size() && changed_cs == 0x80)
        memory_card_select(channel, current_cs != 0);
    } else {
      registers[index] = value;
      if (index == 3 && (value & 1))
        exi_transfer(channel);
    }
    update_interrupts(interrupt_exi);
    return;
  }
  unavailable(address, true, "Unimplemented hardware register write");
}

u16 Devices::read_half(u32 address) {
  if (address >= mi_base && address <= mi_base + 0x5a) {
    const u32 offset = address - mi_base;
    if (offset <= 0x10 || (offset >= 0x1c && offset <= 0x24) || offset >= 0x32)
      return mi_[offset / 2];
    invalid(address, false, "Unmapped memory interface register");
  }
  if (address >= vi_base && address <= vi_base + 0x74) {
    const u32 offset = address - vi_base;
    if (offset == 0x2c)
      return u16(1 + halfline_ / 2);
    if (offset == 0x2e) {
      const u32 width = pair(vi_, 4) & 1023;
      return u16(1 +
                 width * (video_remainder_ + (halfline_ & 1) * video_period()) / video_period());
    }
    return vi_[offset / 2];
  }
  if (address >= dsp_base && address <= dsp_base + 0x3a) {
    const u32 offset = address - dsp_base;
    if (offset < 8) {
      if (!dsp_read)
        unavailable(address, false, "DSP instruction runtime is not implemented");
      return dsp_read(offset);
    }
    if (offset == 0x0a && dsp_read)
      return u16((dsp_[5] & ~0xc07) | (dsp_read(offset) & 0xc07));
    if (offset == 0x0a || offset == 0x12 || offset == 0x16 || offset == 0x1a ||
        (offset >= 0x20 && offset <= 0x2a) || offset == 0x30 || offset == 0x32 || offset == 0x36)
      return dsp_[offset / 2];
    if (offset == 0x3a)
      return audio_blocks_ > 0 ? u16(audio_blocks_ - 1) : 0;
  }
  unavailable(address, false, "Unimplemented hardware halfword read");
}

void Devices::write_half(u32 address, u16 value) {
  if (address >= mi_base && address <= mi_base + 0x5a) {
    const u32 offset = address - mi_base;
    if (offset <= 0x10 || (offset >= 0x1c && offset <= 0x24) || offset >= 0x32) {
      mi_[offset / 2] = value;
      return;
    }
    invalid(address, true, "Unmapped memory interface register");
  }
  if (address >= vi_base && address <= vi_base + 0x74) {
    const u32 offset = address - vi_base;
    if (offset == 0x2c || offset == 0x2e)
      unavailable(address, true, "VI beam-position writes are unsupported");
    if (offset == 2) {
      vi_[1] = value & 0x3fd;
      if (value & 2)
        for (u32 i = 0x30; i <= 0x3e; i += 2)
          vi_[i / 2] = 0;
    } else {
      vi_[offset / 2] = value;
      if ((offset == 0x1c || offset == 0x20 || offset == 0x24 || offset == 0x28) &&
          (value & 0xe000))
        vi_[offset / 2] &= ~0x1000;
    }
    if (offset == 0 || offset == 4 || offset == 6 || (offset >= 0x0c && offset <= 0x12) ||
        offset == 0x6c)
      update_video_timing();
    update_interrupts(interrupt_vi);
    return;
  }
  if (address >= dsp_base && address <= dsp_base + 0x3a) {
    const u32 offset = address - dsp_base;
    if (offset < 8) {
      if (!dsp_write)
        unavailable(address, true, "DSP instruction runtime is not implemented");
      if (offset >= 4)
        invalid(address, true, "DSP output mailbox is read only");
      dsp_write(offset, value);
      return;
    }
    if (offset == 0x0a) {
      if (dsp_write) {
        dsp_write(offset, value);
        if (!dsp_read)
          unavailable(address, true, "DSP control read callback is not connected");
      } else if ((value & 4) == 0 || (value & 0xc02) != 0)
        unavailable(address, true, "DSP instruction execution is not implemented");
      dsp_[5] = u16((dsp_[5] & 0x2a8 & ~value) | (value & 0xf154));
      if (dsp_read)
        dsp_[5] = u16((dsp_[5] & ~0xc07) | (dsp_read(offset) & 0xc07));
      if (value & 1)
        dsp_[0x36 / 2] = 0;
      update_interrupts(interrupt_dsp);
      return;
    }
    u16 mask = 0;
    switch (offset) {
    case 0x12:
      mask = 0x7f;
      break;
    case 0x1a:
      mask = 0x7ff;
      break;
    case 0x20:
    case 0x24:
      mask = 0x3ff;
      break;
    case 0x28:
      mask = 0x83ff;
      break;
    case 0x22:
    case 0x26:
    case 0x2a:
    case 0x32:
      mask = 0xffe0;
      break;
    case 0x30:
      mask = console_ == Console::Wii ? 0x1fff : 0x3ff;
      break;
    case 0x36:
      if ((value & 0x8000) && !dma_read)
        unavailable(address, true, "Audio DMA is not connected");
      if ((value & 0x8000) && (dsp_[offset / 2] & 0x8000) == 0) {
        audio_address_ = (u32(dsp_[0x30 / 2]) << 16) | dsp_[0x32 / 2];
        audio_blocks_ = value & 0x7fff;
        audio_start_interrupts_.push_back(200);
      }
      mask = 0xffff;
      break;
    default:
      unavailable(address, true, "Unimplemented or read-only DSP register");
    }
    dsp_[offset / 2] = value & mask;
    if (offset == 0x2a)
      start_aram_dma();
    return;
  }
  unavailable(address, true, "Unimplemented hardware halfword write");
}

void Devices::update_interrupts(u32 devices) {
  bool video = false;
  for (u32 offset = 0x30; offset <= 0x3c; offset += 4)
    video = video || ((pair(vi_, offset) & 0x90000000u) == 0x90000000u);
  if (devices & interrupt_vi)
    set_interrupt(interrupt_vi, video);
  if (devices & interrupt_di)
    set_interrupt(interrupt_di, (((di_[0] >> 1) & di_[0] & 0x2a) != 0) || ((di_[1] & 6) == 6));
  if (si_status_ & 0x20202020)
    si_csr_ |= 0x10000000;
  else
    si_csr_ &= ~0x10000000u;
  if (devices & interrupt_si)
    set_interrupt(interrupt_si, (((si_csr_ >> 1) & si_csr_ & 0x48000000) != 0));
  if (devices & interrupt_ai)
    set_interrupt(interrupt_ai, (ai_[0] & 12) == 12);
  bool expansion = false;
  for (unsigned channel = 0; channel < exi_.size(); ++channel) {
    auto &registers = exi_[channel];
    if (channel < cards_.size() && !cards_[channel].image.empty() &&
        cards_[channel].interrupt_switch && cards_[channel].interrupt)
      registers[0] |= 2;
    expansion = expansion || (((registers[0] >> 1) & registers[0] & 0x405) != 0);
  }
  if (devices & interrupt_exi)
    set_interrupt(interrupt_exi, expansion);
  if (devices & interrupt_dsp)
    set_interrupt(interrupt_dsp, (((dsp_[5] >> 1) & dsp_[5] & 0xa8) != 0));
  level_interrupts_ = ((ai_[0] & 12) == 12 ? interrupt_ai : 0) |
                      (((dsp_[5] >> 1) & dsp_[5] & 0xa8) != 0 ? interrupt_dsp : 0) |
                      (expansion ? interrupt_exi : 0);
}

void Devices::update_video_timing() {
  const u32 equalization = 3 * (vi_[0] & 15), active = 2 * ((vi_[0] >> 4) & 1023);
  const u32 odd_blank = pair(vi_, 0x0c), even_blank = pair(vi_, 0x10);
  video_field_halflines_[0] =
      equalization + active + (odd_blank & 1023) + ((odd_blank >> 16) & 1023);
  video_field_halflines_[1] =
      equalization + active + (even_blank & 1023) + ((even_blank >> 16) & 1023);
  const u32 odd_start = equalization + (odd_blank & 1023);
  const s32 difference = s32((odd_blank >> 16) & 1023) - s32((even_blank >> 16) & 1023);
  const u32 even_start =
      u32(s32(video_field_halflines_[0] + equalization + (even_blank & 1023)) - difference);
  video_field_last_halflines_ = {odd_start + active - 1, even_start + active - 1};
  video_active_ = active != 0;
  video_sample_ticks_ = u32(2 * clock_ / ((vi_[0x6c / 2] & 1) ? 54000000 : 27000000));
  video_halfline_ticks_ = u64(video_sample_ticks_) * (pair(vi_, 4) & 1023);
}

u32 Devices::video_halflines(unsigned field) const {
  return video_field_halflines_[field != 0];
}

u32 Devices::video_ticks_per_sample() const {
  return video_sample_ticks_;
}

u64 Devices::video_nominal_period() const {
  return video_halfline_ticks_;
}

u64 Devices::video_period() const {
  return std::max<u64>(1, video_nominal_period());
}

float Devices::video_aspect_ratio() const {
  const u32 horizontal = pair(vi_, 8);
  const s32 samples =
      s32(pair(vi_, 4) & 1023) + s32((horizontal >> 17) & 1023) - s32((horizontal >> 7) & 1023);
  const float cycle_duration = 1.0f / float(clock_);
  const float half_line = cycle_duration * float(video_nominal_period());
  const float line = half_line * 2.0f;
  const float field = half_line * float(video_halflines(1));
  const float visible_line = cycle_duration * float(video_ticks_per_sample()) * float(samples);
  const float visible_field = float((vi_[0] >> 4) & 1023) * line;
  float across = visible_line / line;
  float down = visible_field / field;
  const bool pal = ((vi_[1] >> 8) & 3) == 1;
  across *= pal ? 64.0f / 52.0f : 63.555555f / 52.655555f;
  down *= pal ? 625.0f / 576.0f : 525.0f / 486.0f;
  const float relative = across / down;
  return std::isnormal(relative) ? relative * (4.0f / 3.0f) : 4.0f / 3.0f;
}

Xfb Devices::xfb(unsigned field) const {
  const u32 top = pair(vi_, 0x1c);
  const u32 selected = pair(vi_, (field != 0 && (vi_[1] & 4) == 0) ? 0x24 : 0x1c);
  return {(selected & 0xffffff) << ((top & 0x10000000) ? 5 : 0),
          u32((vi_[0x48 / 2] >> 8) & 127) * 16,
          u32((vi_[0] >> 4) & 1023),
          u32(vi_[0x48 / 2] & 255) * 32,
          field,
          cycles_,
          video_aspect_ratio()};
}

void Devices::video_halfline() {
  const u32 odd_length = video_halflines(0), even_length = video_halflines(1);
  if (video_active_ && (halfline_ == video_field_last_halflines_[0] ||
                        halfline_ == video_field_last_halflines_[1])) {
    const unsigned field = halfline_ == video_field_last_halflines_[0] ? 0 : 1;
    ++fields_;
    const Xfb frame = xfb(field);
    if ((vi_[1] & 1) && frame.address != 0 && xfb_present)
      xfb_present(frame);
  }
  if (halfline_ == si_next_poll_) {
    poll_controllers();
    const u32 poll_lines = (si_poll_ >> 16) & 1023;
    si_next_poll_ += 2 * poll_lines;
  }
  if (halfline_ == 0 || halfline_ == odd_length)
    si_next_poll_ = halfline_ + 15;
  ++halfline_;
  if (halfline_ >= odd_length + even_length)
    halfline_ = 0;
  for (u32 offset = 0x30; offset <= 0x3c; offset += 4) {
    u32 irq = pair(vi_, offset);
    const u32 parity = (irq & 2047) > (pair(vi_, 4) & 1023) ? 1 : 0;
    if (1 + halfline_ / 2 == ((irq >> 16) & 2047) && (halfline_ & 1) == parity)
      set_pair(vi_, offset, irq | 0x80000000);
  }
  update_interrupts(interrupt_vi);
}

u64 Devices::event_free_cycles() {
  if ((pi_[0] & 0x70u) != level_interrupts_)
    return 0;
  if (!next_deadline_)
    rebuild_deadline();
  if (!next_deadline_)
    return 0;
  return std::min(next_deadline_ - 1, std::numeric_limits<u64>::max() - cycles_);
}

void Devices::advance_events(u64 amount) {
  if (amount > std::numeric_limits<u64>::max() - cycles_)
    throw std::overflow_error("Device cycle counter overflow");
  cycles_ += amount;
  const u64 earlier = deferred_cycles_;
  deferred_cycles_ = 0;
  next_deadline_ = 0;
  // Materialize prior event-free calls first: a callback may throw before later
  // counters consume this call, so combining both intervals changes failure state.
  if (earlier)
    advance_quiet(earlier);
  advance_state(amount);
  rebuild_deadline();
}

void Devices::flush_elapsed() {
  if (!deferred_cycles_)
    return;
  const u64 elapsed = deferred_cycles_;
  deferred_cycles_ = 0;
  advance_quiet(elapsed);
}

void Devices::advance_quiet(u64 amount) {
  // Deferred calls exclude every completion and periodic boundary. Their
  // counters can be materialized without dispatching events or rebuilding the
  // deadline, which was already reduced by each call to advance().
  if (di_remaining_)
    di_remaining_ -= amount;
  if (aram_remaining_)
    aram_remaining_ -= amount;
  for (u64 &remaining : audio_start_interrupts_)
    remaining -= amount;
  for (auto &card : cards_) {
    if (card.command_remaining)
      card.command_remaining -= amount;
    for (u64 &remaining : card.transfer_remaining)
      remaining -= amount;
  }
  audio_remainder_ += amount;
  disc_stream_.advance(amount, disc_audio_divisor(), (ai_[0] & 1) != 0);
  if (ai_[0] & 1)
    ai_remainder_ += amount;
  video_remainder_ += amount;
  pi_[0] = (pi_[0] & ~0x70u) | level_interrupts_;
}

void Devices::rebuild_deadline() {
  const u64 vi_period = video_period();
  const auto remaining = [](u64 period, u64 elapsed) {
    return elapsed < period ? period - elapsed : 0;
  };
  next_deadline_ =
      std::min(remaining(audio_period_, audio_remainder_), remaining(vi_period, video_remainder_));
  if (ai_[0] & 1) {
    const u64 divisor = (console_ == Console::Wii ? 2250u : 2248u) * ((ai_[0] & 2) ? 2u : 3u) / 2;
    const u64 sample_cycles = clock_ * divisor / 108000000;
    next_deadline_ = std::min(next_deadline_, remaining(sample_cycles, ai_remainder_));
  }
  const auto countdown = [&](u64 value) {
    if (value)
      next_deadline_ = std::min(next_deadline_, value);
  };
  countdown(di_remaining_);
  countdown(aram_remaining_);
  for (u64 interrupt_remaining : audio_start_interrupts_)
    countdown(interrupt_remaining);
  next_deadline_ = std::min(next_deadline_, disc_stream_.next_event());
  for (const auto &card : cards_) {
    countdown(card.command_remaining);
    for (u64 transfer : card.transfer_remaining)
      next_deadline_ = std::min(next_deadline_, transfer);
  }
}

void Devices::advance_state(u64 amount) {
  if (di_remaining_ != 0) {
    if (amount >= di_remaining_) {
      di_remaining_ = 0;
      finish_disc_command();
    } else
      di_remaining_ -= amount;
  }
  if (aram_remaining_ != 0) {
    if (amount >= aram_remaining_) {
      aram_remaining_ = 0;
      dsp_[5] = u16((dsp_[5] & ~0x200) | 0x20);
    } else
      aram_remaining_ -= amount;
  }
  for (u64 &remaining : audio_start_interrupts_) {
    if (amount >= remaining) {
      remaining = 0;
      dsp_[5] |= 8;
    } else
      remaining -= amount;
  }
  std::erase(audio_start_interrupts_, u64(0));
  for (unsigned slot = 0; slot < cards_.size(); ++slot) {
    auto &card = cards_[slot];
    if (card.command_remaining != 0) {
      if (amount >= card.command_remaining) {
        card.command_remaining = 0;
        card.status = u8((card.status & ~0x80) | 1);
        card.interrupt = true;
      } else
        card.command_remaining -= amount;
    }
    for (u64 &remaining : card.transfer_remaining) {
      if (amount >= remaining) {
        exi_[slot][0] |= 8;
        remaining = 0;
      } else
        remaining -= amount;
    }
    std::erase(card.transfer_remaining, u64(0));
  }
  // A rate write does not replace an already queued tick. The callback chooses
  // the period for the next tick after its sample delivery has returned.
  u64 audio_elapsed = amount;
  while (audio_elapsed >= audio_period_ - audio_remainder_) {
    audio_elapsed -= audio_period_ - audio_remainder_;
    audio_remainder_ = 0;
    audio_dma_block();
    const u64 divisor = (console_ == Console::Wii ? 2250u : 2248u) * ((ai_[0] & 64) ? 3u : 2u) / 2;
    audio_period_ = clock_ * 8 * divisor / 108000000;
  }
  audio_remainder_ += audio_elapsed;
  disc_stream_.advance(amount, disc_audio_divisor(), (ai_[0] & 1) != 0);
  if (ai_[0] & 1) {
    const u64 divisor = (console_ == Console::Wii ? 2250u : 2248u) * ((ai_[0] & 2) ? 2u : 3u) / 2;
    const u64 sample_cycles = clock_ * divisor / 108000000;
    const u64 samples = cupid::hw::detail::elapsed_periods(amount, sample_cycles, ai_remainder_);
    if (samples != 0) {
      const u32 start = ai_[2] + 1;
      const u32 end = ai_[2] + u32(samples);
      if (samples > 0xffffffffull || u32(ai_[3] - start) <= u32(end - start))
        ai_[0] |= 8;
      ai_[2] = end;
    }
  }
  const u64 period = video_period();
  u64 halflines = cupid::hw::detail::elapsed_periods(amount, period, video_remainder_);
  while (halflines-- != 0)
    video_halfline();
  update_interrupts(interrupt_ai | interrupt_dsp | interrupt_exi);
}

void Devices::start_disc_command() {
  di_failed_ = false;
  di_data_.clear();
  const u8 command = u8(di_[2] >> 24);
  if (command != 0xe0)
    di_error_ = 0;
  auto fail = [this](u32 error) {
    di_error_ = error;
    di_failed_ = true;
  };
  if ((di_[7] & 4) != 0)
    fail(0x40800);
  else if (command == 0xe0) {
    disc_immediate(di_[2], di_[3], di_[4]);
  } else if (command == 0x12) {
    if (!dma_write)
      unavailable(di_base + 0x1c, true, "Disc DMA is not connected");
    di_data_.resize(12);
    put_word(std::span<u8>(di_data_).subspan(0, 4), 2);
    put_word(std::span<u8>(di_data_).subspan(4, 4), 0x20060526);
    put_word(std::span<u8>(di_data_).subspan(8, 4), 0x41000000);
  } else if (command == 0xa8) {
    const bool id = (di_[2] & 255) == 0x40;
    if (id)
      disc_id_read();
    else if (drive_state_ == DriveState::ReadyNoReads && (di_[2] & 255) == 0)
      drive_state_ = DriveState::Ready;
    if (!disc_read_ready()) {
      // Preserve the drive-specific error set by the precondition check.
    } else if ((di_[2] & 255) != 0 && !id)
      fail(0x52400);
    else if ((di_[7] & 2) == 0)
      fail(0x40800);
    else {
      if (!dma_write)
        unavailable(di_base + 0x1c, true, "Disc DMA is not connected");
      const u32 length = std::min(id ? 32u : di_[4], di_[6]);
      if (length > 64u * 1024 * 1024)
        unavailable(di_base + 0x1c, true, "Disc DMA exceeds supported transaction size");
      di_data_.resize(length);
      try {
        disc_read(id ? 0 : (u64(di_[3]) << 2), di_data_);
      } catch (const std::out_of_range &) {
        fail(0x52100);
        di_data_.clear();
      } catch (const std::exception &) {
        fail(0x31100);
        di_data_.clear();
      }
    }
  } else if (command == 0xe1 || command == 0xe2 || command == 0xe3 || command == 0xe4 ||
             command == 0xab)
    disc_immediate(di_[2], di_[3], di_[4]);
  else
    fail(0x52000);
  di_remaining_ = 300 * (clock_ / 1000000);
}

void Devices::finish_disc_command() {
  if (!di_failed_) {
    if (!di_data_.empty())
      dma_write(di_[5], di_data_);
    di_[5] += di_[6];
    di_[6] = 0;
  }
  di_data_.clear();
  di_[7] &= ~1u;
  di_[0] |= di_failed_ ? 4u : 16u;
  update_interrupts(interrupt_di);
}

void Devices::set_pad(unsigned port, const PadState &pad) {
  if (port >= pads_.size())
    throw std::out_of_range("Controller port must be 0 through 3");
  if (pad.connected != pads_[port].connected) {
    pad_origins_[port] = PadState{};
    pad_origin_held_[port] = false;
    pad_modes_[port] = 3;
  }
  pads_[port] = pad;
}

void Devices::capture_pad_origin(unsigned port) {
  const auto &pad = pads_[port];
  auto &origin = pad_origins_[port];
  origin.stick_x = pad.stick_x;
  origin.stick_y = pad.stick_y;
  origin.cstick_x = pad.cstick_x;
  origin.cstick_y = pad.cstick_y;
  origin.left_trigger = pad.left_trigger;
  origin.right_trigger = pad.right_trigger;
}

std::array<u32, 2> Devices::pad_data(unsigned port) {
  const auto &pad = pads_[port];
  u16 buttons = pad.buttons;
  if (buttons & 0x2000)
    capture_pad_origin(port);
  // Holding X, Y and Start for more than three seconds captures the six analog
  // origins. The request flag lasts for the response that captures them; the
  // game then reads the calibration packet before resuming normal pad decoding.
  const bool held = (buttons & 0xff00) == 0x1c00;
  if (held != pad_origin_held_[port]) {
    pad_origin_held_[port] = held;
    if (held)
      pad_origin_started_[port] = cycles_;
  }
  if (held && u32(cycles_ - pad_origin_started_[port]) > clock_ * 3) {
    capture_pad_origin(port);
    buttons |= 0x2000;
    pad_origin_held_[port] = false;
  }
  const u32 high = (u32(buttons | 0x80) << 16) | (u32(pad.stick_x) << 8) | pad.stick_y;
  u32 low;
  switch (pad_modes_[port]) {
  case 1:
    low = (u32(pad.cstick_x >> 4) << 28) | (u32(pad.cstick_y >> 4) << 24) |
          (u32(pad.left_trigger) << 16) | (u32(pad.right_trigger) << 8) |
          (u32(pad.analog_a >> 4) << 4) | (pad.analog_b >> 4);
    break;
  case 2:
    low = (u32(pad.cstick_x >> 4) << 28) | (u32(pad.cstick_y >> 4) << 24) |
          (u32(pad.left_trigger >> 4) << 20) | (u32(pad.right_trigger >> 4) << 16) |
          (u32(pad.analog_a) << 8) | pad.analog_b;
    break;
  case 3:
    low = (u32(pad.cstick_x) << 24) | (u32(pad.cstick_y) << 16) | (u32(pad.left_trigger) << 8) |
          pad.right_trigger;
    break;
  case 4:
    low = (u32(pad.cstick_x) << 24) | (u32(pad.cstick_y) << 16) | (u32(pad.analog_a) << 8) |
          pad.analog_b;
    break;
  default:
    low = (u32(pad.cstick_x) << 24) | (u32(pad.cstick_y) << 16) |
          (u32(pad.left_trigger >> 4) << 12) | (u32(pad.right_trigger >> 4) << 8) |
          (u32(pad.analog_a >> 4) << 4) | (pad.analog_b >> 4);
    break;
  }
  return {high, low};
}

void Devices::poll_controllers() {
  for (unsigned port = 0; port < 4; ++port) {
    if (pads_[port].connected) {
      const auto data = pad_data(port);
      si_channels_[port][1] = data[0] | (si_channels_[port][1] & 0x40000000u);
      si_channels_[port][2] = data[1];
      si_status_ |= 0x20000000u >> (port * 8);
    } else {
      si_channels_[port][1] |= 0xc0000000;
      si_status_ |= 0x08000000u >> (port * 8);
    }
  }
  update_interrupts(interrupt_si);
}

void Devices::controller_transfer() {
  const unsigned port = (si_csr_ >> 1) & 3;
  if (!pads_[port].connected) {
    si_csr_ |= 0x20000000;
    si_status_ |= 0x08000000u >> (port * 8);
  } else {
    si_csr_ &= ~0x20000000u;
    if (pads_[port].buttons & 0x2000)
      capture_pad_origin(port);
    switch (si_buffer_[0]) {
    case 0:
    case 0xff:
      si_buffer_[0] = 9;
      si_buffer_[1] = 0;
      si_buffer_[2] = 0;
      break;
    case 0x40: {
      const auto data = pad_data(port);
      put_word(std::span<u8>(si_buffer_).subspan(0, 4), data[0]);
      put_word(std::span<u8>(si_buffer_).subspan(4, 4), data[1]);
      break;
    }
    case 0x41:
    case 0x42: {
      const auto &origin = pad_origins_[port];
      std::fill_n(si_buffer_.begin(), 10, u8(0));
      si_buffer_[2] = origin.stick_x;
      si_buffer_[3] = origin.stick_y;
      si_buffer_[4] = origin.cstick_x;
      si_buffer_[5] = origin.cstick_y;
      si_buffer_[6] = origin.left_trigger;
      si_buffer_[7] = origin.right_trigger;
      break;
    }
    default:
      unavailable(si_base + 0x34, true, "Unsupported controller buffer command");
    }
  }
  si_csr_ = (si_csr_ & ~1u) | 0x80000000;
}

void Devices::set_ipl(std::span<const u8> rom) {
  if (rom.size() != 0x200000)
    throw std::invalid_argument("IPL image must contain exactly 2 MiB");
  ipl_rom_.assign(rom.begin(), rom.end());
  for (auto &font : ipl_fonts_)
    font.clear();
}

void Devices::set_ipl_fonts(std::span<const u8> shift_jis, std::span<const u8> western) {
  if (shift_jis.empty() || shift_jis.size() > 0x4d000 || western.empty() || western.size() > 0x3100)
    throw std::invalid_argument("IPL fonts must fit their nonempty ROM slots");
  ipl_fonts_[0].assign(shift_jis.begin(), shift_jis.end());
  ipl_fonts_[1].assign(western.begin(), western.end());
}

void Devices::read_aram(u32 address, std::span<u8> output) const {
  if (address > aram_.size() || output.size() > aram_.size() - address)
    throw std::out_of_range("ARAM read exceeds available storage");
  std::copy_n(aram_.begin() + address, output.size(), output.begin());
}

void Devices::set_sram(std::span<const u8> data) {
  if (data.size() != sram_.size())
    throw std::invalid_argument("SRAM image must contain 68 bytes");
  std::copy(data.begin(), data.end(), sram_.begin());
}

u8 Devices::ipl_byte(u8 input) {
  if (ipl_command_bytes_ < 4) {
    ipl_command_ = (ipl_command_ << 8) | input;
    if (++ipl_command_bytes_ == 4)
      put_word(std::span<u8>(sram_).first(4), rtc_epoch_ + u32(cycles_ / clock_));
    return 255;
  }
  const bool writing = (ipl_command_ & 0x80000000) != 0;
  const u32 address = (ipl_command_ >> 6) & 0x1ffffff;
  if (address < 0x200000) {
    if (writing)
      return input;
    const u32 byte_address = (address + ipl_cursor_) % 0x200000;
    if (ipl_rom_.empty()) {
      for (unsigned slot = 0; slot < ipl_fonts_.size(); ++slot) {
        const u32 start = slot == 0 ? 0x1aff00 : 0x1fcf00;
        if (byte_address >= start && byte_address - start < ipl_fonts_[slot].size()) {
          ++ipl_cursor_;
          return ipl_fonts_[slot][byte_address - start];
        }
      }
      std::array<char, 8> requested{};
      const auto converted =
          std::to_chars(requested.data(), requested.data() + requested.size(), byte_address, 16);
      throw BusFault(exi_base + 12, false,
                     "IPL ROM has not been supplied; requested byte address 0x" +
                         std::string(requested.data(), converted.ptr),
                     true);
    }
    ++ipl_cursor_;
    return ipl_rom_[byte_address];
  }
  if (address >= 0x800000 && address < 0x800044) {
    auto &byte = sram_[(address - 0x800000 + ipl_cursor_++) % sram_.size()];
    if (writing)
      byte = input;
    return byte;
  }
  if (address == 0x840020) {
    if (writing)
      rtc_flags_ = input;
    return rtc_flags_;
  }
  const bool uart = address >= 0x800400 && address < 0x800450;
  const bool euart = address >= 0xc00000 && address < 0xc00008;
  if (uart || euart) {
    const bool fifo = address == 0x800400 || address == 0xc00004;
    if (!fifo)
      return input;
    if (!writing)
      return 0;
    if (input != 0)
      uart_buffer_.push_back(char(input));
    if (input == '\r') {
      if (guest_text)
        guest_text(uart_buffer_);
      uart_buffer_.clear();
    }
    return input;
  }
  std::array<char, 8> hex{};
  const auto converted = std::to_chars(hex.data(), hex.data() + hex.size(), address, 16);
  throw BusFault(exi_base + 12, writing,
                 "Unsupported IPL peripheral address 0x" + std::string(hex.data(), converted.ptr),
                 true);
}

void Devices::exi_transfer(unsigned channel) {
  auto &registers = exi_[channel];
  const u32 select = (registers[0] >> 7) & 7;
  if (select != 1 && select != 2 && select != 4)
    return;
  const unsigned direction = (registers[3] >> 2) & 3;
  const bool rom_device = channel == 0 && select == 2;
  const bool card_device = channel < cards_.size() && select == 1 && !cards_[channel].image.empty();
  const bool dma = (registers[3] & 2) != 0;
  if (direction == 2 && !dma) {
    // This transfer mode has no byte operation for the ROM and memory card devices.
    registers[3] &= ~1u;
    if (!card_device)
      registers[0] |= 8;
    return;
  }
  if (direction > 1)
    unavailable(exi_base + channel * 20 + 12, true, "EXI transfer direction is unsupported");
  if (dma) {
    if (registers[2] > 64u * 1024 * 1024)
      unavailable(exi_base + channel * 20 + 12, true, "EXI DMA exceeds supported transaction size");
    std::vector<u8> data(registers[2]);
    const u32 ram_address = registers[1] & 0x3fffffff;
    if (direction == 1) {
      if (!dma_read)
        unavailable(exi_base + channel * 20 + 12, true, "EXI DMA is not connected");
      dma_read(ram_address, data);
    }
    if (card_device) {
      auto &card = cards_[channel];
      if (card.address > card.image.size() || data.size() > card.image.size() - card.address)
        invalid(exi_base + channel * 20 + 12, direction == 1, "Memory card DMA is out of bounds");
      auto selected = std::span<u8>(card.image).subspan(card.address, data.size());
      if (direction == 0)
        std::copy(selected.begin(), selected.end(), data.begin());
      else {
        std::copy(data.begin(), data.end(), selected.begin());
        card.dirty = true;
      }
      const u32 rate = direction == 0 ? 524288 : 98432;
      card.transfer_remaining.push_back(data.size() * (clock_ / rate));
    } else {
      for (auto &byte : data)
        if (rom_device)
          byte = ipl_byte(byte);
    }
    if (direction == 0) {
      if (!dma_write)
        unavailable(exi_base + channel * 20 + 12, false, "EXI DMA is not connected");
      dma_write(ram_address, data);
    }
  } else {
    const unsigned length = ((registers[3] >> 4) & 3) + 1;
    u32 result = 0;
    for (unsigned i = 0; i < length; ++i) {
      u8 byte = direction == 0 ? 0 : u8(registers[4] >> (24 - i * 8));
      if (rom_device)
        byte = ipl_byte(byte);
      else if (card_device)
        byte = memory_card_byte(channel, byte);
      result |= u32(byte) << (24 - i * 8);
    }
    if (direction == 0)
      registers[4] = result;
  }
  registers[3] &= ~1u;
  if (!card_device)
    registers[0] |= 8;
}

std::vector<u8> Devices::format_memory_card(u16 size_mbits, u64 format_time,
                                            const std::array<u8, 12> &flash_id, bool shift_jis,
                                            u32 language) {
  if (size_mbits < 4 || size_mbits > 256 || (size_mbits & (size_mbits - 1)) != 0)
    throw std::invalid_argument("Memory card size must be a power of two from 4 through 256 Mbit");
  std::vector<u8> image(std::size_t(size_mbits) * 131072, 255);
  auto header = std::span<u8>(image).first(8192);
  u64 random = format_time;
  for (unsigned i = 0; i < 12; ++i) {
    random = (random * 0x41c64e6d + 0x3039) >> 16;
    header[i] = u8(flash_id[i] + random);
    random = ((random * 0x41c64e6d + 0x3039) >> 16) & 0x7fff;
  }
  put_word(header.subspan(12, 4), u32(format_time >> 32));
  put_word(header.subspan(16, 4), u32(format_time));
  put_word(header.subspan(20, 4), 0);
  put_word(header.subspan(24, 4), language);
  put_word(header.subspan(28, 4), 0);
  put_word(header.subspan(32, 2), 0);
  put_word(header.subspan(34, 2), size_mbits);
  put_word(header.subspan(36, 2), shift_jis ? 1 : 0);
  card_checksums(header, 0, 0x1fc, 0x1fc);
  for (unsigned block = 1; block < 3; ++block) {
    auto directory = std::span<u8>(image).subspan(block * 8192, 8192);
    put_word(directory.subspan(0x1ffa, 2), 0);
    card_checksums(directory, 0, 0x1ffc, 0x1ffc);
  }
  for (unsigned block = 3; block < 5; ++block) {
    auto allocation = std::span<u8>(image).subspan(block * 8192, 8192);
    std::fill(allocation.begin(), allocation.end(), u8(0));
    put_word(allocation.subspan(6, 2), size_mbits * 16 - 5);
    put_word(allocation.subspan(8, 2), 4);
    card_checksums(allocation, 4, 8192, 0);
  }
  return image;
}

void Devices::set_memory_card(unsigned slot, std::span<const u8> image) {
  if (slot >= cards_.size())
    throw std::out_of_range("Memory card slot must be 0 or 1");
  const auto size = image.size();
  if (size != 0 && (size < 524288 || size > 33554432 || (size & (size - 1)) != 0))
    throw std::invalid_argument("Raw memory card image size is invalid");
  flush_elapsed();
  next_deadline_ = 0;
  cards_[slot] = MemoryCard{};
  cards_[slot].image.assign(image.begin(), image.end());
  exi_[slot][0] = (exi_[slot][0] & ~0x1000u) | 0x800u | (size ? 0x1000u : 0u);
  if (size != 0) {
    u64 random = (u64(buffer_word(image.subspan(12, 4))) << 32) | buffer_word(image.subspan(16, 4));
    u8 sum = 0;
    for (unsigned i = 0; i < 12; ++i) {
      random = (random * 0x41c64e6d + 0x3039) >> 16;
      const u8 flash_byte = u8(image[i] - u8(random));
      sram_[24 + slot * 12 + i] = flash_byte;
      sum = u8(sum + flash_byte);
      random = ((random * 0x41c64e6d + 0x3039) >> 16) & 0x7fff;
    }
    sram_[62 + slot] = sum ^ 255;
  }
  update_interrupts(interrupt_exi);
}

std::span<const u8> Devices::card_image(unsigned slot) const {
  if (slot >= cards_.size())
    throw std::out_of_range("Memory card slot must be 0 or 1");
  return cards_[slot].image;
}

bool Devices::memory_card_dirty(unsigned slot) const {
  if (slot >= cards_.size())
    throw std::out_of_range("Memory card slot must be 0 or 1");
  return cards_[slot].dirty;
}

void Devices::mark_memory_card_saved(unsigned slot) {
  if (slot >= cards_.size())
    throw std::out_of_range("Memory card slot must be 0 or 1");
  cards_[slot].dirty = false;
}

void Devices::memory_card_select(unsigned slot, bool selected) {
  auto &card = cards_[slot];
  if (card.image.empty())
    return;
  if (selected) {
    card.position = 0;
    return;
  }
  if (card.command == 0xf1 && card.position > 2) {
    const u32 address = card.address & u32(card.image.size() - 1);
    if ((address & 8191) != 0)
      invalid(exi_base + slot * 20, true, "Memory card erase address is not block aligned");
    std::fill_n(card.image.begin() + address, 8192, u8(255));
    card.dirty = true;
    card.status = u8((card.status | 0x80) & ~1);
    card.command_remaining = 5000;
  } else if (card.command == 0xf4 && card.position > 2) {
    std::fill(card.image.begin(), card.image.end(), u8(255));
    card.dirty = true;
    card.status &= ~0x80;
  } else if (card.command == 0xf2 && card.position >= 5) {
    const u32 count = card.position - 5;
    card.status &= ~0x80;
    for (u32 i = 0; i < count; ++i) {
      if (card.address >= card.image.size())
        invalid(exi_base + slot * 20, true, "Memory card program address is out of bounds");
      card.image[card.address] = card.programming[i & 127];
      card.address = (card.address & ~511u) | ((card.address + 1) & 511);
    }
    card.dirty = card.dirty || count != 0;
    card.command_remaining = 5000;
  }
}

u8 Devices::memory_card_byte(unsigned slot, u8 input) {
  auto &card = cards_[slot];
  u8 output = 255;
  if (card.position == 0) {
    card.command = input;
    if (input == 0x89) {
      card.status = u8((card.status & ~0x18) | 1);
      card.interrupt = false;
    } else if (input != 0 && input != 0x52 && input != 0x81 && input != 0x83 && input != 0x85 &&
               input != 0xf1 && input != 0xf2 && input != 0xf4) {
      unavailable(exi_base + slot * 20 + 12, true, "Unsupported memory card command");
    }
  } else {
    switch (card.command) {
    case 0:
      output = card.position == 1
                   ? 0x80
                   : u8((card.image.size() / 131072) >> (24 - (((card.position - 2) & 3) * 8)));
      break;
    case 0x83:
      output = card.status;
      break;
    case 0x85:
      output = card.position == 1 || (card.position & 1) == 0 ? 0xc2 : 0x21;
      break;
    case 0x81:
      if (card.position == 1)
        card.interrupt_switch = input;
      break;
    case 0xf4:
      break;
    case 0xf1:
      if (card.position == 1)
        card.address = u32(input) << 17;
      if (card.position == 2)
        card.address |= u32(input) << 9;
      break;
    case 0x52:
    case 0xf2:
      if (card.position == 1)
        card.address = u32(input) << 17;
      if (card.position == 2)
        card.address |= u32(input) << 9;
      if (card.position == 3)
        card.address |= u32(input & 3) << 7;
      if (card.position == 4)
        card.address |= input & 127;
      if (card.command == 0xf2 && card.position >= 5)
        card.programming[(card.position - 5) & 127] = input;
      if (card.command == 0x52 && card.position > 1) {
        output = card.image[card.address & (card.image.size() - 1)];
        if (card.position >= 9)
          card.address = (card.address & ~511u) | ((card.address + 1) & 511);
      }
      break;
    default:
      break;
    }
  }
  ++card.position;
  return output;
}

void Devices::start_aram_dma() {
  if (aram_remaining_ != 0)
    unavailable(dsp_base + 0x2a, true, "ARAM DMA replaced before completion");
  u32 main_address = ((u32(dsp_[0x20 / 2]) << 16) | dsp_[0x22 / 2]) & 0x3ffffff;
  u32 ar_address = ((u32(dsp_[0x24 / 2]) << 16) | dsp_[0x26 / 2]) & 0x3ffffff;
  const u32 control = (u32(dsp_[0x28 / 2]) << 16) | dsp_[0x2a / 2];
  const u32 count = control & 0x7fffffff;
  const bool disconnected_port = console_ == Console::GameCube && ar_address >= aram_.size();
  if (!dma_read || !dma_write)
    unavailable(dsp_base + 0x2a, true, "ARAM DMA is not connected");
  std::array<u8, 32> data{};
  for (u32 done = 0; done < count; done += 32) {
    if (disconnected_port) {
      // The unattached high-speed port discards writes and leaves MRAM unchanged
      // on reads. Validate the RAM range without manufacturing returned bytes.
      dma_read(main_address, data);
    } else if (control & 0x80000000) {
      if (console_ == Console::Wii)
        dma_read(0x10000000 | (ar_address & 0x3ffffff), data);
      else
        std::copy_n(aram_.begin() + (ar_address & 0xffffff), data.size(), data.begin());
      dma_write(main_address, data);
    } else {
      dma_read(main_address, data);
      if (console_ == Console::Wii) {
        dma_write(0x10000000 | (ar_address & 0x3ffffff), data);
        if ((dsp_[0x12 / 2] & 15) == 4 && ar_address < 0x400000)
          dma_write(0x10400000 | ar_address, data);
      } else {
        // Register masks keep every block aligned, so a block cannot straddle
        // the end of ARAM or the boundary of the duplicated four-MiB bank.
        const auto destination = aram_.begin() + (ar_address & 0xffffff);
        std::copy(data.begin(), data.end(), destination);
        if ((dsp_[0x12 / 2] & 15) == 4 && ar_address < 0x400000)
          std::copy(data.begin(), data.end(), destination + 0x400000);
      }
    }
    main_address += 32;
    ar_address += 32;
  }
  dsp_[0x20 / 2] = u16(main_address >> 16);
  dsp_[0x22 / 2] = u16(main_address);
  dsp_[0x24 / 2] = u16(ar_address >> 16);
  dsp_[0x26 / 2] = u16(ar_address);
  dsp_[0x28 / 2] &= 0x8000;
  dsp_[0x2a / 2] = 0;
  aram_remaining_ = (count / 32) * 246ull;
  dsp_[5] |= 0x200;
  if (count == 0)
    dsp_[5] = u16((dsp_[5] & ~0x200) | 0x20);
  update_interrupts(interrupt_dsp);
}

void Devices::audio_dma_block() {
  const bool enabled = (dsp_[0x36 / 2] & 0x8000) != 0;
  std::array<s16, 16> samples{};
  if (enabled) {
    std::array<u8, 32> bytes{};
    dma_read(audio_address_, bytes);
    for (unsigned i = 0; i < samples.size(); ++i)
      samples[i] = static_cast<s16>((u16(bytes[i * 2]) << 8) | bytes[i * 2 + 1]);
  }
  const u32 divisor = (console_ == Console::Wii ? 2250u : 2248u) * ((ai_[0] & 64) ? 3u : 2u) / 2;
  if (audio_samples)
    audio_samples(samples, 108000000u / divisor);
  // The DAC clock continues with silence while DMA is disabled, without reading RAM.
  if (!enabled)
    return;
  if (audio_blocks_ != 0) {
    --audio_blocks_;
    audio_address_ += 32;
  }
  if (audio_blocks_ == 0) {
    audio_address_ = (u32(dsp_[0x30 / 2]) << 16) | dsp_[0x32 / 2];
    audio_blocks_ = dsp_[0x36 / 2] & 0x7fff;
    dsp_[5] |= 8;
  }
}
} // namespace cupid::hw
