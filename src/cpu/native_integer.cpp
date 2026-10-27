// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/native_integer.hpp"
#include "cupid/cpu/interpreter.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <vector>

#if defined(_M_X64) || defined(__x86_64__)
#define CUPID_NATIVE_X64 1
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <sys/mman.h>
#include <unistd.h>
#endif
#else
#define CUPID_NATIVE_X64 0
#endif

namespace cupid::cpu {
namespace {
constexpr std::size_t legacy_sequence_slot_bytes = 2048;

bool eligible(u32 opcode) {
  const unsigned primary = opcode >> 26;
  if (primary == 7 || primary == 8 || (primary >= 10 && primary <= 15) || primary == 20 ||
      primary == 21 || primary == 23 || (primary >= 24 && primary <= 29))
    return true;
  if (primary != 31)
    return false;
  const unsigned operation = (opcode >> 1) & 1023;
  switch (operation) {
  case 0:
  case 24:
  case 26:
  case 28:
  case 32:
  case 60:
  case 124:
  case 284:
  case 316:
  case 412:
  case 444:
  case 476:
  case 536:
  case 792:
  case 824:
  case 922:
  case 954:
    return true;
  default:
    break;
  }
  switch (operation & 511) {
  case 8:
  case 10:
  case 11:
  case 40:
  case 75:
  case 104:
  case 136:
  case 138:
  case 200:
  case 202:
  case 232:
  case 234:
  case 235:
  case 266:
    return true;
  default:
    return false;
  }
}

#if CUPID_NATIVE_X64
static_assert(std::is_standard_layout_v<State> && std::is_trivially_copyable_v<State>);
using Operation = void (*)(State *);
using ControlOperation = u32 (*)(State *);
using RamOperation = u32 (*)(State *, const NativeRamContext *);
constexpr std::size_t maximum_sequence_bytes = 8 * 256;
constexpr std::size_t sequence_alignment = 16;

struct BlockKey {
  std::array<u32, 8> words{};
  u32 count = 0;
  bool operator==(const BlockKey &) const = default;
};

struct BlockHash {
  std::size_t operator()(const BlockKey &key) const noexcept {
    u32 hash = key.count;
    for (unsigned n = 0; n < key.count; ++n)
      hash = (hash ^ key.words[n]) * 0x9e3779b1u;
    hash ^= hash >> 16;
    hash *= 0x85ebca6bu;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35u;
    return hash ^ (hash >> 16);
  }
};

struct BlockEntry {
  BlockKey key;
  Operation operation = nullptr;
};

// Separate typed entries prevent control results from calling a void entry.
// Both kinds share the byte arena and retire together on resource exhaustion.
struct ControlEntry {
  BlockKey key;
  ControlOperation operation = nullptr;
};

struct RamEntry {
  BlockKey key;
  RamOperation operation = nullptr;
};

// Identical host arguments do not make the RAM and RAM/control result contracts
// interchangeable. Keep all lookup and hint publication in this namespace.
struct RamControlOperation {
  using Entry = u32 (*)(State *, const NativeRamContext *);
  Entry entry = nullptr;

  explicit operator bool() const noexcept {
    return entry != nullptr;
  }
};

struct RamControlEntry {
  BlockKey key;
  RamControlOperation operation{};
};

struct RamControlCache {
  explicit RamControlCache(unsigned reservation) {
    lookup.reserve(reservation);
  }
  std::unordered_map<BlockKey, RamControlOperation, BlockHash> lookup;
  RamControlEntry last{};

