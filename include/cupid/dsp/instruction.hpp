// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/types.hpp"
#include <array>
#include <bitset>
#include <functional>
#include <span>
#include <utility>

namespace cupid::dsp {
namespace detail {
enum class Operation : u8;
}

struct InstructionState {
  std::array<u16, 32> registers{};
  std::array<std::array<u16, 32>, 4> stacks{};
  std::array<u8, 4> stack_positions{};
  u16 pc = 0, control = 0x804;
  u8 exceptions = 0;
  u64 instructions = 0, unmapped_accesses = 0;
};

class InstructionCore {
public:
  InstructionCore();

  InstructionState &state() noexcept {
    return state_;
  }

  const InstructionState &state() const noexcept {
    return state_;
  }

  std::span<u16> instruction_ram() noexcept {
    return instruction_ram_;
  }

  std::span<u16> data_ram() noexcept {
    return data_ram_;
  }

  void install_roms(std::span<const u16> program, std::span<const u16> coefficients);
  void code_changed();
  void reset();
  void step();
  u32 run(u32 instructions);
  void external_interrupt();
  bool check_exceptions();
  u16 read_register(unsigned index);
  void write_register(unsigned index, u16 value);
  u16 read_instruction(u16 address);
  u16 read_data(u16 address);
  void write_data(u16 address, u16 value);

  std::function<u16(u16)> hardware_read;
  std::function<void(u16, u16)> hardware_write;

private:
  InstructionState state_;
  std::array<u16, 4096> instruction_ram_{}, instruction_rom_{}, data_ram_{};
  std::array<u16, 2048> coefficients_{};
  std::bitset<65536> loop_ends_, idle_starts_;
  std::array<std::pair<unsigned, u16>, 6> pending_{};
  unsigned pending_count_ = 0;

  u16 fetch();
  void push(unsigned stack, u16 value);
  u16 pop(unsigned stack);
  bool condition(unsigned code) const;
  u16 address_update(unsigned reg, s16 amount, bool subtract = false) const;
  u16 address_increment(unsigned reg) const;
  u16 address_decrement(unsigned reg) const;
  void extend_middle(unsigned reg);
  s64 accumulator(unsigned index) const;
  void accumulator(unsigned index, s64 value);
  s32 auxiliary(unsigned index) const;
  s64 product() const;
  s64 rounded_product() const;
  void product(s64 value);
  s64 multiply(u16 left, u16 right, unsigned sign) const;
  void status40(s64 value, bool carry = false, bool overflow = false);
  void status16(s16 value, bool outside32);
  void arithmetic_status(s64 left, s64 right, s64 value, bool subtract);
  void logic_zero(bool value);
  void pending(unsigned reg, u16 value);
  void extend_pending_middle(unsigned reg, u16 value);
  void execute(detail::Operation operation, u16 instruction);
  void execute_arithmetic(detail::Operation operation, u16 instruction);
  void execute_extension(detail::Operation operation, u16 instruction);
};
} // namespace cupid::dsp
