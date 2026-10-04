// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/memory.hpp"
#include "cupid/cpu/floating.hpp"
#include "cupid/cpu/interpreter.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

using namespace cupid;
using namespace cupid::cpu;

namespace {
class Memory : public Bus {
public:
  std::vector<u8> bytes = std::vector<u8>(1024 * 1024);
  bool unsupported_device = false;

  u8 read8(u32 address) override {
    if (unsupported_device && address == 0x3000)
      throw BusFault(address, false, "Test unfinished device", true);
    if (address >= bytes.size())
      throw BusFault(address, false, "Test memory boundary");
    return bytes[address];
  }

  void write8(u32 address, u8 value) override {
    if (unsupported_device && address == 0x3000)
      throw BusFault(address, true, "Test unfinished device", true);
    if (address >= bytes.size())
      throw BusFault(address, true, "Test memory boundary");
    bytes[address] = value;
  }
};

u32 imm(unsigned op, unsigned d, unsigned a, u16 value) {
  return (op << 26) | (d << 21) | (a << 16) | value;
}

u32 ext(unsigned xo, unsigned d, unsigned a = 0, unsigned b = 0, bool rc = false) {
  return (31u << 26) | (d << 21) | (a << 16) | (b << 11) | (xo << 1) | static_cast<u32>(rc);
}

u32 fp(unsigned xo, unsigned d, unsigned a, unsigned b, unsigned c = 0, unsigned op = 63,
       bool rc = false) {
  return (op << 26) | (d << 21) | (a << 16) | (b << 11) | (c << 6) | (xo << 1) |
         static_cast<u32>(rc);
}

u32 special(unsigned xo, unsigned d, unsigned index) {
  return ext(xo, d) | ((index & 31) << 16) | ((index >> 5) << 11);
}

struct Fixture {
  Memory memory;
  Interpreter cpu{memory};
  State &s = cpu.state();

  u32 run(u32 opcode) {
    // Each injected opcode executes freshly; cache visibility has separate tests.
    s.spr[spr::hid0] &= ~0x8000u;
    s.pc = 0x1000;
    memory.write32(0x1000, opcode);
    return cpu.step();
  }
};

struct Access {
  u32 address, value;
  unsigned bytes;
  bool write;
  bool operator==(const Access &) const = default;
};

class TransactionMemory : public Memory {
public:
  bool recording = false, unsupported_fault = false, change_on_read = false;
  bool external_regions = false, unsupported_error = false;
  unsigned memory_errors = 0;
  std::function<void()> error_hook;
  std::optional<u32> fail_read, fail_write, host_write_error;
  std::vector<Access> accesses;

  u8 read8(u32 address) override {
    log(address, 0, 1, false);
    return Memory::read8(address);
  }

  void write8(u32 address, u8 value) override {
    log(address, value, 1, true);
    if (external_regions && address >= 0x08000000)
      return;
    Memory::write8(address, value);
  }

  void write16(u32 address, u16 value) override {
    log(address, value, 2, true);
    if (external_regions && address >= 0x08000000)
      return;
    Memory::write8(address, static_cast<u8>(value >> 8));
    Memory::write8(address + 1, static_cast<u8>(value));
  }

  u32 read32(u32 address) override {
    log(address, 0, 4, false);
    if (fail_read == address)
      throw BusFault(address, false, "Authored word-read fault", unsupported_fault);
    u32 value = 0;
    for (unsigned n = 0; n < 4; ++n)
      value = (value << 8) | Memory::read8(address + n);
    if (change_on_read && address == 0x2000) {
      const u32 updated = value + 0x01020304;
      for (unsigned n = 0; n < 4; ++n)
        Memory::write8(address + n, static_cast<u8>(updated >> ((3 - n) * 8)));
    }
    return value;
  }

  void write32(u32 address, u32 value) override {
    log(address, value, 4, true);
    if (fail_write == address)
      throw BusFault(address, true, "Authored word-write fault", unsupported_fault);
    if (host_write_error == address)
      throw std::runtime_error("Authored host write failure");
    if (external_regions && address >= 0x08000000)
      return;
    for (unsigned n = 0; n < 4; ++n)
      Memory::write8(address + n, static_cast<u8>(value >> ((3 - n) * 8)));
  }

  void write64(u32 address, u64 value) override {
    log(address, static_cast<u32>(value), 8, true);
    if (external_regions && address >= 0x08000000)
      return;
    for (unsigned n = 0; n < 8; ++n)
      Memory::write8(address + n, static_cast<u8>(value >> ((7 - n) * 8)));
  }

  void memory_access_error() override {
    if (unsupported_error)
      throw BusFault(0, true, "Authored unfinished memory interrupt", true);
    ++memory_errors;
    if (recording)
      accesses.push_back({0, 0, 0, true});
    if (error_hook)
      error_hook();
  }

private:
  void log(u32 address, u32 value, unsigned byte_count, bool write) {
    if (recording && address >= 0x2000)
      accesses.push_back({address, value, byte_count, write});
  }
};

struct TransactionFixture {
  TransactionMemory memory;
  Interpreter cpu{memory};
  State &s = cpu.state();

  u32 run(u32 opcode) {
    memory.recording = false;
    s.pc = 0x1000;
    memory.write32(0x1000, opcode);
    memory.accesses.clear();
    memory.recording = true;
    const u32 cycles = cpu.step();
    memory.recording = false;
    return cycles;
  }
};

class CpuPathMemory : public TransactionMemory {
public:
  std::vector<Access> data_accesses, page_updates;
  bool reject_page_updates = false;

  u64 read_cpu_data(u32 physical, unsigned byte_count) override {
    data_accesses.push_back({physical, 0, byte_count, false});
    return Bus::read_cpu_data(physical, byte_count);
  }

  void write_cpu_data(u32 physical, u32 value, unsigned byte_count) override {
    data_accesses.push_back({physical, value, byte_count, true});
    Bus::write_cpu_data(physical, value, byte_count);
  }