  void clear() noexcept {
    lookup.clear();
    last = {};
  }
};

template <std::size_t Capacity = 256> class Encoder {
public:
  explicit Encoder(bool retain_register = false) : retain_register_(retain_register) {
  }

  void retain_result(bool retain) {
    retain_register_ = retain;
  }

  void entry() {
    bytes({0xf3, 0x0f, 0x1e, 0xfa}); // ENDBR64 permits indirect entry with CET.
#ifdef _WIN32
    bytes({0x49, 0x89, 0xca}); // RCX -> R10
#else
    bytes({0x49, 0x89, 0xfa}); // RDI -> R10
#endif
  }

  void finish() {
    spill();
    byte(0xc3);
  }

  void flush_register() {
    spill();
  }

  std::size_t jump_if(u8 condition) {
    bytes({0x0f, condition});
    const auto fixup = size_;
    word(0);
    return fixup;
  }

  void bind(std::size_t fixup) {
    const auto displacement = static_cast<u32>(size_ - fixup - 4);
    for (unsigned n = 0; n < 4; ++n)
      storage_[fixup + n] = static_cast<u8>(displacement >> (n * 8));
  }

  void control_exit(unsigned retired, bool taken, u32 displacement = 0, bool absolute = false,
                    bool link = false) {
    spill();
    if (link) {
      memory(0x8b, 0, offsetof(State, pc));
      accumulator(0x05, retired * 4);
      memory(0x89, 0, offsetof(State, spr) + spr::lr * sizeof(u32));
    }
    if (absolute)
      accumulator(0xb8, displacement);
    else {
      memory(0x8b, 0, offsetof(State, pc));
      accumulator(0x05, taken ? (retired - 1) * 4 + displacement : retired * 4);
    }
    memory(0x89, 0, offsetof(State, pc));
    accumulator(0xb8, retired | (taken ? 0x100u : 0));
    byte(0xc3);
  }

  void bytes(std::initializer_list<u8> values) {
    for (u8 value : values)
      byte(value);
  }

  void byte(u8 value) {
    if (size_ == storage_.size())
      throw std::logic_error("Native integer operation exceeds its encoding bound");
    storage_[size_++] = value;
  }

  void word(u32 value) {
    for (unsigned n = 0; n < 4; ++n)
      byte(static_cast<u8>(value >> (n * 8)));
  }

  void memory(u8 instruction, unsigned reg, std::size_t displacement) {
    // R10 holds State; all emitted scratch registers are volatile in both ABIs.
    bytes({static_cast<u8>(0x41 | (reg >= 8 ? 4 : 0)), instruction});
    reg &= 7;
    if (displacement <= 127) {
      byte(static_cast<u8>(0x42 | (reg << 3)));
      byte(static_cast<u8>(displacement));
    } else {
      byte(static_cast<u8>(0x82 | (reg << 3)));
      word(static_cast<u32>(displacement));
    }
  }

  void load(unsigned reg, unsigned guest) {
    if (guest == retained_guest_)
      bytes({static_cast<u8>(0x44 | (reg >= 8 ? 1 : 0)), 0x89,
             static_cast<u8>(0xd8 | (reg & 7))}); // R11D -> scratch register
    else
      memory(0x8b, reg, offsetof(State, gpr) + guest * sizeof(u32));
  }

  void store(unsigned guest) {
    if (!retain_register_) {
      if (guest == retained_guest_)
        retained_guest_ = 32;
      else
        spill();
      memory(0x89, 0, offsetof(State, gpr) + guest * sizeof(u32));
      return;
    }
    if (guest != retained_guest_)
      spill();
    bytes({0x41, 0x89, 0xc3}); // EAX -> R11D; flags and other scratch registers survive.
    retained_guest_ = guest;
  }

  void accumulator(u8 instruction, u32 immediate) {
    byte(instruction);
    word(immediate);
  }

  void condition(unsigned field, bool signed_values) {
    // CMP or TEST flags remain live through MOV and CMOV. The field value is
    // GT, LT or EQ together with the current sticky SO, retaining other fields.
    accumulator(0xba, 4); // MOV EDX, GT
    accumulator(0xb9, 8); // MOV ECX, LT
    bytes({0x0f, static_cast<u8>(signed_values ? 0x4c : 0x42), 0xd1});
    accumulator(0xb9, 2); // MOV ECX, EQ
    bytes({0x0f, 0x44, 0xd1});
    memory(0x8b, 1, offsetof(State, spr) + spr::xer * sizeof(u32));
    bytes({0xc1, 0xe9, 31, 0x09, 0xca});
    const unsigned shift = (7 - field) * 4;
    if (shift)
      bytes({0xc1, 0xe2, static_cast<u8>(shift)});
    memory(0x8b, 0, offsetof(State, cr));
    accumulator(0x25, ~(15u << shift));
    bytes({0x09, 0xd0});
    memory(0x89, 0, offsetof(State, cr));
  }

  void record() {
    // Retain the guest result before this method reuses EAX for the CR word.
    bytes({0x85, 0xc0});
    condition(0, true);
  }

  void flags(bool carry, bool overflow, bool invert_carry = false, bool carry_ready = false,
             bool overflow_ready = false) {
    // Capture host flags before modifying any scratch register. R8B can also
    // hold an instruction-specific carry that differs from the host carry.
    if (carry && !carry_ready)
      bytes({0x41, 0x0f, static_cast<u8>(invert_carry ? 0x93 : 0x92), 0xc0});
    if (overflow && !overflow_ready)
      bytes({0x41, 0x0f, 0x90, 0xc1});
    memory(0x8b, 2, offsetof(State, spr) + spr::xer * sizeof(u32));
    bytes({0x81, 0xe2});
    word(~((carry ? 0x20000000u : 0) | (overflow ? 0x40000000u : 0)));
    if (carry) {
      bytes({0x45, 0x0f, 0xb6, 0xc0, 0x41, 0xc1, 0xe0, 29, 0x44, 0x09, 0xc2});
    }
    if (overflow) {
      bytes({0x45, 0x0f, 0xb6, 0xc9, 0x41, 0xc1, 0xe1, 30, 0x44, 0x09, 0xca});
      bytes({0x41, 0xd1, 0xe1, 0x44, 0x09, 0xca}); // SO retains any earlier overflow.
    }
    memory(0x89, 2, offsetof(State, spr) + spr::xer * sizeof(u32));
  }

  std::span<const u8> code() const {
    return std::span(storage_).first(size_);
  }

private:
  void spill() {
    if (retained_guest_ < 32) {
      memory(0x89, 11, offsetof(State, gpr) + retained_guest_ * sizeof(u32));
      retained_guest_ = 32;
    }
  }

  std::array<u8, Capacity> storage_{};
  std::size_t size_ = 0;
  bool retain_register_ = false;
  unsigned retained_guest_ = 32;
};

template <std::size_t Capacity> void encode_instruction(Encoder<Capacity> &code, u32 opcode) {
  const unsigned primary = opcode >> 26, source = (opcode >> 21) & 31;
  const unsigned address = (opcode >> 16) & 31, count = (opcode >> 11) & 31;
  bool record = false;
  if (primary == 10 || primary == 11) {
    code.load(0, address);
    code.accumulator(0x3d, primary == 10
                               ? opcode & 0xffff
                               : static_cast<u32>(static_cast<s32>(static_cast<s16>(opcode))));
    code.condition((opcode >> 23) & 7, primary == 11);
  } else if (primary == 7 || primary == 8 || primary == 12 || primary == 13) {
    const u32 immediate = static_cast<u32>(static_cast<s32>(static_cast<s16>(opcode)));
    code.load(0, address);
    if (primary == 7) {
      code.bytes({0x69, 0xc0}); // IMUL EAX, EAX, signed immediate
      code.word(immediate);
    } else if (primary == 8) {
      code.bytes({0x89, 0xc1});
      code.accumulator(0xb8, immediate);
      code.bytes({0x29, 0xc8});
      code.flags(true, false, true);
    } else {
      code.accumulator(0x05, immediate);
      code.flags(true, false);
      record = primary == 13;
    }
    code.store(source);
  } else if (primary == 14 || primary == 15) {
    const u32 immediate = static_cast<u32>(static_cast<s32>(static_cast<s16>(opcode)))
                          << (primary == 15 ? 16 : 0);
    if (address == 0)
      code.accumulator(0xb8, immediate);
    else {
      code.load(0, address);
      code.accumulator(0x05, immediate);
    }
    code.store(source);
  } else if (primary >= 24 && primary <= 29) {
    const u32 immediate = (opcode & 0xffff) << ((primary & 1) ? 16 : 0);
    code.load(0, source);
    code.accumulator(primary < 26 ? 0x0d : primary < 28 ? 0x35 : 0x25, immediate);
    code.store(address);
    record = primary >= 28;
  } else if (primary == 31) {
    const unsigned operation = (opcode >> 1) & 1023, arithmetic = operation & 511;
    if (operation == 0 || operation == 32) {
      code.load(0, address);
      code.load(1, count);
      code.bytes({0x39, 0xc8});
      code.condition((opcode >> 23) & 7, operation == 0);
    } else if (operation == 24 || operation == 536) {
      code.load(0, source);
      code.load(1, count);
      code.bytes({0xd3, static_cast<u8>(operation == 24 ? 0xe0 : 0xe8)});
      code.bytes({0x31, 0xd2, 0xf6, 0xc1, 32, 0x0f, 0x45, 0xc2});
      code.store(address);
      record = opcode & 1;
    } else if (operation == 792 || operation == 824) {
      code.load(0, source);
      code.bytes({0x48, 0x63, 0xc0, 0x48, 0x89, 0xc2}); // sign-extend; retain input
      if (operation == 824)
        code.accumulator(0xb9, count);
      else
        code.load(1, count);
      // A 64-bit SAR uses exactly the guest's six-bit variable shift count and
      // already saturates a sign-extended 32-bit input for counts above 31.
      code.bytes({0x48, 0xd3, 0xf8, 0x49, 0x89, 0xc0, 0x49, 0xd3, 0xe0});
      code.bytes({0x49, 0x39, 0xd0, 0x41, 0x0f, 0x95, 0xc0}); // lost bits
      code.bytes({0x85, 0xd2, 0x41, 0x0f, 0x98, 0xc1, 0x45, 0x20, 0xc8});
      code.flags(true, false, false, true);
      code.store(address);
      record = opcode & 1;
    } else if (operation == 26 || operation == 922 || operation == 954) {
      code.load(0, source);
      if (operation == 26) {
        code.accumulator(0xba, 63);
        code.bytes({0x0f, 0xbd, 0xc0, 0x0f, 0x44, 0xc2}); // BSR; defined zero result
        code.accumulator(0x35, 31);
      } else {
        code.bytes({0x0f, static_cast<u8>(operation == 922 ? 0xbf : 0xbe), 0xc0});
      }
      code.store(address);
      record = opcode & 1;
    } else if (operation == 28 || operation == 60 || operation == 124 || operation == 284 ||
               operation == 316 || operation == 412 || operation == 444 || operation == 476) {
      code.load(0, source);
      code.load(1, count);
      if (operation == 60 || operation == 412)
        code.bytes({0xf7, 0xd1});
      const u8 instruction = operation == 28 || operation == 60 || operation == 476 ? 0x21
                             : operation == 284 || operation == 316                 ? 0x31
                                                                                    : 0x09;
      code.bytes({instruction, 0xc8});
      if (operation == 124 || operation == 284 || operation == 476)
        code.bytes({0xf7, 0xd0});
      code.store(address);
      record = opcode & 1;
    } else {
      const bool overflow = (opcode & 0x400) != 0;
      code.load(0, address);
      if (arithmetic == 104) {
        code.bytes({0xf7, 0xd8});
        if (overflow)
          code.flags(false, true);
      } else if (arithmetic == 11 || arithmetic == 75 || arithmetic == 235) {
        code.load(1, count);
        if (arithmetic == 235) {
          code.bytes({0x0f, 0xaf, 0xc1});
          if (overflow)
            code.flags(false, true);
        } else {
          code.bytes({0xf7, static_cast<u8>(arithmetic == 11 ? 0xe1 : 0xe9), 0x89, 0xd0});
        }
      } else {
        const bool subtract = arithmetic == 8 || arithmetic == 40 || arithmetic == 136 ||
                              arithmetic == 200 || arithmetic == 232;
        const bool minus_one = arithmetic == 232 || arithmetic == 234;
        const bool incoming =
            arithmetic != 8 && arithmetic != 10 && arithmetic != 40 && arithmetic != 266;
        const bool carry = arithmetic != 40 && arithmetic != 266;
        if (subtract)
          code.bytes({0xf7, 0xd0});
        if (minus_one) {
          code.bytes({0x41, 0x89, 0xc0}); // Retain complemented or ordinary left operand.
          code.accumulator(0xb9, 0xffffffff);
        } else if (arithmetic == 200 || arithmetic == 202) {
          code.accumulator(0xb9, 0);
        } else {
          code.load(1, count);
        }
        if (incoming) {
          code.memory(0x8b, 2, offsetof(State, spr) + spr::xer * sizeof(u32));
          code.bytes({0x0f, 0xba, 0xe2, 29, 0x11, 0xc8}); // BT XER.CA; ADC EAX, ECX
        } else if (subtract) {
          code.bytes({0xf9, 0x11, 0xc8});
        } else {
          code.bytes({0x01, 0xc8});
        }
        if (minus_one) {
          // These two operations test carry against (incoming - 1), including
          // the zero incoming operand when CA is set. Preserve OF separately.
          if (overflow)
            code.bytes({0x41, 0x0f, 0x90, 0xc1});
          code.bytes({0x31, 0xc9, 0x45, 0x85, 0xc0, 0x0f, 0x95, 0xc1});
          code.bytes({0x0f, 0xba, 0xe2, 29, 0x41, 0x0f, 0x93, 0xc0, 0x41, 0x20, 0xc8});
          code.flags(true, overflow, false, true, true);
        } else if (carry || overflow) {
          code.flags(carry, overflow);
        }
      }
      code.store(source);
      record = opcode & 1;
    }
  } else {
    const unsigned begin = (opcode >> 6) & 31, end = (opcode >> 1) & 31;
    const u32 from_begin = 0xffffffffu >> begin, through_end = 0xffffffffu << (31 - end);
    const u32 mask = begin <= end ? from_begin & through_end : from_begin | through_end;
    code.load(0, source);
    if (primary == 23) {
      code.load(1, count);
      code.bytes({0xd3, 0xc0}); // ROL EAX, CL masks its count to five bits.
    } else if (count != 0) {
      code.bytes({0xc1, 0xc0, static_cast<u8>(count)});
    }
    code.accumulator(0x25, mask);
    if (primary == 20) {
      code.load(2, address);
      code.bytes({0x81, 0xe2});
      code.word(~mask);
      code.bytes({0x09, 0xd0});
    }
    code.store(address);
    record = (opcode & 1) != 0;
  }
  if (record)
    code.record();
}

Encoder<> encode(u32 opcode) {
  Encoder<> code;
  code.entry();
  encode_instruction(code, opcode);
  code.finish();
  return code;
}

unsigned destination_register(u32 opcode) {
  const unsigned primary = opcode >> 26;
  if (primary == 10 || primary == 11 || primary == 16)
    return 32;
  if (primary == 31) {
    const unsigned operation = (opcode >> 1) & 1023;
    if (operation == 0 || operation == 32)
      return 32;
    switch (operation & 511) {
    case 8:
    case 10:
    case 11:
    case 40:
    case 75:
    case 104:
    case 136:
    case 138:
    case 200:
    case 202:
    case 232:
    case 234:
    case 235:
    case 266:
      return (opcode >> 21) & 31;
    default:
      return (opcode >> 16) & 31;
    }
  }
  return primary >= 20 ? (opcode >> 16) & 31 : (opcode >> 21) & 31;
}

Encoder<maximum_sequence_bytes> encode_sequence(std::span<const u32> words) {
  // R11 is volatile in both host ABIs and unused by instruction scratch work.
  // Sequences contain only register operations, so one dirty guest register can
  // remain here until replacement or return without an observable state gap.
  Encoder<maximum_sequence_bytes> code;
  code.entry();
  for (unsigned n = 0; n < words.size(); ++n) {
    const unsigned destination = destination_register(words[n]);
    unsigned next_destination = 32;
    for (unsigned next = n + 1; next < words.size() && next_destination == 32; ++next)
      next_destination = destination_register(words[next]);
    // Retain only overwritten destinations. Independent writes keep their
    // direct store without occupying a host register or adding move traffic.
    code.retain_result(destination < 32 && destination == next_destination);
    encode_instruction(code, words[n]);
  }
  code.finish();
  return code;
}

Encoder<maximum_sequence_bytes> encode_control_sequence(std::span<const u32> words) {
  Encoder<maximum_sequence_bytes> code;
  code.entry();
  for (unsigned n = 0; n < words.size(); ++n) {
    const u32 opcode = words[n];
    if ((opcode >> 26) != 16) {
      const auto destination = destination_register(opcode);
      unsigned next_destination = 32;
      for (unsigned next = n + 1; next < words.size() && next_destination == 32; ++next)
        next_destination = destination_register(words[next]);
      code.retain_result(destination < 32 && destination == next_destination);
      encode_instruction(code, opcode);
      continue;
    }
    // A taken exit must expose every earlier write, including a value retained
    // for a later overwrite that only an untaken branch would reach.
    code.flush_register();
    const unsigned bo = (opcode >> 21) & 31, bi = (opcode >> 16) & 31;
    std::array<std::size_t, 2> untaken{};
    unsigned exits = 0;
    if (!(bo & 4)) {
      code.memory(0x8b, 0, offsetof(State, spr) + spr::ctr * sizeof(u32));
      code.bytes({0x83, 0xe8, 1}); // SUB EAX, 1; preserve CTR's unsigned wrap.
      code.memory(0x89, 0, offsetof(State, spr) + spr::ctr * sizeof(u32));
      untaken[exits++] = code.jump_if(bo & 2 ? 0x85 : 0x84); // JNZ or JZ
    }
    if (!(bo & 16)) {
      code.memory(0x8b, 0, offsetof(State, cr));
      code.accumulator(0x25, 1u << (31 - bi));
      untaken[exits++] = code.jump_if(bo & 8 ? 0x84 : 0x85);
    }
    const auto displacement = static_cast<u32>(static_cast<s32>(static_cast<s16>(opcode & 0xfffc)));
    code.control_exit(n + 1, true, displacement, (opcode & 2) != 0, (opcode & 1) != 0);
    for (unsigned exit = 0; exit < exits; ++exit)
      code.bind(untaken[exit]);
  }
  code.control_exit(static_cast<unsigned>(words.size()), false);
  return code;
}

Encoder<maximum_sequence_bytes> encode_ram_sequence(std::span<const u32> words) {
  // Keep R11 for the fresh context. Register retention remains disabled, so
  // every completed integer write is already visible at a memory guard exit.
  Encoder<maximum_sequence_bytes> code;
  code.entry();
#ifdef _WIN32
  code.bytes({0x49, 0x89, 0xd3}); // RDX -> R11
#else
  code.bytes({0x49, 0x89, 0xf3}); // RSI -> R11
#endif
  std::array<std::array<std::size_t, 4>, 8> exits{};
  for (unsigned n = 0; n < words.size(); ++n) {
    const u32 opcode = words[n];
    const unsigned primary = opcode >> 26;
    if (primary != 32 && primary != 36) {
      encode_instruction(code, opcode);
      continue;
    }
    const unsigned reg = (opcode >> 21) & 31, address = (opcode >> 16) & 31;
    const u32 immediate = static_cast<u32>(static_cast<s32>(static_cast<s16>(opcode)));
    if (address) {
      code.load(0, address);
      code.accumulator(0x05, immediate);
    } else
      code.accumulator(0xb8, immediate);
    code.bytes({0xa8, 3}); // TEST AL,3; preserve modular 32-bit effective address.
    exits[n][0] = code.jump_if(0x85);
    code.bytes({0x49, 0x8b, 0x53, u8(offsetof(NativeRamContext, bat_entries))});
    code.bytes({0x48, 0x85, 0xd2}); // TEST RDX,RDX
    const auto untranslated = code.jump_if(0x84);
    code.bytes({0x89, 0xc1, 0xc1, 0xe9, 17}); // ECX=effective>>17
    code.bytes({0x8b, 0x14, 0x8a});           // EDX=BAT[ECX]
    code.bytes({0x89, 0xd1, 0x83, 0xe1, 5, 0x83, 0xf9, 1});
    exits[n][1] = code.jump_if(0x85); // Valid and normal cache policy only.
    code.bytes({0x81, 0xe2});
    code.word(0xfffe0000);
    code.accumulator(0x25, 0x1ffff);
    code.bytes({0x09, 0xd0});
    code.bind(untranslated);
    code.bytes({0x41, 0x2b, 0x43, u8(offsetof(NativeRamContext, physical_base))});
    exits[n][2] = code.jump_if(0x82); // Physical address precedes the granted interval.
    code.bytes({0x41, 0x8b, 0x53, u8(offsetof(NativeRamContext, bytes))});
    code.bytes({0x83, 0xea, 4, 0x39, 0xd0}); // Unsigned offset<=bytes-4, without wrap.
    exits[n][3] = code.jump_if(0x87);
    code.bytes({0x49, 0x8b, 0x13}); // RDX=host backing, EAX is a zero-extended offset.
    if (primary == 32) {
      code.bytes({0x8b, 0x04, 0x02, 0x0f, 0xc8}); // Load word, BSWAP EAX.
      code.store(reg);
    } else {
      code.bytes({0x89, 0xc1}); // Preserve checked offset before reading source GPR.
      code.load(0, reg);
      code.bytes({0x0f, 0xc8, 0x89, 0x04, 0x0a}); // BSWAP; store [RDX+RCX].
    }
  }
  code.accumulator(0xb8, static_cast<u32>(words.size()));
  code.finish();
  for (unsigned n = 0; n < words.size(); ++n)
    if (exits[n][0]) {
      for (auto fixup : exits[n])
        code.bind(fixup);
      code.accumulator(0xb8, n);
      code.finish();
    }
  return code;
}

Encoder<maximum_sequence_bytes> encode_ram_control_sequence(std::span<const u32> words) {
  // Keep the existing word-only encoder unchanged. This separate entry reserves
  // R11 for the fresh RAM context and never enables register retention.
  Encoder<maximum_sequence_bytes> code;
  code.entry();
#ifdef _WIN32
  code.bytes({0x49, 0x89, 0xd3}); // RDX -> R11
#else
  code.bytes({0x49, 0x89, 0xf3}); // RSI -> R11
#endif
  std::array<std::array<std::size_t, 4>, 7> exits{};
  for (unsigned n = 0; n < 7; ++n) {
    const u32 opcode = words[n];
    const unsigned primary = opcode >> 26;
    if (primary != 32 && primary != 36) {
      encode_instruction(code, opcode);
      continue;
    }
    const unsigned reg = (opcode >> 21) & 31, address = (opcode >> 16) & 31;
    const u32 immediate = static_cast<u32>(static_cast<s32>(static_cast<s16>(opcode)));
    if (address) {
      code.load(0, address);
      code.accumulator(0x05, immediate);
    } else
      code.accumulator(0xb8, immediate);
    code.bytes({0xa8, 3}); // TEST AL,3; preserve modular 32-bit effective address.
    exits[n][0] = code.jump_if(0x85);
    code.bytes({0x49, 0x8b, 0x53, u8(offsetof(NativeRamContext, bat_entries))});
    code.bytes({0x48, 0x85, 0xd2}); // TEST RDX,RDX
    const auto untranslated = code.jump_if(0x84);
    code.bytes({0x89, 0xc1, 0xc1, 0xe9, 17}); // ECX=effective>>17
    code.bytes({0x8b, 0x14, 0x8a});           // EDX=BAT[ECX]
    code.bytes({0x89, 0xd1, 0x83, 0xe1, 5, 0x83, 0xf9, 1});
    exits[n][1] = code.jump_if(0x85); // Valid and normal cache policy only.
    code.bytes({0x81, 0xe2});
    code.word(0xfffe0000);
    code.accumulator(0x25, 0x1ffff);
    code.bytes({0x09, 0xd0});
    code.bind(untranslated);
    code.bytes({0x41, 0x2b, 0x43, u8(offsetof(NativeRamContext, physical_base))});
    exits[n][2] = code.jump_if(0x82); // Physical address precedes the granted interval.
    code.bytes({0x41, 0x8b, 0x53, u8(offsetof(NativeRamContext, bytes))});
    code.bytes({0x83, 0xea, 4, 0x39, 0xd0}); // Unsigned offset<=bytes-4, without wrap.
    exits[n][3] = code.jump_if(0x87);
    code.bytes({0x49, 0x8b, 0x13}); // RDX=host backing, EAX is a zero-extended offset.
    if (primary == 32) {
      code.bytes({0x8b, 0x04, 0x02, 0x0f, 0xc8}); // Load word, BSWAP EAX.
      code.store(reg);
    } else {
      code.bytes({0x89, 0xc1}); // Preserve checked offset before reading source GPR.
      code.load(0, reg);
      code.bytes({0x0f, 0xc8, 0x89, 0x04, 0x0a}); // BSWAP; store [RDX+RCX].
    }
  }
  // Admission requires the eighth word to be BC with LK=0. CTR and CR are
  // touched only after all seven preceding instructions have completed.
  const u32 branch = words[7];
  const unsigned bo = (branch >> 21) & 31, bi = (branch >> 16) & 31;
  std::array<std::size_t, 2> untaken{};
  unsigned branches = 0;
  if (!(bo & 4)) {
    code.memory(0x8b, 0, offsetof(State, spr) + spr::ctr * sizeof(u32));
    code.bytes({0x83, 0xe8, 1});
    code.memory(0x89, 0, offsetof(State, spr) + spr::ctr * sizeof(u32));
    untaken[branches++] = code.jump_if(bo & 2 ? 0x85 : 0x84);
  }
  if (!(bo & 16)) {
    code.memory(0x8b, 0, offsetof(State, cr));
    code.accumulator(0x25, 1u << (31 - bi));
    untaken[branches++] = code.jump_if(bo & 8 ? 0x84 : 0x85);
  }
  const u32 displacement = static_cast<u32>(static_cast<s32>(static_cast<s16>(branch & 0xfffc)));
  code.control_exit(8, true, displacement, (branch & 2) != 0);
  for (unsigned n = 0; n < branches; ++n)
    code.bind(untaken[n]);
  code.control_exit(8, false);
  for (unsigned n = 0; n < 7; ++n)
    if (exits[n][0]) {
      for (auto fixup : exits[n])
        code.bind(fixup);
      // No branch effects on a memory exit. Publish the next unexecuted PC,
      // expose every completed register/store and return its exact count.
      code.control_exit(n, false);
    }
  return code;
}

Encoder<maximum_sequence_bytes> encode_short_ram_control_sequence(std::span<const u32> words) {
  // Short entries use only volatile integer scratch registers. R11 keeps the
  // fresh RAM context, so every completed GPR write is published immediately.
  const unsigned count = static_cast<unsigned>(words.size());
  const unsigned prefix = count - 1;
  Encoder<maximum_sequence_bytes> code;
  code.entry();
#ifdef _WIN32
  code.bytes({0x49, 0x89, 0xd3}); // RDX -> R11
#else
  code.bytes({0x49, 0x89, 0xf3}); // RSI -> R11
#endif
  std::array<std::array<std::size_t, 4>, 6> exits{};
  for (unsigned n = 0; n < prefix; ++n) {
    const u32 opcode = words[n];
    const unsigned primary = opcode >> 26;
    if (primary != 32 && primary != 36) {
      encode_instruction(code, opcode);
      continue;
    }
    const unsigned reg = (opcode >> 21) & 31, address = (opcode >> 16) & 31;
    const u32 immediate = static_cast<u32>(static_cast<s32>(static_cast<s16>(opcode)));
    if (address) {
      code.load(0, address);
      code.accumulator(0x05, immediate);
    } else
      code.accumulator(0xb8, immediate);
    code.bytes({0xa8, 3}); // TEST AL,3; preserve modular 32-bit effective address.
    exits[n][0] = code.jump_if(0x85);
    code.bytes({0x49, 0x8b, 0x53, u8(offsetof(NativeRamContext, bat_entries))});
    code.bytes({0x48, 0x85, 0xd2}); // TEST RDX,RDX
    const auto untranslated = code.jump_if(0x84);
    code.bytes({0x89, 0xc1, 0xc1, 0xe9, 17}); // ECX=effective>>17
    code.bytes({0x8b, 0x14, 0x8a});           // EDX=BAT[ECX]
    code.bytes({0x89, 0xd1, 0x83, 0xe1, 5, 0x83, 0xf9, 1});
    exits[n][1] = code.jump_if(0x85); // Valid and normal cache policy only.
    code.bytes({0x81, 0xe2});
    code.word(0xfffe0000);
    code.accumulator(0x25, 0x1ffff);
    code.bytes({0x09, 0xd0});
    code.bind(untranslated);
    code.bytes({0x41, 0x2b, 0x43, u8(offsetof(NativeRamContext, physical_base))});
    exits[n][2] = code.jump_if(0x82); // Physical address precedes the granted interval.
    code.bytes({0x41, 0x8b, 0x53, u8(offsetof(NativeRamContext, bytes))});
    code.bytes({0x83, 0xea, 4, 0x39, 0xd0}); // Unsigned offset<=bytes-4, without wrap.
    exits[n][3] = code.jump_if(0x87);
    code.bytes({0x49, 0x8b, 0x13}); // RDX=host backing, EAX is a zero-extended offset.
    if (primary == 32) {
      code.bytes({0x8b, 0x04, 0x02, 0x0f, 0xc8}); // Load word, BSWAP EAX.
      code.store(reg);
    } else {
      code.bytes({0x89, 0xc1}); // Preserve checked offset before reading source GPR.
      code.load(0, reg);
      code.bytes({0x0f, 0xc8, 0x89, 0x04, 0x0a}); // BSWAP; store [RDX+RCX].
    }
  }
  // The final word is the first BC, with LK=0. Its position determines both
  // fallthrough and relative target; no branch state changes on a guard exit.
  const u32 branch = words[prefix];
  const unsigned bo = (branch >> 21) & 31, bi = (branch >> 16) & 31;
  std::array<std::size_t, 2> untaken{};
  unsigned branches = 0;
  if (!(bo & 4)) {
    code.memory(0x8b, 0, offsetof(State, spr) + spr::ctr * sizeof(u32));
    code.bytes({0x83, 0xe8, 1});
    code.memory(0x89, 0, offsetof(State, spr) + spr::ctr * sizeof(u32));
    untaken[branches++] = code.jump_if(bo & 2 ? 0x85 : 0x84);
  }
  if (!(bo & 16)) {
    code.memory(0x8b, 0, offsetof(State, cr));
    code.accumulator(0x25, 1u << (31 - bi));
    untaken[branches++] = code.jump_if(bo & 8 ? 0x84 : 0x85);
  }
  const u32 displacement = static_cast<u32>(static_cast<s32>(static_cast<s16>(branch & 0xfffc)));
  code.control_exit(count, true, displacement, (branch & 2) != 0);
  for (unsigned n = 0; n < branches; ++n)
    code.bind(untaken[n]);
  code.control_exit(count, false);
  for (unsigned n = 0; n < prefix; ++n)
    if (exits[n][0]) {
      for (auto fixup : exits[n])
        code.bind(fixup);
      // No branch effects on a memory exit. Publish the next unexecuted PC,
      // expose every completed register/store and return its exact count.
      code.control_exit(n, false);
    }
  return code;
}

std::size_t page_size();

class ExecutablePage {
public:
  explicit ExecutablePage(std::size_t size, std::size_t limit = 0)
      : size_(size), limit_(limit ? limit : size), page_bytes_(page_size()) {
#ifdef _WIN32
    memory_ =
        static_cast<u8 *>(VirtualAlloc(nullptr, size_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory_)
      throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                              "Native code allocation failed");
#else
    void *mapping =
        mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED)
      throw std::system_error(errno, std::generic_category(), "Native code allocation failed");
    memory_ = static_cast<u8 *>(mapping);
#endif
  }

