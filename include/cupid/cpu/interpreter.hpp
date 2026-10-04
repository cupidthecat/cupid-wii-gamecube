// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/bus.hpp"
#include "cupid/cpu/cache.hpp"
#include "cupid/cpu/native_integer.hpp"
#include <array>
#include <stdexcept>

namespace cupid::cpu {
namespace msr {
inline constexpr u32 le = 1, ri = 2, dr = 0x10, ir = 0x20, fp = 0x2000;
inline constexpr u32 pr = 0x4000, ee = 0x8000, ile = 0x10000;
} // namespace msr

namespace spr {
inline constexpr unsigned xer = 1, lr = 8, ctr = 9, dsisr = 18, dar = 19;
inline constexpr unsigned dec = 22, sdr1 = 25, srr0 = 26, srr1 = 27;
inline constexpr unsigned tbl = 268, tbu = 269, pvr = 287;
inline constexpr unsigned hid0 = 1008, hid1 = 1009, hid2 = 920, hid4 = 1011;
inline constexpr unsigned mmcr0 = 952, mmcr1 = 956, pmc1 = 953, pmc2 = 954;
inline constexpr unsigned pmc3 = 957, pmc4 = 958;
} // namespace spr

struct FloatingRegister {
  std::array<u64, 2> bits{};
  double value(unsigned lane = 0) const;
  void set(double value, unsigned lane = 0);
};

struct State {
  std::array<u32, 32> gpr{};
  std::array<FloatingRegister, 32> fpr{};
  std::array<u32, 1024> spr{};
  std::array<u32, 16> sr{};
  u32 pc = 0, msr = 0, cr = 0, fpscr = 0;
  u32 last_pc = 0, last_opcode = 0;
  u64 cycles = 0, instructions = 0, timebase = 0;
};

class UnsupportedInstruction : public std::runtime_error {
public:
  UnsupportedInstruction(u32 pc, u32 opcode, const char *reason);
  u32 pc, opcode;
};

class Interpreter {
public:
  explicit Interpreter(Bus &bus, Console console = Console::GameCube);

  State &state() {
    return state_;
  }

  const State &state() const {
    return state_;
  }

  void reset();
  void configure_boot();
  u32 step();
  void set_execution_backend(ExecutionBackend backend);

  ExecutionBackend execution_backend() const noexcept {
    return backend_;
  }

  NativeIntegerStatistics native_statistics() const noexcept;

  bool last_step_fetched_instruction() const noexcept {
    return last_step_fetched_;
  }

  void set_external_interrupt(bool active) {
    external_interrupt_ = active;
  }

  // Uses the guest's current relocation state and raises a guest exception on failure.
  u32 translate(u32 effective, bool instruction = false, bool write = false);

private:
  struct GuestFault {};

  struct TlbEntry {
    u32 effective = 0, vsid = 0, physical = 0, pte_address = 0;
    bool valid = false, changed = false, uncached = false;
  };

  Bus &bus_;
  Console console_;
  ExecutionBackend backend_ = ExecutionBackend::Interpreter;
  std::unique_ptr<NativeInteger> native_integer_;
  State state_{};
  InstructionCache instruction_cache_;
  std::array<TlbEntry, 128> instruction_tlb_{}, data_tlb_{};
  std::array<u8, 64> instruction_tlb_recent_{}, data_tlb_recent_{};
  bool external_interrupt_ = false, decrementer_pending_ = false, monitor_pending_ = false;
  bool reservation_ = false;
  bool last_step_fetched_ = false;
  u32 reservation_address_ = 0, next_pc_ = 0, current_opcode_ = 0;
  u32 tick_remainder_ = 0, decrementer_remainder_ = 0;
  void exception(u32 vector, u32 saved_pc, u32 cause = 0);
  bool take_interrupt(u32 saved_pc);
  [[noreturn]] void program(u32 cause);
  [[noreturn]] void storage_fault(u32 effective, bool instruction, bool write);
  [[noreturn]] void alignment(u32 effective);
  [[noreturn]] void unsupported(const char *reason);
  void privileged();
  void execute(u32 opcode);
  void execute_extended(u32 opcode);
  void execute_float(u32 opcode);
  void execute_paired(u32 opcode);
  void quantized(u32 opcode, bool indexed);
  u64 load(u32 effective, unsigned bytes);
  void store(u32 effective, u64 value, unsigned bytes);
  u32 translate_access(u32 effective, bool instruction, bool write, bool &uncached);
  void store_physical(u32 physical, u32 value, unsigned bytes, bool uncached);
  void clear_cache_line(u32 effective);
  void field(unsigned index, u32 value);
  void record(u32 value);
  void compare(unsigned index, u32 a, u32 b, bool signed_values);
  void carry(bool value);
  void overflow(bool value);
  void write_spr(unsigned index, u32 value);
  u32 read_spr(unsigned index);
  void invalidate_tlb(u32 effective);
  void tick(u32 cycles);
  void monitor(u32 cycles, u32 opcode);
};
} // namespace cupid::cpu