  bool write_page_table32(u32 physical, u32 value) override {
    page_updates.push_back({physical, value, 4, true});
    if (reject_page_updates)
      return false;
    return Bus::write_page_table32(physical, value);
  }
};

void cpu_data_path_tests() {
  CpuPathMemory memory;
  Interpreter cpu(memory);
  auto &state = cpu.state();
  const auto run = [&](u32 opcode) {
    state.pc = 0x1000;
    memory.write32(0x1000, opcode);
    memory.data_accesses.clear();
    memory.page_updates.clear();
    cpu.step();
    CHECK(state.pc == 0x1004);
  };
  memory.write32(0x2000, 0x12345678);
  state.gpr[7] = 0x2000;
  run(imm(32, 9, 7, 0));
  CHECK(state.gpr[9] == 0x12345678);
  CHECK(memory.data_accesses == (std::vector<Access>{{0x2000, 0, 4, false}}));
  run(imm(38, 9, 7, 0));
  CHECK(memory.data_accesses == (std::vector<Access>{{0x2000, 0x12345678, 1, true}}));
  CHECK(memory.read8(0x2000) == 0x78);

  const u32 table = 0x20000 | (((0x123u ^ 3u) << 6) & 0xffff);
  memory.write32(table, 0x80000000 | (0x123u << 7));
  memory.write32(table + 4, 0x2003);
  memory.write32(0x2000, 0x31415926);
  memory.reject_page_updates = true;
  state.sr[4] = 0x123;
  state.spr[spr::sdr1] = 0x20000;
  state.msr = msr::dr;
  state.gpr[7] = 0x40003000;
  run(imm(32, 9, 7, 0));
  CHECK(state.gpr[9] == 0x31415926);
  CHECK(
      memory.data_accesses ==
      (std::vector<Access>{{table, 0, 4, false}, {table + 4, 0, 4, false}, {0x2000, 0, 4, false}}));
  CHECK(memory.page_updates == (std::vector<Access>{{table + 4, 0x2103, 4, true}}));
  CHECK(memory.read32(table + 4) == 0x2003);
  state.gpr[9] = 0xabcdef01;
  run(imm(38, 9, 7, 0));
  CHECK(memory.data_accesses == (std::vector<Access>{{table, 0, 4, false},
                                                     {table + 4, 0, 4, false},
                                                     {0x2000, 0xabcdef01, 1, true}}));
  CHECK(memory.page_updates == (std::vector<Access>{{table + 4, 0x2183, 4, true}}));
  CHECK(memory.read32(table + 4) == 0x2003 && memory.read8(0x2000) == 1);
  run(imm(32, 9, 7, 0));
  CHECK(memory.data_accesses == (std::vector<Access>{{0x2000, 0, 4, false}}));
  CHECK(memory.page_updates.empty() && state.gpr[9] == 0x01415926);

  state.msr = 0;
  state.spr[spr::hid0] = 0x4000;
  state.gpr[7] = 0x2003;
  run(ext(1014, 0, 0, 7));
  std::vector<Access> clearing;
  for (unsigned word = 0; word < 8; ++word)
    clearing.push_back({0x2000u + word * 4, 0, 4, true});
  CHECK(memory.data_accesses == clearing);
  CHECK(memory.page_updates.empty());
}

void physical_data_memory_tests() {
  unsigned cases = 0;
  for (Console console : {Console::GameCube, Console::Wii}) {
    cupid::Memory memory(console);
    unsigned errors = 0;
    memory.memory_error = [&] { ++errors; };
    Interpreter cpu(memory, console);
    auto &state = cpu.state();
    const auto run = [&](u32 opcode) {
      state.pc = 0x1000;
      state.spr[spr::hid0] = 0;
      memory.write32(0x1000, opcode);
      cpu.step();
      CHECK(state.pc == 0x1004);
    };
    state.msr = msr::fp;
    state.spr[spr::dar] = 0x13579bdf;
    state.spr[spr::dsisr] = 0x2468ace0;
    constexpr std::array<u32, 6> boundaries{0x01800000, 0x02000000, 0x04000000,
                                            0x06000000, 0x07800000, 0x08000000};
    for (u32 boundary : boundaries)
      for (int displacement = -8; displacement <= 8; ++displacement)
        for (unsigned width : {1u, 2u, 4u, 8u}) {
          const u32 address = boundary + static_cast<u32>(displacement);
          if (u64(address) + width > 0x08000000 || (width == 8 && (address & 3)))
            continue;
          ++cases;
          u64 expected = 0;
          for (int offset = -4; offset < 16; ++offset)
            memory.write_cpu_data((address + static_cast<u32>(offset)) & 0x01ffffff,
                                  static_cast<u8>(0xa0 + offset), 1);
          for (unsigned byte = 0; byte < width; ++byte)
            expected = (expected << 8) | (0xa0 + byte);
          state.gpr[7] = address;
          const unsigned load_op = width == 1 ? 34 : width == 2 ? 40 : width == 4 ? 32 : 50;
          const unsigned store_op = width == 1 ? 38 : width == 2 ? 44 : width == 4 ? 36 : 54;
          run(imm(load_op, 9, 7, 0));
          CHECK((width == 8 ? state.fpr[9].bits[0] : state.gpr[9]) == expected);
          constexpr u64 payload = 0x123456789abcdef0ull;
          state.gpr[9] = static_cast<u32>(payload);
          state.fpr[9].bits[0] = payload;
          run(imm(store_op, 9, 7, 0));
          for (int offset = -4; offset < 16; ++offset) {
            const u8 value =
                offset >= 0 && static_cast<unsigned>(offset) < width
                    ? static_cast<u8>(payload >> ((width - 1 - static_cast<unsigned>(offset)) * 8))
                    : static_cast<u8>(0xa0 + offset);
            CHECK(memory.read_cpu_data((address + static_cast<u32>(offset)) & 0x01ffffff, 1) ==
                  value);
          }
          CHECK(errors == 0 && state.msr == msr::fp);
          CHECK(state.spr[spr::dar] == 0x13579bdf && state.spr[spr::dsisr] == 0x2468ace0);
        }
    CHECK_THROWS(memory.range(0x01800000, 1));
    CHECK_THROWS(memory.read32(0x01800000));
    CHECK_THROWS(memory.locked_cache_dma(0x01800000, 0, 32, true));

    for (u32 address : {0x10000000u, 0x14000000u, 0x40000020u, 0x80000020u, 0xc0000020u})
      for (unsigned width : {1u, 2u, 4u, 8u}) {
        if (console == Console::Wii && address == 0x10000000)
          continue;
        const unsigned before = errors;
        state.gpr[7] = address;
        state.gpr[9] = 0xabcdef01;
        state.fpr[9].bits[0] = 0x123456789abcdef0ull;
        run(imm(width == 1 ? 34 : width == 2 ? 40 : width == 4 ? 32 : 50, 9, 7, 0));
        CHECK((width == 8 ? state.fpr[9].bits[0] : state.gpr[9]) == 0);
        CHECK(errors == before + 1);
        state.gpr[9] = 0xabcdef01;
        state.fpr[9].bits[0] = 0x123456789abcdef0ull;
        run(imm(width == 1 ? 38 : width == 2 ? 44 : width == 4 ? 36 : 54, 9, 7, 0));
        CHECK(errors == before + 1 + (width == 8 ? 2u : 1u));
        CHECK(state.msr == msr::fp && state.spr[spr::dar] == 0x13579bdf &&
              state.spr[spr::dsisr] == 0x2468ace0);
      }

    // PTE reads use allocated data backing, but R/C updates use strict host bounds.
    const u32 table = 0x01800000 | (((0x321u ^ 7u) << 6) & 0xffff);
    memory.write_cpu_data(table, 0x80000000 | (0x321u << 7), 4);
    memory.write_cpu_data(table + 4, 0x4003, 4);
    memory.write32(0x4000, 0x31415926);
    state.sr[4] = 0x321;
    state.spr[spr::sdr1] = 0x01800000;
    state.msr = msr::dr;
    state.gpr[7] = 0x40007000;
    const unsigned before = errors;
    const u64 rejected_before = memory.page_table_write_errors();
    run(imm(32, 9, 7, 0));
    CHECK(state.gpr[9] == 0x31415926);
    CHECK(memory.read_cpu_data(table + 4, 4) == 0x4003);
    CHECK(memory.page_table_write_errors() == rejected_before + 1);
    state.gpr[9] = 0xabcdef01;
    run(imm(36, 9, 7, 0));
    CHECK(memory.read32(0x4000) == 0xabcdef01);
    CHECK(memory.read_cpu_data(table + 4, 4) == 0x4003 && errors == before);
    CHECK(memory.page_table_write_errors() == rejected_before + 2);
    run(imm(32, 9, 7, 0));
    CHECK(state.gpr[9] == 0xabcdef01 && memory.page_table_write_errors() == rejected_before + 2);
    CHECK(state.msr == msr::dr && state.spr[spr::dar] == 0x13579bdf &&
          state.spr[spr::dsisr] == 0x2468ace0);
  }
  CHECK(cases == 602u);
}

void physical_cache_alias_tests() {
  unsigned cases = 0;
  for (Console console : {Console::GameCube, Console::Wii}) {
    cupid::Memory memory(console);
    Interpreter cpu(memory, console);
    auto &state = cpu.state();
    unsigned errors = 0;
    memory.memory_error = [&] { ++errors; };
    for (bool extended : {false, true}) {
      if (extended && console == Console::GameCube)
        continue;
      const std::array<u32, 6> aliases{extended ? 0x04000000u : 0x02000000u,
                                       extended ? 0x08000000u : 0x04000000u,
                                       extended ? 0x0c000000u : 0x06000000u,
                                       0x40000000u,
                                       0x80000000u,
                                       0xc0000000u};
      for (bool locked : {false, true})
        for (unsigned set : {0u, 1u, 63u, 127u})
          for (unsigned word = 0; word < 8; ++word)
            for (u32 alias : aliases) {
              ++cases;
              cpu.reset();
              const u32 physical =
                  (extended ? 0x10120000u : 0x00120000u) | (set << 5) | (word << 2);
              const u16 old_value = static_cast<u16>(0x80 + cases);
              const u16 new_value = static_cast<u16>(0x4000 + cases);
              memory.write32(physical, imm(14, 9, 0, old_value));
              state.pc = physical;
              state.spr[spr::hid0] = 0x8000;
              cpu.step();
              CHECK(state.pc == physical + 4 && state.gpr[9] == old_value);
              memory.write32(physical, imm(14, 9, 0, new_value));
              state.pc = physical | alias;
              state.gpr[9] = 0x91827364;
              state.spr[spr::hid0] = locked ? 0xa000 : 0x8000;
              cpu.step();
              CHECK(state.pc == (physical | alias) + 4 && state.gpr[9] == old_value);
              CHECK(state.last_opcode == imm(14, 9, 0, old_value));
              CHECK(state.instructions == 2 && errors == 0);

              memory.write32(0x1000, ext(982, 0, 0, 7));
              state.pc = 0x1000;
              state.gpr[7] = physical | alias;
              cpu.step();
              CHECK(state.pc == 0x1004);
              state.pc = physical;
              state.gpr[9] = 0x91827364;
              cpu.step();
              CHECK(state.pc == physical + 4 && state.gpr[9] == new_value);
            }
    }
  }
  CHECK(cases == 1152u);
}

void bat_mapping_boundary_tests() {
  struct Mapping {
    u32 mask, effective_base, physical_base;
    std::array<u32, 8> physical;
  };

  constexpr u32 missing = 0xffffffff;
  constexpr std::array<Mapping, 4> mappings{{
      {0, 0, 0, {0, missing, missing, missing, missing, missing, missing, missing}},
      {1, 1, 0, {missing, 1, missing, missing, missing, missing, missing, missing}},
      {5, 1, 2, {missing, 3, missing, missing, missing, 7, missing, missing}},
      {5, 0, 1, {1, 1, missing, missing, 5, 5, missing, missing}},
  }};
  unsigned cases = 0;
  for (Console console : {Console::GameCube, Console::Wii})
    for (bool extended : {false, true})
      for (unsigned bank = 0; bank < 8; ++bank)
        for (unsigned validity = 0; validity < 4; ++validity)
          for (bool user : {false, true})
            for (bool instruction : {false, true})
              for (const auto &mapping : mappings)
                for (unsigned index = 0; index < 8; ++index) {
                  ++cases;
                  Memory memory;
                  Interpreter cpu(memory, console);
                  auto &state = cpu.state();
                  const u32 address = 0x40000020 | (index << 17);
                  const unsigned first = instruction ? 528 : 536;
                  const unsigned second = instruction ? 560 : 568;
                  const unsigned bat = bank < 4 ? first + bank * 2 : second + (bank - 4) * 2;
                  state.spr[bat] =
                      0x40000000 | (mapping.effective_base << 17) | (mapping.mask << 2) | validity;
                  state.spr[bat + 1] = (mapping.physical_base << 17) | 2;
                  state.spr[spr::hid4] = extended ? 0x02000000 : 0;
                  state.spr[spr::dar] = 0x13579bdf;
                  state.spr[spr::dsisr] = 0x2468ace0;
                  state.sr.fill(0x321);
                  state.msr = (instruction ? msr::ir : msr::dr) | (user ? msr::pr : 0);
                  state.pc = instruction ? address : 0x1000;
                  state.gpr[7] = address;
                  state.gpr[9] = 0x91827364;
                  state.cr = 0x12345678;
                  for (unsigned physical = 0; physical < 8; ++physical)
                    if (instruction)
                      memory.write32((physical << 17) | 0x20,
                                     imm(14, 9, 0, static_cast<u16>(0x80 + physical)));
                    else
                      memory.write8((physical << 17) | 0x20, static_cast<u8>(0x80 + physical));
                  if (!instruction)
                    memory.write32(0x1000, imm(34, 9, 7, 0));
                  const bool enabled = bank < 4 || (console == Console::Wii && extended);
                  const bool translated = enabled && validity && mapping.physical[index] != missing;
                  cpu.step();
                  CHECK(state.gpr[7] == address && state.cr == 0x12345678);
                  if (translated) {
                    CHECK(state.pc == (instruction ? address : 0x1000u) + 4);
                    CHECK(state.gpr[9] == 0x80u + mapping.physical[index]);
                    CHECK(state.spr[spr::dar] == 0x13579bdf && state.spr[spr::dsisr] == 0x2468ace0);
                  } else {
                    CHECK(state.pc == (instruction ? 0x400u : 0x300u));
                    CHECK(state.gpr[9] == 0x91827364);
                    CHECK(state.spr[spr::srr0] == (instruction ? address : 0x1000u));
                    if (instruction) {
                      CHECK((state.spr[spr::srr1] & 0x40000000) != 0);
                      CHECK(state.spr[spr::dar] == 0x13579bdf &&
                            state.spr[spr::dsisr] == 0x2468ace0);
                    } else {
                      CHECK(state.spr[spr::dar] == address && state.spr[spr::dsisr] == 0x40000000);
                    }
                  }
                }
  CHECK(cases == 16384u);
}

void integer_tests() {
  Fixture f;
  CHECK(f.s.spr[spr::pvr] == 0x00083214);
  f.s.gpr[0] = 123;
  f.run(imm(14, 3, 0, 0xfffb));
  CHECK(f.s.gpr[3] == 0xfffffffbu);
  CHECK(f.s.last_pc == 0x1000 && f.s.last_opcode == imm(14, 3, 0, 0xfffb));
  f.run(imm(15, 4, 0, 0x8000));
  CHECK(f.s.gpr[4] == 0x80000000);
  f.s.gpr[5] = 0xffffffff;
  f.run(imm(12, 6, 5, 1));
  CHECK(f.s.gpr[6] == 0 && (f.s.spr[spr::xer] & 0x20000000));
  f.run(imm(13, 6, 5, 0));
  CHECK((f.s.cr >> 28) == 8);
  f.s.gpr[5] = 0x7fffffff;
  f.s.gpr[6] = 1;
  f.run(ext(778, 7, 5, 6, true));
  CHECK(f.s.gpr[7] == 0x80000000);
  CHECK((f.s.spr[spr::xer] & 0xc0000000) == 0xc0000000 && (f.s.cr >> 28) == 9);
  f.run(ext(778, 7, 6, 6));
  CHECK((f.s.spr[spr::xer] & 0xc0000000) == 0x80000000);
  f.s.gpr[5] = 0;
  f.s.gpr[6] = 0;
  f.s.spr[spr::xer] = 0;
  f.run(ext(8, 7, 5, 6));
  CHECK(f.s.gpr[7] == 0 && (f.s.spr[spr::xer] & 0x20000000));
  f.s.gpr[5] = 0xffffffff;
  f.s.gpr[6] = 0;
  f.s.spr[spr::xer] = 0x20000000;
  f.run(ext(138, 7, 5, 6));
  CHECK(f.s.gpr[7] == 0 && (f.s.spr[spr::xer] & 0x20000000));
  f.s.gpr[5] = 0x80000000;
  f.s.gpr[6] = 0xffffffff;
  CHECK(f.run(ext(1003, 7, 5, 6)) == 40);
  CHECK(f.s.gpr[7] == 0xffffffff);
  CHECK((f.s.spr[spr::xer] & 0xc0000000) == 0xc0000000);
  f.s.gpr[5] = 5;
  f.s.gpr[6] = 0;
  f.run(ext(459, 7, 5, 6));
  CHECK(f.s.gpr[7] == 0);
  f.s.gpr[5] = 0xfffffff9;
  f.s.gpr[6] = 3;
  f.run(ext(491, 7, 5, 6));
  CHECK(f.s.gpr[7] == 0xfffffffe);
  f.s.gpr[5] = 0xffffffff;
  f.s.gpr[6] = 2;
  f.run(ext(75, 7, 5, 6));
  CHECK(f.s.gpr[7] == 0xffffffff);
  f.run(ext(11, 7, 5, 6));
  CHECK(f.s.gpr[7] == 1);
  f.s.gpr[5] = 0x80000001;
  f.s.gpr[6] = 31;
  f.run(ext(792, 5, 7, 6));
  CHECK(f.s.gpr[7] == 0xffffffff && (f.s.spr[spr::xer] & 0x20000000));
  f.s.gpr[6] = 32;
  f.run(ext(24, 5, 7, 6));
  CHECK(f.s.gpr[7] == 0);
  f.s.gpr[6] = 64;
  f.run(ext(24, 5, 7, 6));
  CHECK(f.s.gpr[7] == 0x80000001);
  f.run(ext(26, 6, 7));
  CHECK(f.s.gpr[7] == 25);
  f.s.gpr[5] = 0x89abcdef;
  f.run((21u << 26) | (5u << 21) | (7u << 16) | (8u << 11) | (0u << 6) | (7u << 1));
  CHECK(f.s.gpr[7] == 0xab000000);
  f.s.gpr[7] = 0x12345678;
  f.run((20u << 26) | (5u << 21) | (7u << 16) | (0u << 11) | (28u << 6) | (3u << 1));
  CHECK(f.s.gpr[7] == 0x8234567f);
  u32 random = 0x92384983;
  for (unsigned n = 0; n < 1000; ++n) {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    const u32 left = random;
    random = random * 1664525u + 1013904223u;
    const u32 right = random;
    f.s.gpr[5] = left;
    f.s.gpr[6] = right;
    f.run(ext(10, 7, 5, 6));
    CHECK(f.s.gpr[7] == left + right);
    CHECK(((f.s.spr[spr::xer] >> 29) & 1) == ((static_cast<u64>(left) + right) >> 32));
    f.run(ext(8, 7, 5, 6));
    CHECK(f.s.gpr[7] == right - left);
    CHECK(((f.s.spr[spr::xer] >> 29) & 1) == static_cast<u32>(right >= left));
  }
}

void branch_tests() {
  Fixture f;
  f.run(0x48000009);
  CHECK(f.s.pc == 0x1008 && f.s.spr[spr::lr] == 0x1004);
  f.run(0x4bfffffc);
  CHECK(f.s.pc == 0xffc);
  f.s.spr[spr::ctr] = 2;
  f.s.spr[spr::lr] = 0x888;
  f.run((16u << 26) | (16u << 21) | 9);
  CHECK(f.s.pc == 0x1008 && f.s.spr[spr::ctr] == 1);
  f.s.spr[spr::lr] = 0x888;
  f.run((16u << 26) | (16u << 21) | 9);
  CHECK(f.s.pc == 0x1004 && f.s.spr[spr::lr] == 0x888);
  f.s.spr[spr::lr] = 0x2083;
  f.run(0x4e800021);
  CHECK(f.s.pc == 0x2080 && f.s.spr[spr::lr] == 0x1004);
  f.s.cr = 0x20000000;
  f.run((16u << 26) | (12u << 21) | (2u << 16) | 8);
  CHECK(f.s.pc == 0x1008);
  f.run((16u << 26) | (4u << 21) | (2u << 16) | 8);
  CHECK(f.s.pc == 0x1004);
}

void cache_range_loop_tests() {
  Fixture f;
  // Authored line-count calculation uses unrelated registers and public input sizes.
  const u32 divide_by_32 =
      (21u << 26) | (7u << 21) | (7u << 16) | (27u << 11) | (5u << 6) | (31u << 1);
  for (u32 length : {1u, 31u, 32u, 33u, 96u, 0x04000000u, 0x80000000u}) {
    f.s.gpr[7] = length;
    f.run(imm(14, 7, 7, 31));
    f.run(divide_by_32);
    CHECK(f.s.gpr[7] == (length + 31) / 32);
    f.run(special(467, 7, spr::ctr));
    CHECK(f.s.spr[spr::ctr] == (length + 31) / 32);
  }
  f.cpu.reset();
  f.s.spr[spr::hid0] &= ~0x8000u;
  f.s.gpr[7] = 96;
  f.s.gpr[8] = 0x2000;
  const std::array<u32, 6> program{imm(14, 7, 7, 31),         divide_by_32,
                                   special(467, 7, spr::ctr), ext(86, 0, 0, 8),
                                   imm(14, 8, 8, 32),         (16u << 26) | (16u << 21) | 0xfff8};
  for (unsigned i = 0; i < program.size(); ++i)
    f.memory.write32(0x1000 + i * 4, program[i]);
  f.s.pc = 0x1000;
  for (unsigned i = 0; i < 12; ++i)
    f.cpu.step();
  CHECK(f.s.pc == 0x1018 && f.s.gpr[7] == 3 && f.s.gpr[8] == 0x2060);
  CHECK(f.s.spr[spr::ctr] == 0 && f.s.instructions == 12);
}

void memory_tests() {
  Fixture f;
  f.s.gpr[3] = 0x34567abc;
  f.s.gpr[4] = 0x2000;
  f.run(imm(36, 3, 4, 1));
  CHECK(f.memory.read32(0x2001) == 0x34567abc);
  f.run(imm(32, 5, 4, 1));
  CHECK(f.s.gpr[5] == 0x34567abc);
  f.run(imm(33, 5, 4, 1));
  CHECK(f.s.gpr[4] == 0x2001);
  f.s.gpr[4] = 0x2000;
  f.memory.write16(0x2000, 0xff80);
  f.run(imm(42, 5, 4, 0));
  CHECK(f.s.gpr[5] == 0xffffff80);
  f.s.gpr[6] = 0;
  f.run(ext(790, 5, 4, 6));
  CHECK(f.s.gpr[5] == 0x80ff);
  f.memory.write32(0x2000, 0x11223344);
  f.run(ext(20, 5, 4, 6));
  CHECK(f.s.gpr[5] == 0x11223344);
  f.run(ext(150, 3, 4, 6, true));
  CHECK(f.memory.read32(0x2000) == 0x34567abc && (f.s.cr >> 28) == 2);
  f.s.gpr[3] = 0;
  f.run(ext(150, 3, 4, 6, true));
  CHECK(f.memory.read32(0x2000) == 0x34567abc && (f.s.cr >> 28) == 0);
  f.s.gpr[4] = 0x3000;
  f.s.gpr[30] = 5;
  f.s.gpr[31] = 6;
  CHECK(f.run(imm(47, 30, 4, 0)) == 11);
  f.s.gpr[30] = f.s.gpr[31] = 0;
  f.run(imm(46, 30, 4, 0));
  CHECK(f.s.gpr[30] == 5 && f.s.gpr[31] == 6);
  f.s.gpr[4] = 0xffffff00;
  f.s.gpr[5] = 123;
  f.run(imm(33, 5, 4, 0));
  CHECK(f.s.pc == 0x300 && f.s.gpr[5] == 123 && f.s.gpr[4] == 0xffffff00);
  CHECK(f.s.spr[spr::dar] == 0xffffff00 && f.s.spr[spr::srr0] == 0x1000);
  f.s.gpr[4] = 0x2001;
  f.run(ext(20, 5, 4));
  CHECK(f.s.pc == 0x600);
  f.s.gpr[4] = 0x3000;
  f.memory.unsupported_device = true;
  const auto instructions = f.s.instructions;
  bool caught = false;
  try {
    f.run(imm(32, 5, 4, 0));
  } catch (const UnsupportedInstruction &error) {
    caught = true;
    CHECK(error.pc == 0x1000 && error.opcode == imm(32, 5, 4, 0));
  }
  CHECK(caught && f.s.instructions == instructions && f.s.pc == 0x1000);
}

void cache_zero_transaction_tests() {
  for (const bool locked : {false, true}) {
    TransactionFixture f;
    f.s.spr[spr::hid0] = 0x4000;
    f.s.spr[spr::hid2] = 0x30000000;
    f.s.cr = 0x18ab5327;
    const u32 opcode = locked ? fp(1014, 0, 7, 8, 0, 4) : ext(1014, 0, 7, 8);
    for (unsigned offset = 0; offset < 32; ++offset) {
      std::fill(f.memory.bytes.begin() + 0x1ffc, f.memory.bytes.begin() + 0x2024, 0xa5);
      f.s.gpr[7] = 0x2000;
      f.s.gpr[8] = offset;
      CHECK(f.run(opcode) == (locked ? 1u : 5u));
      std::vector<Access> expected;
      for (unsigned word = 0; word < 8; ++word)
        expected.push_back({0x2000 + word * 4, 0, 4, true});
      CHECK(f.memory.accesses == expected);
      CHECK(f.s.pc == 0x1004 && f.s.cr == 0x18ab5327);
      for (unsigned i = 0; i < 32; ++i)
        CHECK(f.memory.bytes[0x2000 + i] == 0);
      CHECK(f.memory.bytes[0x1fff] == 0xa5 && f.memory.bytes[0x2020] == 0xa5);
    }
    f.s.spr[spr::hid0] = 0;
    f.s.gpr[8] = 7;
    f.run(opcode);
    CHECK(f.s.pc == 0x600 && f.s.spr[spr::dar] == 0x2007);
    CHECK(f.memory.accesses.empty());
  }

  TransactionFixture f;
  f.cpu.configure_boot();
  f.s.msr = msr::dr;
  f.s.gpr[7] = 0x80002013;
  f.s.gpr[8] = 0;
  f.run(ext(1014, 0, 7, 8));
  CHECK(f.memory.accesses.size() == 8 && f.memory.accesses.front().address == 0x2000);
  f.s.msr = 0;
  f.s.gpr[7] = 0x2000;
  f.memory.fail_write = 0x2008;
  std::fill(f.memory.bytes.begin() + 0x2000, f.memory.bytes.begin() + 0x2020, 0xa5);
  f.run(ext(1014, 0, 7, 8));
  CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x2008);
  CHECK(f.s.spr[spr::dsisr] == 0x42000000 && f.memory.accesses.size() == 3);
  for (unsigned i = 0; i < 32; ++i)
    CHECK(f.memory.bytes[0x2000 + i] == (i < 8 ? 0 : 0xa5));
  f.memory.fail_write = 0x2000;
  f.memory.unsupported_fault = true;
  const auto cycles = f.s.cycles, instructions = f.s.instructions;
  const u32 dar = f.s.spr[spr::dar], dsisr = f.s.spr[spr::dsisr];
  CHECK_THROWS(f.run(ext(1014, 0, 7, 8)));
  CHECK(f.s.pc == 0x1000 && f.s.cycles == cycles && f.s.instructions == instructions);
  CHECK(f.s.spr[spr::dar] == dar && f.s.spr[spr::dsisr] == dsisr);
}

void string_store_transaction_tests() {
  TransactionFixture f;
  const auto check_case = [&](unsigned first, unsigned offset, unsigned count, bool indexed) {
    std::fill(f.memory.bytes.begin() + 0x1ffc, f.memory.bytes.begin() + 0x2090, 0xa5);
    for (unsigned reg = 0; reg < 32; ++reg)
      f.s.gpr[reg] = 0x3927c15b ^ (0x01458329u * reg);
    f.s.gpr[7] = 0x2000 + offset;
    f.s.gpr[8] = 0;
    f.s.spr[spr::xer] = count;
    f.s.cr = 0x1734568a;
    const auto registers = f.s.gpr;
    std::array<u8, 144> expected_bytes;
    expected_bytes.fill(0xa5);
    for (unsigned byte = 0; byte < count; ++byte)
      expected_bytes[offset + byte] =
          static_cast<u8>(registers[(first + byte / 4) & 31] >> ((3 - (byte & 3)) * 8));
    std::vector<Access> expected_accesses;
    if (offset)
      expected_accesses.push_back({0x2000, 0, 4, false});
    const unsigned words = (offset + count + 3) / 4;
    for (unsigned word = 0; word < words; ++word) {
      const u32 address = 0x2000 + word * 4;
      if (word + 1 == words && (offset + count) % 4)
        expected_accesses.push_back({address, 0, 4, false});
      u32 value = 0;
      for (unsigned byte = 0; byte < 4; ++byte)
        value = (value << 8) | expected_bytes[word * 4 + byte];
      expected_accesses.push_back({address, value, 4, true});
    }
    const u32 opcode = indexed ? ext(661, first, 7, 8) : ext(725, first, 7, count & 31);
    CHECK(f.run(opcode) == 1);
    CHECK(f.memory.accesses == expected_accesses);
    CHECK(f.s.gpr == registers && f.s.cr == 0x1734568a && f.s.pc == 0x1004);
    CHECK(f.memory.bytes[0x1fff] == 0xa5);
    for (unsigned byte = 0; byte < expected_bytes.size(); ++byte)
      CHECK(f.memory.bytes[0x2000 + byte] == expected_bytes[byte]);
  };
  for (unsigned first = 0; first < 32; ++first)
    for (unsigned offset = 0; offset < 4; ++offset) {
      for (unsigned count = 0; count < 128; ++count)
        check_case(first, offset, count, true);
      for (unsigned count = 1; count <= 32; ++count)
        check_case(first, offset, count, false);
    }

  // A misaligned zero-count store has separate prefix/suffix read side effects.
  for (unsigned offset = 1; offset < 4; ++offset) {
    f.memory.change_on_read = false;
    f.memory.write32(0x2000, 0x12345678);
    f.memory.change_on_read = true;
    f.s.gpr[7] = 0x2000 + offset;
    f.s.spr[spr::xer] = 0;
    f.run(ext(661, 29, 7, 8));
    f.memory.change_on_read = false;
    const u32 prefix_mask = ~u32{0} << ((4 - offset) * 8);
    const u32 merged = (0x12345678u & prefix_mask) | (0x1336597cu & ~prefix_mask);
    CHECK(f.memory.read32(0x2000) == merged);
    CHECK((f.memory.accesses == std::vector<Access>{{0x2000, 0, 4, false},
                                                    {0x2000, 0, 4, false},
                                                    {0x2000, merged, 4, true}}));
  }

  f.s.gpr[7] = 0x2001;
  f.s.gpr[8] = 0;
  f.s.gpr[29] = 0x12345678;
  f.memory.fail_read = 0x2000;
  f.run(ext(725, 29, 7, 1));
  CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x2000);
  CHECK(f.s.spr[spr::dsisr] == 0x40000000 && f.memory.accesses.size() == 1);
  f.memory.fail_read = 0x2004;
  f.s.gpr[7] = 0x2000;
  f.s.gpr[30] = 0x87654321;
  f.memory.write32(0x2000, 0);
  f.memory.write32(0x2004, 0xaabbccdd);
  f.run(ext(725, 29, 7, 5));
  CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x2004);
  CHECK(f.memory.read32(0x2000) == 0x12345678);
  f.memory.fail_read.reset();
  CHECK(f.memory.read32(0x2004) == 0xaabbccdd);
  f.memory.fail_write = 0x2000;
  f.memory.unsupported_fault = true;
  const auto cycles = f.s.cycles, instructions = f.s.instructions;
  const u32 dar = f.s.spr[spr::dar], dsisr = f.s.spr[spr::dsisr];
  CHECK_THROWS(f.run(ext(725, 29, 7, 4)));
  CHECK(f.s.pc == 0x1000 && f.s.cycles == cycles && f.s.instructions == instructions);
  CHECK(f.s.spr[spr::dar] == dar && f.s.spr[spr::dsisr] == dsisr);
}

