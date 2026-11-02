// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/dsp/instruction.hpp"
#include "operations.hpp"

namespace cupid::dsp {
u16 InstructionCore::address_increment(unsigned reg) const {
  const u32 old = state_.registers[reg], wrap = state_.registers[8 + reg];
  u32 value = old + 1;
  if ((value ^ old) > (2 * (wrap | 1)))
    value -= wrap + 1;
  return u16(value);
}

u16 InstructionCore::address_decrement(unsigned reg) const {
  const u32 old = state_.registers[reg], wrap = state_.registers[8 + reg];
  u32 value = old + wrap;
  if (((value ^ old) & (2 * (wrap | 1))) > wrap)
    value -= wrap + 1;
  return u16(value);
}

u16 InstructionCore::address_update(unsigned reg, s16 amount, bool subtract) const {
  const u32 old = state_.registers[reg], wrap = state_.registers[8 + reg];
  const u32 offset = u32(s32(amount));
  u32 value = subtract ? old - offset : old + offset;
  const u32 changed = (value ^ old ^ (subtract ? ~offset : offset)) & (2 * (wrap | 1));
  const bool positive = subtract ? (amount < 0 && amount != -32768) : amount >= 0;
  if (positive) {
    if (changed > wrap)
      value -= wrap + 1;
  } else if ((((value + wrap + 1) ^ value) & changed) <= wrap) {
    value += wrap + 1;
  }
  return u16(value);
}

void InstructionCore::external_interrupt() {
  if (state_.registers[19] & 0x800) {
    state_.exceptions |= 0x80;
    state_.control &= u16(~2u);
  }
}

bool InstructionCore::check_exceptions() {
  auto &r = state_.registers;
  for (unsigned exception = 7; exception != 0; --exception)
    if ((state_.exceptions & (1u << exception)) && (exception == 7 || (r[19] & 0x200))) {
      push(0, state_.pc);
      push(1, r[19]);
      state_.pc = u16(exception * 2);
      state_.exceptions &= u8(~(1u << exception));
      r[19] &= u16(~(exception == 7 ? 0x800u : 0x200u));
      return true;
    }
  return false;
}

void InstructionCore::step() {
  check_exceptions();
  auto &r = state_.registers;
  ++state_.instructions;
  const u16 instruction = fetch();
  const auto &decoded = detail::decode(instruction);
  pending_count_ = 0;
  if (decoded.extension)
    execute_extension(detail::decode_extension(instruction), instruction);
  execute(decoded.operation, instruction);
  for (unsigned index = 0; index < pending_count_; ++index)
    write_register(pending_[index].first, pending_[index].second);
  pending_count_ = 0;
  // The analyzed loop end is the last fetched word, not the branch's input PC.
  // At PC zero, the original promoted subtraction does not match address ffff.
  if (state_.pc && loop_ends_.test(u16(state_.pc - 1)) && r[14] && r[15] &&
      state_.pc - 1 == r[14]) {
    if (--r[15]) {
      state_.pc = r[12];
    } else {
      pop(0);
      pop(2);
      pop(3);
    }
  }
}

u32 InstructionCore::run(u32 instructions) {
  u32 completed = 0;
  for (unsigned warmup = 0; warmup < 8 && completed < instructions; ++warmup) {
    if (state_.control & 4)
      return completed;
    step();
    ++completed;
  }
  while (completed < instructions) {
    for (unsigned probe = 0; probe < 8 && completed < instructions; ++probe) {
      if ((state_.control & 4) || idle_starts_.test(state_.pc))
        return completed;
      step();
      ++completed;
    }
    // The selected non-threaded caller checks halt/idle at the probe boundary,
    // then executes this bounded batch without another halt test.
    for (unsigned batch = 0; batch < 200 && completed < instructions; ++batch) {
      step();
      ++completed;
    }
  }
  return completed;
}

void InstructionCore::execute(detail::Operation operation, u16 word) {
  using O = detail::Operation;
  auto &r = state_.registers;
  const auto load_register = [&](unsigned reg, u16 value) {
    write_register(reg, value);
    extend_middle(reg);
  };
  switch (operation) {
  case O::nop:
  case O::nx:
    return;
  case O::halt:
    state_.control |= 4;
    --state_.pc;
    return;
  case O::dar:
    r[word & 3] = address_decrement(word & 3);
    return;
  case O::iar:
    r[word & 3] = address_increment(word & 3);
    return;
  case O::subarn:
    r[word & 3] = address_update(word & 3, s16(r[4 + (word & 3)]), true);
    return;
  case O::addarn:
    r[word & 3] = address_update(word & 3, s16(r[4 + ((word >> 2) & 3)]));
    return;
  case O::lri:
    load_register(word & 31, fetch());
    return;
  case O::lris:
    load_register(24 + ((word >> 8) & 7), u16(s16(s8(word))));
    return;
  case O::mrr:
    load_register((word >> 5) & 31, read_register(word & 31));
    return;
  case O::sbclr:
    r[19] &= u16(~(1u << (6 + (word & 7))));
    return;
  case O::sbset:
    r[19] |= u16(1u << (6 + (word & 7)));
    return;
  case O::srbith: {
    const unsigned mode = (word >> 8) & 7;
    if (mode >= 2) {
      const u16 bit = mode < 4 ? 0x2000 : mode < 6 ? 0x8000 : 0x4000;
      r[19] = u16((r[19] & ~bit) | ((mode & 1) ? bit : 0));
    }
    return;
  }
  case O::call:
  case O::jcc: {
    const u16 destination = fetch();
    if (condition(word)) {
      if (operation == O::call)
        push(0, state_.pc);
      state_.pc = destination;
    }
    return;
  }
  case O::callr:
  case O::jmprcc:
    if (condition(word)) {
      const u16 destination = read_register((word >> 5) & 7);
      if (operation == O::callr)
        push(0, state_.pc);
      state_.pc = destination;
    }
    return;
  case O::ret:
  case O::rti:
    if (condition(word)) {
      if (operation == O::rti)
        r[19] = pop(1);
      state_.pc = pop(0);
    }
    return;
  case O::ifcc:
    if (!condition(word))
      state_.pc = u16(state_.pc + detail::decode(read_instruction(state_.pc)).words);
    return;
  case O::loop:
  case O::loopi:
  case O::bloop:
  case O::bloopi: {
    const bool block = operation == O::bloop || operation == O::bloopi;
    const bool immediate = operation == O::loopi || operation == O::bloopi;
    const u16 count = immediate ? word & 255 : read_register(word & 31);
    const u16 end = block ? fetch() : state_.pc;
    if (count) {
      push(0, state_.pc);
      push(2, end);
      push(3, count);
    } else {
      state_.pc = end;
      state_.pc = u16(state_.pc + detail::decode(read_instruction(state_.pc)).words);
    }
    return;
  }
  case O::lr:
    load_register(word & 31, read_data(fetch()));
    return;
  case O::sr: {
    const u16 address = fetch();
    write_data(address, read_register(word & 31));
    return;
  }
  case O::si:
    write_data(u16(s16(s8(word))), fetch());
    return;
  case O::lrs:
    load_register(24 + ((word >> 8) & 7), read_data(u16((r[18] << 8) | (word & 255))));
    return;
  case O::srs:
  case O::srsh: {
    const unsigned source = operation == O::srs ? 28 + ((word >> 8) & 3) : 16 + ((word >> 8) & 1);
    write_data(u16((r[18] << 8) | (word & 255)), read_register(source));
    return;
  }
  case O::lrr:
  case O::lrrd:
  case O::lrri:
  case O::lrrn:
  case O::srr:
  case O::srrd:
  case O::srri:
  case O::srrn: {
    const unsigned address = (word >> 5) & 3, operand = word & 31;
    if ((word & 0x0200) == 0)
      load_register(operand, read_data(r[address]));
    else
      write_data(r[address], read_register(operand));
    const unsigned update = (word >> 7) & 3;
    if (update == 1)
      r[address] = address_decrement(address);
    else if (update == 2)
      r[address] = address_increment(address);
    else if (update == 3)
      r[address] = address_update(address, s16(r[4 + address]));
    return;
  }
  case O::ilrr:
  case O::ilrrd:
  case O::ilrri:
  case O::ilrrn: {
    const unsigned address = word & 3;
    load_register(30 + ((word >> 8) & 1), read_instruction(r[address]));
    const unsigned update = (word >> 2) & 3;
    if (update == 1)
      r[address] = address_decrement(address);
    else if (update == 2)
      r[address] = address_increment(address);
    else if (update == 3)
      r[address] = address_update(address, s16(r[4 + address]));
    return;
  }
  default:
    execute_arithmetic(operation, word);
  }
}
} // namespace cupid::dsp