  ~ExecutablePage() {
#ifdef _WIN32
    (void)VirtualFree(memory_, 0, MEM_RELEASE);
#else
    (void)munmap(memory_, size_);
#endif
  }

  ExecutablePage(const ExecutablePage &) = delete;
  ExecutablePage &operator=(const ExecutablePage &) = delete;

  bool fits(std::size_t bytes, std::size_t alignment = 1) const {
    const auto padding = (alignment - used_ % alignment) % alignment;
    return padding <= limit_ - used_ && bytes <= limit_ - used_ - padding;
  }

  template <typename Entry = Operation>
  Entry append(std::span<const u8> code, std::size_t alignment = 1) {
    if (!fits(code.size(), alignment))
      throw std::logic_error("Native code page capacity exceeded");
    const auto offset = (used_ + alignment - 1) / alignment * alignment;
    const auto operation = replace(offset, code);
    used_ = offset + code.size();
    return reinterpret_cast<Entry>(operation);
  }

  void rewind() noexcept {
    used_ = 0;
  }

private:
  u8 *replace(std::size_t offset, std::span<const u8> code) {
    if (offset > limit_ || code.empty() || code.size() > limit_ - offset)
      throw std::logic_error("Native code replacement exceeds its arena bound");
    // Emitted operations make no calls. Compilation runs on the owning CPU
    // thread between operations, so an existing page is never executing here.
    // Only touched pages change permissions. Unwritten pages start writable;
    // earlier entries on other pages remain readable and executable.
    const auto begin = offset / page_bytes_ * page_bytes_;
    const auto end = (offset + code.size() + page_bytes_ - 1) / page_bytes_ * page_bytes_;
    if (begin < sealed_bytes_)
      protect(begin, std::min(end, sealed_bytes_) - begin, false);
    std::memcpy(memory_ + offset, code.data(), code.size());
    protect(begin, end - begin, true);
    sealed_bytes_ = std::max(sealed_bytes_, end);
#ifdef _WIN32
    if (!FlushInstructionCache(GetCurrentProcess(), memory_ + offset, code.size()))
      throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                              "Native instruction-cache flush failed");
#else
    __builtin___clear_cache(reinterpret_cast<char *>(memory_ + offset),
                            reinterpret_cast<char *>(memory_ + offset + code.size()));
#endif
    return memory_ + offset;
  }

