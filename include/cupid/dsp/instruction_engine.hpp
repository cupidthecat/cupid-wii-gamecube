// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/memory.hpp"
#include "cupid/dsp/accelerator.hpp"
#include "cupid/dsp/instruction.hpp"
#include <array>
#include <functional>
#include <limits>

namespace cupid::dsp {
class InstructionEngine {
public:
  InstructionEngine(Memory &memory, Console console, std::span<const u8> program,
                    std::span<const u8> coefficients);
  u16 read16(u32 offset);
  void write16(u32 offset, u16 value);
  void advance(u64 cycles);

  u64 event_free_cycles() const noexcept {
    return std::min(until_update_ - 1, std::numeric_limits<u64>::max() - cycles_);
  }

  InstructionCore &core() noexcept {
    return core_;
  }

  const InstructionCore &core() const noexcept {
    return core_;
  }

  u32 program_hash() const noexcept {
    return program_hash_;
  }

  s64 slice_budget() const noexcept {
    return slice_;
  }

  std::function<void()> interrupt;
  std::function<void(u32, std::span<u8>)> aram_read;
  std::function<void(u32, std::span<const u8>)> aram_write;
  std::function<u64()> guest_timebase;

private:
  Memory &memory_;
  Console console_;
  InstructionCore core_;
  SampleAccelerator accelerator_;
  std::array<u16, 256> registers_{};
  std::array<s16, 16> coefficients_{};
  std::array<u32, 2> mailboxes_{};
  u64 cycles_ = 0, until_update_ = 12600, init_clear_time_ = 0;
  s64 slice_ = 12600;
  u32 program_hash_ = 0;

  u64 timebase() const;
  void mail_slice();
  void write_control(u16 value);
  u16 read_control();
  void write_mail(unsigned index, bool high, u16 value);
  u16 read_mail(unsigned index, bool high);
  u16 read_hardware(u16 address);
  void write_hardware(u16 address, u16 value);
  void transfer();
  void load_instructions(u32 address, unsigned word_offset, unsigned bytes);
  u8 sample_read(u32 address);
  void sample_write(u32 address, u8 value);
};
} // namespace cupid::dsp
