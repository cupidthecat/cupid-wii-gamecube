// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/dsp/instruction.hpp"
#include "operations.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace cupid::dsp {
namespace {
s64 signed40(u64 value) {
  value &= 0xffffffffffull;
  return s64(value ^ 0x8000000000ull) - s64(0x8000000000ull);
}
} // namespace

InstructionCore::InstructionCore() {
  std::fill_n(state_.registers.begin() + 8, 4, u16(0xffff));
  state_.registers[19] = 0xa00;
  instruction_ram_.fill(0x21);
}

void InstructionCore::install_roms(std::span<const u16> program,
                                   std::span<const u16> coefficients) {
  if (program.size() != instruction_rom_.size() || coefficients.size() != coefficients_.size())
    throw std::invalid_argument("DSP program and coefficient ROM sizes are invalid");
  std::copy(program.begin(), program.end(), instruction_rom_.begin());
  std::copy(coefficients.begin(), coefficients.end(), coefficients_.begin());
  code_changed();
}

void InstructionCore::reset() {
  state_.pc = 0x8000;
  std::fill_n(state_.registers.begin() + 8, 4, u16(0xffff));
}

void InstructionCore::code_changed() {
  loop_ends_.reset();
  idle_starts_.reset();
  for (unsigned base : {0u, 0x8000u})
    for (unsigned address = base; address < base + 4096;) {
      const u16 word = read_instruction(u16(address));
      if ((word & 0xffe0) == 0x60 || (word & 0xff00) == 0x1100)
        loop_ends_.set(read_instruction(u16(address + 1)));
      else if ((word & 0xffe0) == 0x40 || (word & 0xff00) == 0x1000)
        loop_ends_.set(u16(address + 1));
      address += detail::decode(word).words;
    }
  for (unsigned base : {0u, 0x8000u})
    for (unsigned address = base; address < base + 4096; ++address) {
      const auto at = [&](unsigned offset) { return read_instruction(u16(address + offset)); };
      const u16 first = at(0);
      bool idle = false;
      if ((first & 0xfefd) == 0x26fc) {
        const u16 accumulator_bit = first & 0x100;
        const bool incoming = (first & 2) != 0;
        idle = at(1) == (0x02c0 | accumulator_bit) && at(2) == 0x8000 &&
               at(3) == (incoming ? 0x029c : 0x029d);
        if (!incoming)
          idle |= at(1) == (0x02a0 | accumulator_bit) && at(2) == 0x8000 && at(3) == 0x029c;
      } else if (first == 0x00de) {
        idle = at(1) == 0xfffe && at(2) == 0x02c0 && at(3) == 0x8000 && at(4) == 0x029c;
      } else if (first == 0x00da) {
        idle = at(1) == 0x0352 && at(2) == 0x8600 && at(3) == 0x0295;
      }
      if (idle)
        idle_starts_.set(address);
    }
}

u16 InstructionCore::read_instruction(u16 address) {
  if ((address >> 12) == 0)
    return instruction_ram_[address & 4095];
  if ((address >> 12) == 8)
    return instruction_rom_[address & 4095];
  ++state_.unmapped_accesses;
  return 0;
}

u16 InstructionCore::fetch() {
  const u16 value = read_instruction(state_.pc);
  ++state_.pc;
  return value;
}

u16 InstructionCore::read_data(u16 address) {
  switch (address >> 12) {
  case 0:
    return data_ram_[address & 4095];
  case 1:
    return coefficients_[address & 2047];
  case 15:
    if (!hardware_read)
      throw std::runtime_error("DSP hardware read is not connected");
    return hardware_read(address);
  default:
    ++state_.unmapped_accesses;
    return 0;
  }
}

void InstructionCore::write_data(u16 address, u16 value) {
  switch (address >> 12) {
  case 0:
    data_ram_[address & 4095] = value;
    return;
  case 15:
    if (!hardware_write)
      throw std::runtime_error("DSP hardware write is not connected");
    hardware_write(address, value);
    return;
  default:
    ++state_.unmapped_accesses;
    return;
  }
}

void InstructionCore::push(unsigned stack, u16 value) {
  auto &position = state_.stack_positions[stack];
  position = u8((position + 1) & 31);
  state_.stacks[stack][position] = state_.registers[12 + stack];
  state_.registers[12 + stack] = value;
}

u16 InstructionCore::pop(unsigned stack) {
  auto &position = state_.stack_positions[stack];
  const u16 value = state_.registers[12 + stack];
  state_.registers[12 + stack] = state_.stacks[stack][position];
  position = u8((position - 1) & 31);
  return value;
}

u16 InstructionCore::read_register(unsigned index) {
  index &= 31;
  if (index >= 12 && index < 16)
    return pop(index - 12);
  if (index >= 30 && (state_.registers[19] & 0x4000)) {
    const s64 value = accumulator(index - 30);
    if (value > std::numeric_limits<s32>::max())
      return 0x7fff;
    if (value < std::numeric_limits<s32>::min())
      return 0x8000;
  }
  return state_.registers[index];
}