  void protect(std::size_t offset, std::size_t bytes, bool executable) {
#ifdef _WIN32
    DWORD previous = 0;
    if (!VirtualProtect(memory_ + offset, bytes, executable ? PAGE_EXECUTE_READ : PAGE_READWRITE,
                        &previous))
      throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                              "Native code page protection failed");
#else
    if (mprotect(memory_ + offset, bytes, PROT_READ | (executable ? PROT_EXEC : PROT_WRITE)) != 0)
      throw std::system_error(errno, std::generic_category(), "Native code page protection failed");
#endif
  }

  u8 *memory_ = nullptr;
  std::size_t size_ = 0, limit_ = 0, page_bytes_ = 0, used_ = 0, sealed_bytes_ = 0;
};

std::size_t page_size() {
#ifdef _WIN32
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  return info.dwPageSize;
#else
  const long size = sysconf(_SC_PAGESIZE);
  if (size <= 0)
    throw std::runtime_error("Native code page size is unavailable");
  return static_cast<std::size_t>(size);
#endif
}
#endif
} // namespace

struct NativeInteger::Impl {
  explicit Impl(unsigned limit, unsigned block_limit, std::size_t byte_budget)
      : capacity(limit), block_capacity(block_limit), sequence_byte_budget(byte_budget) {
#if CUPID_NATIVE_X64
    operations.reserve(capacity);
    block_lookup.reserve(block_capacity);
    control_lookup.reserve(block_capacity);
    ram_lookup.reserve(block_capacity);
#endif
  }