void uncached_write_tests() {
  TransactionFixture f;
  f.s.msr = msr::dr;
  f.s.spr[536] = 0x80000003;
  const auto rotated_word = [](u32 value, unsigned bytes) {
    std::array<u8, 4> source{};
    for (unsigned n = 0; n < 4; ++n)
      source[n] = static_cast<u8>(value >> ((3 - n) * 8));
    u32 result = 0;
    for (unsigned n = 0; n < 4; ++n)
      result = (result << 8) | source[(n + 4 - (bytes & 3)) & 3];
    return result;
  };
  for (unsigned attributes = 0; attributes < 16; ++attributes)
    for (unsigned width : {1u, 2u, 4u})
      for (unsigned offset = 0; offset < 16; ++offset)
        for (u32 value : {0xa1b2c3d4u, 0x80000001u, 0x000000ffu, 0x6e3907b4u})
          for (const bool update : {false, true}) {
            std::fill(f.memory.bytes.begin() + 0x1ffc, f.memory.bytes.begin() + 0x2020, 0xa5);
            f.s.spr[537] = (attributes << 3) | 2;
            f.s.gpr[7] = 0x80002000 + offset;
            f.s.gpr[9] = value;
            f.memory.memory_errors = 0;
            std::array<u8, 32> expected_bytes;
            expected_bytes.fill(0xa5);
            std::vector<Access> expected_accesses;
            const bool duplicated = (attributes & 12) && (width < 4 || (offset & 3));
            if (duplicated) {
              expected_accesses.push_back({0, 0, 0, true});
              const u32 word = rotated_word(value, (offset & 3) + width);
              for (unsigned address = offset & ~7u; address < ((offset + width + 7) & ~7u);
                   address += 4) {
                expected_accesses.push_back({0x2000 + address, word, 4, true});
                for (unsigned byte = 0; byte < 4; ++byte)
                  expected_bytes[address + byte] = static_cast<u8>(word >> ((3 - byte) * 8));
              }
            } else {
              const u32 mask = width == 4 ? ~u32{0} : (u32{1} << (width * 8)) - 1;
              expected_accesses.push_back({0x2000 + offset, value & mask, width, true});
              for (unsigned byte = 0; byte < width; ++byte)
                expected_bytes[offset + byte] = static_cast<u8>(value >> ((width - 1 - byte) * 8));
            }
            const unsigned operation = width == 1 ? 38 : width == 2 ? 44 : 36;
            f.run(imm(operation + static_cast<unsigned>(update), 9, 7, 0));
            CHECK(f.s.pc == 0x1004 && f.s.msr == msr::dr);
            CHECK(f.s.gpr[7] == 0x80002000 + offset && f.s.gpr[9] == value);
            CHECK(f.memory.memory_errors == static_cast<unsigned>(duplicated));
            CHECK(f.memory.accesses == expected_accesses);
            CHECK(f.memory.bytes[0x1fff] == 0xa5);
            for (unsigned byte = 0; byte < expected_bytes.size(); ++byte)
              CHECK(f.memory.bytes[0x2000 + byte] == expected_bytes[byte]);
          }

  f.s.spr[537] = 0x22;
  f.s.gpr[7] = 0x80002001;
  f.s.gpr[8] = 0;
  f.s.gpr[9] = 0xa1b2c3d4;
  f.run(ext(918, 9, 7, 8));
  CHECK(f.memory.read32(0x2000) == 0xb2d4c3a1 && f.memory.read32(0x2004) == 0xb2d4c3a1);
  CHECK((f.memory.accesses == std::vector<Access>{{0, 0, 0, true},
                                                  {0x2000, 0xb2d4c3a1, 4, true},
                                                  {0x2004, 0xb2d4c3a1, 4, true}}));

  // A higher overlapping BAT determines both the address and its attributes.
  f.s.spr[538] = 0x80000003;
  f.s.spr[539] = 2;
  f.memory.memory_errors = 0;
  f.run(imm(38, 9, 7, 0));
  CHECK(f.memory.memory_errors == 0);
  f.s.spr[537] = 2;
  f.s.spr[539] = 0x42;
  f.run(imm(38, 9, 7, 0));
  CHECK(f.memory.memory_errors == 1);
  f.s.spr[538] = 0;

  // An untranslated store ignores attributes in inactive BATs.
  f.s.msr = 0;
  f.s.gpr[7] = 0x2001;
  f.memory.memory_errors = 0;
  f.run(imm(38, 9, 7, 0));
  CHECK(f.memory.memory_errors == 0);

  // PI is asserted before the first duplicated physical write.
  f.s.msr = msr::dr | msr::ee;
  f.s.spr[537] = 0x22;
  f.s.gpr[7] = 0x80002000;
  std::fill(f.memory.bytes.begin() + 0x2000, f.memory.bytes.begin() + 0x2008, 0xa5);
  f.memory.error_hook = [&] {
    CHECK(f.memory.bytes[0x2000] == 0xa5 && f.memory.bytes[0x2007] == 0xa5);
    f.cpu.set_external_interrupt(true);
  };
  f.run(imm(38, 9, 7, 0));
  CHECK(f.s.pc == 0x1004 && f.s.msr == (msr::dr | msr::ee));
  f.cpu.step();
  CHECK(f.s.pc == 0x500 && f.s.spr[spr::srr0] == 0x1004);
  CHECK(f.s.spr[spr::srr1] == (msr::dr | msr::ee));
  f.cpu.set_external_interrupt(false);
  f.memory.error_hook = {};

  f.s.msr = msr::dr;
  f.memory.unsupported_error = true;
  const auto cycles = f.s.cycles, instructions = f.s.instructions;
  const u32 dar = f.s.spr[spr::dar], dsisr = f.s.spr[spr::dsisr];
  CHECK_THROWS(f.run(imm(38, 9, 7, 0)));
  CHECK(f.memory.accesses.empty() && f.s.pc == 0x1000);
  CHECK(f.s.cycles == cycles && f.s.instructions == instructions && f.s.msr == msr::dr);
  CHECK(f.s.spr[spr::dar] == dar && f.s.spr[spr::dsisr] == dsisr);
}

void page_write_attribute_result_tests() {
  for (unsigned attributes = 0; attributes < 16; ++attributes)
    for (unsigned width : {1u, 2u, 4u})
      for (unsigned offset = 0; offset < 8; ++offset)
        for (bool update : {false, true}) {
          TransactionFixture f;
          f.s.msr = msr::dr;
          f.s.spr[spr::sdr1] = 0x20000;
          f.s.sr[4] = 0x123;
          const u32 pteg = 0x20000 | (((0x123u ^ 3u) << 6) & 0xffff);
          f.memory.write32(pteg, 0x80000000 | (0x123 << 7));
          f.memory.write32(pteg + 4, 0x5003 | (attributes << 3));
          std::fill(f.memory.bytes.begin() + 0x5000, f.memory.bytes.begin() + 0x5010, 0xa5);
          const u32 address = 0x40003000 + offset;
          f.s.gpr[7] = address - 5;
          f.s.gpr[9] = 0xa1b2c3d4;
          f.s.cr = 0x18ab4267;
          const unsigned operation = width == 1 ? 38 : width == 2 ? 44 : 36;
          f.run(imm(operation + static_cast<unsigned>(update), 9, 7, 5));
          CHECK(f.s.pc == 0x1004 && f.memory.memory_errors == 0);
          CHECK(f.s.gpr[7] == (update ? address : address - 5) && f.s.gpr[9] == 0xa1b2c3d4);
          CHECK(f.s.cr == 0x18ab4267);
          CHECK(f.memory.read32(pteg + 4) == (0x5183u | (attributes << 3)));
          std::vector<Access> data;
          for (const auto &access : f.memory.accesses)
            if (access.address < 0x10000)
              data.push_back(access);
          const u32 value = width == 4 ? 0xa1b2c3d4u : width == 2 ? 0xc3d4u : 0xd4u;
          CHECK((data == std::vector<Access>{{0x5000 + offset, value, width, true}}));
          for (unsigned n = 0; n < 16; ++n) {
            const u8 expected =
                n >= offset && n < offset + width
                    ? static_cast<u8>(0xa1b2c3d4u >> ((width - 1 - (n - offset)) * 8))
                    : u8{0xa5};
            CHECK(f.memory.bytes[0x5000 + n] == expected);
          }
        }
}

void uncached_translation_tests() {
  for (const bool old_attribute : {false, true}) {
    TransactionFixture f;
    f.s.msr = msr::dr;
    f.s.spr[spr::sdr1] = 0x20000;
    f.s.sr[4] = 0x123;
    const u32 pteg = 0x20000 | (((0x123u ^ 3u) << 6) & 0xffff);
    f.memory.write32(pteg, 0x80000000 | (0x123 << 7));
    f.memory.write32(pteg + 4, 0x5002 | (old_attribute ? 0x20u : 0));
    f.s.gpr[7] = 0x40003001;
    f.s.gpr[9] = 0xa1b2c3d4;
    f.run(imm(34, 6, 7, 0)); // Cache the original attributes with C clear.
    CHECK((f.memory.read32(pteg + 4) & 0x180) == 0x100);
    f.memory.write32(pteg + 4, 0x5102 | (old_attribute ? 0 : 0x40u));
    const auto check_write = [&] {
      std::fill(f.memory.bytes.begin() + 0x5000, f.memory.bytes.begin() + 0x5008, 0xa5);
      f.memory.memory_errors = 0;
      f.run(imm(38, 9, 7, 0));
      CHECK(f.memory.memory_errors == 0);
      CHECK(f.memory.read32(0x5000) == 0xa5d4a5a5u);
      CHECK(f.memory.read32(0x5004) == 0xa5a5a5a5u);
    };
    check_write(); // C reparsing keeps normal writes through the returned page result.
    CHECK((f.memory.read32(pteg + 4) & 0x180) == 0x180);
    check_write(); // Cached PTE attributes likewise do not reach the physical writer.
    f.run(ext(306, 0, 0, 7));
    check_write();
  }

  TransactionFixture f;
  f.s.msr = msr::dr;
  f.s.spr[spr::sdr1] = 0x20000;
  f.s.sr[4] = 0x123;
  for (unsigned page = 7; page < 9; ++page) {
    const u32 pteg = 0x20000 | (((0x123u ^ page) << 6) & 0xffff);
    f.memory.write32(pteg, 0x80000000 | (0x123 << 7));
    f.memory.write32(pteg + 4, (page == 7 ? 0x4000u : 0x8000u) | 0x1a2);
  }
  f.s.gpr[7] = 0x40007fff;
  f.s.gpr[9] = 0xa1b2c3d4;
  f.run(imm(36, 9, 7, 0));
  std::vector<Access> data_accesses;
  for (const auto &access : f.memory.accesses)
    if (access.address < 0x10000)
      data_accesses.push_back(access);
  CHECK((data_accesses == std::vector<Access>{{0x4fff, 0xa1, 1, true},
                                              {0x8000, 0xb2, 1, true},
                                              {0x8001, 0xc3, 1, true},
                                              {0x8002, 0xd4, 1, true}}));
  const u32 second_pteg = 0x20000 | (((0x123u ^ 8u) << 6) & 0xffff);
  f.memory.write32(second_pteg, 0);
  f.s.gpr[8] = 0x40008000;
  f.run(ext(306, 0, 0, 8));
  f.run(imm(37, 9, 7, 0));
  CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x40008000);
  CHECK(f.s.spr[spr::dsisr] == 0x42000000 && f.s.gpr[7] == 0x40007fff);
  CHECK(f.memory.read32(0x4ff8) == 0 && f.memory.read32(0x4ffc) == 0xa1);
}

void uncached_region_tests() {
  TransactionFixture f;
  f.memory.external_regions = true;
  f.s.msr = msr::dr | msr::fp;
  f.s.spr[536] = 0x80000003;
  f.s.gpr[9] = 0xa1b2c3d4;
  // These physical regions precede RAM duplication in the actual caller.
  for (u32 address : {0x0c008031u, 0x0c006001u, 0x08002001u, 0xe0002001u}) {
    f.s.spr[537] = (address & 0xfffe0000) | 0x62;
    f.s.gpr[7] = 0x80000000 | (address & 0x1ffff);
    f.memory.memory_errors = 0;
    f.run(imm(38, 9, 7, 0));
    CHECK(f.memory.memory_errors == 0);
    CHECK((f.memory.accesses == std::vector<Access>{{address, 0xd4, 1, true}}));
  }
  f.s.spr[537] = 0x10000062;
  f.s.gpr[7] = 0x80002001;
  f.run(imm(38, 9, 7, 0));
  CHECK((f.memory.accesses == std::vector<Access>{{0, 0, 0, true},
                                                  {0x10002000, 0xc3d4a1b2, 4, true},
                                                  {0x10002004, 0xc3d4a1b2, 4, true}}));
  f.s.spr[537] = 0x62;
  f.s.gpr[7] = 0x80002004;
  f.s.fpr[9].bits[0] = 0x12345678a1b2c3d4ull;
  f.run(imm(54, 9, 7, 0));
  CHECK((f.memory.accesses ==
         std::vector<Access>{{0x2004, 0x12345678, 4, true}, {0x2008, 0xa1b2c3d4, 4, true}}));
  f.s.spr[537] = 0x0c000062;
  f.s.gpr[7] = 0x80008004;
  f.run(imm(54, 9, 7, 0));
  CHECK((f.memory.accesses == std::vector<Access>{{0x0c008004, 0x12345678, 4, true},
                                                  {0x0c008008, 0xa1b2c3d4, 4, true}}));
  f.s.spr[537] = 0x08000062;
  f.s.gpr[7] = 0x80002000;
  const auto cycles = f.s.cycles, instructions = f.s.instructions;
  const u32 dar = f.s.spr[spr::dar], dsisr = f.s.spr[spr::dsisr];
  CHECK_THROWS(f.run(imm(54, 9, 7, 0)));
  CHECK(f.memory.accesses.empty() && f.s.pc == 0x1000);
  CHECK(f.s.cycles == cycles && f.s.instructions == instructions);
  CHECK(f.s.spr[spr::dar] == dar && f.s.spr[spr::dsisr] == dsisr);
}

void split_store_fault_tests() {
  TransactionFixture f;
  f.s.msr = msr::dr | msr::fp;
  f.s.spr[spr::sdr1] = 0x20000;
  f.s.sr[4] = 0x123;
  const u32 second_pteg = 0x20000 | (((0x123u ^ 8u) << 6) & 0xffff);
  f.memory.write32(second_pteg, 0x80000000 | (0x123 << 7));
  f.memory.write32(second_pteg + 4, 0x8003);
  f.s.gpr[7] = 0x40007fff;
  f.s.gpr[9] = 0xa1b2c3d4;
  f.run(imm(37, 9, 7, 0));
  CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x40007fff);
  CHECK(f.s.spr[spr::dsisr] == 0x42000000 && f.s.gpr[7] == 0x40007fff);
  CHECK(f.s.spr[spr::srr1] == (msr::dr | msr::fp));
  CHECK(f.memory.read32(second_pteg + 4) == 0x8183);
  CHECK(f.memory.bytes[0x8000] == 0xb2 && f.memory.bytes[0x8001] == 0xc3 &&
        f.memory.bytes[0x8002] == 0xd4);

  f.s.msr = msr::dr | msr::fp;
  f.s.gpr[7] = 0x40007ffc;
  f.s.fpr[9].bits[0] = 0x12345678a1b2c3d4ull;
  f.memory.write32(0x8000, 0);
  f.run(imm(55, 9, 7, 0));
  CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x40007ffc);
  CHECK(f.s.gpr[7] == 0x40007ffc && f.memory.read32(0x8000) == 0xa1b2c3d4);
  CHECK(f.s.spr[spr::srr1] == (msr::dr | msr::fp));

  f.s.msr = msr::dr;
  f.memory.write32(second_pteg, 0);
  f.s.gpr[8] = 0x40008000;
  f.run(ext(306, 0, 0, 8));
  f.s.gpr[7] = 0x40007fff;
  f.run(imm(36, 9, 7, 0));
  CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x40008000);
  CHECK(f.s.spr[spr::srr1] == msr::dr);
  f.s.msr = msr::dr | msr::fp;
  f.s.gpr[7] = 0x40007ffc;
  f.run(imm(54, 9, 7, 0));
  CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x40008000);
  CHECK(f.s.spr[spr::srr1] == (msr::dr | msr::fp));

  // An unfinished bus operation and a host error stop the second half.
  f.cpu.reset();
  f.s.msr = msr::dr | msr::fp;
  f.s.spr[536] = 0x80000003;
  f.s.spr[537] = 2;
  f.s.gpr[7] = 0x80002000;
  f.s.fpr[9].bits[0] = 0x12345678a1b2c3d4ull;
  f.memory.fail_write = 0x2000;
  f.memory.unsupported_fault = true;
  const auto cycles = f.s.cycles, instructions = f.s.instructions;
  CHECK_THROWS(f.run(imm(54, 9, 7, 0)));
  CHECK((f.memory.accesses == std::vector<Access>{{0x2000, 0x12345678, 4, true}}));
  CHECK(f.s.pc == 0x1000 && f.s.cycles == cycles && f.s.instructions == instructions);
  CHECK(f.s.spr[spr::dar] == 0 && f.s.spr[spr::dsisr] == 0);
  f.memory.fail_write.reset();
  f.memory.host_write_error = 0x2000;
  CHECK_THROWS(f.run(imm(54, 9, 7, 0)));
  CHECK((f.memory.accesses == std::vector<Access>{{0x2000, 0x12345678, 4, true}}));
  CHECK(f.s.pc == 0x1000 && f.s.cycles == cycles && f.s.instructions == instructions);
  CHECK(f.s.spr[spr::dar] == 0 && f.s.spr[spr::dsisr] == 0);
  f.memory.host_write_error.reset();
  f.memory.fail_write = 0x2000;
  f.memory.unsupported_fault = false;
  f.run(imm(54, 9, 7, 0));
  CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x80002000);
  CHECK(f.s.spr[spr::srr1] == (msr::dr | msr::fp));
  CHECK(f.memory.read32(0x2004) == 0xa1b2c3d4);

  // A storage fault after the first duplicated word retains the PI request.
  f.cpu.reset();
  f.s.msr = msr::dr;
  f.s.spr[536] = 0x80000003;
  f.s.spr[537] = 0x22;
  f.s.gpr[7] = 0x80002001;
  f.memory.fail_write = 0x2004;
  f.memory.memory_errors = 0;
  std::fill(f.memory.bytes.begin() + 0x2000, f.memory.bytes.begin() + 0x2008, 0xa5);
  f.run(imm(38, 9, 7, 0));
  CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x80002001);
  CHECK(f.memory.memory_errors == 1);
  CHECK(f.memory.read32(0x2000) == 0 && f.memory.read32(0x2004) == 0xa5a5a5a5);
}

void changed_mapping_transaction_tests() {
  for (const bool locked : {false, true}) {
    TransactionFixture f;
    f.s.msr = msr::dr;
    f.s.spr[spr::hid0] = 0x4000;
    f.s.spr[spr::hid2] = 0x30000000;
    f.s.spr[spr::sdr1] = 0x20000;
    f.s.sr[4] = 0x123;
    const u32 pteg = 0x20000 | (((0x123u ^ 3u) << 6) & 0xffff);
    f.memory.write32(pteg, 0x80000000 | (0x123 << 7));
    f.memory.write32(pteg + 4, 0x5002);
    f.s.gpr[7] = 0x40003013;
    f.run(imm(34, 6, 7, 0));
    f.memory.write32(pteg + 4, 0x6002);
    std::fill(f.memory.bytes.begin() + 0x5000, f.memory.bytes.begin() + 0x5020, 0xa5);
    std::fill(f.memory.bytes.begin() + 0x6000, f.memory.bytes.begin() + 0x6020, 0xa5);
    f.run(locked ? fp(1014, 0, 7, 8, 0, 4) : ext(1014, 0, 7, 8));
    for (unsigned byte = 0; byte < 32; ++byte) {
      CHECK(f.memory.bytes[0x5000 + byte] == 0xa5);
      CHECK(f.memory.bytes[0x6000 + byte] == 0);
    }
  }

  TransactionFixture f;
  f.s.msr = msr::dr | msr::fp;
  f.s.spr[spr::sdr1] = 0x20000;
  f.s.sr[4] = 0x123;
  const u32 pteg = 0x20000 | (((0x123u ^ 3u) << 6) & 0xffff);
  f.memory.write32(pteg, 0x80000000 | (0x123 << 7));
  f.memory.write32(pteg + 4, 0x5002);
  f.s.gpr[7] = 0x40003004;
  f.run(imm(32, 6, 7, 0));
  f.memory.write32(pteg + 4, 0x6002);
  f.s.fpr[9].bits[0] = 0x12345678a1b2c3d4ull;
  f.run(imm(54, 9, 7, 0));
  CHECK(f.memory.read32(0x6004) == 0x12345678);
  CHECK(f.memory.read32(0x5008) == 0xa1b2c3d4);
  CHECK(f.memory.read32(0x5004) == 0 && f.memory.read32(0x6008) == 0);
}