void InstructionCore::write_register(unsigned index, u16 value) {
  index &= 31;
  if (index >= 12 && index < 16) {
    push(index - 12, value);
    return;
  }
  if (index == 16 || index == 17)
    value = u16(s16(s8(value)));
  else if (index == 18 || index == 22)
    value &= 255;
  else if (index == 19)
    value &= u16(0xfeff);
  state_.registers[index] = value;
}

void InstructionCore::extend_middle(unsigned reg) {
  if (reg >= 30 && reg < 32 && (state_.registers[19] & 0x4000)) {
    const u16 value = state_.registers[reg];
    state_.registers[reg - 14] = (value & 0x8000) ? 0xffff : 0;
    state_.registers[reg - 2] = 0;
  }
}

s64 InstructionCore::accumulator(unsigned index) const {
  const auto &r = state_.registers;
  return signed40((u64(r[16 + index] & 255) << 32) | (u64(r[30 + index]) << 16) | r[28 + index]);
}

void InstructionCore::accumulator(unsigned index, s64 value) {
  auto &r = state_.registers;
  r[28 + index] = u16(value);
  r[30 + index] = u16(u64(value) >> 16);
  r[16 + index] = u16(s16(s8(u64(value) >> 32)));
}

s32 InstructionCore::auxiliary(unsigned index) const {
  return std::bit_cast<s32>((u32(state_.registers[26 + index]) << 16) |
                            state_.registers[24 + index]);
}

s64 InstructionCore::product() const {
  const auto &r = state_.registers;
  return s64(s8(r[22])) * 0x100000000ll + (s64(u32(r[21]) + r[23]) * 65536) + r[20];
}

s64 InstructionCore::rounded_product() const {
  const s64 value = product();
  return (value + ((value & 0x10000) ? 0x8000 : 0x7fff)) & ~s64(0xffff);
}

void InstructionCore::product(s64 value) {
  auto &r = state_.registers;
  r[20] = u16(value);
  r[21] = u16(u64(value) >> 16);
  r[22] = u16((u64(value) >> 32) & 255);
  r[23] = 0;
}

s64 InstructionCore::multiply(u16 left, u16 right, unsigned sign) const {
  const bool unsigned_mode = (state_.registers[19] & 0x8000) != 0;
  const s64 a = unsigned_mode && sign != 0 ? s64(left) : s64(s16(left));
  const s64 b = unsigned_mode && sign == 1 ? s64(right) : s64(s16(right));
  return a * b * ((state_.registers[19] & 0x2000) ? 1 : 2);
}

void InstructionCore::status40(s64 value, bool carry, bool overflow) {
  u16 flags = u16(carry ? 1 : 0);
  if (overflow)
    flags |= 0x82;
  if (value == 0)
    flags |= 4;
  if (value < 0)
    flags |= 8;
  if (value < std::numeric_limits<s32>::min() || value > std::numeric_limits<s32>::max())
    flags |= 16;
  const auto top = (u64(value) >> 30) & 3;
  if (top == 0 || top == 3)
    flags |= 32;
  state_.registers[19] = u16((state_.registers[19] & ~0x3fu) | flags);
}

void InstructionCore::status16(s16 value, bool outside32) {
  u16 flags = u16((value == 0 ? 4 : 0) | (value < 0 ? 8 : 0) | (outside32 ? 16 : 0));
  if ((u16(value) >> 14) == 0 || (u16(value) >> 14) == 3)
    flags |= 32;
  state_.registers[19] = u16((state_.registers[19] & ~0x3fu) | flags);
}

void InstructionCore::arithmetic_status(s64 left, s64 right, s64 value, bool subtract) {
  const s64 second = subtract ? -right : right;
  const bool carry = subtract ? u64(left) >= u64(value) : u64(left) > u64(value);
  const bool overflow =
      ((u64(left) ^ u64(value)) & (u64(second) ^ u64(value)) & (u64(1) << 63)) != 0;
  status40(value, carry, overflow);
}

void InstructionCore::logic_zero(bool value) {
  auto &status = state_.registers[19];
  status = u16((status & ~0x40u) | (value ? 0x40 : 0));
}

bool InstructionCore::condition(unsigned code) const {
  const u16 flags = state_.registers[19];
  const bool less = bool(flags & 2) != bool(flags & 8), zero = flags & 4;
  const bool special = !(flags & 0x30) || zero;
  const std::array<bool, 16> result{
      !less,    less,         !less && !zero,  less || zero,     !zero,
      zero,     !(flags & 1), bool(flags & 1), !(flags & 16),    bool(flags & 16),
      !special, special,      !(flags & 64),   bool(flags & 64), bool(flags & 2),
      true};
  return result[code & 15];
}

void InstructionCore::pending(unsigned reg, u16 value) {
  if (pending_count_ == pending_.size())
    throw std::logic_error("DSP parallel writeback exceeds its bounded register set");
  pending_[pending_count_++] = {reg, value};
}

void InstructionCore::extend_pending_middle(unsigned reg, u16 value) {
  if (reg >= 30 && (state_.registers[19] & 0x4000)) {
    pending(reg - 14, (value & 0x8000) ? 0xffff : 0);
    pending(reg, value);
    pending(reg - 2, 0);
  } else {
    pending(reg, value);
  }
}
} // namespace cupid::dsp
