// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/machine.hpp"
#include "test_support.hpp"
#include <array>
#include <vector>

namespace {
using namespace cupid;

void fifo_pointer(Memory &memory, unsigned offset, u32 value) {
  memory.write16(0x0c000000 + offset, static_cast<u16>(value));
  memory.write16(0x0c000002 + offset, static_cast<u16>(value >> 16));
}

void bp(Memory &memory, unsigned index, u32 value) {
  std::vector<u8> bytes{0x61};
  const auto command = (index << 24) | (value & 0xffffff);
  for (unsigned shift = 24;; shift -= 8) {
    bytes.push_back(static_cast<u8>(command >> shift));
    if (!shift)
      break;
  }
  bytes.resize(32);
  for (u8 byte : bytes)
    memory.write8(0x0c008000, byte);
}

void efb_access(Console console) {
  runtime::Machine machine(console);
  machine.load_executable(runtime::self_test_dol());
  auto &memory = machine.memory();
  memory.write32(0x0c00300c, 0x10000);
  memory.write32(0x0c003010, 0x11000);
  memory.write32(0x0c003014, 0x10000);
  for (unsigned offset : {0x20u, 0x34u, 0x38u})
    fifo_pointer(memory, offset, 0x10000);
  fifo_pointer(memory, 0x24, 0x11000);
  memory.write16(0x0c000002, 17);
  bp(memory, 0x43, 1);
  bp(memory, 0x41, 24);
  bp(memory, 0x40, 16);
  bp(memory, 0x49, 0);
  bp(memory, 0x4a, 15 | (1 << 10));
  bp(memory, 0x4f, 0x4411);
  bp(memory, 0x50, 0x2233);
  bp(memory, 0x51, 0xabc123);
  bp(memory, 0x52, 1 << 11);

  CHECK(memory.read32(0x08000000) == 0x00102030);
  memory.write16(0x0c001008, 1);
  CHECK(memory.read32(0x08000000) == 0xff102030);
  memory.write16(0x0c001008, 0x12);
  CHECK(memory.read32(0x08000000) == 0x45102030);
  for (u32 alias : {0x08000000u, 0x09000000u, 0x0a000000u, 0x0b000000u}) {
    CHECK(memory.read32(alias + 4) == 0x45102030);
    CHECK(memory.read32(alias + 0x400004) == 0xabc123);
  }
  CHECK(memory.read8(0x08000003) == 0x30);
  CHECK(memory.read16(0x08000002) == 0x2030);
  CHECK(memory.read64(0x08000000) == 0x0000000045102030ULL);
  CHECK(memory.read32(0x08000040) == 0);
  CHECK(memory.read32(0x08000000 + 640 * 4) == 0);
  CHECK(memory.read32(0x08000000 + 528 * 4096) == 0);
  CHECK(memory.read32(0x08400000 + 640 * 4) == 0);

  auto &state = machine.cpu().state();
  state.pc = 0x80006000;
  state.gpr[3] = 0xc8000000;
  state.gpr[4] = 0xc8800000;
  const std::array<u32, 6> code{(32u << 26) | (5 << 21) | (3 << 16),     // lwz r5,0(r3)
                                (34u << 26) | (6 << 21) | (3 << 16) | 3, // lbz r6,3(r3)
                                (40u << 26) | (7 << 21) | (3 << 16) | 2, // lhz r7,2(r3)
                                (50u << 26) | (8 << 21) | (3 << 16),     // lfd f8,0(r3)
                                (32u << 26) | (9 << 21) | (4 << 16),     // combined color/depth lwz
                                (36u << 26) | (5 << 21) | (3 << 16)};    // unsupported EFB stw
  for (unsigned i = 0; i < code.size(); ++i)
    memory.write32(0x6000 + i * 4, code[i]);
  for (unsigned i = 0; i < 4; ++i) {
    machine.step();
    CHECK(state.pc == 0x80006004 + i * 4);
  }
  CHECK(state.gpr[5] == 0x45102030);
  CHECK(state.gpr[6] == 0x30);
  CHECK(state.gpr[7] == 0x2030);
  CHECK(state.fpr[8].bits[0] == 0x45102030);
  CHECK(state.spr[cpu::spr::dar] == 0);
  CHECK(state.spr[cpu::spr::dsisr] == 0);
  const auto cycles = state.cycles, instructions = state.instructions;
  for (u32 pc : {0x80006010u, 0x80006014u}) {
    state.pc = pc;
    bool unsupported = false;
    try {
      machine.step();
    } catch (const cpu::UnsupportedInstruction &error) {
      unsupported = true;
      CHECK(error.pc == pc);
    }
    CHECK(unsupported);
    CHECK(state.pc == pc);
    CHECK(state.cycles == cycles && state.instructions == instructions);
    CHECK(state.spr[cpu::spr::dar] == 0 && state.spr[cpu::spr::dsisr] == 0);
  }

  memory.locked_cache_dma(0x08000000, 0xe003ffe0, 64, true);
  for (u32 offset = 0; offset < 64; offset += 4) {
    const auto cache = 0xe0000000 | ((0x3ffe0 + offset) & (Memory::cache_size - 1));
    CHECK(memory.read32(cache) == 0x45102030);
  }
  memory.locked_cache_dma(0x08400000, 0xe0001000, 32, true);
  for (u32 offset = 0; offset < 32; offset += 4)
    CHECK(memory.read32(0xe0001000 + offset) == 0xabc123);

  for (u32 format : {0u, 2u, 3u}) {
    bp(memory, 0x43, format);
    CHECK(memory.read32(0x08000000) == 0xff108311);
    CHECK(memory.read32(0x08400000) == 0xabc123);
  }
  bp(memory, 0x43, 1);
  CHECK(memory.read32(0x08000000) == 0x45102030);
  CHECK_THROWS(memory.read32(0x08800000));
  CHECK_THROWS(memory.write32(0x08000000, 0xdeadbeef));
  try {
    memory.write64(0x08000000, 0xdeadbeef11223344ULL);
    CHECK(false);
  } catch (const BusFault &error) {
    CHECK(error.unsupported_reason && error.write && error.address == 0x08000000);
  }
  CHECK(memory.read32(0x08000000) == 0x45102030);
  memory.write16(0x0c001008, 3);
  CHECK_THROWS(memory.read32(0x08000000));
  CHECK(memory.read32(0x08000000 + 640 * 4) == 0);
  CHECK(memory.read32(0x08400000) == 0xabc123);
  CHECK_THROWS(memory.read64(0x0c001000));
}
} // namespace

int main() {
  return run_tests([] {
    efb_access(Console::GameCube);
    efb_access(Console::Wii);
  });
}