void paired_page_store_tests() {
  const auto prepare = [](TransactionFixture &f, bool first_page, bool second_page, bool first_wi,
                          bool second_wi) {
    f.s.msr = msr::dr | msr::fp;
    f.s.spr[spr::hid2] = 0xa0000000;
    f.s.spr[spr::sdr1] = 0x20000;
    f.s.sr[4] = 0x123;
    f.s.fpr[9].set(1.0);
    f.s.fpr[9].set(2.0, 1);
    for (unsigned page = 7; page < 9; ++page) {
      const bool first = page == 7, mapped = first ? first_page : second_page;
      const bool wi = first ? first_wi : second_wi;
      const u32 pteg = 0x20000 | (((0x123u ^ page) << 6) & 0xffff);
      f.memory.write32(pteg, mapped ? 0x80000000 | (0x123 << 7) : 0);
      f.memory.write32(pteg + 4, (first ? 0x4000u : 0x8000u) | 2 | (wi ? 0x20u : 0));
    }
    std::fill(f.memory.bytes.begin() + 0x4fe0, f.memory.bytes.begin() + 0x5010, 0xa5);
    std::fill(f.memory.bytes.begin() + 0x8000, f.memory.bytes.begin() + 0x8010, 0xa5);
  };
  constexpr std::array<u8, 8> payload{0x3f, 0x80, 0, 0, 0x40, 0, 0, 0};
  for (unsigned distance = 1; distance < 8; ++distance) {
    TransactionFixture f;
    prepare(f, true, true, false, false);
    f.s.gpr[7] = 0x40008000 - distance;
    f.run(imm(60, 9, 7, 0));
    CHECK(f.s.pc == 0x1004 && f.memory.memory_errors == 0);
    for (unsigned byte = 0; byte < payload.size(); ++byte) {
      const u32 physical = byte < distance ? 0x5000 - distance + byte : 0x8000 + byte - distance;
      CHECK(f.memory.bytes[physical] == payload[byte]);
    }
    CHECK(f.memory.bytes[0x5000] == 0xa5 && f.memory.bytes[0x8000 + 8 - distance] == 0xa5);
  }

  for (const bool first_wi : {false, true})
    for (const bool second_wi : {false, true}) {
      TransactionFixture f;
      prepare(f, true, true, first_wi, second_wi);
      f.s.gpr[7] = 0x40007ffd;
      f.run(imm(60, 9, 7, 0));
      CHECK(f.s.pc == 0x1004);
      CHECK(f.memory.memory_errors == 0);
      CHECK(f.memory.bytes[0x4ffd] == 0x3f && f.memory.bytes[0x4ffe] == 0x80 &&
            f.memory.bytes[0x4fff] == 0);
      for (unsigned byte = 3; byte < payload.size(); ++byte)
        CHECK(f.memory.bytes[0x8000 + byte - 3] == payload[byte]);
      CHECK(f.memory.bytes[0x5000] == 0xa5 && f.memory.bytes[0x8008] == 0xa5);
    }

  for (const bool second_wi : {false, true}) {
    TransactionFixture f;
    prepare(f, false, true, false, second_wi);
    f.s.gpr[7] = 0x40007ffd;
    f.run(imm(61, 9, 7, 0));
    CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x40007ffd);
    CHECK(f.s.gpr[7] == 0x40007ffd && f.s.spr[spr::dsisr] == 0x42000000);
    CHECK(f.s.spr[spr::srr1] == (msr::dr | msr::fp));
    CHECK(f.memory.memory_errors == 0);
    for (unsigned byte = 3; byte < payload.size(); ++byte)
      CHECK(f.memory.bytes[0x8000 + byte - 3] == payload[byte]);
    const u32 pteg = 0x20000 | (((0x123u ^ 8u) << 6) & 0xffff);
    CHECK((f.memory.read32(pteg + 4) & 0x180) == 0x180);
  }
  for (const bool first_page : {false, true}) {
    TransactionFixture f;
    prepare(f, first_page, false, false, false);
    f.s.gpr[7] = 0x40007ffd;
    f.run(imm(61, 9, 7, 0));
    CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x40008001);
    CHECK(f.s.gpr[7] == 0x40007ffd && f.s.spr[spr::dsisr] == 0x42000000);
    CHECK(f.s.spr[spr::srr1] == (msr::dr | msr::fp));
    if (first_page)
      CHECK(f.memory.bytes[0x4ffd] == 0x3f && f.memory.bytes[0x4ffe] == 0x80 &&
            f.memory.bytes[0x4fff] == 0);
    else
      CHECK(f.memory.read32(0x4ffc) == 0xa5a5a5a5);
    CHECK(f.memory.bytes[0x5000] == 0xa5 && f.memory.read32(0x8000) == 0xa5a5a5a5);
  }
}

class ReadTransactionMemory : public TransactionMemory {
public:
  std::vector<u32> failed_bytes;
  std::optional<u32> host_read_error;

  u8 read8(u32 address) override {
    const u8 value = TransactionMemory::read8(address);
    if (std::find(failed_bytes.begin(), failed_bytes.end(), address) != failed_bytes.end())
      throw BusFault(address, false, "Authored byte-read fault", unsupported_fault);
    if (host_read_error == address)
      throw std::runtime_error("Authored host read failure");
    return value;
  }

  u16 read16(u32 address) override {
    if (recording && address >= 0x2000)
      accesses.push_back({address, 0, 2, false});
    return static_cast<u16>((u16{Memory::read8(address)} << 8) | Memory::read8(address + 1));
  }

  u64 read64(u32 address) override {
    if (recording && address >= 0x2000)
      accesses.push_back({address, 0, 8, false});
    u64 value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = (value << 8) | Memory::read8(address + n);
    return value;
  }
};

struct ReadTransactionFixture {
  ReadTransactionMemory memory;
  Interpreter cpu{memory};
  State &s = cpu.state();

  void run(u32 opcode) {
    memory.recording = false;
    s.pc = 0x1000;
    memory.write32(0x1000, opcode);
    memory.accesses.clear();
    memory.recording = true;
    cpu.step();
    memory.recording = false;
  }
};

u32 load_page_table_group(unsigned page) {
  return 0x20000 | (((0x123u ^ page) << 6) & 0xffff);
}

void prepare_split_load(ReadTransactionFixture &f, bool first, bool second, u32 first_attribute = 0,
                        u32 second_attribute = 0) {
  f.s.msr = msr::dr | msr::fp | msr::pr | msr::ri;
  f.s.spr[spr::hid2] = 0xa0000000;
  f.s.spr[spr::sdr1] = 0x20000;
  f.s.sr[4] = 0x123;
  for (unsigned page = 7; page < 9; ++page) {
    const bool mapped = page == 7 ? first : second;
    f.memory.write32(load_page_table_group(page), mapped ? 0x80000000 | (0x123 << 7) : 0);
    f.memory.write32(load_page_table_group(page) + 4,
                     (page == 7 ? 0x4000u : 0x8000u) | 3 |
                         (page == 7 ? first_attribute : second_attribute));
  }
  f.s.gpr[9] = 0x8ac12467;
  f.s.fpr[9].bits = {0x1122334455667788ull, 0x9abcdef012345678ull};
  f.s.cr = 0x8d6152af;
}

std::vector<Access> load_data_accesses(const ReadTransactionFixture &f) {
  std::vector<Access> result;
  for (const auto &access : f.memory.accesses)
    if (access.address < 0x10000)
      result.push_back(access);
  return result;
}

void split_load_tests() {
  struct Instruction {
    unsigned direct, indexed, width;
    bool floating;
  };

  constexpr std::array<Instruction, 4> instructions{
      {{40, 279, 2, false}, {42, 343, 2, false}, {32, 23, 4, false}, {50, 599, 8, true}}};
  constexpr std::array<u8, 8> payload{0x81, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
  for (const auto &instruction : instructions)
    for (unsigned distance = 1; distance < instruction.width; ++distance) {
      if (instruction.floating && distance != 4)
        continue;
      for (const bool indexed : {false, true})
        for (const bool update : {false, true})
          for (unsigned mappings = 0; mappings < 4; ++mappings)
            for (u32 attributes : {0u, 0x20u, 0x40u, 0x60u}) {
              ReadTransactionFixture f;
              const bool first = (mappings & 1) != 0, second = (mappings & 2) != 0;
              prepare_split_load(f, first, second, attributes, attributes ^ 0x60);
              const u32 address = 0x40008000 - distance, base = address - 5;
              f.s.gpr[7] = base;
              f.s.gpr[8] = 5;
              std::vector<Access> expected;
              u64 packed = 0;
              for (unsigned n = 0; n < instruction.width; ++n) {
                const u32 physical = n < distance ? 0x5000 - distance + n : 0x8000 + n - distance;
                f.memory.bytes[physical] = payload[n];
                packed = (packed << 8) | payload[n];
                if (n < distance ? first : second)
                  expected.push_back({physical, 0, 1, false});
              }
              const u32 original_msr = f.s.msr;
              f.run(indexed ? ext(instruction.indexed + (update ? 32 : 0), 9, 7, 8)
                            : imm(instruction.direct + static_cast<unsigned>(update), 9, 7, 5));
              CHECK(load_data_accesses(f) == expected);
              CHECK(f.memory.read32(load_page_table_group(7) + 4) ==
                    (0x4003u | attributes | (first ? 0x100u : 0)));
              CHECK(f.memory.read32(load_page_table_group(8) + 4) ==
                    (0x8003u | (attributes ^ 0x60) | (second ? 0x100u : 0)));
              CHECK(f.s.cr == 0x8d6152af);
              if (first && second) {
                CHECK(f.s.pc == 0x1004 && f.s.msr == original_msr);
                CHECK(f.s.gpr[7] == (update ? address : base));
                if (instruction.floating)
                  CHECK(f.s.fpr[9].bits[0] == packed &&
                        f.s.fpr[9].bits[1] == 0x9abcdef012345678ull);
                else {
                  const u32 expected_value =
                      instruction.direct == 42
                          ? static_cast<u32>(static_cast<s32>(static_cast<s16>(packed)))
                          : static_cast<u32>(packed);
                  CHECK(f.s.gpr[9] == expected_value);
                }
              } else {
                CHECK(f.s.pc == 0x300 && f.s.gpr[7] == base && f.s.gpr[9] == 0x8ac12467);
                CHECK(f.s.fpr[9].bits[0] == 0x1122334455667788ull &&
                      f.s.fpr[9].bits[1] == 0x9abcdef012345678ull);
                CHECK(f.s.spr[spr::srr0] == 0x1000 && f.s.spr[spr::srr1] == original_msr);
                CHECK(f.s.spr[spr::dsisr] == 0x40000000);
                CHECK(f.s.spr[spr::dar] ==
                      (second ? 0x40007fffu : address + instruction.width - 1));
              }
            }
    }

  for (unsigned type : {0u, 4u, 5u, 6u, 7u})
    for (const bool scalar : {false, true}) {
      const unsigned width = type == 0 ? 4 : type == 4 || type == 6 ? 1 : 2;
      const unsigned bytes = width * (scalar ? 1 : 2);
      for (unsigned distance = 1; distance < bytes; ++distance)
        for (const bool indexed : {false, true})
          for (const bool update : {false, true})
            for (unsigned mappings = 0; mappings < 4; ++mappings) {
              ReadTransactionFixture f;
              const bool first = (mappings & 1) != 0, second = (mappings & 2) != 0;
              prepare_split_load(f, first, second);
              f.s.spr[913] = type << 16;
              const u32 address = 0x40008000 - distance;
              f.s.gpr[7] = address - 5;
              f.s.gpr[8] = 5;
              std::vector<Access> expected;
              constexpr std::array<u8, 8> float_payload{0x3f, 0xc0, 0, 0, 0xc0, 0x10, 0, 0};
              for (unsigned n = 0; n < bytes; ++n) {
                const u32 physical = n < distance ? 0x5000 - distance + n : 0x8000 + n - distance;
                f.memory.bytes[physical] = type == 0 ? float_payload[n] : payload[n];
                if (n < distance ? first : second)
                  expected.push_back({physical, 0, 1, false});
              }
              const u32 opcode =
                  indexed ? (4u << 26) | (9u << 21) | (7u << 16) | (8u << 11) | (1u << 7) |
                                ((update ? 38u : 6u) << 1) | (scalar ? 0x400u : 0)
                          : imm(update ? 57 : 56, 9, 7,
                                static_cast<u16>(5 | (1u << 12) | (scalar ? 0x8000u : 0)));
              f.run(opcode);
              CHECK(load_data_accesses(f) == expected);
              CHECK(f.memory.read32(load_page_table_group(7) + 4) == (first ? 0x4103u : 0x4003u));
              CHECK(f.memory.read32(load_page_table_group(8) + 4) == (second ? 0x8103u : 0x8003u));
              if (first && second) {
                CHECK(f.s.pc == 0x1004 && f.s.gpr[7] == (update ? address : address - 5));
                if (type == 0)
                  CHECK(f.s.fpr[9].value() == 1.5 && f.s.fpr[9].value(1) == (scalar ? 1.0 : -2.25));
                else {
                  const double first_value = type == 4   ? 129.0
                                             : type == 5 ? 33059.0
                                             : type == 6 ? -127.0
                                                         : -32477.0;
                  const double second_value = type == 4 || type == 6 ? 35.0 : 17767.0;
                  CHECK(f.s.fpr[9].value() == first_value &&
                        f.s.fpr[9].value(1) == (scalar ? 1.0 : second_value));
                }
              } else {
                CHECK(f.s.pc == 0x300 && f.s.gpr[7] == address - 5);
                CHECK(f.s.fpr[9].bits[0] == 0x1122334455667788ull &&
                      f.s.fpr[9].bits[1] == 0x9abcdef012345678ull);
                CHECK(f.s.spr[spr::dar] == (second ? 0x40007fffu : address + bytes - 1));
              }
            }
    }

  for (unsigned failed_index = 0; failed_index < 4; ++failed_index) {
    ReadTransactionFixture f;
    prepare_split_load(f, true, true);
    const u32 address = 0x40007ffe;
    f.s.gpr[7] = address;
    const u32 physical = failed_index < 2 ? 0x4ffe + failed_index : 0x8000 + failed_index - 2;
    f.memory.failed_bytes.push_back(physical);
    f.run(imm(33, 9, 7, 0));
    CHECK(f.s.pc == 0x300 && f.s.gpr[9] == 0x8ac12467 && f.s.gpr[7] == address);
    CHECK(f.s.spr[spr::dar] == address + failed_index);
    CHECK(load_data_accesses(f).size() == 4);
    CHECK(f.memory.read32(load_page_table_group(7) + 4) == 0x4103 &&
          f.memory.read32(load_page_table_group(8) + 4) == 0x8103);
  }

  for (const bool unsupported : {false, true})
    for (unsigned stopped_index = 0; stopped_index < 4; ++stopped_index) {
      ReadTransactionFixture f;
      prepare_split_load(f, true, true);
      f.s.gpr[7] = 0x40007ffe;
      f.s.spr[spr::dar] = 0x13579bdf;
      f.s.spr[spr::dsisr] = 0x2468ace0;
      const u32 original_msr = f.s.msr;
      const u32 physical = stopped_index < 2 ? 0x4ffe + stopped_index : 0x8000 + stopped_index - 2;
      if (unsupported) {
        f.memory.failed_bytes.push_back(physical);
        f.memory.unsupported_fault = true;
      } else
        f.memory.host_read_error = physical;
      bool rejected = false;
      try {
        f.run(imm(33, 9, 7, 0));
      } catch (const UnsupportedInstruction &error) {
        CHECK(unsupported && error.pc == 0x1000 && error.opcode == imm(33, 9, 7, 0));
        rejected = true;
      } catch (const std::runtime_error &error) {
        CHECK(!unsupported && std::string(error.what()) == "Authored host read failure");
        rejected = true;
      }
      CHECK(rejected && f.s.pc == 0x1000 && f.s.gpr[7] == 0x40007ffe && f.s.gpr[9] == 0x8ac12467);
      CHECK(f.s.msr == original_msr && f.s.cycles == 0 && f.s.instructions == 0);
      CHECK(f.s.spr[spr::dar] == 0x13579bdf && f.s.spr[spr::dsisr] == 0x2468ace0);
      CHECK(load_data_accesses(f).size() == stopped_index + 1);
    }
}

void external_control_tests() {
  for (const bool write : {false, true})
    for (u32 ear : {0u, 0x01234567u, 0x7fffffffu})
      for (unsigned offset = 0; offset < 4; ++offset)
        for (const bool user : {false, true})
          for (const bool relocation : {false, true}) {
            TransactionFixture f;
            const u32 original_msr = (user ? msr::pr : 0) | (relocation ? msr::dr : 0);
            f.s.msr = original_msr;
            f.s.spr[282] = ear;
            f.s.spr[spr::dsisr] = 0x13579bdf;
            f.s.gpr[7] = 0x2000;
            f.s.gpr[8] = offset;
            f.s.gpr[9] = 0x8ac12467;
            f.s.cr = 0x8d6152af;
            CHECK(f.run(ext(write ? 438 : 310, 9, 7, 8)) == 1);
            CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x2000 + offset);
            CHECK(f.s.spr[spr::dsisr] == 0x13579bdf && f.s.spr[spr::srr1] == original_msr);
            CHECK(f.s.gpr[9] == 0x8ac12467 && f.s.cr == 0x8d6152af);
            CHECK(f.memory.accesses.empty());
          }

  for (const bool write : {false, true}) {
    TransactionFixture f;
    f.s.spr[282] = 0x80000000;
    f.s.msr = msr::pr;
    f.s.spr[spr::dsisr] = 0x13579bdf;
    f.s.gpr[7] = 0x2001;
    f.s.gpr[9] = 0x8ac12467;
    f.run(ext(write ? 438 : 310, 9, 7));
    CHECK(f.s.pc == 0x600 && f.s.spr[spr::dar] == 0x2001);
    CHECK(f.s.spr[spr::dsisr] == 0x13579bdf && f.s.gpr[9] == 0x8ac12467);
    CHECK(f.memory.accesses.empty());

    f.s.msr = msr::dr | msr::pr;
    f.s.gpr[7] = 0x40003000;
    f.run(ext(write ? 438 : 310, 9, 7));
    CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x40003000);
    CHECK(f.s.spr[spr::dsisr] == (write ? 0x42000000u : 0x40000000u));
    CHECK(f.s.gpr[9] == (write ? 0x8ac12467u : 0u));

    f.s.msr = msr::pr;
    f.s.gpr[7] = 0x3000;
    f.s.gpr[9] = 0x8ac12467;
    if (write)
      f.memory.fail_write = 0x3000;
    else
      f.memory.fail_read = 0x3000;
    f.run(ext(write ? 438 : 310, 9, 7));
    CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x3000);
    CHECK(f.s.gpr[9] == (write ? 0x8ac12467u : 0u));

    f.s.msr = msr::pr;
    f.s.gpr[9] = 0x8ac12467;
    f.memory.unsupported_fault = true;
    const auto cycles = f.s.cycles, instructions = f.s.instructions;
    const u32 dar = f.s.spr[spr::dar], dsisr = f.s.spr[spr::dsisr];
    CHECK_THROWS(f.run(ext(write ? 438 : 310, 9, 7)));
    CHECK(f.s.pc == 0x1000 && f.s.gpr[9] == 0x8ac12467 && f.s.msr == msr::pr);
    CHECK(f.s.cycles == cycles && f.s.instructions == instructions);
    CHECK(f.s.spr[spr::dar] == dar && f.s.spr[spr::dsisr] == dsisr);
  }

  for (const bool write : {false, true}) {
    Fixture f;
    f.s.msr = msr::pr;
    f.s.spr[spr::dsisr] = 0x13579bdf;
    f.s.gpr[7] = 0x2000;
    f.s.gpr[9] = 0x8ac12467;
    f.s.cr = 0x81abce37;
    f.memory.write32(0x2000, 0x10325476);
    const u32 opcode = ext(write ? 438 : 310, 9, 7);
    f.run(opcode);
    CHECK(f.s.pc == 0x300 && f.s.gpr[9] == 0x8ac12467);
    // The guest handler enables EAR and retries after returning to user mode.
    const std::array<u32, 3> handler{imm(14, 6, 0, 0xffff), special(467, 6, 282), 0x4c000064};
    for (unsigned n = 0; n < handler.size(); ++n)
      f.memory.write32(0x300 + n * 4, handler[n]);
    for (unsigned n = 0; n < handler.size(); ++n)
      f.cpu.step();
    CHECK(f.s.pc == 0x1000 && f.s.msr == msr::pr && f.s.spr[282] == 0xffffffff);
    f.cpu.step();
    CHECK(f.s.pc == 0x1004 && f.s.cr == 0x81abce37);
    CHECK(f.s.spr[spr::dsisr] == 0x13579bdf);
    CHECK(f.s.gpr[9] == (write ? 0x8ac12467u : 0x10325476u));
    CHECK(f.memory.read32(0x2000) == (write ? 0x8ac12467u : 0x10325476u));
  }

  Fixture f;
  f.s.spr[282] = 0x80000000;
  f.s.gpr[0] = 0xffffffff;
  f.s.gpr[7] = 0x2000;
  f.memory.write32(0x2000, 0x10325476);
  f.run(ext(310, 7, 0, 7));
  CHECK(f.s.gpr[7] == 0x10325476); // RD may alias the indexed address register.
  f.s.gpr[7] = 0x2000;
  f.run(ext(438, 7, 0, 7));
  CHECK(f.memory.read32(0x2000) == 0x2000 && f.s.gpr[7] == 0x2000);
}