  unsigned capacity, block_capacity;
  std::size_t sequence_byte_budget;
#if CUPID_NATIVE_X64
  std::unordered_map<u32, Operation> operations;
  std::vector<std::unique_ptr<ExecutablePage>> pages;
  std::unordered_map<BlockKey, Operation, BlockHash> block_lookup;
  BlockEntry last_block{};
  std::unordered_map<BlockKey, ControlOperation, BlockHash> control_lookup;
  ControlEntry last_control{};
  std::unordered_map<BlockKey, RamOperation, BlockHash> ram_lookup;
  RamEntry last_ram{};
  std::array<RamEntry, 4096> ram_pc_hints{};
  // PC hints accelerate dispatch only; exact fetched contents remain authoritative.
  std::array<ControlEntry, 4096> control_pc_hints{};
  std::unique_ptr<ExecutablePage> block_arena;
  // Existing nonbranch users do not allocate or reserve this namespace.
  // Its metadata also dies before the arena during ordinary owner destruction.
  std::unique_ptr<RamControlCache> ram_control;

  std::size_t sequence_count() const noexcept {
    return block_lookup.size() + control_lookup.size() + ram_lookup.size() +
           (ram_control ? ram_control->lookup.size() : 0);
  }

  void clear_blocks(bool release = true) noexcept {
    block_lookup.clear();
    last_block = {};
    control_lookup.clear();
    last_control = {};
    ram_lookup.clear();
    last_ram = {};
    ram_pc_hints = {};
    control_pc_hints = {};
    if (ram_control)
      ram_control->clear();
    if (release)
      block_arena.reset();
    else if (block_arena)
      block_arena->rewind();
  }
#endif
};

