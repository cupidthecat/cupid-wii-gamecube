// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/memory.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>

int main() {
  return run_tests([] {
    cupid::Memory gc(cupid::Console::GameCube);
    gc.write32(8, 0x12345678);
    CHECK(gc.read8(8) == 0x12);
    CHECK(gc.read16(10) == 0x5678);
    CHECK(gc.read32(8) == 0x12345678);
    gc.write64(16, 0x0123456789abcdefULL);
    CHECK(gc.read64(16) == 0x0123456789abcdefULL);
    CHECK_THROWS(gc.read32(0x01800000));
    CHECK_THROWS(gc.read32(0x10000000));
    gc.write8(cupid::Memory::mem1_size - 1, 0x55);
    CHECK_THROWS(gc.write32(cupid::Memory::mem1_size - 2, 0x12345678));
    CHECK(gc.read8(cupid::Memory::mem1_size - 1) == 0x55);
    CHECK_THROWS(gc.read64(0xfffffffc));
    gc.write32(0xe003fffc, 0x10203040);
    CHECK(gc.read32(0xe003fffc) == 0x10203040);
    CHECK_THROWS(gc.write32(0xe003ffff, 1));
    CHECK(cupid::Memory::boot_physical(0x80000008) == 8);
    CHECK(cupid::Memory::boot_physical(0x90000000) == 0x10000000);
    cupid::Memory wii(cupid::Console::Wii);
    wii.write32(0x13fffffc, 0xaabbccdd);
    CHECK(wii.read32(0x13fffffc) == 0xaabbccdd);
    CHECK_THROWS(wii.read32(0x14000000));
    unsigned count = 0;
    gc.mmio_handles = [](cupid::u32 a) { return a == 0x0c003000; };
    gc.mmio_read = [&](cupid::u32, unsigned width) {
      CHECK(width == 4);
      ++count;
      return 0x11223344;
    };
    CHECK(gc.read32(0x0c003000) == 0x11223344);
    CHECK(count == 1);
    std::array<cupid::u8, 8> fifo{};
    unsigned pos = 0;
    gc.fifo_write = [&](cupid::u8 byte) { fifo.at(pos++) = byte; };
    gc.write64(0x0c008000, 0x1020304050607080);
    CHECK(fifo[0] == 0x10);
    CHECK(fifo[7] == 0x80);
    CHECK(pos == 8);

    std::array<cupid::u8, 4096> transfer{};
    for (unsigned i = 0; i < transfer.size(); ++i)
      transfer[i] = static_cast<cupid::u8>(19 + 37 * i);
    gc.copy_in(0x1000, transfer);
    for (cupid::u32 alias : {0u, 0x40000000u, 0x80000000u, 0xc0000000u}) {
      gc.locked_cache_dma(alias | 0x1000, 0xe0000120, 4096, true);
      CHECK(std::equal(transfer.begin(), transfer.end(), gc.range(0xe0000120, 4096).begin()));
      gc.write32(0x2ffc, 0x11223344);
      gc.write32(0x4000, 0xaabbccdd);
      gc.locked_cache_dma(alias | 0x3000, 0xe0000120, 4096, false);
      CHECK(std::equal(transfer.begin(), transfer.end(), gc.range(0x3000, 4096).begin()));
      CHECK(gc.read32(0x2ffc) == 0x11223344 && gc.read32(0x4000) == 0xaabbccdd);
    }
    wii.copy_in(0x10001000, transfer);
    wii.locked_cache_dma(0x90001000, 0x40000, 4096, true);
    wii.locked_cache_dma(0xd0003000, 0xe0000000, 4096, false);
    CHECK(std::equal(transfer.begin(), transfer.end(), wii.range(0x10003000, 4096).begin()));
    gc.locked_cache_dma(0x1000, 0xe003ffe0, 32, true);
    CHECK(std::equal(transfer.begin(), transfer.begin() + 32, gc.range(0xe003ffe0, 32).begin()));
    CHECK_THROWS(gc.locked_cache_dma(0x1000, 0xe003ffe0, 64, true));
    CHECK(std::equal(transfer.begin(), transfer.begin() + 32, gc.range(0xe003ffe0, 32).begin()));
    CHECK_THROWS(gc.locked_cache_dma(0x1000, 0, 0, true));
    CHECK_THROWS(gc.locked_cache_dma(0x1000, 0, 33, true));
    CHECK_THROWS(gc.locked_cache_dma(0x1000, 0, 4128, true));
    CHECK_THROWS(gc.locked_cache_dma(0x1001, 0, 32, true));
    CHECK_THROWS(gc.locked_cache_dma(0x1000, 1, 32, true));
    gc.write32(0xe0008000, 0x11223344);
    CHECK_THROWS(gc.locked_cache_dma(cupid::Memory::mem1_size, 0x8000, 32, true));
    CHECK(gc.read32(0xe0008000) == 0x11223344);
    gc.write32(cupid::Memory::mem1_size - 32, 0x55667788);
    CHECK_THROWS(gc.locked_cache_dma(cupid::Memory::mem1_size - 32, 0, 64, false));
    CHECK(gc.read32(cupid::Memory::mem1_size - 32) == 0x55667788);

    unsigned dma_reads = 0, dma_writes = 0;
    std::array<cupid::u32, 16> copied_words{};
    gc.mmio_handles = [](cupid::u32 address) {
      return address >= 0x0c100000 && address < 0x0c100040;
    };
    gc.mmio_read = [&](cupid::u32 address, unsigned width) {
      CHECK(width == 4);
      ++dma_reads;
      return 0xa1000000 | (address - 0x0c100000);
    };
    gc.mmio_write = [&](cupid::u32 address, cupid::u32 value, unsigned width) {
      CHECK(width == 4);
      ++dma_writes;
      copied_words.at((address - 0x0c100000) / 4) = value;
    };
    gc.locked_cache_dma(0x0c100000, 0xe003ffe0, 64, true);
    CHECK(dma_reads == 16);
    CHECK(gc.read32(0xe003ffe0) == 0xa1000000);
    CHECK(gc.read32(0xe003fffc) == 0xa100001c);
    CHECK(gc.read32(0xe0000000) == 0xa1000020);
    CHECK(gc.read32(0xe000001c) == 0xa100003c);
    gc.locked_cache_dma(0x0c100000, 0xe003ffe0, 64, false);
    CHECK(dma_writes == 16);
    for (unsigned i = 0; i < copied_words.size(); ++i)
      CHECK(copied_words[i] == (0xa1000000 | (i * 4)));
  });
}