void conditional_store_fault_tests() {
  for (const bool summary_overflow : {false, true}) {
    Fixture f;
    f.s.gpr[3] = 0x5a8c1234;
    f.s.gpr[4] = 0x3000;
    f.memory.write32(0x3000, 0x11223344);
    f.run(ext(20, 5, 4));
    CHECK(f.s.gpr[5] == 0x11223344);
    f.s.spr[spr::xer] = summary_overflow ? 0x80000000 : 0;
    f.s.cr = 0xf7654321;
    f.s.gpr[6] = msr::dr;
    f.run(ext(146, 6)); // No mapping exists for the reserved address yet.
    f.run(ext(150, 3, 4, 0, true));
    CHECK(f.s.pc == 0x300 && f.s.spr[spr::srr0] == 0x1000);
    CHECK(f.s.spr[spr::srr1] == msr::dr && f.s.msr == 0);
    CHECK(f.s.spr[spr::dar] == 0x3000 && f.s.spr[spr::dsisr] == 0x42000000);
    CHECK(f.s.cr == (0x07654321u | (summary_overflow ? 0x10000000u : 0)));
    CHECK(f.memory.read32(0x3000) == 0x11223344);

    // The authored handler maps the page and returns to the faulting store.
    // No second reservation load is executed before the successful retry.
    const std::array<u32, 5> handler{imm(14, 6, 0, 3), special(467, 6, 536), imm(14, 6, 0, 2),
                                     special(467, 6, 537), 0x4c000064};
    for (unsigned i = 0; i < handler.size(); ++i)
      f.memory.write32(0x300 + i * 4, handler[i]);
    for (unsigned i = 0; i < handler.size(); ++i)
      f.cpu.step();
    CHECK(f.s.pc == 0x1000 && f.s.msr == msr::dr);
    f.cpu.step();
    CHECK(f.s.pc == 0x1004 && f.memory.read32(0x3000) == 0x5a8c1234);
    CHECK((f.s.cr >> 28) == (summary_overflow ? 3u : 2u));
    f.run(ext(150, 3, 4, 0, true));
    CHECK((f.s.cr >> 28) == static_cast<u32>(summary_overflow));

    // A physical bus storage fault has the same failed CR0 result.
    f.cpu.reset();
    f.s.gpr[3] = 0x3a1bcdef;
    f.s.gpr[4] = 0x3000;
    f.run(ext(20, 5, 4));
    f.s.spr[spr::xer] = summary_overflow ? 0x80000000 : 0;
    f.s.cr = 0xf1234567;
    f.memory.bytes.resize(0x3000);
    f.run(ext(150, 3, 4, 0, true));
    CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x3000);
    CHECK(f.s.spr[spr::dsisr] == 0x42000000);
    CHECK(f.s.cr == (0x01234567u | (summary_overflow ? 0x10000000u : 0)));
    f.memory.bytes.resize(1024 * 1024);
    f.run(ext(150, 3, 4, 0, true));
    CHECK(f.memory.read32(0x3000) == 0x3a1bcdef);
    CHECK((f.s.cr >> 28) == (summary_overflow ? 3u : 2u));
  }

  Fixture f;
  f.s.gpr[3] = 0x128743ac;
  f.s.gpr[4] = 0x3000;
  f.run(ext(20, 5, 4));
  f.s.cr = 0xe1234567;
  f.s.spr[spr::dar] = 0x88776655;
  f.s.spr[spr::dsisr] = 0x11223344;
  f.memory.unsupported_device = true;
  const auto cycles = f.s.cycles, instructions = f.s.instructions;
  bool rejected = false;
  try {
    f.run(ext(150, 3, 4, 0, true));
  } catch (const UnsupportedInstruction &error) {
    rejected = true;
    CHECK(error.pc == 0x1000 && error.opcode == ext(150, 3, 4, 0, true));
  }
  CHECK(rejected && f.s.pc == 0x1000 && f.s.cr == 0xe1234567);
  CHECK(f.s.cycles == cycles && f.s.instructions == instructions);
  CHECK(f.s.spr[spr::dar] == 0x88776655 && f.s.spr[spr::dsisr] == 0x11223344);
  f.memory.unsupported_device = false;
  f.run(ext(150, 3, 4, 0, true));
  CHECK(f.memory.read32(0x3000) == 0x128743ac && (f.s.cr >> 28) == 2);

  // Alignment precedes reservation checking and leaves CR0 unchanged.
  f.s.gpr[4] = 0x3001;
  f.s.cr = 0xf1234567;
  f.run(ext(150, 3, 4, 0, true));
  CHECK(f.s.pc == 0x600 && f.s.spr[spr::dar] == 0x3001 && f.s.cr == 0xf1234567);
  CHECK(f.s.spr[spr::dsisr] == 0x11223344);
}

void exceptions_and_system() {
  Fixture f;
  f.s.gpr[3] = 0x12345678;
  f.run(special(467, 3, spr::lr));
  CHECK(f.s.spr[spr::lr] == 0x12345678);
  f.run(special(339, 4, spr::lr));
  CHECK(f.s.gpr[4] == 0x12345678);
  f.s.msr = msr::pr;
  f.run(ext(83, 5));
  CHECK(f.s.pc == 0x700 && (f.s.spr[spr::srr1] & 0x40000));
  f.s.msr = msr::fp | msr::dr | msr::ee;
  f.run(0x44000002);
  CHECK(f.s.pc == 0xc00 && f.s.spr[spr::srr0] == 0x1004);
  CHECK(!(f.s.msr & (msr::dr | msr::ir | msr::ee | msr::fp)));
  f.run(0x4c000064);
  CHECK(f.s.pc == 0x1004 && (f.s.msr & msr::fp));
  f.s.msr = 0;
  f.run(fp(21, 1, 2, 3));
  CHECK(f.s.pc == 0x800 && f.s.spr[spr::srr0] == 0x1000);
  f.run(1);
  CHECK(f.s.pc == 0x700 && (f.s.spr[spr::srr1] & 0x80000));
  f.run(imm(3, 4, 0, 0));
  CHECK(f.s.pc == 0x700 && (f.s.spr[spr::srr1] & 0x20000));
  f.cpu.reset();
  f.s.pc = 0x1000;
  f.cpu.set_external_interrupt(true);
  f.memory.write32(0x1000, imm(14, 3, 0, 7));
  f.cpu.step();
  CHECK(f.s.gpr[3] == 7);
  f.s.msr |= msr::ee;
  f.cpu.step();
  CHECK(f.s.pc == 0x500 && f.s.spr[spr::srr0] == 0x1004);
  f.cpu.set_external_interrupt(false);
  f.cpu.reset();
  f.s.spr[spr::dec] = 0;
  for (unsigned n = 0; n < 12; ++n)
    f.run(0x60000000);
  CHECK(f.s.timebase == 1 && f.s.spr[spr::dec] == 0xffffffff);
  f.s.msr = msr::ee;
  f.s.pc = 0x4000;
  f.cpu.step();
  CHECK(f.s.pc == 0x900 && f.s.spr[spr::srr0] == 0x4000);
}

void special_register_fidelity_tests() {
  Fixture f;
  f.s.msr = msr::pr; // XER is writable in user mode.
  f.s.gpr[3] = 0xffffffff;
  f.run(special(467, 3, spr::xer));
  f.run(special(339, 4, spr::xer));
  CHECK(f.s.gpr[4] == 0xe000ff7f);
  f.run(ext(512, 0)); // Move XER status to CR0, clearing status but preserving string control.
  CHECK((f.s.cr >> 28) == 14 && f.s.spr[spr::xer] == 0xff7f);
  f.s.msr = 0;
  for (unsigned index = 912; index < 920; ++index) {
    f.run(special(467, 3, index));
    f.run(special(339, 4, index));
    CHECK(f.s.gpr[4] == 0xffffffff); // Quantization register spare bits retain guest writes.
  }
  for (const unsigned index : {0u, spr::lr, spr::dec, 270u, 284u, 285u, 1023u}) {
    for (const u32 mode : {0u, msr::pr}) {
      f.s.msr = mode;
      f.s.pc = 0x1000;
      f.s.gpr[4] = 0x3456789a;
      const auto cycles = f.s.cycles, instructions = f.s.instructions;
      const auto saved = f.s.spr[spr::srr1];
      bool rejected = false;
      const auto opcode = special(371, 4, index);
      try {
        f.run(opcode);
      } catch (const UnsupportedInstruction &error) {
        rejected = true;
        CHECK(error.pc == 0x1000 && error.opcode == opcode);
      }
      CHECK(rejected && f.s.pc == 0x1000 && f.s.msr == mode);
      CHECK(f.s.gpr[4] == 0x3456789a && f.s.spr[spr::srr1] == saved);
      CHECK(f.s.cycles == cycles && f.s.instructions == instructions);
    }
  }
}

void msr_exception_tests() {
  for (const u32 enables : {0x100u, 0x800u, 0x900u}) {
    Fixture f;
    f.s.fpscr = 0xc4000010; // Enabled sticky divide-by-zero summary.
    f.s.gpr[3] = msr::fp | msr::ri | enables;
    f.run(ext(146, 3));
    CHECK(f.s.pc == 0x700 && f.s.spr[spr::srr0] == 0x1000);
    CHECK(f.s.spr[spr::srr1] == (f.s.gpr[3] | 0x100000));
    CHECK(f.s.fpscr == 0xc4000010);
  }
  Fixture f;
  f.s.fpscr = 0xc4000010;
  f.s.gpr[3] = msr::fp;
  f.run(ext(146, 3));
  CHECK(f.s.pc == 0x1004 && f.s.msr == msr::fp);
  f.s.msr = msr::pr;
  f.s.gpr[3] = msr::fp | 0x800;
  f.run(ext(146, 3));
  CHECK(f.s.pc == 0x700 && f.s.spr[spr::srr1] == (msr::pr | 0x40000));

  f.cpu.reset();
  f.cpu.set_external_interrupt(true);
  f.s.gpr[3] = msr::ee | msr::ri;
  f.run(ext(146, 3));
  CHECK(f.s.pc == 0x500 && f.s.spr[spr::srr0] == 0x1004);
  CHECK(f.s.spr[spr::srr1] == (msr::ee | msr::ri));
  f.cpu.set_external_interrupt(false);

  f.cpu.reset();
  f.s.gpr[3] = 0;
  f.run(special(467, 3, spr::dec));
  f.s.gpr[3] = msr::ee | msr::ri;
  f.run(ext(146, 3));
  CHECK(f.s.pc == 0x900 && f.s.spr[spr::srr0] == 0x1004);

  f.cpu.reset();
  f.s.fpscr = 0xc4000010;
  f.cpu.set_external_interrupt(true);
  f.s.gpr[3] = msr::ee | msr::fp | 0x800;
  f.run(ext(146, 3));
  CHECK(f.s.pc == 0x700 && (f.s.spr[spr::srr1] & 0x100000));
  f.cpu.set_external_interrupt(false); // Synchronous floating cause wins at this instruction.
}

void floating_exception_retry_tests() {
  Fixture f;
  f.s.msr = msr::fp | 0x800;
  f.s.fpscr = 0x10; // Divide-by-zero enable.
  f.s.fpr[1].set(17);
  f.s.fpr[2].set(4);
  f.s.fpr[3].set(0);
  f.run(fp(18, 1, 2, 3));
  CHECK(f.s.pc == 0x700 && f.s.spr[spr::srr0] == 0x1000);
  CHECK(f.s.fpr[1].value() == 17 && f.s.fpscr == 0xc4000010);
  f.s.fpr[3].set(1); // The handler repairs the operand without clearing the sticky summary.
  f.memory.write32(0x700, 0x4c000064);
  f.cpu.step();
  CHECK(f.s.pc == 0x1000 && f.s.msr == (msr::fp | 0x800));
  f.cpu.step();
  CHECK(f.s.pc == 0x1004 && f.s.fpr[1].value() == 4);
  CHECK((f.s.fpscr & 0xc4000010) == 0xc4000010);
  f.s.fpr[3].set(-2, 1);
  for (const unsigned operation : {40u, 72u, 136u, 264u, 583u}) {
    const auto sticky = f.s.fpscr;
    f.run(fp(operation, 6, 2, 3, 0, 63, true));
    CHECK(f.s.pc == 0x1004 && f.s.fpscr == sticky);
    CHECK(((f.s.cr >> 24) & 15) == (sticky >> 28));
  }
  f.s.spr[spr::hid2] |= 0x20000000;
  for (const unsigned operation : {40u, 72u, 136u, 264u, 528u, 560u, 592u, 624u}) {
    const auto sticky = f.s.fpscr;
    f.run(fp(operation, 6, 2, 3, 0, 4, true));
    CHECK(f.s.pc == 0x1004 && f.s.fpscr == sticky);
  }
  f.s.fpr[3].set(0);
  f.run(fp(18, 1, 2, 3));
  CHECK(f.s.pc == 0x700 && (f.s.spr[spr::srr1] & 0x100000));
  CHECK(f.s.fpr[1].value() == 4); // A newly signaled event still traps when its sticky bit was set.
  f.s.msr = msr::fp | 0x800;
  f.run(fp(70, 5, 0, 0)); // Clear divide-by-zero; summary is recomputed before delivery.
  CHECK(f.s.pc == 0x1004 && !(f.s.fpscr & 0x44000000));
  f.s.fpscr = 0x04000000;
  f.run(fp(38, 27, 0, 0)); // Enabling ZE exposes the already-set condition.
  CHECK(f.s.pc == 0x700 && (f.s.spr[spr::srr1] & 0x100000));
}

void locked_cache_dma_tests() {
  for (const auto console : {Console::GameCube, Console::Wii}) {
    cupid::Memory ram(console);
    Interpreter cpu(ram, console);
    auto &state = cpu.state();
    const auto run = [&](u32 opcode) {
      state.pc = 0x1000;
      state.spr[spr::hid0] &= ~0x8000u;
      ram.write32(0x1000, opcode);
      return cpu.step();
    };
    for (const unsigned blocks : {1u, 2u, 3u, 4u, 5u, 63u, 64u, 127u, 128u}) {
      const unsigned encoded = blocks == 128 ? 0 : blocks;
      const u32 physical = console == Console::Wii ? 0x10010000 : 0x10000;
      const u32 cache = 0xe0014000;
      for (unsigned byte = 0; byte < blocks * 32; ++byte)
        ram.write8(physical + byte, static_cast<u8>((byte * 73) ^ (byte >> 3) ^ blocks));
      ram.write8(cache - 1, 0x5c);
      ram.write8(cache + blocks * 32, 0x5d);
      state.gpr[3] = (physical | 0x80000000) | (encoded >> 2);
      CHECK(run(special(467, 3, 922)) == 2);
      run(special(339, 4, 922));
      CHECK(state.gpr[4] == state.gpr[3]);
      state.gpr[3] = cache | ((encoded & 3) << 2) | 0x13; // Load, trigger, retained DMA_F.
      const u32 written = state.gpr[3];
      run(special(467, 3, 923));
      run(special(339, 4, 923));
      CHECK(state.gpr[4] == (written & ~2u));
      CHECK(ram.read8(cache - 1) == 0x5c && ram.read8(cache + blocks * 32) == 0x5d);
      for (unsigned byte = 0; byte < blocks * 32; ++byte)
        CHECK(ram.read8(cache + byte) == static_cast<u8>((byte * 73) ^ (byte >> 3) ^ blocks));

      const u32 destination = physical + 0x2000;
      state.gpr[3] = (destination | 0xc0000000) | (encoded >> 2);
      run(special(467, 3, 922));
      state.gpr[3] = cache | ((encoded & 3) << 2) | 3;
      run(special(467, 3, 923));
      CHECK(state.spr[923] == (state.gpr[3] & ~2u));
      for (unsigned byte = 0; byte < blocks * 32; ++byte)
        CHECK(ram.read8(destination + byte) == ram.read8(cache + byte));
    }

    // DMA uses physical addresses independently of guest relocation. Guest cache loads still
    // use the ordinary BAT mapping, exposing the actual copied bytes to CPU execution.
    state.gpr[3] = 0xe0001fff;
    run(special(467, 3, 542));
    state.gpr[3] = 0xe0000002;
    run(special(467, 3, 543));
    state.msr = msr::dr;
    state.gpr[3] = 0xe0014000;
    run(imm(32, 4, 3, 0));
    CHECK(state.gpr[4] == ram.read32(0xe0014000));
    state.msr = 0;
    const auto before = ram.read32(0xe0014000);
    state.gpr[3] = 0xe0014000 | 0x11; // No trigger: no transfer.
    run(special(467, 3, 923));
    CHECK(ram.read32(0xe0014000) == before && state.spr[923] == state.gpr[3]);

    state.gpr[3] = cupid::Memory::mem1_size - 32;
    run(special(467, 3, 922));
    state.gpr[3] = 0xe0014000 | 0x1a; // Two blocks exceed the MEM1 boundary.
    const auto cycles = state.cycles, instructions = state.instructions;
    const auto saved_dma = state.spr[923];
    bool rejected = false;
    try {
      run(special(467, 3, 923));
    } catch (const UnsupportedInstruction &error) {
      rejected = true;
      CHECK(error.pc == 0x1000 && error.opcode == special(467, 3, 923));
    }
    CHECK(rejected && state.spr[923] == saved_dma && state.pc == 0x1000);
    CHECK(state.cycles == cycles && state.instructions == instructions);
    CHECK(state.spr[spr::dar] == 0 && state.spr[spr::dsisr] == 0);
    CHECK(ram.read32(0xe0014000) == before);
    state.msr = msr::pr;
    state.gpr[3] = 0;
    run(special(467, 3, 922));
    CHECK(state.pc == 0x700 && (state.spr[spr::srr1] & 0x40000));
    CHECK(state.spr[922] == cupid::Memory::mem1_size - 32);
  }

  Fixture unfinished;
  unfinished.s.gpr[3] = 0x2000;
  unfinished.run(special(467, 3, 922));
  unfinished.s.gpr[3] = 0xe0000016;
  CHECK_THROWS(unfinished.run(special(467, 3, 923)));
  CHECK(unfinished.s.spr[923] == 0 && unfinished.s.pc == 0x1000);
}