bool NativeInteger::supported() noexcept {
  return CUPID_NATIVE_X64 != 0;
}

bool NativeInteger::can_execute(u32 opcode) noexcept {
  return eligible(opcode);
}

NativeInteger::NativeInteger(unsigned capacity, unsigned block_capacity)
    : NativeInteger(capacity, block_capacity,
                    std::size_t{block_capacity} * legacy_sequence_slot_bytes) {
}

NativeInteger::NativeInteger(unsigned capacity, unsigned block_capacity,
                             std::size_t sequence_byte_budget) {
  if (!supported())
    throw std::runtime_error("Native integer execution requires an x86-64 host");
  if (capacity == 0 || capacity > 4096)
    throw std::invalid_argument("Native integer cache capacity must be between 1 and 4096");
  if (block_capacity == 0 || block_capacity > 512)
    throw std::invalid_argument("Native sequence cache capacity must be between 1 and 512");
  if (sequence_byte_budget < legacy_sequence_slot_bytes ||
      sequence_byte_budget > maximum_sequence_byte_budget)
    throw std::invalid_argument("Native sequence byte budget must be between 2048 and 134217728");
  impl_ = std::make_unique<Impl>(capacity, block_capacity, sequence_byte_budget);
}

NativeInteger::~NativeInteger() = default;

bool NativeInteger::execute_other(State &state, u32 opcode) {
  auto &hint = hints_[(opcode ^ (opcode >> 11) ^ (opcode >> 21)) & 2047];
  if (!eligible(opcode)) {
    hint = {opcode, Hint::Kind::Unsupported, nullptr};
    ++statistics_.fallback;
    return false;
  }
#if CUPID_NATIVE_X64
  Operation operation = nullptr;
  if (const auto found = impl_->operations.find(opcode); found != impl_->operations.end()) {
    operation = found->second;
    ++statistics_.cache_hits;
  } else {
    if (impl_->operations.size() == impl_->capacity) {
      hint = {opcode, Hint::Kind::Capacity, nullptr};
      ++statistics_.capacity_fallbacks;
      ++statistics_.fallback;
      return false;
    }
    const auto code = encode(opcode);
    const std::size_t page_bytes = page_size();
    if (impl_->pages.empty() || !impl_->pages.back()->fits(code.code().size()))
      impl_->pages.push_back(std::make_unique<ExecutablePage>(page_bytes));
    operation = impl_->pages.back()->append(code.code());
    impl_->operations.emplace(opcode, operation);
    ++statistics_.compiled_operations;
    statistics_.cache_entries = impl_->operations.size();
    statistics_.executable_bytes = impl_->pages.size() * page_bytes;
  }
  hint = {opcode, Hint::Kind::Emitted, operation};
  invoke_operation(state, operation);
  ++statistics_.executed;
  return true;
#else
  (void)state;
  ++statistics_.fallback;
  return false;
#endif
}

void NativeInteger::reset() {
  impl_ =
      std::make_unique<Impl>(impl_->capacity, impl_->block_capacity, impl_->sequence_byte_budget);
  hints_ = {};
  statistics_ = {};
}

