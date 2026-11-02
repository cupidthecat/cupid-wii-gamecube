// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/dsp/engine.hpp"
#include "cupid/dsp/instruction_engine.hpp"
#include <algorithm>
#include <stdexcept>

namespace cupid::dsp {
InstructionEngine::InstructionEngine(Memory &memory, Console console, std::span<const u8> program,
                                     std::span<const u8> coefficients)
    : memory_(memory), console_(console) {
  if (program.size() != 8192 || coefficients.size() != 4096)
    throw std::invalid_argument(
        "DSP ROM files must contain 8192 program and 4096 coefficient bytes");
  std::array<u16, 4096> decoded_program{};
  std::array<u16, 2048> decoded_coefficients{};
  for (unsigned i = 0; i < decoded_program.size(); ++i)
    decoded_program[i] = u16((u16(program[i * 2]) << 8) | program[i * 2 + 1]);
  for (unsigned i = 0; i < decoded_coefficients.size(); ++i)
    decoded_coefficients[i] = u16((u16(coefficients[i * 2]) << 8) | coefficients[i * 2 + 1]);
  core_.install_roms(decoded_program, decoded_coefficients);
  core_.reset();
  core_.hardware_read = [this](u16 address) { return read_hardware(address); };
  core_.hardware_write = [this](u16 address, u16 value) { write_hardware(address, value); };
  accelerator_.memory_read = [this](u32 address) { return sample_read(address); };
  accelerator_.memory_write = [this](u32 address, u8 value) { sample_write(address, value); };
  accelerator_.exception = [this](unsigned code) { core_.state().exceptions |= u8(1u << code); };
}

u64 InstructionEngine::timebase() const {
  return guest_timebase ? guest_timebase() : cycles_ / 12;
}

void InstructionEngine::advance(u64 amount) {
  if (amount > std::numeric_limits<u64>::max() - cycles_)
    throw std::overflow_error("DSP instruction clock overflow");
  cycles_ += amount;
  if (amount < until_update_) {
    until_update_ -= amount;
    return;
  }
  u64 late = amount - until_update_;
  if (late > u64(std::numeric_limits<s32>::max()) - 12600)
    throw std::invalid_argument("DSP instruction updates require bounded CPU timing slices");
  for (;;) {
    if (slice_ >= 6)
      core_.run(u32(slice_ / 6));
    // Preserve the source scheduler's late-callback correction and signed
    // remainder, rather than executing one DSP cycle per CPU memory access.
    slice_ %= 6;
    slice_ += 12600 - s64(late);
    if (late < 12600) {
      until_update_ = 12600 - late;
      return;
    }
    late -= 12600;
  }
}

void InstructionEngine::mail_slice() {
  if (slice_ > 72) {
    core_.run(12);
    slice_ -= 72;
  }
}

void InstructionEngine::write_mail(unsigned index, bool high, u16 value) {
  auto &mail = mailboxes_[index];
  if (high)
    mail = ((mail & 0xffff) | (u32(value) << 16)) & 0x7fffffff;
  else
    mail = (mail & 0xffff0000) | value | 0x80000000;
}

u16 InstructionEngine::read_mail(unsigned index, bool high) {
  auto &mail = mailboxes_[index];
  if (high)
    return u16(mail >> 16);
  const u16 value = u16(mail);
  mail &= 0x7fffffff;
  return value;
}

u16 InstructionEngine::read16(u32 offset) {
  if (offset < 8 && !(offset & 1)) {
    if (!(offset & 2))
      mail_slice();
    return read_mail(offset / 4, (offset & 2) == 0);
  }
  if (offset == 0x0a)
    return read_control();
  throw std::invalid_argument("Invalid DSP instruction mailbox/control read");
}

void InstructionEngine::write16(u32 offset, u16 value) {
  if (offset == 0 || offset == 2) {
    write_mail(0, offset == 0, value);
    return;
  }
  if (offset == 0x0a) {
    write_control(value);
    return;
  }
  throw std::invalid_argument("Invalid or read-only DSP instruction mailbox/control write");
}

void InstructionEngine::write_control(u16 value) {
  const u16 previous = core_.state().control;
  if (value & 1) {
    core_.reset();
    core_.code_changed();
    value &= u16(~1u);
  }
  if ((previous & 0x800) && !(value & 0x800)) {
    core_.state().pc = 0;
    load_instructions(0x81000000, 0, 0x1000);
    value = u16((value & ~0x800u) | 0x400);
    init_clear_time_ = timebase() + 130;
  }
  core_.state().control = value;
  if (value & 2) {
    core_.external_interrupt();
    core_.check_exceptions();
  }
}

u16 InstructionEngine::read_control() {
  auto &control = core_.state().control;
  if ((control & 0x400) && timebase() >= init_clear_time_)
    control &= u16(~0x400u);
  return control;
}

u16 InstructionEngine::read_hardware(u16 address) {
  const u8 reg = u8(address);
  if (reg >= 0xfc)
    return read_mail(reg >= 0xfe ? 0 : 1, (reg & 1) == 0);
  if (SampleAccelerator::readable(reg))
    return accelerator_.read(reg, coefficients_);
  return registers_[reg];
}

void InstructionEngine::write_hardware(u16 address, u16 value) {
  const u8 reg = u8(address);
  if (reg >= 0xfc) {
    write_mail(reg >= 0xfe ? 0 : 1, (reg & 1) == 0, value);
    return;
  }
  if (reg == 0xfb) {
    if ((value & 1) && interrupt)
      interrupt();
    return;
  }
  if (reg == 0xcb) {
    registers_[reg] = value;
    registers_[0xc9] |= 4;
    if (registers_[0xef] == 0)
      transfer();
    registers_[0xc9] &= u16(~4u);
    registers_[reg] = 0;
    return;
  }
  if (SampleAccelerator::writable(reg)) {
    accelerator_.write(reg, value);
    return;
  }
  registers_[reg] = value;
  if (reg >= 0xa0 && reg < 0xb0)
    coefficients_[reg - 0xa0] = s16(value);
}

void InstructionEngine::load_instructions(u32 address, unsigned word_offset, unsigned bytes) {
  auto destination = core_.instruction_ram();
  if (word_offset > destination.size() || bytes > (destination.size() - word_offset) * 2)
    throw std::runtime_error("DSP instruction DMA exceeds instruction RAM");
  if (bytes) {
    const auto source = memory_.range(address & 0x3fffffff, bytes);
    for (unsigned i = 0; i < bytes / 2; ++i)
      destination[word_offset + i] = u16((u16(source[i * 2]) << 8) | source[i * 2 + 1]);
    program_hash_ = firmware_hash(source);
  } else {
    program_hash_ = 0;
  }
  core_.code_changed();
}

void InstructionEngine::transfer() {
  const u32 address = (u32(registers_[0xce]) << 16) | registers_[0xcf];
  const unsigned bytes = registers_[0xcb];
  const unsigned word_offset = registers_[0xcd] & 0x7fff;
  const unsigned direction = registers_[0xc9] & 3;
  if (bytes > 0x4000)
    throw std::runtime_error("DSP DMA length exceeds the supported transfer limit");
  // The selected source has no instruction-memory output transfer. Preserve
  // that defined no-effect path instead of inventing an IRAM readback copy.
  if (direction == 3)
    return;
  if (direction == 2) {
    load_instructions(address, word_offset, bytes);
    return;
  }
  auto words = core_.data_ram();
  if (word_offset > words.size() || bytes > (words.size() - word_offset) * 2)
    throw std::runtime_error("DSP data DMA exceeds data RAM");
  if (bytes == 0)
    return;
  std::span<u8> backing;
  try {
    backing = memory_.range(address & 0x3fffffff, bytes);
  } catch (const BusFault &) {
    // Bounded host data copies retain their destination on rejected backing.
    return;
  }
  for (unsigned i = 0; i < bytes / 2; ++i) {
    if (direction == 0) {
      words[word_offset + i] = u16((u16(backing[i * 2]) << 8) | backing[i * 2 + 1]);
    } else {
      const u16 value = words[word_offset + i];
      backing[i * 2] = u8(value >> 8);
      backing[i * 2 + 1] = u8(value);
    }
  }
}

u8 InstructionEngine::sample_read(u32 address) {
  if (console_ == Console::Wii) {
    if (address & 0x10000000)
      return memory_.range(0x10000000 | (address & 0x3ffffff), 1)[0];
    const u32 physical = address & 0x1ffffff;
    return physical < Memory::mem1_size ? memory_.range(physical, 1)[0] : 0;
  }
  if (!aram_read)
    throw std::runtime_error("DSP instruction samples require connected ARAM reads");
  u8 value = 0;
  aram_read(address & 0xffffff, std::span<u8>(&value, 1));
  return value;
}

void InstructionEngine::sample_write(u32 address, u8 value) {
  if (console_ == Console::Wii) {
    // The pinned write caller selects MEM2 regardless of the read selector bit.
    memory_.range(0x10000000 | (address & 0x3ffffff), 1)[0] = value;
    return;
  }
  if (!aram_write)
    throw std::runtime_error("DSP instruction samples require connected ARAM writes");
  aram_write(address & 0xffffff, std::span<const u8>(&value, 1));
}
} // namespace cupid::dsp