void non_ieee_boundary_tests() {
  Fixture f;
  f.s.msr = msr::fp;
  for (unsigned mode = 0; mode < 4; ++mode) {
    for (const bool ni : {false, true}) {
      for (const bool negative : {false, true}) {
        const u64 sign = negative ? u64{1} << 63 : 0;
        const bool round_up = mode == 0 || (mode == 2 && !negative) || (mode == 3 && negative);
        const u64 magnitude = ni ? 0 : round_up ? 0x0010000000000000ULL : 0x000fffffffffffffULL;
        for (const unsigned operation : {25u, 28u, 29u, 30u, 31u}) {
          f.s.fpscr = mode | (ni ? 4u : 0u);
          f.s.fpr[2].bits[0] = sign | 0x0010000000000000ULL;
          f.s.fpr[3].bits[0] = 0x3fefffffffffffffULL;
          f.s.fpr[4].set(0);
          f.run(fp(operation, 1, 2, 4, 3));
          const u64 result_sign = sign ^ (operation >= 30 ? u64{1} << 63 : 0);
          CHECK(f.s.fpr[1].bits[0] == (result_sign | magnitude));
        }

        // This quotient is just beyond the halfway boundary below the smallest normal.
        f.s.fpscr = mode | (ni ? 4u : 0u);
        f.s.fpr[2].bits[0] = sign | 0x001ffffffffffffeULL;
        f.s.fpr[3].bits[0] = 0x3fffffffffffffffULL;
        f.run(fp(18, 1, 2, 3));
        const bool quotient_up = (mode == 2 && !negative) || (mode == 3 && negative);
        const u64 quotient = ni ? 0 : quotient_up ? 0x0010000000000000ULL : 0x000fffffffffffffULL;
        CHECK(f.s.fpr[1].bits[0] == (sign | quotient));

        // NI preserves subnormal inputs. Multiplication here produces exactly 2^-51.
        f.s.fpscr = mode | (ni ? 4u : 0u);
        f.s.fpr[2].bits[0] = sign | 1;
        f.s.fpr[3].bits[0] = 0x7fe0000000000000ULL;
        f.run(fp(25, 1, 2, 0, 3));
        CHECK(f.s.fpr[1].bits[0] == (sign | 0x3cc0000000000000ULL));
        f.s.fpr[2].bits[0] = sign | 0x0010000000000000ULL;
        f.s.fpr[3].set(1);
        f.run(fp(25, 1, 2, 0, 3));
        CHECK(f.s.fpr[1].bits[0] == (sign | 0x0010000000000000ULL));

        // Fused results sufficiently near the threshold round to normal before
        // the exponent range is applied. NI must retain those normal results.
        for (const u64 multiplier :
             {0x3fd0000000000000ULL, 0x3fe0000000000000ULL, 0x3fe0000000000001ULL,
              0x3fe8000000000000ULL, 0x3fefffffffffffffULL}) {
          const bool outward = (mode == 2 && !negative) || (mode == 3 && negative);
          const bool normal = ni ? (mode == 0 && multiplier >= 0x3fe8000000000000ULL) ||
                                       (outward && multiplier > 0x3fe0000000000000ULL)
                                 : (mode == 0 && multiplier >= 0x3fe0000000000000ULL) || outward;
          const u64 expected = normal ? 0x0010000000000000ULL : ni ? 0 : 0x000fffffffffffffULL;
          for (const unsigned operation : {28u, 29u, 30u, 31u}) {
            f.s.fpscr = mode | (ni ? 4u : 0u);
            f.s.fpr[2].bits[0] = sign | 1;
            f.s.fpr[3].bits[0] = multiplier;
            f.s.fpr[4].bits[0] = sign | 0x000fffffffffffffULL;
            if (operation == 28 || operation == 30)
              f.s.fpr[4].bits[0] ^= u64{1} << 63;
            f.run(fp(operation, 1, 2, 4, 3));
            const u64 final_sign = sign ^ (operation >= 30 ? u64{1} << 63 : 0);
            CHECK(f.s.fpr[1].bits[0] == (final_sign | expected));
            const bool final_negative = final_sign != 0;
            const u32 category = normal           ? (final_negative ? 8u : 4u)
                                 : ni             ? (final_negative ? 0x12u : 2u)
                                 : final_negative ? 0x18u
                                                  : 0x14u;
            CHECK((f.s.fpscr & 0x1f000) == category << 12);
          }
        }
      }
    }
  }
}

void timebase_tests() {
  for (unsigned phase = 0; phase < 12; ++phase) {
    for (const unsigned half : {284u, 285u}) {
      Fixture f;
      f.s.timebase = half == 284 ? 0x1122334476543210ULL : 0x11223344fffffffeULL;
      for (unsigned i = 0; i < phase; ++i)
        f.run(0x60000000);
      f.s.gpr[3] = half == 284 ? 0xfffffffe : 0xfedcba98;
      const u64 written_at = f.s.cycles;
      const u64 expected = half == 284 ? 0x11223344fffffffeULL : 0xfedcba98fffffffeULL;
      CHECK(f.run(special(467, 3, half)) == 2);
      while (f.s.cycles - written_at < 37) {
        CHECK(f.s.timebase == expected + (f.s.cycles - written_at) / 12);
        const u64 before_read = f.s.timebase;
        f.s.msr = msr::pr; // Both timebase read forms are available to user code.
        f.run(special(371, 4, spr::tbl));
        CHECK(f.s.gpr[4] == static_cast<u32>(before_read));
        const u64 before_high = f.s.timebase;
        f.run(special(339, 5, spr::tbu));
        CHECK(f.s.gpr[5] == static_cast<u32>(before_high >> 32));
      }
    }
  }

  Fixture f;
  f.s.timebase = 0xffffffffffffffffULL;
  for (unsigned i = 0; i < 12; ++i)
    f.run(0x60000000);
  CHECK(f.s.timebase == 0); // Full-width rollover is modulo 64 bits.

  // A second half write replaces the fractional phase independently of the first write.
  f.cpu.reset();
  f.s.gpr[3] = 0xffffffff;
  f.run(special(467, 3, 284));
  for (unsigned i = 0; i < 9; ++i)
    f.run(0x60000000);
  f.s.gpr[3] = 0xffffffff;
  const u64 last_write = f.s.cycles;
  f.run(special(467, 3, 285));
  CHECK(f.s.timebase == 0xffffffffffffffffULL);
  while (f.s.cycles - last_write < 12)
    f.run(0x60000000);
  CHECK(f.s.timebase == 0);

  // Rejected writes leave both the timebase value and its fractional phase intact.
  for (const unsigned half : {284u, 285u, spr::tbl, spr::tbu}) {
    f.cpu.reset();
    f.s.timebase = 0x12345678abcdef00ULL;
    for (unsigned i = 0; i < 10; ++i)
      f.run(0x60000000);
    f.s.msr = half < 284 ? 0 : msr::pr;
    f.s.gpr[3] = 0;
    f.run(special(467, 3, half));
    CHECK(f.s.pc == 0x700 && f.s.timebase == 0x12345678abcdef01ULL);
    CHECK((f.s.spr[spr::srr1] & 0xc0000) == (half < 284 ? 0x80000u : 0x40000u));
  }

  // Timebase writes restart their own divider without moving an existing DEC deadline.
  f.cpu.reset();
  f.s.gpr[3] = 8;
  const u64 dec_write = f.s.cycles;
  f.run(special(467, 3, spr::dec));
  for (unsigned i = 0; i < 9; ++i)
    f.run(0x60000000);
  const u64 tb_write = f.s.cycles;
  f.s.gpr[3] = 55;
  f.run(special(467, 3, 284));
  CHECK(f.s.timebase == 55 && f.s.spr[spr::dec] == 7);
  while (f.s.cycles - dec_write < 96) {
    CHECK(f.s.timebase == 55 + (f.s.cycles - tb_write) / 12);
    CHECK(f.s.spr[spr::dec] == 8 - (f.s.cycles - dec_write) / 12);
    f.run(0x60000000);
  }
  CHECK(f.s.spr[spr::dec] == 0xffffffff);
  f.s.msr = msr::ee;
  f.run(0x60000000);
  CHECK(f.s.pc == 0x900 && f.s.spr[spr::srr0] == 0x1000);

  f.cpu.reset();
  for (unsigned i = 0; i < 11; ++i)
    f.run(0x60000000);
  f.s.gpr[3] = 5;
  f.run(special(467, 3, spr::dec));
  CHECK(f.s.timebase == 1 && f.s.spr[spr::dec] == 5);
  while (f.s.cycles < 24) {
    CHECK(f.s.timebase == f.s.cycles / 12);
    f.run(0x60000000);
  }
  CHECK(f.s.timebase == 2); // DEC writes likewise do not restart the timebase phase.
}

void decrementer_tests() {
  for (unsigned phase = 0; phase < 12; ++phase) {
    Fixture f;
    for (unsigned i = 0; i < phase; ++i)
      f.run(0x60000000);
    f.s.gpr[3] = 3;
    const u64 written_at = f.s.cycles;
    f.run(special(467, 3, spr::dec));
    CHECK(f.s.spr[spr::dec] == 3);
    while (f.s.cycles - written_at < 36) {
      CHECK(f.s.spr[spr::dec] == 3 - (f.s.cycles - written_at) / 12);
      f.run(0x60000000);
    }
    CHECK(f.s.spr[spr::dec] == 0xffffffff);
    for (unsigned i = 0; i < 25; ++i)
      f.run(0x60000000);
    f.run(special(339, 4, spr::dec));
    CHECK(f.s.gpr[4] == 0xffffffff);
    const u32 saved_msr = msr::ee | msr::ri | msr::fp | msr::dr;
    f.s.msr = saved_msr;
    f.s.pc = 0x1200;
    f.memory.write32(0x1200, 0x60000000);
    f.cpu.step();
    CHECK(f.s.pc == 0x900 && f.s.spr[spr::srr0] == 0x1200);
    CHECK(f.s.spr[spr::srr1] == saved_msr);
    CHECK((f.s.msr & (msr::ee | msr::fp | msr::dr)) == 0);
    f.memory.write32(0x900, 0x4c000064);
    f.cpu.step();
    CHECK(f.s.pc == 0x1200 && f.s.msr == saved_msr);
    f.cpu.step();
    CHECK(f.s.pc == 0x1204); // The acknowledged event does not fire repeatedly.
  }

  Fixture f;
  f.s.gpr[3] = 0;
  f.run(special(467, 3, spr::dec));
  CHECK(f.s.spr[spr::dec] == 0xffffffff);
  f.s.msr = msr::ee;
  f.cpu.set_external_interrupt(true);
  f.run(0x60000000);
  CHECK(f.s.pc == 0x500 && f.s.spr[spr::srr0] == 0x1000);
  f.cpu.set_external_interrupt(false);
  f.s.msr = msr::ee;
  f.run(0x60000000);
  CHECK(f.s.pc == 0x900 && f.s.spr[spr::srr0] == 0x1000);

  f.cpu.reset();
  f.s.gpr[3] = 0x80000000;
  f.run(special(467, 3, spr::dec));
  for (unsigned i = 0; i < 40; ++i)
    f.run(0x60000000);
  f.run(special(339, 4, spr::dec));
  CHECK(f.s.gpr[4] == 0x80000000);
  f.s.msr = msr::ee;
  f.run(0x60000000);
  CHECK(f.s.pc == 0x1004); // Negative-to-negative writes do not request an interrupt.
  f.s.msr = 0;
  f.s.gpr[3] = 100;
  f.run(special(467, 3, spr::dec));
  f.s.gpr[3] = 0x80000001;
  f.run(special(467, 3, spr::dec));
  f.s.msr = msr::ee;
  f.run(0x60000000);
  CHECK(f.s.pc == 0x900); // A positive-to-negative write requests an interrupt.
  f.s.msr = 0;
  for (unsigned i = 0; i < 1250; ++i)
    f.run(0x60000000);
  f.s.msr = msr::ee;
  f.run(0x60000000);
  CHECK(f.s.pc == 0x1004 && f.s.spr[spr::dec] == 0x80000001);

  f.cpu.reset();
  f.s.gpr[3] = 4;
  f.run(special(467, 3, spr::dec));
  for (unsigned i = 0; i < 10; ++i)
    f.run(0x60000000);
  f.s.gpr[3] = 2;
  f.run(special(467, 3, spr::dec));
  for (unsigned i = 0; i < 21; ++i)
    f.run(0x60000000);
  CHECK(f.s.spr[spr::dec] == 1);
  f.run(0x60000000);
  CHECK(f.s.spr[spr::dec] == 0xffffffff);
  f.s.gpr[3] = 5;
  f.run(special(467, 3, spr::dec));
  f.s.msr = msr::ee;
  f.run(0x60000000);
  CHECK(f.s.pc == 0x900 && f.s.spr[spr::dec] == 5);

  for (u32 value : {1u, 3u, 7u, 0x7fffffffu}) {
    f.cpu.reset();
    for (unsigned i = 0; i < 9; ++i)
      f.run(0x60000000);
    f.s.gpr[3] = value;
    const u64 written_at = f.s.cycles;
    f.run(special(467, 3, spr::dec));
    f.s.gpr[4] = 100;
    f.s.gpr[5] = 2;
    CHECK(f.run(ext(459, 10, 4, 5)) == 40);
    const u64 elapsed = f.s.cycles - written_at;
    CHECK(f.s.gpr[10] == 50);
    CHECK(f.s.spr[spr::dec] ==
          (elapsed >= u64(value) * 12 ? 0xffffffffu : value - static_cast<u32>(elapsed / 12)));
    if (value <= 3) {
      f.s.msr = msr::ee;
      f.cpu.step();
      CHECK(f.s.pc == 0x900 && f.s.spr[spr::srr0] == 0x1004);
    }
  }
}

void performance_monitor_tests() {
  {
    Fixture f;
    f.s.msr = msr::fp;
    f.s.spr[spr::hid0] |= 0x4000;
    f.s.spr[spr::hid2] = 0x30000000;
    f.s.spr[spr::mmcr0] = (1 << 6) | 11;
    f.s.spr[spr::mmcr1] = (11u << 27) | (1 << 22);
    f.s.gpr[4] = 0x2000;
    f.s.fpr[2].set(1);
    f.s.fpr[2].set(2, 1);
    f.s.fpr[3].set(2);
    f.s.fpr[3].set(4, 1);
    const auto check = [&](u32 opcode, u32 cycles, u32 loads, u32 floating) {
      const u32 before_cycles = f.s.spr[spr::pmc1];
      const u32 before_loads = f.s.spr[spr::pmc2];
      const u32 before_floating = f.s.spr[spr::pmc3];
      CHECK(f.run(opcode) == cycles);
      CHECK(f.s.spr[spr::pmc1] == before_cycles + cycles);
      CHECK(f.s.spr[spr::pmc4] == f.s.spr[spr::pmc1]);
      CHECK(f.s.spr[spr::pmc2] == before_loads + loads);
      CHECK(f.s.spr[spr::pmc3] == before_floating + floating);
    };
    check(imm(14, 5, 0, 7), 1, 0, 0);
    check(imm(36, 5, 4, 0), 1, 1, 0);
    check(ext(23, 5, 4), 1, 1, 0);
    check(ext(86, 0, 0, 4), 5, 1, 0);
    check(ext(246, 0, 0, 4), 2, 0, 0);
    check(ext(278, 0, 0, 4), 2, 0, 0);
    check(ext(982, 0, 0, 4), 4, 1, 0);
    check(ext(1014, 0, 0, 4), 5, 1, 0);
    check(ext(595, 5), 3, 0, 0);
    check(ext(659, 5), 3, 0, 0);
    check(ext(4, 0, 4, 5), 2, 0, 0);
    check(imm(46, 30, 4, 0), 11, 1, 0);
    check(imm(50, 1, 4, 0), 1, 1, 1);
    check(ext(599, 1, 4), 1, 1, 1);
    check(fp(21, 1, 2, 3), 1, 0, 1);
    check(fp(18, 1, 2, 3, 0, 59), 17, 0, 1);
    check(fp(18, 1, 2, 3), 31, 0, 1);
    check(fp(18, 1, 2, 3, 0, 4), 17, 0, 1);
    check(fp(26, 1, 0, 3, 0, 4), 2, 0, 1);
    check(fp(70, 0, 0, 0), 3, 0, 1);
    check((56u << 26) | (1 << 21) | (4 << 16), 1, 1, 1);
    check((4u << 26) | (1014 << 1) | (4 << 11), 1, 1, 0);
    // Guest faults still use the decoded instruction's events, as in the target interpreter.
    f.s.gpr[4] = 0xffffff00;
    check(imm(32, 5, 4, 0), 1, 1, 0);
    CHECK(f.s.pc == 0x300);
    f.s.msr = msr::fp;
    check(fp(1, 1, 2, 3), 1, 0, 0);
    CHECK(f.s.pc == 0x700);
    f.s.msr = 0;
    check(fp(21, 1, 2, 3), 1, 0, 1);
    CHECK(f.s.pc == 0x800);
    const u32 count = f.s.spr[spr::pmc1];
    f.s.pc = 0xffffff00;
    f.cpu.step();
    CHECK(f.s.pc == 0x400 && f.s.spr[spr::pmc1] == count);
  }
  {
    Fixture f;
    f.s.spr[spr::mmcr0] = 1 << 6;
    f.s.gpr[3] = 0xffffffff;
    CHECK(f.run(special(467, 3, spr::pmc1)) == 2);
    CHECK(f.s.spr[spr::pmc1] == 1); // Writing a counter counts the write instruction itself.
    for (const auto pair : {std::array<unsigned, 2>{937, spr::pmc1},
                            {938, spr::pmc2},
                            {941, spr::pmc3},
                            {942, spr::pmc4}}) {
      f.s.spr[pair[1]] = 0x12345678;
      f.run(special(339, 3, pair[0]));
      CHECK(f.s.gpr[3] == 0x12345678 && f.s.spr[pair[0]] == 0x12345678);
    }
    f.s.msr = msr::pr;
    f.s.gpr[3] = 0xabcdef;
    f.run(special(339, 3, 937));
    CHECK(f.s.pc == 0x700 && f.s.gpr[3] == 0xabcdef);
    for (u32 control : {2u << 6, 2u, 12u}) {
      f.s.spr[spr::mmcr0] = 0;
      f.s.gpr[3] = control;
      CHECK_THROWS(f.run(special(467, 3, spr::mmcr0)));
      CHECK(f.s.spr[spr::mmcr0] == 0 && f.s.pc == 0x1000);
    }
    for (u32 control : {2u << 27, 2u << 22, 11u << 22}) {
      f.s.gpr[3] = control;
      CHECK_THROWS(f.run(special(467, 3, spr::mmcr1)));
      CHECK(f.s.spr[spr::mmcr1] == 0 && f.s.pc == 0x1000);
    }
  }
  for (unsigned counter : {spr::pmc1, spr::pmc2, spr::pmc3, spr::pmc4}) {
    Fixture f;
    f.s.spr[counter] = 0x7fffffff;
    f.s.spr[spr::mmcr0] = counter == spr::pmc1 ? (1 << 6) | (1 << 15) : 1 << 14;
    if (counter == spr::pmc2)
      f.s.spr[spr::mmcr0] |= 1;
    if (counter == spr::pmc3)
      f.s.spr[spr::mmcr1] = 1 << 27;
    if (counter == spr::pmc4)
      f.s.spr[spr::mmcr1] = 1 << 22;
    f.run(imm(14, 5, 0, 1));
    CHECK(f.s.spr[counter] == 0x80000000 && f.s.pc == 0x1004);
    // A pending request survives clearing both the counter and its control register.
    f.s.spr[counter] = 0;
    f.s.spr[spr::mmcr0] = f.s.spr[spr::mmcr1] = 0;
    const u32 interrupted_msr = msr::ee | msr::ri | msr::fp;
    f.s.msr = interrupted_msr;
    const u64 instructions = f.s.instructions;
    f.cpu.step();
    CHECK(f.s.pc == 0xf00 && f.s.instructions == instructions);
    CHECK(f.s.spr[spr::srr0] == 0x1004 && f.s.spr[spr::srr1] == interrupted_msr);
    CHECK(f.s.msr == (interrupted_msr & ~0x04ef36u));
    f.memory.write32(0xf00, (19u << 26) | (50 << 1));
    f.cpu.step();
    CHECK(f.s.pc == 0x1004 && f.s.msr == interrupted_msr);
    f.memory.write32(0x1004, imm(14, 5, 0, 2));
    f.cpu.step();
    CHECK(f.s.pc == 0x1008 && f.s.gpr[5] == 2);
  }
  {
    Fixture f;
    f.s.spr[spr::mmcr0] = (1 << 6) | (1 << 15);
    f.s.spr[spr::pmc1] = 0x7fffffff;
    f.s.spr[spr::dec] = 0;
    f.run(imm(14, 5, 0, 1));
    f.s.spr[spr::mmcr0] = 0;
    f.cpu.set_external_interrupt(true);
    f.s.msr = msr::ee;
    f.cpu.step();
    CHECK(f.s.pc == 0x500);
    f.cpu.set_external_interrupt(false);
    f.s.msr = msr::ee;
    f.cpu.step();
    CHECK(f.s.pc == 0xf00 && f.s.spr[spr::srr0] == 0x500);
    f.s.msr = msr::ee;
    f.cpu.step();
    CHECK(f.s.pc == 0x900 && f.s.spr[spr::srr0] == 0xf00);
  }
}