bool NativeInteger::execute_sequence(State &state, std::span<const u32> words) {
  if (words.size() < 2 || words.size() > 8)
    return false;
#if CUPID_NATIVE_X64
  BlockKey key;
  key.count = static_cast<u32>(words.size());
  std::copy(words.begin(), words.end(), key.words.begin());
  bool hit = false;
  Operation operation = nullptr;
  const bool last_hit = impl_->last_block.operation && impl_->last_block.key == key;
  if (last_hit) {
    operation = impl_->last_block.operation;
    hit = true;
  } else if (const auto found = impl_->block_lookup.find(key); found != impl_->block_lookup.end()) {
    operation = found->second;
    hit = true;
  }
  if (!hit) {
    if (!std::all_of(words.begin(), words.end(), eligible))
      return false;
    try {
      const auto code = encode_sequence(words);
      if (!impl_->block_arena) {
        const std::size_t requested = impl_->sequence_byte_budget;
        const auto page = page_size();
        const auto bytes = (requested + page - 1) / page * page;
        impl_->block_arena = std::make_unique<ExecutablePage>(bytes, requested);
        statistics_.block_executable_bytes = bytes;
      }
      // Dispatch collisions never retire compiled code. Aligned nonempty
      // bodies also bound metadata by the byte budget, independently of hints.
      // Reclaim only when this body cannot fit in the remaining arena.
      if (!impl_->block_arena->fits(code.code().size(), sequence_alignment)) {
        statistics_.block_replacements += impl_->sequence_count();
        impl_->clear_blocks(false);
        statistics_.block_cache_entries = 0;
      }
      operation = impl_->block_arena->append(code.code(), sequence_alignment);
      impl_->block_lookup.emplace(key, operation);
      statistics_.block_cache_entries = impl_->sequence_count();
      ++statistics_.block_compilations;
    } catch (const std::bad_alloc &) {
      impl_->clear_blocks();
      statistics_.block_cache_entries = statistics_.block_executable_bytes = 0;
      ++statistics_.block_compilation_failures;
      return false;
    } catch (const std::system_error &) {
      // Failed page protection may affect old slots too. Retire all entries
      // before returning to ordinary execution, which can retry safely.
      impl_->clear_blocks();
      statistics_.block_cache_entries = statistics_.block_executable_bytes = 0;
      ++statistics_.block_compilation_failures;
      return false;
    }
  }
  if (!last_hit) {
    impl_->last_block = {key, operation};
  }
  invoke_operation(state, operation);
  statistics_.executed += words.size();
  statistics_.block_executed += words.size();
  ++statistics_.block_runs;
  if (hit) {
    ++statistics_.block_cache_hits;
    statistics_.cache_hits += words.size();
  }
  return true;
#else
  (void)state;
  return false;
#endif
}

bool NativeInteger::execute_control_sequence(State &state, std::span<const u32> words,
                                             NativeControlRun &result) {
  if (words.size() < 2 || words.size() > 8)
    return false;
#if CUPID_NATIVE_X64
  BlockKey key;
  key.count = static_cast<u32>(words.size());
  std::copy(words.begin(), words.end(), key.words.begin());
  ControlOperation operation = nullptr;
  bool hit = false;
  const unsigned pc_hint_index =
      (state.pc >> 2) & static_cast<unsigned>(impl_->control_pc_hints.size() - 1);
  auto &pc_hint = impl_->control_pc_hints[pc_hint_index];
  const bool last_hit = impl_->last_control.operation && impl_->last_control.key == key;
  bool pc_hit = false;
  if (last_hit) {
    operation = impl_->last_control.operation;
    hit = true;
  } else if (pc_hint.operation && pc_hint.key == key) {
    operation = pc_hint.operation;
    pc_hit = true;
    hit = true;
  } else if (const auto found = impl_->control_lookup.find(key);
             found != impl_->control_lookup.end()) {
    operation = found->second;
    hit = true;
  }
  if (!hit) {
    bool branch = false;
    for (u32 opcode : words) {
      if ((opcode >> 26) == 16)
        branch = true;
      else if (!can_group(opcode))
        return false;
    }
    if (!branch)
      return false;
    try {
      const auto code = encode_control_sequence(words);
      if (!impl_->block_arena) {
        const std::size_t requested = impl_->sequence_byte_budget;
        const auto page = page_size();
        const auto bytes = (requested + page - 1) / page * page;
        impl_->block_arena = std::make_unique<ExecutablePage>(bytes, requested);
        statistics_.block_executable_bytes = bytes;
      }
      if (!impl_->block_arena->fits(code.code().size(), sequence_alignment)) {
        statistics_.block_replacements += impl_->sequence_count();
        impl_->clear_blocks(false);
        statistics_.block_cache_entries = 0;
      }
      operation = impl_->block_arena->append<ControlOperation>(code.code(), sequence_alignment);
      impl_->control_lookup.emplace(key, operation);
      statistics_.block_cache_entries = impl_->sequence_count();
      ++statistics_.block_compilations;
    } catch (const std::bad_alloc &) {
      impl_->clear_blocks();
      statistics_.block_cache_entries = statistics_.block_executable_bytes = 0;
      ++statistics_.block_compilation_failures;
      return false;
    } catch (const std::system_error &) {
      // Shared arena writes can affect either kind. Discard every typed entry
      // and hint before the uncertain storage is released or invoked again.
      impl_->clear_blocks();
      statistics_.block_cache_entries = statistics_.block_executable_bytes = 0;
      ++statistics_.block_compilation_failures;
      return false;
    }
  }
  if (!last_hit) {
    impl_->last_control = {key, operation};
    if (!pc_hit)
      pc_hint = {key, operation};
  }
  const u32 encoded = invoke_control(state, operation);
  result = {encoded & 255u, (encoded & 256u) != 0};
  statistics_.executed += result.instructions;
  statistics_.block_executed += result.instructions;
  ++statistics_.block_runs;
  if (hit) {
    ++statistics_.block_cache_hits;
    statistics_.cache_hits += result.instructions;
  }
  return true;
#else
  (void)state;
  (void)result;
  return false;
#endif
}

bool NativeInteger::execute_ram_sequence(State &state, std::span<const u32> words,
                                         const NativeRamContext &context, u32 &retired) {
  if (words.size() < 2 || words.size() > 8 || !context.data || context.bytes < 4 ||
      context.physical_base > 0xffffffffu - (context.bytes - 1) || (state.msr & msr::le) ||
      bool(state.msr & msr::dr) != bool(context.bat_entries))
    return false;
#if CUPID_NATIVE_X64
  BlockKey key;
  key.count = static_cast<u32>(words.size());
  std::copy(words.begin(), words.end(), key.words.begin());
  RamOperation operation = nullptr;
  bool hit = false;
  const unsigned pc_index = (state.pc >> 2) & unsigned(impl_->ram_pc_hints.size() - 1);
  auto &pc_hint = impl_->ram_pc_hints[pc_index];
  bool pc_hit = false;
  const bool last_hit = impl_->last_ram.operation && impl_->last_ram.key == key;
  if (last_hit) {
    operation = impl_->last_ram.operation;
    hit = true;
  } else if (pc_hint.operation && pc_hint.key == key) {
    operation = pc_hint.operation;
    hit = pc_hit = true;
  } else if (const auto found = impl_->ram_lookup.find(key); found != impl_->ram_lookup.end()) {
    operation = found->second;
    hit = true;
  }
  if (!hit) {
    bool memory = false;
    for (u32 opcode : words) {
      const auto primary = opcode >> 26;
      if (primary == 32 || primary == 36)
        memory = true;
      else if (!can_group(opcode))
        return false;
    }
    if (!memory)
      return false;
    try {
      const auto code = encode_ram_sequence(words);
      if (!impl_->block_arena) {
        const auto requested = impl_->sequence_byte_budget;
        const auto page = page_size();
        const auto bytes = (requested + page - 1) / page * page;
        impl_->block_arena = std::make_unique<ExecutablePage>(bytes, requested);
        statistics_.block_executable_bytes = bytes;
      }
      if (!impl_->block_arena->fits(code.code().size(), sequence_alignment)) {
        statistics_.block_replacements += impl_->sequence_count();
        impl_->clear_blocks(false);
        statistics_.block_cache_entries = 0;
      }
      operation = impl_->block_arena->append<RamOperation>(code.code(), sequence_alignment);
      impl_->ram_lookup.emplace(key, operation);
      statistics_.block_cache_entries = impl_->sequence_count();
      ++statistics_.block_compilations;
    } catch (const std::bad_alloc &) {
      impl_->clear_blocks();
      statistics_.block_cache_entries = statistics_.block_executable_bytes = 0;
      ++statistics_.block_compilation_failures;
      return false;
    } catch (const std::system_error &) {
      impl_->clear_blocks();
      statistics_.block_cache_entries = statistics_.block_executable_bytes = 0;
      ++statistics_.block_compilation_failures;
      return false;
    }
  }
  if (!last_hit) {
    impl_->last_ram = {key, operation};
    if (!pc_hit)
      pc_hint = {key, operation};
  }
  retired = invoke_ram(state, context, operation);
  statistics_.executed += retired;
  statistics_.block_executed += retired;
  ++statistics_.block_runs;
  if (hit) {
    ++statistics_.block_cache_hits;
    statistics_.cache_hits += retired;
  }
  return true;
#else
  (void)state;
  (void)retired;
  return false;
#endif
}

