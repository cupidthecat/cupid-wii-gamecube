// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/types.hpp"
#include <array>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cupid::hw {

struct PadState {
  bool connected = false;
  u16 buttons = 0;
  u8 stick_x = 128, stick_y = 128, cstick_x = 128, cstick_y = 128;
  u8 left_trigger = 0, right_trigger = 0, analog_a = 0, analog_b = 0;
};

struct Xfb {
  u32 address = 0;
  u32 width = 0, height = 0, stride_bytes = 0;
  unsigned field = 0;
  u64 cycles = 0;
  float aspect_ratio = 4.0f / 3.0f;
};

class Devices {
public:
  explicit Devices(Console console);
  bool handles(u32 address) const;
  u32 read(u32 address, unsigned width_bytes);
  void write(u32 address, u32 value, unsigned width_bytes);
  void advance(u64 cpu_cycles);
  bool external_interrupt_pending() const;
  void set_interrupt(u32 cause, bool active);
  void dsp_interrupt();
  void reset_disc(bool spinup);
  void set_pad(unsigned port, const PadState &pad);
  void poll_controllers();
  Xfb xfb(unsigned field = 0) const;
  float video_aspect_ratio() const;

  u64 field_count() const {
    return fields_;
  }

  u64 cycles() const {
    return cycles_;
  }

  u64 ticks_per_second() const {
    return clock_;
  }

  void set_ipl(std::span<const u8> decoded_rom);
  void set_ipl_fonts(std::span<const u8> shift_jis, std::span<const u8> western);
  void read_aram(u32 address, std::span<u8> output) const;

  void set_rtc_seconds(u32 seconds_since_2000) {
    rtc_epoch_ = seconds_since_2000;
  }

  u32 rtc_seconds() const {
    return rtc_epoch_ + u32(cycles_ / clock_);
  }

  std::span<const u8> sram() const {
    return sram_;
  }

  void set_sram(std::span<const u8> data);
  void set_memory_card(unsigned slot, std::span<const u8> image);
  std::span<const u8> card_image(unsigned slot) const;
  bool memory_card_dirty(unsigned slot) const;
  void mark_memory_card_saved(unsigned slot);
  static std::vector<u8> format_memory_card(u16 size_mbits, u64 format_time,
                                            const std::array<u8, 12> &flash_id = {},
                                            bool shift_jis = false, u32 language = 0);

  std::function<void(u32, std::span<const u8>)> dma_write;
  std::function<void(u32, std::span<u8>)> dma_read;
  std::function<void(u64, std::span<u8>)> disc_read;
  std::function<void(const Xfb &)> xfb_present;
  std::function<void(std::span<const s16>, u32 sample_rate)> audio_samples;
  std::function<u32(u32)> ipc_read;
  std::function<void(u32, u32)> ipc_write;
  std::function<u16(u32)> dsp_read;
  std::function<void(u32, u16)> dsp_write;
  std::function<void()> fifo_reset;
  std::function<u32(u32, unsigned)> graphics_read;
  std::function<void(u32, u32, unsigned)> graphics_write;
  std::function<void(unsigned, bool)> rumble;
  std::function<void(std::string_view)> guest_text;

private:
  u32 canonical(u32 address) const;
  void flush_elapsed();
  void rebuild_deadline();
  void advance_state(u64 amount);
  u64 next_deadline_ = 0, deferred_cycles_ = 0;
  u32 level_interrupts_ = 0;
  u32 read_word(u32 address);
  void write_word(u32 address, u32 value);
  u16 read_half(u32 address);
  void write_half(u32 address, u16 value);
  void update_interrupts(u32 devices = 0xffffffffu);
  void video_halfline();
  u32 video_halflines(unsigned field) const;
  u32 video_ticks_per_sample() const;
  u64 video_nominal_period() const;
  u64 video_period() const;
  void start_disc_command();
  void finish_disc_command();
  void controller_transfer();
  std::array<u32, 2> pad_data(unsigned port) const;
  void exi_transfer(unsigned channel);
  u8 ipl_byte(u8 input);
  void start_aram_dma();
  void audio_dma_block();
  u8 memory_card_byte(unsigned slot, u8 input);
  void memory_card_select(unsigned slot, bool selected);
  Console console_;
  u64 clock_, cycles_ = 0;
  std::array<u32, 13> pi_{};
  std::array<u16, 0x2e> mi_{};
  std::array<u16, 0x3b> vi_{};
  u64 video_remainder_ = 0, fields_ = 0;
  u32 halfline_ = 0, si_next_poll_ = 15;
  std::array<u32, 10> di_{};
  u64 di_remaining_ = 0;
  u32 di_error_ = 0;
  bool di_failed_ = false, motor_stopped_ = false;
  std::vector<u8> di_data_;
  std::array<PadState, 4> pads_{};
  std::array<u8, 4> pad_modes_{3, 3, 3, 3};
  std::array<std::array<u32, 3>, 4> si_channels_{};
  std::array<u8, 128> si_buffer_{};
  u32 si_poll_ = 492u << 16, si_csr_ = 0, si_status_ = 0, si_clock_ = 0;
  std::array<u32, 4> ai_{0x42, 0, 0, 0};
  u64 ai_remainder_ = 0;
  std::array<std::array<u32, 5>, 3> exi_{};
  std::vector<u8> ipl_rom_;
  std::array<std::vector<u8>, 2> ipl_fonts_;
  std::array<u8, 0x44> sram_{};
  u32 ipl_command_ = 0, ipl_command_bytes_ = 0, ipl_cursor_ = 0, rtc_epoch_ = 0;
  u8 rtc_flags_ = 0;
  std::string uart_buffer_;
  std::array<u16, 0x1e> dsp_{};
  std::vector<u8> aram_;
  u64 aram_remaining_ = 0;
  u64 audio_remainder_ = 0, audio_start_interrupt_ = 0;
  u32 audio_address_ = 0;
  u16 audio_blocks_ = 0;

  struct MemoryCard {
    std::vector<u8> image;
    std::array<u8, 128> programming{};
    u32 position = 0, address = 0;
    u8 command = 0, status = 0xc1, interrupt_switch = 0;
    bool interrupt = false, dirty = false;
    u64 command_remaining = 0;
    std::vector<u64> transfer_remaining;
  };

  std::array<MemoryCard, 2> cards_;
};
} // namespace cupid::hw
