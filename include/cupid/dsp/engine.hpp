// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/memory.hpp"
#include <array>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>

namespace cupid::dsp {

class AudioProgram;
class CommandAudioProgram;
class HandheldTask;
class InstructionEngine;
struct InstructionState;

class MailQueue {
public:
  std::function<void()> interrupt;
  std::function<void(u32)> delayed_interrupt;

  void push(u32 value, bool request_interrupt = false, u32 delay_cycles = 0);
  u16 read_high();
  u16 read_low();
  void halt(bool value);
  void clear();
  bool pending() const;

private:
  struct Entry {
    u32 value;
    bool interrupt_on_consumption;
  };

  std::deque<Entry> queue_;
  u32 last_ = 0;
  bool halted_ = false;
};

struct BootDescriptor {
  u32 source = 0;
  u16 length = 0;
  u16 instruction_address = 0;
  u16 data_length = 0;
  u16 entry = 0;
};

u32 firmware_hash(std::span<const u8> bytes);

class UnsupportedMicrocode final : public std::runtime_error {
public:
  UnsupportedMicrocode(u32 hash, const BootDescriptor &descriptor);
  const u32 hash;
  const BootDescriptor descriptor;
};

class Engine {
public:
  Engine(Memory &memory, Console console);
  ~Engine();
  Engine(const Engine &) = delete;
  Engine &operator=(const Engine &) = delete;
  Engine(Engine &&) = delete;
  Engine &operator=(Engine &&) = delete;

  std::function<void()> interrupt;
  std::function<void(u32, std::span<u8>)> aram_read;
  std::function<void(u32, std::span<const u8>)> aram_write;
  std::function<u64()> guest_timebase;

  void install_audio_coefficients(std::span<const u8> coefficients);
  void install_instruction_roms(std::span<const u8> program, std::span<const u8> coefficients);

  u16 read16(u32 offset);
  void write16(u32 offset, u16 value);

  void advance(u64 cpu_cycles) {
    if (instruction_engine_) {
      advance_instruction(cpu_cycles);
      return;
    }
    if (cpu_cycles > std::numeric_limits<u64>::max() - cycles_)
      throw std::overflow_error("DSP cycle counter overflow");
    cycles_ += cpu_cycles;
    if (!interrupt_deadlines_.empty())
      deliver_interrupts();
    if (cpu_cycles < update_remaining_) {
      update_remaining_ -= cpu_cycles;
      return;
    }
    advance_update(cpu_cycles);
  }

  // Excludes the update boundary and any cycle-counter overflow.
  u64 event_free_cycles() const noexcept {
    if (instruction_engine_)
      return instruction_event_free_cycles();
    u64 remaining = update_remaining_ ? update_remaining_ - 1 : 0;
    if (!interrupt_deadlines_.empty()) {
      const u64 deadline = interrupt_deadlines_.front();
      const u64 before_interrupt = deadline > cycles_ ? deadline - cycles_ - 1 : 0;
      if (before_interrupt < remaining)
        remaining = before_interrupt;
    }
    const u64 available = std::numeric_limits<u64>::max() - cycles_;
    return remaining < available ? remaining : available;
  }

  const InstructionState *instruction_state() const noexcept;
  u32 program_hash() const;
  const BootDescriptor &boot_descriptor() const;

private:
  enum class Program { Rom, Initialization, Card, Audio, CommandAudio, Handheld };
  Memory &memory_;
  Console console_;
  std::unique_ptr<InstructionEngine> instruction_engine_;
  std::unique_ptr<AudioProgram> audio_;
  std::unique_ptr<CommandAudioProgram> command_audio_;
  std::unique_ptr<CommandAudioProgram> suspended_command_audio_;
  std::unique_ptr<HandheldTask> handheld_;
  std::unique_ptr<HandheldTask> suspended_handheld_;
  std::unique_ptr<AudioProgram> suspended_audio_;
  std::array<s16, 2048> audio_coefficients_{};
  bool have_audio_coefficients_ = false;
  MailQueue mails_;
  Program program_ = Program::Rom;
  u32 hash_ = 0;
  u32 cpu_mail_ = 0;
  u16 control_ = 0x804;
  u32 parameter_ = 0;
  BootDescriptor boot_;
  u64 cycles_ = 0;
  std::deque<u64> interrupt_deadlines_;
  u64 init_clear_time_ = 0;
  u64 update_remaining_;
  u64 update_interval_ = 0;

  void advance_instruction(u64 cpu_cycles);
  u64 instruction_event_free_cycles() const noexcept;
  void select_program(u32 hash);
  void swap_program(u32 hash);
  void handle_mail(u32 value);
  void request_interrupt();
  void schedule_interrupt(u32 delay_cycles);
  void deliver_interrupts();
  void advance_update(u64 cpu_cycles);
};

} // namespace cupid::dsp