bool NativeInteger::execute_ram_control_sequence(State &state, std::span<const u32> words,
                                                 const NativeRamContext &context,
                                                 NativeRamControlRun &result) {
  if (words.size() != 8 || !context.data || context.bytes < 4 ||
      context.physical_base > 0xffffffffu - (context.bytes - 1) || (state.msr & msr::le) ||
      bool(state.msr & msr::dr) != bool(context.bat_entries) || (words[7] >> 26) != 16 ||
      (words[7] & 1))
    return false;
#if CUPID_NATIVE_X64
  BlockKey key;
  key.count = 8;
  std::copy(words.begin(), words.end(), key.words.begin());
  RamControlOperation operation{};
  bool hit = false;
  try {
    if (!impl_->ram_control)
      impl_->ram_control = std::make_unique<RamControlCache>(impl_->block_capacity);
    auto &cache = *impl_->ram_control;
    const bool last_hit = cache.last.operation && cache.last.key == key;
    if (last_hit) {
      operation = cache.last.operation;
      hit = true;
    } else if (const auto found = cache.lookup.find(key); found != cache.lookup.end()) {
      operation = found->second;
      hit = true;
    }
    if (!hit) {
      bool memory = false;
      for (u32 opcode : words.first(7)) {
        const unsigned primary = opcode >> 26;
        if (primary == 32 || primary == 36)
          memory = true;
        else if (!can_group(opcode))
          return false;
      }
      if (!memory)
        return false;
      const auto code = encode_ram_control_sequence(words);
      if (!impl_->block_arena) {
        const auto requested = impl_->sequence_byte_budget;
        const auto page = page_size();
        const auto bytes = (requested + page - 1) / page * page;
        impl_->block_arena = std::make_unique<ExecutablePage>(bytes, requested);
        statistics_.block_executable_bytes = bytes;
      }
      if (!impl_->block_arena->fits(code.code().size(), sequence_alignment)) {
        statistics_.block_replacements += impl_->sequence_count();
        impl_->clear_blocks(false);
        statistics_.block_cache_entries = 0;
      }
      operation.entry =
          impl_->block_arena->append<RamControlOperation::Entry>(code.code(), sequence_alignment);
      cache.lookup.emplace(key, operation);
      statistics_.block_cache_entries = impl_->sequence_count();
      ++statistics_.block_compilations;
    }
    if (!last_hit)
      cache.last = {key, operation};
  } catch (const std::bad_alloc &) {
    impl_->clear_blocks();
    statistics_.block_cache_entries = statistics_.block_executable_bytes = 0;
    ++statistics_.block_compilation_failures;
    return false;
  } catch (const std::system_error &) {
    // Clear every old and new typed address before releasing uncertain storage.
    impl_->clear_blocks();
    statistics_.block_cache_entries = statistics_.block_executable_bytes = 0;
    ++statistics_.block_compilation_failures;
    return false;
  }
  const u32 encoded = invoke_ram(state, context, operation.entry);
  result = {encoded & 255u, (encoded & 256u) != 0};
  statistics_.executed += result.instructions;
  statistics_.block_executed += result.instructions;
  ++statistics_.block_runs;
  if (hit) {
    ++statistics_.block_cache_hits;
    statistics_.cache_hits += result.instructions;
  }
  return true;
#else
  (void)state;
  (void)result;
  return false;
#endif
}

bool NativeInteger::execute_short_ram_control_sequence(
    State &state, std::span<const u32> words, const NativeRamContext &context,
    NativeRamControlRun &result) {
  if ((words.size() != 6 && words.size() != 7) || !context.data || context.bytes < 4 ||
      context.physical_base > 0xffffffffu - (context.bytes - 1) || (state.msr & msr::le) ||
      bool(state.msr & msr::dr) != bool(context.bat_entries) || (words.back() >> 26) != 16 ||
      (words.back() & 1))
    return false;
#if CUPID_NATIVE_X64
  // Default initialization zeroes the unused tail; count separates all three
  // lengths in the shared typed cache. Only executed prefix words are keyed.
  BlockKey key{};
  key.count = static_cast<u32>(words.size());
  std::copy(words.begin(), words.end(), key.words.begin());
  RamControlOperation operation{};
  bool hit = false;
  try {
    if (!impl_->ram_control)
      impl_->ram_control = std::make_unique<RamControlCache>(impl_->block_capacity);
    auto &cache = *impl_->ram_control;
    const bool last_hit = cache.last.operation && cache.last.key == key;
    if (last_hit) {
      operation = cache.last.operation;
      hit = true;
    } else if (const auto found = cache.lookup.find(key); found != cache.lookup.end()) {
      operation = found->second;
      hit = true;
    }
    if (!hit) {
      bool memory = false;
      for (u32 opcode : words.first(words.size() - 1)) {
        const unsigned primary = opcode >> 26;
        if (primary == 32 || primary == 36)
          memory = true;
        else if (!can_group(opcode))
          return false;
      }
      if (!memory)
        return false;
      const auto code = encode_short_ram_control_sequence(words);
      if (!impl_->block_arena) {
        const auto requested = impl_->sequence_byte_budget;
        const auto page = page_size();
        const auto bytes = (requested + page - 1) / page * page;
        impl_->block_arena = std::make_unique<ExecutablePage>(bytes, requested);
        statistics_.block_executable_bytes = bytes;
      }
      if (!impl_->block_arena->fits(code.code().size(), sequence_alignment)) {
        statistics_.block_replacements += impl_->sequence_count();
        impl_->clear_blocks(false);
        statistics_.block_cache_entries = 0;
      }
      operation.entry =
          impl_->block_arena->append<RamControlOperation::Entry>(code.code(), sequence_alignment);
      cache.lookup.emplace(key, operation);
      statistics_.block_cache_entries = impl_->sequence_count();
      ++statistics_.block_compilations;
    }
    if (!last_hit)
      cache.last = {key, operation};
  } catch (const std::bad_alloc &) {
    impl_->clear_blocks();
    statistics_.block_cache_entries = statistics_.block_executable_bytes = 0;
    ++statistics_.block_compilation_failures;
    return false;
  } catch (const std::system_error &) {
    // Clear every old and new typed address before releasing uncertain storage.
    impl_->clear_blocks();
    statistics_.block_cache_entries = statistics_.block_executable_bytes = 0;
    ++statistics_.block_compilation_failures;
    return false;
  }
  const u32 encoded = invoke_ram(state, context, operation.entry);
  result = {encoded & 255u, (encoded & 256u) != 0};
  statistics_.executed += result.instructions;
  statistics_.block_executed += result.instructions;
  ++statistics_.block_runs;
  if (hit) {
    ++statistics_.block_cache_hits;
    statistics_.cache_hits += result.instructions;
  }
  return true;
#else
  (void)state;
  (void)result;
  return false;
#endif
}

NativeIntegerStatistics NativeInteger::statistics() const noexcept {
  return statistics_;
}
} // namespace cupid::cpu