void mmu_tests() {
  Fixture f;
  f.cpu.configure_boot();
  f.s.pc = 0x80001000;
  f.memory.write32(0x1000, imm(14, 3, 0, 11));
  f.cpu.step();
  CHECK(f.s.gpr[3] == 11 && f.s.pc == 0x80001004);
  f.s.msr &= ~msr::ir;
  f.s.gpr[4] = 0xc0002000;
  f.memory.write32(0x2000, 0xabcd1234);
  f.run(imm(32, 5, 4, 0));
  CHECK(f.s.gpr[5] == 0xabcd1234);
  f.cpu.reset();
  f.s.msr = msr::dr;
  f.s.spr[spr::sdr1] = 0x20000;
  f.s.sr[4] = 0x123;
  const u32 ea = 0x40003004, hash = 0x123 ^ 3, pteg = 0x20000 | ((hash << 6) & 0xffff);
  f.memory.write32(pteg, 0x80000000 | (0x123 << 7));
  f.memory.write32(pteg + 4, 0x5002);
  f.memory.write32(0x5004, 0xdeadbeef);
  f.s.gpr[4] = ea;
  f.run(imm(32, 5, 4, 0));
  CHECK(f.s.gpr[5] == 0xdeadbeef && (f.memory.read32(pteg + 4) & 0x100));
  f.s.gpr[5] = 0xabcdef01;
  f.run(imm(36, 5, 4, 0));
  CHECK(f.memory.read32(0x5004) == 0xabcdef01 && (f.memory.read32(pteg + 4) & 0x180) == 0x180);
  // A translation stays cached until tlbie invalidates its effective page.
  f.memory.write32(pteg + 4, 0x6002);
  f.memory.write32(0x6004, 0x10203040);
  f.run(imm(32, 5, 4, 0));
  CHECK(f.s.gpr[5] == 0xabcdef01);
  f.run(ext(306, 0, 0, 4));
  f.run(imm(32, 5, 4, 0));
  CHECK(f.s.gpr[5] == 0x10203040);
  f.s.gpr[4] = 0x40004000;
  f.run(imm(32, 5, 4, 0));
  CHECK(f.s.pc == 0x300 && f.s.spr[spr::dar] == 0x40004000);
  f.cpu.reset();
  f.s.msr = msr::ir;
  f.s.sr[4] = 0x10000123;
  f.s.pc = ea & ~3u;
  f.cpu.step();
  CHECK(f.s.pc == 0x400 && f.s.spr[spr::srr0] == (ea & ~3u));
  Memory memory;
  Interpreter wii(memory, Console::Wii);
  wii.configure_boot();
  CHECK(wii.state().spr[spr::pvr] == 0x00087102);
  CHECK(wii.translate(0x90001234) == 0x10001234 && wii.translate(0xd0001234) == 0x10001234);
}

void floating_tests() {
  CHECK(binary32_to_binary64(0x7fa01234) == 0x7ff4024680000000ull);
  CHECK(binary32_to_binary64(1) == std::bit_cast<u64>(std::ldexp(1.0, -149)));
  CHECK(binary32_to_binary64(0x80000000) == 0x8000000000000000ull);
  for (u32 raw :
       {0u, 1u, 0x007fffffu, 0x00800000u, 0x3f800000u, 0x7f800000u, 0x7fa12345u, 0xffc54321u})
    CHECK(binary64_to_binary32_store(binary32_to_binary64(raw)) == raw);
  Fixture f;
  f.s.msr = msr::fp;
  f.s.fpr[2].set(1.5);
  f.s.fpr[3].set(2.25);
  f.run(fp(21, 1, 2, 3));
  CHECK(f.s.fpr[1].value() == 3.75 && ((f.s.fpscr >> 12) & 31) == 4);
  f.s.fpr[1].set(9, 1);
  f.run(fp(20, 1, 2, 3));
  CHECK(f.s.fpr[1].value() == -0.75 && f.s.fpr[1].value(1) == 9);
  f.run(fp(21, 1, 2, 3, 0, 59));
  CHECK(f.s.fpr[1].value(1) == 3.75);
  f.s.fpr[3].set(0);
  f.run(fp(18, 1, 2, 3));
  CHECK(std::isinf(f.s.fpr[1].value()) && (f.s.fpscr & 0x04000000));
  f.s.fpr[2].set(0);
  f.run(fp(18, 1, 2, 3));
  CHECK(std::isnan(f.s.fpr[1].value()) && (f.s.fpscr & 0x00200000));
  f.s.fpr[3].bits[0] = 0x7ff0000000001234ull;
  f.run(fp(72, 1, 0, 3));
  CHECK(f.s.fpr[1].bits[0] == 0x7ff0000000001234ull);
  f.run(fp(0, 0, 2, 3));
  CHECK((f.s.cr >> 28) == 1 && (f.s.fpscr & 0x01000000));
  f.s.fpscr = 0;
  f.s.fpr[3].set(2.5);
  f.run(fp(14, 1, 0, 3));
  CHECK(static_cast<u32>(f.s.fpr[1].bits[0]) == 2);
  CHECK(f.s.fpscr & 0x02020000);
  f.s.fpr[3].set(3.5);
  f.run(fp(14, 1, 0, 3));
  CHECK(static_cast<u32>(f.s.fpr[1].bits[0]) == 4);
  f.s.fpr[3].set(-0.9);
  f.run(fp(15, 1, 0, 3));
  CHECK(f.s.fpr[1].bits[0] == 0xfff8000100000000ull);
  f.s.fpr[3].set(2147483648.0);
  f.run(fp(15, 1, 0, 3));
  CHECK(static_cast<u32>(f.s.fpr[1].bits[0]) == 0x7fffffff && (f.s.fpscr & 0x100));
  f.s.fpscr = 0;
  f.s.fpr[3].set(1.0 / 3.0);
  f.run(fp(12, 1, 0, 3));
  CHECK(f.s.fpr[1].value() == static_cast<double>(static_cast<float>(1.0 / 3.0)) &&
        f.s.fpr[1].bits[0] == f.s.fpr[1].bits[1]);
  f.s.gpr[4] = 0x2000;
  f.memory.write32(0x2000, 0x7fa12345);
  f.run(imm(48, 1, 4, 0));
  CHECK(f.s.fpr[1].bits[0] == binary32_to_binary64(0x7fa12345));
  f.run(imm(52, 1, 4, 4));
  CHECK(f.memory.read32(0x2004) == 0x7fa12345);
  f.s.fpr[1].set(1.0 / 3.0);
  f.run(imm(52, 1, 4, 8));
  CHECK(f.memory.read32(0x2008) == 0x3eaaaaaa);
  f.s.fpscr = 0;
  f.s.fpr[2].set(50.0);
  f.s.fpr[3].bits[0] = binary32_to_binary64(0x1b1c72a0);
  f.s.fpr[4].bits[0] = binary32_to_binary64(0xbc88cc38);
  f.run(fp(29, 1, 2, 3, 4, 59));
  CHECK(binary64_to_binary32_store(f.s.fpr[1].bits[0]) == 0xbf55bf17);
  f.s.fpscr = 0;
  f.s.fpr[3].set(1.0);
  f.run(fp(24, 1, 0, 3, 0, 59));
  CHECK(f.s.fpr[1].bits[0] == 0x3fefff0000000000ull && f.s.fpr[1].bits[0] == f.s.fpr[1].bits[1]);
  f.run(fp(26, 1, 0, 3));
  CHECK(f.s.fpr[1].bits[0] == 0x3feffe8000000000ull);
  f.s.fpscr = 0x80;
  f.s.fpr[1].set(123);
  f.s.fpr[3].set(-1);
  f.run(fp(26, 1, 0, 3));
  CHECK(f.s.fpr[1].value() == 123 && (f.s.fpscr & 0x200));
  f.s.fpr[2].set(9007199254740992.0);
  f.s.fpr[3].set(1);
  for (unsigned mode = 0; mode < 4; ++mode) {
    f.s.fpscr = mode;
    f.run(fp(21, 1, 2, 3));
    CHECK(f.s.fpr[1].value() == (mode == 2 ? 9007199254740994.0 : 9007199254740992.0));
  }
  f.s.fpr[3].set(1.0 / 3.0);
  for (unsigned mode = 0; mode < 4; ++mode) {
    f.s.fpscr = mode;
    f.run(fp(12, 1, 0, 3));
    CHECK(binary64_to_binary32_store(f.s.fpr[1].bits[0]) ==
          (mode == 0 || mode == 2 ? 0x3eaaaaabu : 0x3eaaaaaau));
  }
}

void paired_tests() {
  Fixture f;
  f.cpu.configure_boot();
  f.s.msr &= ~(msr::ir | msr::dr);
  f.s.fpr[2].set(1.5);
  f.s.fpr[2].set(-2, 1);
  f.s.fpr[3].set(2.25);
  f.s.fpr[3].set(4, 1);
  f.run(fp(21, 1, 2, 3, 0, 4));
  CHECK(f.s.fpr[1].value() == 3.75 && f.s.fpr[1].value(1) == 2);
  f.run(fp(592, 1, 2, 3, 0, 4));
  CHECK(f.s.fpr[1].value() == -2 && f.s.fpr[1].value(1) == 2.25);
  f.s.gpr[4] = 0x2000;
  f.s.spr[913] = (6u << 16) | (2u << 24) | 6u | (2u << 8);
  f.memory.write16(0x2000, 0x80fc);
  f.run((56u << 26) | (1u << 21) | (4u << 16) | (1u << 12));
  CHECK(f.s.fpr[1].value() == -32 && f.s.fpr[1].value(1) == -1);
  f.s.fpr[1].set(50);
  f.s.fpr[1].set(-50, 1);
  f.run((60u << 26) | (1u << 21) | (4u << 16) | (1u << 12) | 2);
  CHECK(f.memory.read16(0x2002) == 0x7f80);
  f.run((56u << 26) | (1u << 21) | (4u << 16) | 0x8000 | (1u << 12));
  CHECK(f.s.fpr[1].value() == -32 && f.s.fpr[1].value(1) == 1);
  f.s.spr[913] = (7u << 16) | (63u << 24);
  f.memory.write32(0x2000, 0x80007fff);
  f.run((56u << 26) | (1u << 21) | (4u << 16) | (1u << 12));
  CHECK(f.s.fpr[1].value() == -65536 && f.s.fpr[1].value(1) == 65534);
  f.s.spr[spr::hid2] = 0;
  f.run(fp(21, 1, 2, 3, 0, 4));
  CHECK(f.s.pc == 0x700 && (f.s.spr[spr::srr1] & 0x80000));
}

void cache_tests() {
  Memory memory;
  InstructionCache cache;
  memory.write32(0x2000, 11);
  CHECK(cache.read(memory, 0x2000, true, false) == 11);
  memory.write32(0x2000, 22);
  CHECK(cache.read(memory, 0x2000, true, false) == 11);
  CHECK(cache.read(memory, 0x2000, false, false) == 22);
  cache.invalidate(0x6000);
  CHECK(cache.read(memory, 0x2000, true, true) == 22);
  memory.write32(0x2000, 33);
  CHECK(cache.read(memory, 0x2000, true, true) == 33);
  CHECK(cache.read(memory, 0x2000, true, false) == 33);
  memory.write32(0x2000, 44);
  CHECK(cache.read(memory, 0x2000, true, true) == 33);
  cache.reset();
  for (unsigned n = 0; n < 8; ++n) {
    memory.write32(0x2000 + n * 0x1000, n + 1);
    CHECK(cache.read(memory, 0x2000 + n * 0x1000, true, false) == n + 1);
  }
  CHECK(cache.read(memory, 0x2000, true, false) == 1);
  memory.write32(0x6000, 99);
  memory.write32(0xa000, 9);
  CHECK(cache.read(memory, 0xa000, true, false) == 9);
  CHECK(cache.read(memory, 0x6000, true, false) == 99);
  cache.invalidate(0xb000);
  memory.write32(0x2000, 123);
  CHECK(cache.read(memory, 0x2000, true, false) == 123);

  class GenericMirrorMemory final : public Memory {
  public:
    unsigned reads = 0;

    u32 read32(u32 physical) override {
      ++reads;
      return Memory::read32(physical & 0xfffff);
    }
  };

  GenericMirrorMemory generic;
  for (u32 alias : {0x02000000u, 0x04000000u, 0x10000000u, 0x20000000u, 0x40000000u, 0x80000000u,
                    0xc0000000u}) {
    cache.reset();
    const u32 physical = 0x2084, other = physical | alias;
    CHECK(generic.instruction_cache_key(other) == other);
    generic.write32(physical, 11);
    CHECK(cache.read(generic, physical, true, false) == 11);
    generic.write32(physical, 22);
    unsigned before = generic.reads;
    CHECK(cache.read(generic, other, true, true) == 22 && generic.reads == before + 1);
    generic.write32(physical, 33);
    before = generic.reads;
    CHECK(cache.read(generic, other, true, false) == 33 && generic.reads == before + 8);
    before = generic.reads;
    CHECK(cache.read(generic, physical, true, true) == 11 && generic.reads == before);
    generic.write32(physical, 44);
    CHECK(cache.read(generic, other, false, false) == 44 && generic.reads == before + 1);
  }

  Interpreter cpu(memory);
  auto &state = cpu.state();
  state.spr[spr::hid0] = 0x8000;
  state.gpr[4] = 0x1000;
  memory.write32(0x1000, imm(14, 3, 0, 1));
  memory.write32(0x1004, ext(982, 0, 0, 4));
  state.pc = 0x1000;
  cpu.step();
  CHECK(state.gpr[3] == 1);
  memory.write32(0x1000, imm(14, 3, 0, 2));
  state.pc = 0x1000;
  cpu.step();
  CHECK(state.gpr[3] == 1);
  state.pc = 0x1004;
  cpu.step();
  state.pc = 0x1000;
  cpu.step();
  CHECK(state.gpr[3] == 2);
  memory.write32(0x1000, imm(14, 3, 0, 3));
  memory.write32(0x2000, special(467, 4, spr::hid0));
  state.gpr[4] = 0x8800;
  state.pc = 0x2000;
  cpu.step();
  CHECK(state.spr[spr::hid0] == 0x8000);
  state.pc = 0x1000;
  cpu.step();
  CHECK(state.gpr[3] == 3);
  state.spr[spr::hid0] = 0xa000;
  memory.write32(0x1000, imm(14, 3, 0, 4));
  state.pc = 0x1000;
  cpu.step();
  CHECK(state.gpr[3] == 3);
  memory.write32(0x3000, imm(14, 3, 0, 5));
  state.pc = 0x3000;
  cpu.step();
  CHECK(state.gpr[3] == 5);
  memory.write32(0x3000, imm(14, 3, 0, 6));
  state.pc = 0x3000;
  cpu.step();
  CHECK(state.gpr[3] == 6);
}
} // namespace

