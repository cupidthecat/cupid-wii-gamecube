// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/interpreter.hpp"
#include "cupid/cpu/floating.hpp"
#include <algorithm>
#include <bit>
#include <iomanip>
#include <sstream>

namespace cupid::cpu {
namespace {
constexpr u32 so = 0x80000000, ov = 0x40000000, ca = 0x20000000;
constexpr u32 illegal = 0x80000, privilege = 0x40000, trap = 0x20000;

u32 signed_immediate(u32 opcode) {
  return static_cast<u32>(static_cast<s32>(static_cast<s16>(opcode)));
}

u32 swap32(u32 v) {
  return ((v & 0xff) << 24) | ((v & 0xff00) << 8) | ((v >> 8) & 0xff00) | (v >> 24);
}

u16 swap16(u16 v) {
  return static_cast<u16>((v << 8) | (v >> 8));
}

u32 rotation_mask(unsigned begin, unsigned end) {
  u32 result = 0;
  for (unsigned n = begin;; n = (n + 1) & 31) {
    result |= 0x80000000u >> n;
    if (n == end)
      return result;
  }
}

bool add_overflow(u32 a, u32 b, u32 result) {
  return ((a ^ result) & (b ^ result) & 0x80000000u) != 0;
}

bool trap_matches(u32 flags, u32 a, u32 b) {
  return ((flags & 16) && static_cast<s32>(a) < static_cast<s32>(b)) ||
         ((flags & 8) && static_cast<s32>(a) > static_cast<s32>(b)) || ((flags & 4) && a == b) ||
         ((flags & 2) && a < b) || ((flags & 1) && a > b);
}

u32 instruction_cycles(u32 opcode) {
  const unsigned primary = opcode >> 26;
  if (primary == 7)
    return 3;
  if (primary == 17)
    return 2;
  if (primary == 46 || primary == 47)
    return 11;
  if (primary == 31) {
    switch ((opcode >> 1) & 511) {
    case 459:
    case 491:
      return 40;
    case 11:
    case 75:
    case 235:
      return 5;
    default:
      break;
    }
  }
  if (primary == 19 && ((opcode >> 1) & 1023) == 50)
    return 2;
  if (primary == 31) {
    switch ((opcode >> 1) & 1023) {
    case 54:
    case 86:
    case 470:
    case 758:
    case 1014:
      return 5;
    case 4:
    case 246:
    case 278:
    case 467:
      return 2;
    case 595:
    case 659:
    case 598:
      return 3;
    case 982:
      return 4;
    default:
      break;
    }
  }
  if (primary == 59 && ((opcode >> 1) & 31) == 18)
    return 17;
  if (primary == 63 && ((opcode >> 1) & 31) == 18)
    return 31;
  if (primary == 4 && ((opcode >> 1) & 31) == 18)
    return 17;
  if (primary == 4 && ((opcode >> 1) & 31) == 26)
    return 2;
  if (primary == 63) {
    const unsigned xo = (opcode >> 1) & 1023;
    if (xo == 38 || xo == 70 || xo == 134 || xo == 711)
      return 3;
  }
  return 1;
}

struct InstructionEvents {
  bool memory = false, floating = false;
};

InstructionEvents instruction_events(u32 opcode) {
  const unsigned primary = opcode >> 26, xo = (opcode >> 1) & 1023;
  if ((primary >= 32 && primary <= 57) || primary == 60 || primary == 61)
    return {true, primary >= 48};
  if (primary == 59 || primary == 63) {
    if (primary == 63) {
      switch (xo) {
      case 0:
      case 12:
      case 14:
      case 15:
      case 32:
      case 38:
      case 40:
      case 64:
      case 70:
      case 72:
      case 134:
      case 136:
      case 264:
      case 583:
      case 711:
        return {false, true};
      default:
        break;
      }
    }
    switch (xo & 31) {
    case 18:
    case 20:
    case 21:
    case 25:
    case 28:
    case 29:
    case 30:
    case 31:
      return {false, true};
    case 23:
    case 26:
      return {false, primary == 63};
    case 24:
      return {false, primary == 59};
    default:
      return {};
    }
  }
  if (primary == 4) {
    if (xo == 1014)
      return {true, false};
    const unsigned quantized = xo & 63;
    if (quantized == 6 || quantized == 7 || quantized == 38 || quantized == 39)
      return {true, true};
    switch (xo) {
    case 0:
    case 32:
    case 40:
    case 64:
    case 72:
    case 96:
    case 136:
    case 264:
    case 528:
    case 560:
    case 592:
    case 624:
      return {false, true};
    default:
      break;
    }
    switch (xo & 31) {
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 18:
    case 20:
    case 21:
    case 23:
    case 24:
    case 25:
    case 26:
    case 28:
    case 29:
    case 30:
    case 31:
      return {false, true};
    default:
      return {};
    }
  }
  if (primary != 31)
    return {};
  switch (xo) {
  case 535:
  case 567:
  case 599:
  case 631:
  case 663:
  case 695:
  case 727:
  case 759:
  case 983:
    return {true, true};
  case 20:
  case 23:
  case 54:
  case 55:
  case 86:
  case 87:
  case 119:
  case 150:
  case 151:
  case 183:
  case 215:
  case 247:
  case 279:
  case 310:
  case 311:
  case 343:
  case 375:
  case 407:
  case 438:
  case 439:
  case 470:
  case 533:
  case 534:
  case 597:
  case 661:
  case 662:
  case 725:
  case 790:
  case 918:
  case 982:
  case 1014:
    return {true, false};
  default:
    return {};
  }
}

std::string diagnostic(u32 pc, u32 opcode, const char *reason) {
  std::ostringstream text;
  text << reason << ": opcode 0x" << std::hex << std::setw(8) << std::setfill('0') << opcode
       << " at PC 0x" << std::setw(8) << pc;
  return text.str();
}
} // namespace

double FloatingRegister::value(unsigned lane) const {
  return std::bit_cast<double>(bits.at(lane));
}

void FloatingRegister::set(double value, unsigned lane) {
  bits.at(lane) = std::bit_cast<u64>(value);
}

UnsupportedInstruction::UnsupportedInstruction(u32 pc, u32 opcode, const char *reason)
    : std::runtime_error(diagnostic(pc, opcode, reason)), pc(pc), opcode(opcode) {
}

Interpreter::Interpreter(Bus &bus, Console console) : bus_(bus), console_(console) {
  reset();
}

void Interpreter::reset() {
  state_ = {};
  state_.spr[spr::pvr] = console_ == Console::Wii ? 0x00087102 : 0x00083214;
  state_.spr[spr::hid1] = 0x80000000;
  state_.spr[spr::dec] = 0xffffffff;
  instruction_tlb_ = {};
  data_tlb_ = {};
  instruction_tlb_recent_ = {};
  data_tlb_recent_ = {};
  instruction_cache_.reset();
  external_interrupt_ = decrementer_pending_ = monitor_pending_ = reservation_ = false;
  tick_remainder_ = decrementer_remainder_ = 0;
}

void Interpreter::configure_boot() {
  state_.msr = msr::ri | msr::dr | msr::ir | msr::fp;
  state_.spr[spr::hid0] = console_ == Console::Wii ? 0x0011c664 : 0x0011c464;
  state_.spr[spr::hid2] = 0xe0000000;
  state_.spr[spr::hid4] = console_ == Console::Wii ? 0x83900000 : 0;
  state_.spr[528] = state_.spr[536] = 0x80001fff;
  state_.spr[529] = state_.spr[537] = 2;
  state_.spr[538] = 0xc0001fff;
  state_.spr[539] = 0x2a;
  if (console_ == Console::Wii) {
    state_.spr[560] = state_.spr[568] = 0x90001fff;
    state_.spr[561] = state_.spr[569] = 0x10000002;
    state_.spr[570] = 0xd0001fff;
    state_.spr[571] = 0x1000002a;
  }
  instruction_tlb_ = {};
  data_tlb_ = {};
}

void Interpreter::exception(u32 vector, u32 saved_pc, u32 cause) {
  state_.spr[spr::srr0] = saved_pc;
  state_.spr[spr::srr1] = (state_.msr & 0x87c0ffff) | cause;
  state_.msr = (state_.msr & ~msr::le) | ((state_.msr & msr::ile) ? msr::le : 0);
  state_.msr &= ~0x04ef36u;
  next_pc_ = vector;
}

[[noreturn]] void Interpreter::program(u32 cause) {
  exception(0x700, state_.pc, cause);
  throw GuestFault{};
}

[[noreturn]] void Interpreter::storage_fault(u32 effective, bool instruction, bool write) {
  if (instruction)
    exception(0x400, effective, 0x40000000);
  else {
    state_.spr[spr::dar] = effective;
    state_.spr[spr::dsisr] = 0x40000000 | (write ? 0x02000000 : 0);
    exception(0x300, state_.pc);
  }
  throw GuestFault{};
}

[[noreturn]] void Interpreter::alignment(u32 effective) {
  state_.spr[spr::dar] = effective;
  exception(0x600, state_.pc);
  throw GuestFault{};
}

[[noreturn]] void Interpreter::unsupported(const char *reason) {
  throw UnsupportedInstruction(state_.pc, current_opcode_, reason);
}

void Interpreter::privileged() {
  if (state_.msr & msr::pr)
    program(privilege);
}

void Interpreter::field(unsigned index, u32 value) {
  const unsigned shift = (7 - index) * 4;
  state_.cr = (state_.cr & ~(15u << shift)) | ((value & 15) << shift);
}

void Interpreter::record(u32 value) {
  compare(0, value, 0, true);
}

void Interpreter::compare(unsigned index, u32 a, u32 b, bool signed_values) {
  const bool less = signed_values ? static_cast<s32>(a) < static_cast<s32>(b) : a < b;
  field(index, (a == b ? 2u : less ? 8u : 4u) | ((state_.spr[spr::xer] & so) ? 1u : 0u));
}

void Interpreter::carry(bool value) {
  state_.spr[spr::xer] = (state_.spr[spr::xer] & ~ca) | (value ? ca : 0);
}

void Interpreter::overflow(bool value) {
  state_.spr[spr::xer] = (state_.spr[spr::xer] & ~ov) | (value ? ov | so : 0);
}

void Interpreter::invalidate_tlb(u32 effective) {
  const unsigned set = (effective >> 12) & 63;
  for (auto *table : {&instruction_tlb_, &data_tlb_})
    for (unsigned way = 0; way < 2; ++way)
      (*table)[set * 2 + way].valid = false;
}

u32 Interpreter::translate(u32 effective, bool instruction, bool write) {
  bool uncached = false;
  return translate_access(effective, instruction, write, uncached);
}

u32 Interpreter::translate_access(u32 effective, bool instruction, bool write, bool &uncached) {
  uncached = false;
  if (!(state_.msr & (instruction ? msr::ir : msr::dr)))
    return effective;
  // Higher numbered overlapping BATs win, including the extended bank when enabled.
  const unsigned count = console_ == Console::Wii && (state_.spr[spr::hid4] & 0x02000000) ? 8 : 4;
  for (unsigned n = count; n-- > 0;) {
    const unsigned base =
        n < 4 ? (instruction ? 528 : 536) + n * 2 : (instruction ? 560 : 568) + (n - 4) * 2;
    const u32 upper = state_.spr[base], lower = state_.spr[base + 1];
    if (!(upper & 3))
      continue;
    const u32 mask = ((upper & 0x1ffc) << 15) | 0x1ffff;
    const u32 matching_mask = mask & ~(upper & 0xfffe0000);
    if ((effective & ~matching_mask) == (upper & 0xfffe0000)) {
      uncached = (lower & 0x60) != 0;
      return (lower & 0xfffe0000) | (effective & mask);
    }
  }
  const u32 segment = state_.sr[effective >> 28], vsid = segment & 0xffffff;
  auto &tlb = instruction ? instruction_tlb_ : data_tlb_;
  auto &recent = instruction ? instruction_tlb_recent_ : data_tlb_recent_;
  const u32 page = effective & ~4095u;
  const unsigned set = (effective >> 12) & 63;
  bool updating_changed_bit = false;
  for (unsigned way = 0; way < 2; ++way) {
    auto &entry = tlb[set * 2 + way];
    if (entry.valid && entry.effective == page && entry.vsid == vsid) {
      if (write && !entry.changed) {
        entry.changed = true;
        updating_changed_bit = true;
        break;
      }
      recent[set] = static_cast<u8>(way);
      // Hashed-page transaction results keep the default normal-write flag.
      uncached = false;
      return entry.physical | (effective & 4095);
    }
  }
  if (segment & 0x80000000)
    unsupported("Direct-store segment access is not implemented");
  if (instruction && (segment & 0x10000000))
    storage_fault(effective, true, false);
  const u32 hash = vsid ^ ((effective >> 12) & 0xffff);
  const u32 mask = ((state_.spr[spr::sdr1] & 0x1ff) << 16) | 0xffff;
  const u32 base = state_.spr[spr::sdr1] & 0xffff0000;
  for (unsigned secondary = 0; secondary < 2; ++secondary) {
    const u32 group = base | (((secondary ? ~hash : hash) << 6) & mask);
    const u32 match = 0x80000000 | (vsid << 7) | (secondary << 6) | ((effective >> 22) & 63);
    for (unsigned entry_index = 0; entry_index < 8; ++entry_index) {
      const u32 address = group + entry_index * 8;
      if (bus_.read_cpu_data(address, 4) != match)
        continue;
      const u32 flags = static_cast<u32>(bus_.read_cpu_data(address + 4, 4));
      const u32 updated = flags | 0x100 | (write ? 0x80 : 0);
      if (flags != updated)
        (void)bus_.write_page_table32(address + 4, updated);
      if (!updating_changed_bit) {
        const unsigned way = recent[set] == 0 && tlb[set * 2].valid ? 1 : 0;
        recent[set] = static_cast<u8>(way);
        tlb[set * 2 + way] = {page,
                              vsid,
                              flags & 0xfffff000,
                              address + 4,
                              true,
                              (updated & 0x80) != 0,
                              (flags & 0x60) != 0};
      }
      uncached = false;
      return (flags & 0xfffff000) | (effective & 4095);
    }
  }
  storage_fault(effective, instruction, write);
}

u64 Interpreter::load(u32 effective, unsigned bytes) {
  if (state_.msr & msr::le)
    unsupported("Little-endian memory mode is not implemented");
  if ((effective & 4095) + bytes > 4096) {
    const u32 original_msr = state_.msr;
    bool failed = false;
    u32 exception_msr = 0;
    u64 value = 0;
    for (unsigned n = 0; n < bytes; ++n) {
      value <<= 8;
      try {
        value |= load(effective + n, 1);
      } catch (const GuestFault &) {
        failed = true;
        exception_msr = state_.msr;
        state_.msr = original_msr;
      } catch (...) {
        if (failed)
          state_.msr = exception_msr;
        throw;
      }
    }
    if (failed) {
      state_.msr = exception_msr;
      throw GuestFault{};
    }
    return value;
  }
  try {
    const u32 physical = translate(effective);
    return bus_.read_cpu_data(physical, bytes);
  } catch (const BusFault &fault) {
    if (fault.unsupported_reason)
      unsupported(fault.what());
    storage_fault(effective, false, false);
  }
}

void Interpreter::store(u32 effective, u64 value, unsigned bytes, bool reject_efb) {
  if (state_.msr & msr::le)
    unsupported("Little-endian memory mode is not implemented");
  const auto transfer_pair = [&](const auto &first, const auto &second) {
    const u32 original_msr = state_.msr;
    try {
      first();
    } catch (const GuestFault &) {
      // Split transfers finish before the instruction delivers a guest DSI.
      const u32 exception_msr = state_.msr;
      state_.msr = original_msr;
      try {
        second();
      } catch (const GuestFault &) {
        throw;
      } catch (...) {
        state_.msr = exception_msr;
        throw;
      }
      state_.msr = exception_msr;
      throw;
    }
    second();
  };
  if (bytes == 8) {
    transfer_pair([&] { store(effective, static_cast<u32>(value >> 32), 4, true); },
                  [&] { store(effective + 4, static_cast<u32>(value), 4, true); });
    return;
  }
  if (bytes <= 4 && (effective & 4095) + bytes > 4096) {
    const unsigned first = 4096 - (effective & 4095), second = bytes - first;
    transfer_pair(
        [&] {
          store(effective, std::rotr(static_cast<u32>(value), static_cast<int>(second * 8)), first,
                reject_efb);
        },
        [&] { store(effective + first, value, second, reject_efb); });
    return;
  }
  const auto first = [&] {
    try {
      bool uncached = false;
      const u32 physical = translate_access(effective, false, true, uncached);
      if (reject_efb && physical >= 0x08000000 && physical < 0x0c000000)
        unsupported("CPU EFB binary64 writes are not implemented");
      store_physical(physical, static_cast<u32>(value), bytes, uncached);
    } catch (const BusFault &fault) {
      if (fault.unsupported_reason)
        unsupported(fault.what());
      storage_fault(effective, false, true);
    }
  };
  first();
}

void Interpreter::store_physical(u32 physical, u32 value, unsigned bytes, bool uncached) {
  const bool device = (physical & 0xf8000000) == 0x08000000;
  const bool locked = physical >= 0xe0000000 && physical < 0xe0040000;
  if (uncached && !device && !locked && (bytes < 4 || (physical & 3))) {
    bus_.memory_access_error();
    const u32 word = std::rotr(value, static_cast<int>(((physical & 3) + bytes) * 8));
    const unsigned blocks = ((physical & 7) + bytes + 7) / 8;
    for (unsigned block = 0; block < blocks; ++block) {
      const u32 address = (physical & ~7u) + block * 8;
      bus_.write_cpu_data(address, word, 4);
      bus_.write_cpu_data(address + 4, word, 4);
    }
    return;
  }
  bus_.write_cpu_data(physical, value, bytes);
}

void Interpreter::clear_cache_line(u32 effective) {
  if (state_.msr & msr::le)
    unsupported("Little-endian memory mode is not implemented");
  effective &= ~31u;
  unsigned offset = 0;
  try {
    const u32 physical = translate(effective, false, true);
    for (; offset < 32; offset += 4)
      bus_.write_cpu_data(physical + offset, 0, 4);
  } catch (const BusFault &fault) {
    if (fault.unsupported_reason)
      unsupported(fault.what());
    storage_fault(effective + offset, false, true);
  }
}

void Interpreter::tick(u32 cycles) {
  state_.cycles += cycles;
  tick_remainder_ += cycles;
  const u32 ticks = tick_remainder_ / 12;
  tick_remainder_ %= 12;
  state_.timebase += ticks;
  const u32 old = state_.spr[spr::dec];
  if (!(old & 0x80000000)) {
    decrementer_remainder_ += cycles;
    const u32 decrement = decrementer_remainder_ / 12;
    decrementer_remainder_ %= 12;
    if (decrement >= old) {
      state_.spr[spr::dec] = 0xffffffff;
      decrementer_remainder_ = 0;
      decrementer_pending_ = true;
    } else {
      state_.spr[spr::dec] -= decrement;
    }
  }
}

u32 Interpreter::step() {
  next_pc_ = state_.pc + 4;
  u32 cycles = 1;
  if (take_interrupt(state_.pc)) {
    state_.pc = next_pc_;
    tick(1);
    return 1;
  }
  bool fetched = false;
  try {
    if (state_.pc & 3)
      alignment(state_.pc);
    if (state_.msr & msr::le)
      unsupported("Little-endian instruction mode is not implemented");
    try {
      current_opcode_ = instruction_cache_.read(bus_, translate(state_.pc, true),
                                                (state_.spr[spr::hid0] & 0x8000) != 0,
                                                (state_.spr[spr::hid0] & 0x2000) != 0);
    } catch (const BusFault &fault) {
      if (fault.unsupported_reason)
        unsupported(fault.what());
      storage_fault(state_.pc, true, false);
    }
    state_.last_pc = state_.pc;
    state_.last_opcode = current_opcode_;
    fetched = true;
    cycles = current_opcode_ == 0 ? 0 : instruction_cycles(current_opcode_);
    if (current_opcode_ != 0) {
      execute(current_opcode_);
      ++state_.instructions;
    }
  } catch (const GuestFault &) {
  }
  if (fetched)
    monitor(cycles, current_opcode_);
  state_.pc = next_pc_;
  tick(cycles);
  return cycles;
}

bool Interpreter::take_interrupt(u32 saved_pc) {
  if (!(state_.msr & msr::ee) || !(external_interrupt_ || monitor_pending_ || decrementer_pending_))
    return false;
  exception(external_interrupt_ ? 0x500 : monitor_pending_ ? 0xf00 : 0x900, saved_pc);
  if (!external_interrupt_ && monitor_pending_)
    monitor_pending_ = false;
  else if (!external_interrupt_)
    decrementer_pending_ = false;
  return true;
}

void Interpreter::monitor(u32 cycles, u32 opcode) {
  const u32 first = state_.spr[spr::mmcr0], second = state_.spr[spr::mmcr1];
  if (!(first || second))
    return;
  const auto events = instruction_events(opcode);
  const std::array<unsigned, 4> selectors{(first >> 6) & 127, first & 63, (second >> 27) & 31,
                                          (second >> 22) & 31};
  constexpr std::array<unsigned, 4> counters{spr::pmc1, spr::pmc2, spr::pmc3, spr::pmc4};
  for (unsigned index = 0; index < counters.size(); ++index) {
    const unsigned selection = selectors[index];
    const u32 increment = selection == 1 ? cycles
                          : selection == 11
                              ? static_cast<u32>(index == 1 ? events.memory : events.floating)
                              : 0;
    state_.spr[counters[index]] += increment;
    const bool interrupt = (first & (index == 0 ? 1u << 15 : 1u << 14)) != 0;
    if (interrupt && (state_.spr[counters[index]] & 0x80000000))
      monitor_pending_ = true;
  }
}

void Interpreter::execute(u32 opcode) {
  const unsigned op = opcode >> 26, d = (opcode >> 21) & 31, a = (opcode >> 16) & 31,
                 b = (opcode >> 11) & 31;
  const u32 immediate = signed_immediate(opcode), base = a ? state_.gpr[a] : 0;
  const bool rc = opcode & 1;
  switch (op) {
  case 3:
    if (trap_matches(d, state_.gpr[a], immediate))
      program(trap);
    return;
  case 4:
    execute_paired(opcode);
    return;
  case 7:
    state_.gpr[d] = static_cast<u32>(static_cast<s64>(static_cast<s32>(state_.gpr[a])) *
                                     static_cast<s32>(immediate));
    return;
  case 8:
    state_.gpr[d] = immediate - state_.gpr[a];
    carry(immediate >= state_.gpr[a]);
    return;
  case 10:
  case 11:
    compare((opcode >> 23) & 7, state_.gpr[a], op == 10 ? opcode & 0xffff : immediate, op == 11);
    return;
  case 12:
  case 13: {
    const u64 sum = static_cast<u64>(state_.gpr[a]) + immediate;
    state_.gpr[d] = static_cast<u32>(sum);
    carry(sum >> 32);
    if (op == 13)
      record(state_.gpr[d]);
    return;
  }
  case 14:
    state_.gpr[d] = base + immediate;
    return;
  case 15:
    state_.gpr[d] = base + (immediate << 16);
    return;
  case 16: {
    if (!(d & 4))
      --state_.spr[spr::ctr];
    const bool counter = (d & 4) || ((state_.spr[spr::ctr] != 0) != ((d & 2) != 0));
    const bool condition = (d & 16) || (((state_.cr >> (31 - a)) & 1) == ((d >> 3) & 1));
    if (counter && condition) {
      const u32 offset = static_cast<u32>(static_cast<s32>(static_cast<s16>(opcode & 0xfffc)));
      next_pc_ = ((opcode & 2) ? 0 : state_.pc) + offset;
      if (rc)
        state_.spr[spr::lr] = state_.pc + 4;
    }
    return;
  }
  case 17:
    exception(0xc00, next_pc_);
    return;
  case 18: {
    const u32 offset = static_cast<u32>(static_cast<s32>((opcode & 0x03fffffc) << 6) >> 6);
    next_pc_ = ((opcode & 2) ? 0 : state_.pc) + offset;
    if (rc)
      state_.spr[spr::lr] = state_.pc + 4;
    return;
  }
  case 19: {
    const unsigned xo = (opcode >> 1) & 1023;
    if (xo == 16 || xo == 528) {
      if (xo == 528 && !(d & 4))
        program(illegal);
      const u32 target = state_.spr[xo == 16 ? spr::lr : spr::ctr] & ~3u;
      if (!(d & 4))
        --state_.spr[spr::ctr];
      if (((d & 4) || ((state_.spr[spr::ctr] != 0) != ((d & 2) != 0))) &&
          ((d & 16) || (((state_.cr >> (31 - a)) & 1) == ((d >> 3) & 1)))) {
        next_pc_ = target;
        if (rc)
          state_.spr[spr::lr] = state_.pc + 4;
      }
      return;
    }
    if (xo == 50) {
      privileged();
      state_.msr =
          ((state_.msr & ~0x87c0ffffu) | (state_.spr[spr::srr1] & 0x87c0ffff)) & 0xfffbffff;
      next_pc_ = state_.spr[spr::srr0];
      return;
    }
    if (xo == 150)
      return;
    if (xo == 0) {
      field((opcode >> 23) & 7, (state_.cr >> (28 - ((opcode >> 18) & 7) * 4)) & 15);
      return;
    }
    const bool av = (state_.cr >> (31 - a)) & 1, bv = (state_.cr >> (31 - b)) & 1;
    bool value;
    switch (xo) {
    case 33:
      value = !(av || bv);
      break;
    case 129:
      value = av && !bv;
      break;
    case 193:
      value = av != bv;
      break;
    case 225:
      value = !(av && bv);
      break;
    case 257:
      value = av && bv;
      break;
    case 289:
      value = av == bv;
      break;
    case 417:
      value = av || !bv;
      break;
    case 449:
      value = av || bv;
      break;
    default:
      program(illegal);
    }
    const u32 mask = 0x80000000u >> d;
    state_.cr = (state_.cr & ~mask) | (value ? mask : 0);
    return;
  }
  case 20:
  case 21:
  case 23: {
    const u32 mask = rotation_mask((opcode >> 6) & 31, (opcode >> 1) & 31);
    const u32 rotated =
        std::rotl(state_.gpr[d], static_cast<int>(op == 23 ? state_.gpr[b] & 31 : b));
    state_.gpr[a] = (rotated & mask) | (op == 20 ? state_.gpr[a] & ~mask : 0);
    if (rc)
      record(state_.gpr[a]);
    return;
  }
  case 24:
  case 25:
    state_.gpr[a] = state_.gpr[d] | ((opcode & 0xffff) << (op == 25 ? 16 : 0));
    return;
  case 26:
  case 27:
    state_.gpr[a] = state_.gpr[d] ^ ((opcode & 0xffff) << (op == 27 ? 16 : 0));
    return;
  case 28:
  case 29:
    state_.gpr[a] = state_.gpr[d] & ((opcode & 0xffff) << (op == 29 ? 16 : 0));
    record(state_.gpr[a]);
    return;
  case 31:
    execute_extended(opcode);
    return;
  case 32:
  case 33:
  case 34:
  case 35:
  case 40:
  case 41:
  case 42:
  case 43: {
    const u32 address = ((op & 1) ? state_.gpr[a] : base) + immediate;
    const unsigned width = op < 34 ? 4 : op < 40 ? 1 : 2;
    const u32 loaded = static_cast<u32>(load(address, width));
    state_.gpr[d] =
        op >= 42 ? static_cast<u32>(static_cast<s32>(static_cast<s16>(loaded))) : loaded;
    if (op & 1)
      state_.gpr[a] = address;
    return;
  }
  case 36:
  case 37:
  case 38:
  case 39:
  case 44:
  case 45: {
    const u32 address = ((op & 1) ? state_.gpr[a] : base) + immediate;
    store(address, state_.gpr[d], op < 38 ? 4 : op < 44 ? 1 : 2);
    if (op & 1)
      state_.gpr[a] = address;
    return;
  }
  case 46:
  case 47: {
    u32 address = base + immediate;
    if ((address & 3) || (state_.msr & msr::le))
      alignment(address);
    for (unsigned reg = d; reg < 32; ++reg, address += 4)
      if (op == 46)
        state_.gpr[reg] = static_cast<u32>(load(address, 4));
      else
        store(address, state_.gpr[reg], 4);
    return;
  }
  case 48:
  case 49:
  case 50:
  case 51:
  case 52:
  case 53:
  case 54:
  case 55: {
    if (!(state_.msr & msr::fp)) {
      exception(0x800, state_.pc);
      throw GuestFault{};
    }
    const u32 address = ((op & 1) ? state_.gpr[a] : base) + immediate;
    if (address & 3)
      alignment(address);
    if (op < 50) {
      const u64 value = binary32_to_binary64(static_cast<u32>(load(address, 4)));
      state_.fpr[d].bits = {value, value};
    } else if (op < 52)
      state_.fpr[d].bits[0] = load(address, 8);
    else if (op < 54)
      store(address, binary64_to_binary32_store(state_.fpr[d].bits[0]), 4);
    else
      store(address, state_.fpr[d].bits[0], 8);
    if (op & 1)
      state_.gpr[a] = address;
    return;
  }
  case 56:
  case 57:
  case 60:
  case 61:
    quantized(opcode, false);
    return;
  case 59:
  case 63:
    execute_float(opcode);
    return;
  default:
    program(illegal);
  }
}

u32 Interpreter::read_spr(unsigned index) {
  if ((state_.msr & msr::pr) && index != spr::xer && index != spr::lr && index != spr::ctr &&
      index != spr::tbl && index != spr::tbu)
    program(privilege);
  if (index == spr::tbl)
    return static_cast<u32>(state_.timebase);
  if (index == spr::tbu)
    return static_cast<u32>(state_.timebase >> 32);
  if (index == 1010)
    return state_.spr[index] & ~1u;
  if (index == 937 || index == 938 || index == 941 || index == 942) {
    const unsigned counter = index + 16;
    state_.spr[index] = state_.spr[counter];
  }
  return state_.spr[index];
}

void Interpreter::write_spr(unsigned index, u32 value) {
  if ((state_.msr & msr::pr) && index != spr::xer && index != spr::lr && index != spr::ctr)
    program(privilege);
  if (index == spr::pvr)
    return;
  if (index == spr::tbl || index == spr::tbu)
    program(illegal);
  if (index == spr::mmcr0) {
    const unsigned first = (value >> 6) & 127, second = value & 63;
    if ((first != 0 && first != 1) || (second != 0 && second != 1 && second != 11))
      unsupported("Performance monitor event selector is not implemented");
  }
  if (index == spr::mmcr1) {
    const unsigned third = (value >> 27) & 31, fourth = (value >> 22) & 31;
    if ((third != 0 && third != 1 && third != 11) || (fourth != 0 && fourth != 1))
      unsupported("Performance monitor event selector is not implemented");
  }
  const u32 old = state_.spr[index];
  if (index == 284 || index == 285) {
    if (index == 284)
      state_.timebase = (state_.timebase & 0xffffffff00000000ull) | value;
    else
      state_.timebase = (state_.timebase & 0xffffffffull) | (static_cast<u64>(value) << 32);
    tick_remainder_ = 0;
  }
  if (index == spr::hid1)
    value &= 0xf8000000;
  if (index == spr::xer)
    value &= 0xe000ff7f;
  if (index == spr::hid2)
    value = (value & 0xf0ff0000) | (old & 0x0f000000);
  if (index == spr::hid0 && (value & 0x800)) {
    instruction_cache_.reset();
    value &= ~0x800u;
  }
  if (index == spr::dec) {
    decrementer_remainder_ = 0;
    if (!(old & 0x80000000) && (value & 0x80000000))
      decrementer_pending_ = true;
  }
  if (index == 923) {
    if (value & 2) {
      const u32 upper = state_.spr[922];
      const u32 blocks = ((upper & 31) << 2) | ((value >> 2) & 3);
      try {
        bus_.locked_cache_dma(upper & ~31u, value & ~31u, (blocks ? blocks : 128) * 32,
                              (value & 16) != 0);
      } catch (const BusFault &fault) {
        unsupported(fault.what());
      }
    }
    value &= ~2u;
  }
  state_.spr[index] = value;
}

void Interpreter::execute_extended(u32 opcode) {
  const unsigned d = (opcode >> 21) & 31, a = (opcode >> 16) & 31, b = (opcode >> 11) & 31;
  const unsigned xo = (opcode >> 1) & 1023, arithmetic = xo & 511;
  const u32 av = state_.gpr[a], bv = state_.gpr[b], sv = state_.gpr[d];
  const u32 ea = (a ? av : 0) + bv;
  const bool rc = opcode & 1, oe = opcode & 0x400;
  auto logical = [&](u32 result) {
    state_.gpr[a] = result;
    if (rc)
      record(result);
  };
  switch (xo) {
  case 0:
  case 32:
    compare((opcode >> 23) & 7, av, bv, xo == 0);
    return;
  case 4:
    if (trap_matches(d, av, bv))
      program(trap);
    return;
  case 19:
    state_.gpr[d] = state_.cr;
    return;
  case 24:
    logical((bv & 32) ? 0 : sv << (bv & 31));
    return;
  case 26:
    logical(static_cast<u32>(std::countl_zero(sv)));
    return;
  case 28:
    logical(sv & bv);
    return;
  case 60:
    logical(sv & ~bv);
    return;
  case 124:
    logical(~(sv | bv));
    return;
  case 284:
    logical(~(sv ^ bv));
    return;
  case 316:
    logical(sv ^ bv);
    return;
  case 412:
    logical(sv | ~bv);
    return;
  case 444:
    logical(sv | bv);
    return;
  case 476:
    logical(~(sv & bv));
    return;
  case 536:
    logical((bv & 32) ? 0 : sv >> (bv & 31));
    return;
  case 792:
  case 824: {
    const unsigned amount = xo == 824 ? b : bv & 63;
    const bool negative = (sv & 0x80000000) != 0;
    const u32 result = amount >= 32 ? (negative ? 0xffffffff : 0)
                                    : static_cast<u32>(static_cast<s32>(sv) >> amount);
    carry(negative && (amount >= 32 || (amount && (sv & ((1u << amount) - 1)))));
    logical(result);
    return;
  }
  case 922:
    logical(static_cast<u32>(static_cast<s32>(static_cast<s16>(sv))));
    return;
  case 954:
    logical(static_cast<u32>(static_cast<s32>(static_cast<s8>(sv))));
    return;
  case 83:
    privileged();
    state_.gpr[d] = state_.msr;
    return;
  case 146:
    privileged();
    state_.msr = sv;
    if ((state_.fpscr & 0x40000000) && (state_.msr & 0x900))
      program(0x100000);
    take_interrupt(next_pc_);
    return;
  case 144: {
    const unsigned fields = (opcode >> 12) & 255;
    for (unsigned n = 0; n < 8; ++n)
      if (fields & (0x80u >> n))
        field(n, sv >> (28 - n * 4));
    return;
  }
  case 512:
    field((opcode >> 23) & 7, state_.spr[spr::xer] >> 28);
    state_.spr[spr::xer] &= 0x0fffffff;
    return;
  case 371: {
    const unsigned index = ((opcode >> 16) & 31) | ((opcode >> 6) & 992);
    if (index != spr::tbl && index != spr::tbu)
      unsupported("Reserved timebase register selector");
    state_.gpr[d] = read_spr(index);
    return;
  }
  case 339:
    state_.gpr[d] = read_spr(((opcode >> 16) & 31) | ((opcode >> 6) & 992));
    return;
  case 467:
    write_spr(((opcode >> 16) & 31) | ((opcode >> 6) & 992), sv);
    return;
  case 210:
    privileged();
    state_.sr[(opcode >> 16) & 15] = sv;
    return;
  case 242:
    privileged();
    state_.sr[bv >> 28] = sv;
    return;
  case 595:
    privileged();
    state_.gpr[d] = state_.sr[(opcode >> 16) & 15];
    return;
  case 659:
    privileged();
    state_.gpr[d] = state_.sr[bv >> 28];
    return;
  case 306:
    privileged();
    invalidate_tlb(bv);
    return;
  case 566:
    privileged();
    return;
  case 598:
  case 854:
  case 278:
  case 246:
    return;
  case 758:
    unsupported("Cache-block allocation is not implemented");
  case 982:
    instruction_cache_.invalidate(ea);
    return;
  case 54:
  case 86:
  case 470:
    // The current interpreter uses coherent backing memory and has no host instruction cache.
    if (xo == 470)
      privileged();
    return;
  case 1014: {
    if (!(state_.spr[spr::hid0] & 0x4000))
      alignment(ea);
    clear_cache_line(ea);
    return;
  }
  case 20: {
    if (ea & 3)
      alignment(ea);
    state_.gpr[d] = static_cast<u32>(load(ea, 4));
    reservation_ = true;
    reservation_address_ = ea;
    return;
  }
  case 150: {
    if (ea & 3)
      alignment(ea);
    if (reservation_ && reservation_address_ == ea) {
      try {
        store(ea, sv, 4);
      } catch (const GuestFault &) {
        field(0, (state_.spr[spr::xer] & so) ? 1 : 0);
        throw;
      }
      reservation_ = false;
      field(0, 2 | ((state_.spr[spr::xer] & so) ? 1 : 0));
    } else
      field(0, (state_.spr[spr::xer] & so) ? 1 : 0);
    return;
  }
  case 23:
  case 55:
  case 87:
  case 119:
  case 279:
  case 311:
  case 343:
  case 375:
  case 534:
  case 790: {
    const bool update = xo == 55 || xo == 119 || xo == 311 || xo == 375;
    const u32 address = (update ? av : (a ? av : 0)) + bv;
    const unsigned width = xo == 87 || xo == 119 ? 1 : xo == 23 || xo == 55 || xo == 534 ? 4 : 2;
    u32 value = static_cast<u32>(load(address, width));
    if (xo == 343 || xo == 375)
      value = static_cast<u32>(static_cast<s32>(static_cast<s16>(value)));
    if (xo == 534)
      value = swap32(value);
    if (xo == 790)
      value = swap16(static_cast<u16>(value));
    state_.gpr[d] = value;
    if (update)
      state_.gpr[a] = address;
    return;
  }
  case 151:
  case 183:
  case 215:
  case 247:
  case 407:
  case 439:
  case 662:
  case 918: {
    const bool update = xo == 183 || xo == 247 || xo == 439;
    const u32 address = (update ? av : (a ? av : 0)) + bv;
    u32 value = sv;
    if (xo == 662)
      value = swap32(value);
    if (xo == 918)
      value = (value & 0xffff0000) | swap16(static_cast<u16>(value));
    store(address, value, xo == 215 || xo == 247 ? 1 : xo == 407 || xo == 439 || xo == 918 ? 2 : 4);
    if (update)
      state_.gpr[a] = address;
    return;
  }
  case 310:
  case 438:
    if (!(state_.spr[282] & 0x80000000)) {
      state_.spr[spr::dar] = ea;
      exception(0x300, state_.pc);
      throw GuestFault{};
    }
    if (ea & 3)
      alignment(ea);
    if (xo == 310) {
      try {
        state_.gpr[d] = static_cast<u32>(load(ea, 4));
      } catch (const GuestFault &) {
        state_.gpr[d] = 0;
        throw;
      }
    } else
      store(ea, sv, 4);
    return;
  case 533:
  case 597:
  case 661:
  case 725: {
    const bool immediate = xo == 597 || xo == 725, read = xo == 533 || xo == 597;
    const unsigned count = immediate ? (b ? b : 32) : state_.spr[spr::xer] & 255;
    const u32 address = (a ? av : 0) + (immediate ? 0 : bv);
    if (state_.msr & msr::le)
      alignment(address);
    if (!read) {
      u32 word_address = address & ~3u;
      unsigned leading = address & 3, remaining = count, source_byte = 0;
      u32 prefix =
          leading ? static_cast<u32>(load(word_address, 4)) & (~u32{0} << ((4 - leading) * 8)) : 0;
      while (remaining || leading) {
        const unsigned copied = std::min(4 - leading, remaining), used = leading + copied;
        u32 word = prefix;
        if (used < 4)
          word |= static_cast<u32>(load(word_address, 4)) & ((u32{1} << ((4 - used) * 8)) - 1);
        for (unsigned byte = 0; byte < copied; ++byte) {
          const unsigned index = source_byte + byte;
          const u32 value = state_.gpr[(d + index / 4) & 31] >> ((3 - (index & 3)) * 8);
          word |= (value & 255) << ((3 - leading - byte) * 8);
        }
        store(word_address, word, 4);
        remaining -= copied;
        source_byte += copied;
        leading = 0;
        prefix = 0;
        word_address += 4;
      }
      return;
    }
    for (unsigned n = 0; n < count; ++n) {
      const unsigned reg = (d + n / 4) & 31, shift = 24 - (n & 3) * 8;
      if (!(n & 3))
        state_.gpr[reg] = 0;
      state_.gpr[reg] |= static_cast<u32>(load(address + n, 1)) << shift;
    }
    return;
  }
  case 535:
  case 567:
  case 599:
  case 631:
  case 663:
  case 695:
  case 727:
  case 759:
  case 983: {
    if (!(state_.msr & msr::fp)) {
      exception(0x800, state_.pc);
      throw GuestFault{};
    }
    const bool update = xo == 567 || xo == 631 || xo == 695 || xo == 759;
    const u32 address = (update ? av : (a ? av : 0)) + bv;
    if (address & 3)
      alignment(address);
    if (xo == 535 || xo == 567) {
      const u64 value = binary32_to_binary64(static_cast<u32>(load(address, 4)));
      state_.fpr[d].bits = {value, value};
    } else if (xo == 599 || xo == 631)
      state_.fpr[d].bits[0] = load(address, 8);
    else if (xo == 983)
      store(address, static_cast<u32>(state_.fpr[d].bits[0]), 4);
    else if (xo == 663 || xo == 695)
      store(address, binary64_to_binary32_store(state_.fpr[d].bits[0]), 4);
    else
      store(address, state_.fpr[d].bits[0], 8);
    if (update)
      state_.gpr[a] = address;
    return;
  }
  default:
    break;
  }
  u32 result = 0;
  switch (arithmetic) {
  case 8:
  case 40:
  case 136:
  case 200:
  case 232:
  case 10:
  case 138:
  case 202:
  case 234:
  case 266: {
    const bool subtract = arithmetic == 8 || arithmetic == 40 || arithmetic == 136 ||
                          arithmetic == 200 || arithmetic == 232;
    const u32 left = subtract ? ~av : av;
    const u32 right = arithmetic == 200 || arithmetic == 202   ? 0
                      : arithmetic == 232 || arithmetic == 234 ? 0xffffffff
                                                               : bv;
    const u32 incoming = arithmetic == 8 || arithmetic == 40     ? 1
                         : arithmetic == 10 || arithmetic == 266 ? 0
                         : (state_.spr[spr::xer] & ca)           ? 1
                                                                 : 0;
    const u64 sum = static_cast<u64>(left) + right + incoming;
    result = static_cast<u32>(sum);
    if (arithmetic != 40 && arithmetic != 266)
      carry(sum >> 32);
    if (oe)
      overflow(add_overflow(left, right, result));
    break;
  }
  case 104:
    result = 0 - av;
    if (oe)
      overflow(av == 0x80000000);
    break;
  case 11:
    result = static_cast<u32>((static_cast<u64>(av) * bv) >> 32);
    break;
  case 75:
    result =
        static_cast<u32>((static_cast<s64>(static_cast<s32>(av)) * static_cast<s32>(bv)) >> 32);
    break;
  case 235: {
    const s64 product = static_cast<s64>(static_cast<s32>(av)) * static_cast<s32>(bv);
    result = static_cast<u32>(product);
    if (oe)
      overflow(product < -2147483648ll || product > 2147483647ll);
    break;
  }
  case 459:
    result = bv ? av / bv : 0;
    if (oe)
      overflow(bv == 0);
    break;
  case 491: {
    const bool invalid = bv == 0 || (av == 0x80000000 && bv == 0xffffffff);
    result = invalid ? (static_cast<s32>(av) < 0 ? 0xffffffff : 0)
                     : static_cast<u32>(static_cast<s32>(av) / static_cast<s32>(bv));
    if (oe)
      overflow(invalid);
    break;
  }
  default:
    program(illegal);
  }
  state_.gpr[d] = result;
  if (rc)
    record(result);
}
} // namespace cupid::cpu
