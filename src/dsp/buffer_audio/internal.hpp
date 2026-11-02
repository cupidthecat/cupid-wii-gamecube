// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/dsp/accelerator.hpp"
#include "cupid/dsp/buffer_audio.hpp"
#include <array>

namespace cupid::dsp {
struct BufferAudioProgram::Impl {
  Memory &memory;
  Console console;
  u32 program_hash;
  bool accelerated, new_flags, swap_channels;
  std::function<void(u32, bool)> send;
  std::function<bool()> pending;
  std::function<void()> interrupt;
  std::function<void(u32, std::span<u8>)> read_aram;
  std::array<u8, 64> parameter{};
  std::array<u16, 2048> output{};
  std::array<u16, 32> input{};
  SampleAccelerator accelerator;
  u32 parameter_address = 0;
  bool next_address = false, uploading = false;
  unsigned upload_index = 0;
  std::array<u32, 10> upload_words{};

  Impl(Memory &ram, Console mode, u32 hash, std::function<void(u32, bool)> mail,
       std::function<bool()> has_mail, std::function<void()> irq,
       std::function<void(u32, std::span<u8>)> aram);
  std::span<u8> backing(u32 address, std::size_t bytes);

  u16 word(unsigned offset) const {
    return u16((u16(parameter[offset]) << 8) | parameter[offset + 1]);
  }

  u32 integer(unsigned offset) const {
    return (u32(word(offset)) << 16) | word(offset + 2);
  }

  void word(unsigned offset, u16 value) {
    parameter[offset] = u8(value >> 8);
    parameter[offset + 1] = u8(value);
  }

  void integer(unsigned offset, u32 value) {
    word(offset, u16(value >> 16));
    word(offset + 2, u16(value));
  }

  void load_parameter();
  void store_parameter();
  void send_samples();
  void load_input(bool align);
  void change_buffer();
  std::array<s16, 2> legacy_sample(unsigned format) const;
  void mix_legacy();
  void mix_accelerated();
};
} // namespace cupid::dsp