namespace {
void translated_fetch_fault_tests() {
  unsigned cases = 0;
  for (Console console : {Console::GameCube, Console::Wii})
    for (bool enabled : {false, true})
      for (unsigned monitor_mode : {0u, 1u, 11u})
        for (u32 mode : {0u, msr::pr, msr::fp, msr::ee, msr::dr, msr::ile})
          for (bool no_execute : {false, true})
            for (u32 address : {0x80342000u, 0x9efff000u}) {
              cupid::Memory memory(console);
              Interpreter cpu(memory, console);
              auto &s = cpu.state();
              unsigned pi = 0;
              memory.memory_error = [&] { ++pi; };
              s.pc = address;
              s.msr = msr::ir | msr::ri | mode;
              s.sr[address >> 28] = no_execute ? 0x10000000u : 0u;
              s.gpr[3] = 0xabcdef01;
              s.spr[spr::dar] = 0xabcdef01;
              s.spr[spr::dsisr] = 0x12345678;
              s.spr[spr::hid0] = enabled ? 0x8000u : 0u;
              s.spr[spr::mmcr0] = monitor_mode == 0 ? 0u : (1u << 6) | monitor_mode;
              s.spr[spr::mmcr1] = monitor_mode == 0 ? 0u : (monitor_mode << 27) | (1u << 22);
              constexpr std::array<unsigned, 4> counters{spr::pmc1, spr::pmc2, spr::pmc3,
                                                         spr::pmc4};
              for (unsigned n = 0; n < counters.size(); ++n)
                s.spr[counters[n]] = 0x12345678u + n;
              s.cycles = 11;
              s.timebase = 0x12345678;
              const u32 old_msr = s.msr;
              CHECK(cpu.step() == 0);
              CHECK(!cpu.last_step_fetched_instruction());
              CHECK(s.pc == 0x400 && s.spr[spr::srr0] == address);
              CHECK(s.spr[spr::srr1] == ((old_msr & 0x87c0ffffu) | 0x40000000u));
              CHECK(s.msr ==
                    (((old_msr & ~msr::le) | ((old_msr & msr::ile) ? msr::le : 0u)) & ~0x04ef36u));
              CHECK(s.gpr[3] == 0xabcdef01 && s.spr[spr::dar] == 0xabcdef01 &&
                    s.spr[spr::dsisr] == 0x12345678);
              CHECK(s.cycles == 11 && s.instructions == 0 && s.timebase == 0x12345678);
              CHECK(pi == 0 && memory.instruction_backing_read_errors() == 0);
              for (unsigned n = 0; n < counters.size(); ++n)
                CHECK(s.spr[counters[n]] == 0x12345678u + n);
              ++cases;
            }
  CHECK(cases == 288);
  {
    Fixture aligned_control;
    aligned_control.s.pc = 0x1001;
    CHECK(aligned_control.cpu.step() == 0);
    CHECK(aligned_control.s.pc == 0x1005 && aligned_control.s.cycles == 0);
    CHECK(aligned_control.s.spr[spr::srr0] == 0);
  }
  {
    Fixture unfinished;
    unfinished.memory.unsupported_device = true;
    unfinished.s.pc = 0x3000;
    unfinished.s.gpr[3] = 0xabcdef01;
    unfinished.s.spr[spr::srr0] = 0x12345678;
    unfinished.s.cycles = 11;
    bool unsupported = false;
    try {
      unfinished.cpu.step();
    } catch (const UnsupportedInstruction &error) {
      unsupported = true;
      CHECK(error.pc == 0x3000);
    }
    CHECK(unsupported && unfinished.s.pc == 0x3000 && unfinished.s.cycles == 11);
    CHECK(unfinished.s.instructions == 0 && unfinished.s.gpr[3] == 0xabcdef01);
    CHECK(unfinished.s.spr[spr::srr0] == 0x12345678);
  }
  {
    class HostFaultFetch final : public Memory {
    public:
      u32 read32(u32) override {
        throw std::runtime_error("Authored host fetch failure");
      }
    } backing;

    Interpreter cpu(backing);
    auto &s = cpu.state();
    s.pc = 0x1000;
    s.cycles = 11;
    s.spr[spr::srr0] = 0x12345678;
    bool host_error = false;
    try {
      cpu.step();
    } catch (const UnsupportedInstruction &) {
      CHECK(false);
    } catch (const std::runtime_error &error) {
      host_error = true;
      CHECK(std::string(error.what()) == "Authored host fetch failure");
    }
    CHECK(host_error && s.pc == 0x1000 && s.cycles == 11 && s.instructions == 0);
    CHECK(s.spr[spr::srr0] == 0x12345678);
  }
  Fixture f;
  for (unsigned n = 0; n < 11; ++n)
    f.run(0x60000000);
  f.s.spr[spr::dec] = 100;
  const auto cycles = f.s.cycles, instructions = f.s.instructions;
  for (unsigned n = 0; n < 10; ++n) {
    f.s.pc = 0x80342000;
    f.s.msr = msr::ir;
    CHECK(f.cpu.step() == 0);
    CHECK(f.s.pc == 0x400 && f.s.spr[spr::srr0] == 0x80342000);
    CHECK(f.s.cycles == cycles && f.s.instructions == instructions);
    CHECK(f.s.timebase == 0 && f.s.spr[spr::dec] == 100);
  }
  f.run(0x60000000);
  CHECK(f.s.cycles == cycles + 1 && f.s.instructions == instructions + 1);
  CHECK(f.s.timebase == 1 && f.s.spr[spr::dec] == 100);
}

void instruction_byte_bus_tests() {
  class ByteBus final : public Bus {
  public:
    u32 fault_address = 0xffffffffu;
    unsigned reads = 0;

    u8 read8(u32 address) override {
      ++reads;
      if (address == fault_address)
        throw BusFault(address, false, "Authored byte backing fault");
      return static_cast<u8>(address * 3u + 7u);
    }

    void write8(u32, u8) override {
      CHECK(false);
    }
  } backing;

  for (unsigned width = 1; width <= 32; ++width) {
    std::array<u8, 64> bytes;
    bytes.fill(0xa5);
    backing.reads = 0;
    backing.fault_address = 0xffffffffu;
    CHECK(backing.read_instruction_bytes(0x1000, std::span(bytes).subspan(16, width)));
    CHECK(backing.reads == width);
    for (unsigned n = 0; n < bytes.size(); ++n) {
      const u8 expected = n >= 16 && n < 16 + width ? static_cast<u8>((0x1000 + n - 16) * 3u + 7u)
                                                    : static_cast<u8>(0xa5);
      CHECK(bytes[n] == expected);
    }
    for (unsigned failed = 0; failed < width; ++failed) {
      bytes.fill(0xa5);
      backing.fault_address = 0x1000u + failed;
      bool rejected = false;
      try {
        backing.read_instruction_bytes(0x1000, std::span(bytes).subspan(16, width));
      } catch (const BusFault &error) {
        rejected = true;
        CHECK(error.address == backing.fault_address && !error.write && !error.unsupported_reason);
      }
      CHECK(rejected && std::ranges::all_of(bytes, [](u8 byte) { return byte == 0xa5; }));
    }
  }
  for (unsigned width : {0u, 33u}) {
    std::array<u8, 64> bytes;
    bytes.fill(0xa5);
    const auto reads = backing.reads;
    bool invalid = false;
    try {
      backing.read_instruction_bytes(0x1000, std::span(bytes).first(width));
    } catch (const std::invalid_argument &) {
      invalid = true;
    }
    CHECK(invalid && backing.reads == reads);
    CHECK(std::ranges::all_of(bytes, [](u8 byte) { return byte == 0xa5; }));
  }
}

void odd_instruction_tests() {
  const std::array<u32, 8> program{0u,
                                   0x60000000u,
                                   imm(14, 3, 0, 7),
                                   imm(14, 3, 3, 0xfffb),
                                   imm(15, 3, 3, 1),
                                   imm(27, 3, 3, 0xa5a5),
                                   0x48000008u,
                                   0x48000009u};
  unsigned cases = 0;
  for (Console console : {Console::GameCube, Console::Wii}) {
    cupid::Memory backing(console);
    Interpreter cpu(backing, console);
    unsigned pi = 0;
    backing.memory_error = [&] { ++pi; };
    for (bool cached : {false, true})
      for (u32 base : {0x2000u, 0x201cu})
        for (unsigned offset = 0; offset < 4; ++offset)
          for (unsigned operation = 0; operation < program.size(); ++operation) {
            const u32 address = base + offset, opcode = program[operation];
            std::ranges::fill(backing.range(address & ~31u, 64), 0);
            backing.write32(address, opcode);
            cpu.reset();
            auto &s = cpu.state();
            s.pc = address;
            s.spr[spr::hid0] = cached ? 0x8000u : 0u;
            s.gpr[3] = 0x12345678;
            s.cr = 0xa1b2c3d4;
            s.spr[spr::lr] = 0xabcdef01;
            s.spr[spr::srr0] = 0x12345678;
            const std::array<u32, 8> expected{0x12345678u, 0x12345678u, 7u,          0x12345673u,
                                              0x12355678u, 0xb7915678u, 0x12345678u, 0x12345678u};
            CHECK(cpu.step() == (opcode == 0 ? 0u : 1u));
            CHECK(cpu.last_step_fetched_instruction());
            CHECK(s.last_pc == address && s.last_opcode == opcode);
            CHECK(s.pc == address + (operation >= 6 ? 8u : 4u));
            CHECK(s.gpr[3] == expected[operation] && s.cr == 0xa1b2c3d4);
            CHECK(s.spr[spr::lr] == (operation == 7 ? address + 4u : 0xabcdef01u));
            CHECK(s.spr[spr::srr0] == 0x12345678 && s.msr == 0);
            CHECK(s.instructions == (opcode == 0 ? 0u : 1u));
            CHECK(s.cycles == (opcode == 0 ? 0u : 1u));
            CHECK(s.timebase == 0 && pi == 0 && backing.instruction_backing_read_errors() == 0);
            ++cases;
          }
  }
  CHECK(cases == 256);
}

void fetch_diagnostic_tests() {
  Fixture initial;
  CHECK(!initial.cpu.last_step_fetched_instruction());
  initial.run(0x60000000);
  CHECK(initial.cpu.last_step_fetched_instruction());
  initial.cpu.reset();
  CHECK(!initial.cpu.last_step_fetched_instruction());

  for (u32 opcode : {1u, fp(21, 1, 2, 3)}) {
    Fixture decoded;
    decoded.run(imm(14, 3, 0, 7));
    const auto instructions = decoded.s.instructions;
    decoded.run(opcode);
    CHECK(decoded.cpu.last_step_fetched_instruction());
    CHECK(decoded.s.last_pc == 0x1000 && decoded.s.last_opcode == opcode);
    CHECK(decoded.s.pc == (opcode == 1 ? 0x700u : 0x800u));
    CHECK(decoded.s.instructions == instructions);
  }

  for (bool translated : {false, true}) {
    Fixture rejected;
    const u32 opcode = imm(14, 3, 0, 7);
    rejected.run(opcode);
    rejected.s.pc = translated ? 0x80342000u : 0x3001u;
    rejected.s.msr = translated ? msr::ir : 0;
    rejected.cpu.step();
    CHECK(rejected.cpu.last_step_fetched_instruction() == !translated);
    CHECK(rejected.s.pc == (translated ? 0x400u : 0x3005u));
    CHECK(rejected.s.last_pc == (translated ? 0x1000u : 0x3001u));
    CHECK(rejected.s.last_opcode == (translated ? opcode : 0u));
  }

  {
    Fixture interrupt;
    const u32 opcode = imm(14, 3, 0, 7);
    interrupt.run(opcode);
    interrupt.s.msr = msr::ee | msr::ri;
    interrupt.cpu.set_external_interrupt(true);
    interrupt.cpu.step();
    CHECK(!interrupt.cpu.last_step_fetched_instruction());
    CHECK(interrupt.s.pc == 0x500);
    CHECK(interrupt.s.last_pc == 0x1000 && interrupt.s.last_opcode == opcode);
  }

  {
    Fixture unavailable;
    const u32 opcode = imm(14, 3, 0, 7);
    unavailable.run(opcode);
    unavailable.memory.unsupported_device = true;
    unavailable.s.pc = 0x3000;
    bool unsupported = false;
    try {
      unavailable.cpu.step();
    } catch (const UnsupportedInstruction &) {
      unsupported = true;
    }
    CHECK(unsupported && !unavailable.cpu.last_step_fetched_instruction());
    CHECK(unavailable.s.pc == 0x3000 && unavailable.s.last_pc == 0x1000);
    CHECK(unavailable.s.last_opcode == opcode);
  }

  {
    Fixture little_endian;
    const u32 opcode = imm(14, 3, 0, 7);
    little_endian.run(opcode);
    little_endian.s.msr = msr::le;
    CHECK_THROWS(little_endian.cpu.step());
    CHECK(!little_endian.cpu.last_step_fetched_instruction());
    CHECK(little_endian.s.last_pc == 0x1000 && little_endian.s.last_opcode == opcode);
  }

  for (bool unfinished_device : {false, true}) {
    Fixture unfinished;
    const u32 opcode = unfinished_device ? imm(32, 3, 4, 0) : special(371, 3, 1);
    unfinished.memory.unsupported_device = unfinished_device;
    unfinished.s.gpr[4] = 0x3000;
    CHECK_THROWS(unfinished.run(opcode));
    CHECK(unfinished.cpu.last_step_fetched_instruction());
    CHECK(unfinished.s.last_pc == 0x1000 && unfinished.s.last_opcode == opcode);
    CHECK(unfinished.s.pc == 0x1000 && unfinished.s.instructions == 0);
  }

  {
    class HostFaultMemory final : public Memory {
    public:
      bool fail = false;

      u32 read32(u32 address) override {
        if (fail)
          throw std::runtime_error("Authored diagnostic fetch failure");
        return Memory::read32(address);
      }
    } memory;

    Interpreter cpu(memory);
    auto &s = cpu.state();
    memory.write32(0x1000, 0x60000000);
    s.pc = 0x1000;
    cpu.step();
    CHECK(cpu.last_step_fetched_instruction());
    memory.fail = true;
    s.pc = 0x1000;
    bool host_error = false;
    try {
      cpu.step();
    } catch (const UnsupportedInstruction &) {
      CHECK(false);
    } catch (const std::runtime_error &error) {
      host_error = true;
      CHECK(std::string(error.what()) == "Authored diagnostic fetch failure");
    }
    CHECK(host_error && !cpu.last_step_fetched_instruction());
    CHECK(s.pc == 0x1000 && s.last_pc == 0x1000 && s.last_opcode == 0x60000000);
  }
}

void zero_dispatch_tests() {
  unsigned cases = 0;
  for (Console console : {Console::GameCube, Console::Wii})
    for (bool enabled : {false, true})
      for (unsigned monitor_mode : {0u, 1u, 11u})
        for (u32 address : {0x2000u, 0x01802000u, 0x02002000u, 0x0c002000u}) {
          cupid::Memory memory(console);
          Interpreter cpu(memory, console);
          auto &s = cpu.state();
          unsigned pi = 0, devices = 0;
          memory.memory_error = [&] { ++pi; };
          memory.mmio_handles = [](u32 value) { return (value & 0xf8000000) == 0x08000000; };
          memory.mmio_read = [&](u32, unsigned) {
            ++devices;
            return 0x3860002au;
          };
          s.pc = address;
          s.gpr[3] = 0xabcdef01;
          s.spr[spr::srr0] = 0x12345678;
          s.spr[spr::srr1] = 0x56789abc;
          s.spr[spr::dar] = 0x23456789;
          s.spr[spr::dsisr] = 0x6789abcd;
          s.spr[spr::hid0] = enabled ? 0x8000u : 0;
          s.spr[spr::mmcr0] = monitor_mode == 0 ? 0u : (1u << 6) | monitor_mode;
          s.spr[spr::mmcr1] = monitor_mode == 0 ? 0u : (monitor_mode << 27) | (1u << 22);
          const std::array<unsigned, 4> counters{spr::pmc1, spr::pmc2, spr::pmc3, spr::pmc4};
          for (unsigned n = 0; n < counters.size(); ++n)
            s.spr[counters[n]] = 0x12345678u + n;
          s.cycles = 12345;
          s.timebase = 0x12345678;
          CHECK(cpu.step() == 0);
          CHECK(cpu.last_step_fetched_instruction());
          CHECK(s.pc == address + 4 && s.last_pc == address && s.last_opcode == 0);
          CHECK(s.cycles == 12345 && s.timebase == 0x12345678);
          CHECK(s.instructions == 0);
          CHECK(s.gpr[3] == 0xabcdef01 && s.spr[spr::srr0] == 0x12345678 &&
                s.spr[spr::srr1] == 0x56789abc);
          CHECK(s.spr[spr::dar] == 0x23456789 && s.spr[spr::dsisr] == 0x6789abcd);
          CHECK(pi == 0 && devices == 0);
          CHECK(memory.instruction_backing_read_errors() == static_cast<u64>(address != 0x2000));
          for (unsigned n = 0; n < counters.size(); ++n)
            CHECK(s.spr[counters[n]] == 0x12345678u + n);
          ++cases;
        }
  CHECK(cases == 48);
  Fixture f;
  for (unsigned n = 0; n < 11; ++n)
    f.run(0x60000000);
  f.s.spr[spr::dec] = 100;
  const auto cycles = f.s.cycles, instructions = f.s.instructions;
  for (unsigned n = 0; n < 10; ++n) {
    CHECK(f.run(0) == 0);
    CHECK(f.s.pc == 0x1004 && f.s.last_opcode == 0);
    CHECK(f.s.cycles == cycles && f.s.instructions == instructions);
    CHECK(f.s.timebase == 0 && f.s.spr[spr::dec] == 100);
  }
  f.run(0x60000000);
  CHECK(f.s.cycles == cycles + 1 && f.s.instructions == instructions + 1);
  CHECK(f.s.timebase == 1 && f.s.spr[spr::dec] == 100);
}

void instruction_fill_failure_tests() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    cupid::Memory memory(console);
    Interpreter cpu(memory, console);
    auto &state = cpu.state();
    unsigned pi = 0, device_reads = 0;
    memory.memory_error = [&] { ++pi; };
    memory.mmio_handles = [](u32 address) { return (address & 0xf8000000) == 0x08000000; };
    memory.mmio_read = [&](u32, unsigned) {
      ++device_reads;
      return imm(14, 11, 0, 99);
    };
    const u32 retained = imm(14, 11, 0, 7);
    memory.write32(0x4000, retained);
    memory.write32(0x1020, ext(982, 0, 0, 4));
    state.spr[spr::hid0] = 0x8000;
    state.pc = 0x4000;
    cpu.step();
    CHECK(state.gpr[11] == 7);
    state.gpr[4] = 0x4000;
    state.pc = 0x1020;
    cpu.step();
    state.pc = 0x02004000;
    state.gpr[11] = 0;
    cpu.step();
    CHECK(state.pc == 0x02004004 && state.gpr[11] == 7);
    CHECK(state.last_opcode == retained && state.last_pc == 0x02004000);
    CHECK(state.spr[spr::srr0] == 0 && state.spr[spr::dar] == 0);
    CHECK(state.instructions == 3 && pi == 0 && device_reads == 0);
    CHECK(memory.instruction_backing_read_errors() == 1);
    // Failed fills still install the line, so a locked hit is defined.
    state.spr[spr::hid0] = 0xa000;
    state.pc = 0x02004000;
    state.gpr[11] = 0;
    cpu.step();
    CHECK(state.pc == 0x02004004 && state.gpr[11] == 7);
    CHECK(memory.instruction_backing_read_errors() == 1);
    cpu.reset();
    state.spr[spr::hid0] = 0x8000;
    state.pc = 0x02004000;
    cpu.step();
    CHECK(state.pc == 0x02004004 && state.gpr[11] == 7);
    CHECK(state.spr[spr::srr0] == 0 && pi == 0 && device_reads == 0);
    CHECK(memory.instruction_backing_read_errors() == 2);

    // A locked invalid miss has no defined initialized value in the caller.
    cpu.reset();
    state.spr[spr::hid0] = 0xa000;
    state.pc = 0x01804000;
    const auto cycles = state.cycles, instructions = state.instructions;
    bool unsupported = false;
    try {
      cpu.step();
    } catch (const UnsupportedInstruction &error) {
      unsupported = true;
      CHECK(error.pc == 0x01804000);
    }
    CHECK(unsupported && state.pc == 0x01804000);
    CHECK(state.cycles == cycles && state.instructions == instructions);
    CHECK(state.spr[spr::srr0] == 0 && state.spr[spr::dar] == 0);
    CHECK(pi == 0 && device_reads == 0);
    CHECK(memory.instruction_backing_read_errors() == 3);
  }
}

} // namespace

int main() {
  return run_tests([] {
    instruction_byte_bus_tests();
    odd_instruction_tests();
    fetch_diagnostic_tests();
    translated_fetch_fault_tests();
    zero_dispatch_tests();
    instruction_fill_failure_tests();
    integer_tests();
    branch_tests();
    cache_range_loop_tests();
    memory_tests();
    cpu_data_path_tests();
    physical_data_memory_tests();
    physical_cache_alias_tests();
    cache_zero_transaction_tests();
    string_store_transaction_tests();
    uncached_write_tests();
    page_write_attribute_result_tests();
    uncached_translation_tests();
    uncached_region_tests();
    split_store_fault_tests();
    changed_mapping_transaction_tests();
    paired_page_store_tests();
    split_load_tests();
    external_control_tests();
    conditional_store_fault_tests();
    exceptions_and_system();
    special_register_fidelity_tests();
    msr_exception_tests();
    floating_exception_retry_tests();
    locked_cache_dma_tests();
    non_ieee_boundary_tests();
    timebase_tests();
    decrementer_tests();
    performance_monitor_tests();
    mmu_tests();
    bat_mapping_boundary_tests();
    floating_tests();
    paired_tests();
    cache_tests();
  });
}
