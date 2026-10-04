// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/memory.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <vector>

namespace {
void fifo_page(cupid::Console console) {
  cupid::Memory memory(console);
  std::vector<cupid::u8> captured, expected;
  memory.fifo_write = [&](cupid::u8 byte) { captured.push_back(byte); };
  const std::array<cupid::u8, 15> bytes{0xa5, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 1,
                                        2,    3,    4,    5,    6,    7,    8};
  auto stores = [&](cupid::u32 address) {
    memory.write8(address + 1, 0xa5);
    memory.write16(address + 2, 0x1234);
    memory.write32(address + 4, 0x56789abc);
    memory.write64(address, 0x0102030405060708);
    expected.insert(expected.end(), bytes.begin(), bytes.end());
    CHECK(captured == expected);
  };
  for (cupid::u32 offset = 0; offset < 0x1000; offset += 32)
    stores(0x0c008000 + offset);
  stores(0x0c008ff8);
  memory.write8(0x0c008fff, 0x11);
  memory.write16(0x0c008ffe, 0x2233);
  memory.write32(0x0c008ffc, 0x44556677);
  const std::array<cupid::u8, 7> end{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
  expected.insert(expected.end(), end.begin(), end.end());
  CHECK(captured == expected);
  CHECK_THROWS(memory.write8(0x0c007fff, 1));
  CHECK_THROWS(memory.write8(0x0c009000, 1));
  CHECK_THROWS(memory.read32(0x0c008020));
  CHECK(captured == expected);
  memory.write32(0x8ffc, 0x12345678);
  CHECK(memory.read32(0x8ffc) == 0x12345678 && captured == expected);
  memory.fifo_write = {};
  try {
    memory.write8(0x0c008fff, 0);
    CHECK(false);
  } catch (const cupid::BusFault &error) {
    CHECK(error.address == 0x0c008fff && error.write && error.unsupported_reason);
  }
}

void memory_error_signal(cupid::Console console) {
  cupid::Memory memory(console);
  unsigned signals = 0;
  try {
    memory.memory_access_error();
    CHECK(false);
  } catch (const cupid::BusFault &error) {
    CHECK(error.write && error.unsupported_reason);
  }
  memory.memory_error = [&] { ++signals; };
  memory.write32(0x1000, 0x12345678);
  CHECK(signals == 0);
  memory.memory_access_error();
  memory.memory_access_error();
  CHECK(signals == 2 && memory.read32(0x1000) == 0x12345678);
}

void cpu_device_read_widths(cupid::Console console) {
  using namespace cupid;
  Memory memory(console);
  unsigned calls = 0, signals = 0;
  u32 expected_address = 0;
  unsigned expected_width = 0;
  memory.memory_error = [&] { ++signals; };
  memory.mmio_handles = [](u32 address) {
    return (address >= 0x08000000 && address < 0x0c000000) || address == 0x0c003000;
  };
  memory.mmio_read = [&](u32 address, unsigned width) {
    CHECK(address == expected_address && width == expected_width);
    ++calls;
    return 0x45102030u;
  };
  for (u32 address : {0x08000000u, 0x08000003u, 0x0c003000u})
    for (unsigned width : {1u, 2u, 4u}) {
      expected_address = address;
      expected_width = width;
      const auto before = calls;
      const u64 expected = width == 1 ? 0x30 : width == 2 ? 0x2030 : 0x45102030;
      CHECK(memory.read_cpu_data(address, width) == expected);
      CHECK(calls == before + 1);
    }
  expected_address = 0x08000000;
  expected_width = 8;
  CHECK(memory.read_cpu_data(0x08000000, 8) == 0x0000000045102030ULL);
  CHECK(calls == 10);
  CHECK_THROWS(memory.read_cpu_data(0x0c003000, 8));
  CHECK(calls == 10 && signals == 0);
  memory.mmio_read = [&](u32 address, unsigned width) {
    CHECK(width == 1 && address == 0x08000fff + (calls - 10));
    ++calls;
    return 0x45102030u ^ address;
  };
  CHECK(memory.read_cpu_data(0x08000fff, 2) == 0xcf30);
  CHECK(calls == 12 && signals == 0);
}

void cpu_ram_and_host_bounds(cupid::Console console) {
  using namespace cupid;
  Memory memory(console);
  unsigned signals = 0;
  memory.memory_error = [&] { ++signals; };
  // Real RAM is 24 MiB; CPU data also sees the allocated 32 MiB and its mirrors.
  for (u32 mirror : {0u, 0x02000000u, 0x04000000u, 0x06000000u})
    for (u32 offset : {0x20u, 0x017ffffcu, 0x01800000u, 0x01fffffcu}) {
      const u32 address = mirror | offset;
      memory.write_cpu_data(address, 0x89abcdef, 4);
      CHECK(memory.read_cpu_data(address, 1) == 0x89);
      CHECK(memory.read_cpu_data(address, 2) == 0x89ab);
      CHECK(memory.read_cpu_data(address, 4) == 0x89abcdef);
      CHECK(memory.read_cpu_data(offset, 4) == 0x89abcdef);
      if (address < Memory::mem1_size)
        CHECK(memory.read32(address) == 0x89abcdef);
      else
        CHECK_THROWS(memory.range(address, 4));
    }
  CHECK(signals == 0);

  memory.write_cpu_data(0x01fffffc, 0x01234567, 4);
  memory.write_cpu_data(0x02000000, 0x89abcdef, 4);
  CHECK(memory.read_cpu_data(0x01fffffc, 8) == 0x0123456789abcdefULL);
  memory.write_cpu_data(0x01fffffe, 0x123456, 3);
  CHECK(memory.read_cpu_data(0x01fffffe, 2) == 0x1234);
  CHECK(memory.read8(0) == 0x56 && signals == 0);
  memory.write_cpu_data(0xe003ffff, 0xab, 1);
  CHECK(memory.read_cpu_data(0xe003ffff, 2) == 0xab00);
  CHECK(signals == 1);

  memory.write32(0x20, 0x10203040);
  for (u32 alias : {0x40000000u, 0x80000000u, 0xc0000000u}) {
    CHECK(memory.read32(alias | 0x20) == 0x10203040);
    CHECK(memory.range(alias | 0x20, 4).data() == memory.range(0x20, 4).data());
    CHECK_THROWS(memory.range(alias | (Memory::mem1_size - 2), 4));
    CHECK(memory.read_cpu_data(alias | 0x20, 4) == 0);
    memory.write_cpu_data(alias | 0x20, 0xaabbccdd, 4);
    CHECK(memory.read32(0x20) == 0x10203040);
  }
  CHECK(signals == 7);
  CHECK_THROWS(memory.read32(Memory::mem1_size));
  CHECK_THROWS(memory.copy_in(Memory::mem1_size, std::array<u8, 4>{1, 2, 3, 4}));
  std::array<u8, 32> guarded;
  guarded.fill(0x5a);
  CHECK_THROWS(memory.copy_out(0x02000000, guarded));
  CHECK(std::all_of(guarded.begin(), guarded.end(), [](u8 byte) { return byte == 0x5a; }));
  memory.write32(0xe0001000, 0x11223344);
  CHECK_THROWS(memory.locked_cache_dma(Memory::mem1_size, 0x1000, 32, true));
  CHECK_THROWS(memory.locked_cache_dma(0x02000000, 0x1000, 32, true));
  CHECK(memory.read32(0xe0001000) == 0x11223344 && signals == 7);

  if (console == Console::Wii) {
    memory.write_cpu_data(0x13fffffc, 0x55667788, 4);
    CHECK(memory.read_cpu_data(0x13fffffc, 4) == 0x55667788);
    CHECK(memory.read32(0x53fffffc) == 0x55667788);
    CHECK_THROWS(memory.range(0x53fffffe, 4));
    CHECK(memory.read_cpu_data(0x14000000, 4) == 0);
  } else {
    CHECK(memory.read_cpu_data(0x10000000, 4) == 0);
  }
  CHECK(signals == 8);
  CHECK_THROWS(memory.read_cpu_data(0, 3));
  CHECK_THROWS(memory.write_cpu_data(0, 1, 8));
  CHECK(signals == 8);

  CHECK(memory.instruction_cache_key(0xc6000040) == 0x40);
  CHECK(memory.instruction_cache_key(0x54000040) == 0x10000040);
  CHECK(memory.instruction_cache_key(0x30000040) == 0x20000040);
}

void strict_page_table_writes(cupid::Console console) {
  using namespace cupid;
  Memory memory(console);
  unsigned signals = 0;
  memory.memory_error = [&] { ++signals; };
  CHECK(memory.write_page_table32(0x80000020, 0x12340180));
  CHECK(memory.read32(0x20) == 0x12340180);
  memory.write_cpu_data(0x01800004, 0x00002002, 4);
  memory.write_cpu_data(0xe0000004, 0x10203040, 4);
  CHECK(!memory.write_page_table32(0x01800004, 0x00002182));
  CHECK(memory.read_cpu_data(0x01800004, 4) == 0x00002002);
  CHECK(!memory.write_page_table32(0x02000020, 0x55667788));
  CHECK(memory.read32(0x20) == 0x12340180);
  CHECK(!memory.write_page_table32(0xe0000004, 0xaabbccdd));
  CHECK(memory.read32(0xe0000004) == 0x10203040);
  CHECK(!memory.write_page_table32(0x017ffffe, 0xffffffff));
  CHECK(memory.page_table_write_errors() == 4 && signals == 0);
  memory.clear();
  CHECK(memory.page_table_write_errors() == 0);
  CHECK(memory.read_cpu_data(0x01800004, 4) == 0 && signals == 0);
}

void instruction_backing_reads(cupid::Console console) {
  using namespace cupid;
  Memory memory(console);
  unsigned signals = 0, device_reads = 0;
  memory.memory_error = [&] { ++signals; };
  memory.mmio_handles = [](u32 address) { return address >= 0x08000000; };
  memory.mmio_read = [&](u32, unsigned) {
    ++device_reads;
    return 0xdeadbeefu;
  };
  const std::array<u8, 32> code{0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x10, 0x20, 0x30,
                                0x40, 0xa5, 0xa6, 0xa7, 0xa8, 0,    0,    0,    0,    0xff, 0xff,
                                0xff, 0xff, 0x80, 0,    0,    0,    0x7f, 0xff, 0xff, 0xff};
  const std::array<u32, 8> expected{0x01234567, 0x89abcdef, 0x10203040, 0xa5a6a7a8,
                                    0,          0xffffffff, 0x80000000, 0x7fffffff};
  memory.copy_in(0x1000, code);
  if (console == Console::Wii)
    memory.copy_in(0x10001000, code);
  for (u32 base : {0u, 0x10000000u}) {
    if (base != 0 && console != Console::Wii)
      continue;
    for (u32 alias : {0u, 0x40000000u, 0x80000000u, 0xc0000000u}) {
      std::array<u32, 8> line{};
      CHECK(memory.read_instruction_words(alias | base | 0x1000, line));
      CHECK(line == expected);
      for (unsigned word = 0; word < expected.size(); ++word) {
        std::array<u32, 1> single{0xdeadbeef};
        CHECK(memory.read_instruction_words(alias | base | (0x1000 + word * 4), single));
        CHECK(single[0] == expected[word]);
      }
    }
  }
  CHECK(memory.instruction_backing_read_errors() == 0 && signals == 0 && device_reads == 0);
  std::array<u32, 8> line;
  line.fill(0xaabbccdd);
  const auto guard = line;
  const std::array<u32, 9> invalid{0x01800000, 0x02001000, 0x017ffff0, 0x08000000, 0x0c003000,
                                   0xe0000000, 0x60001000, 0x14000000, 0x13fffff0};
  for (u32 address : invalid) {
    CHECK(!memory.read_instruction_words(address, line));
    CHECK(line == guard);
  }
  std::array<u32, 1> single{0xfeedface};
  CHECK(!memory.read_instruction_words(0x017ffffe, single));
  CHECK(single[0] == 0xfeedface);
  CHECK(memory.instruction_backing_read_errors() == 10);
  CHECK_THROWS(memory.read_instruction_words(0, std::span<u32>{}));
  CHECK_THROWS(memory.read_instruction_words(0, std::span<u32>(line).first(3)));
  CHECK(memory.instruction_backing_read_errors() == 10);
  CHECK(memory.page_table_write_errors() == 0 && signals == 0 && device_reads == 0);
  memory.clear();
  CHECK(memory.instruction_backing_read_errors() == 0);
  CHECK(memory.read_instruction_words(0x1000, line));
  CHECK(std::all_of(line.begin(), line.end(), [](u32 word) { return word == 0; }));
}

void instruction_byte_reads(cupid::Console console) {
  using namespace cupid;
  Memory memory(console);
  unsigned signals = 0, device_reads = 0;
  memory.memory_error = [&] { ++signals; };
  memory.mmio_handles = [](u32 address) { return address >= 0x08000000; };
  memory.mmio_read = [&](u32, unsigned) {
    ++device_reads;
    return 0xdeadbeefu;
  };
  std::array<u8, 32> source{};
  for (unsigned n = 0; n < source.size(); ++n)
    source[n] = static_cast<u8>(0x31 + 7 * n);
  memory.copy_in(0x101, source);
  memory.copy_in(Memory::mem1_size - 32, source);
  if (console == Console::Wii) {
    memory.copy_in(0x10000101, source);
    memory.copy_in(0x10000000 + Memory::mem2_size - 32, source);
  }
  u64 rejected = 0;
  for (unsigned width = 1; width <= 32; ++width) {
    std::array<u8, 40> guarded;
    const auto check = [&](u32 address, bool accepted, unsigned source_offset = 0) {
      guarded.fill(0x5a);
      const auto retained = guarded;
      const auto output = std::span(guarded).subspan(4, width);
      CHECK(memory.read_instruction_bytes(address, output) == accepted);
      if (accepted) {
        CHECK(std::equal(output.begin(), output.end(), source.begin() + source_offset));
        CHECK(std::all_of(guarded.begin(), guarded.begin() + 4,
                          [](u8 byte) { return byte == 0x5a; }));
        CHECK(std::all_of(guarded.begin() + 4 + width, guarded.end(),
                          [](u8 byte) { return byte == 0x5a; }));
      } else {
        ++rejected;
        CHECK(guarded == retained);
      }
      CHECK(memory.instruction_backing_read_errors() == rejected);
      CHECK(signals == 0 && device_reads == 0 && memory.page_table_write_errors() == 0);
    };
    for (u32 alias : {0u, 0x40000000u, 0x80000000u, 0xc0000000u}) {
      check(alias | 0x101, true);
      check(alias | (Memory::mem1_size - width), true, 32 - width);
      check(alias | (Memory::mem1_size - width + 1), false);
      check(alias | 0x10000101, console == Console::Wii);
      check(alias | (0x10000000 + Memory::mem2_size - width), console == Console::Wii, 32 - width);
      check(alias | (0x10000000 + Memory::mem2_size - width + 1), false);
    }
    for (u32 address : {0x01800000u, 0x02000101u, 0x04000101u, 0x08000000u, 0x0c003000u,
                        0xe0000000u, 0x60000101u, 0xfffffff0u})
      check(address, false);
    CHECK(std::equal(source.begin(), source.end(), memory.range(0x101, source.size()).begin()));
  }
  std::array<u8, 33> invalid;
  invalid.fill(0x7c);
  const auto retained = invalid;
  CHECK_THROWS(memory.read_instruction_bytes(0, std::span<u8>{}));
  CHECK_THROWS(memory.read_instruction_bytes(0, invalid));
  CHECK(invalid == retained && memory.instruction_backing_read_errors() == rejected);
  CHECK(signals == 0 && device_reads == 0);
  memory.clear();
  CHECK(memory.instruction_backing_read_errors() == 0);
  CHECK(memory.read_instruction_bytes(0, std::span(invalid).first(32)));
  CHECK(std::all_of(invalid.begin(), invalid.begin() + 32, [](u8 byte) { return byte == 0; }));
  CHECK(invalid.back() == 0x7c);
}
} // namespace

int main() {
  return run_tests([] {
    fifo_page(cupid::Console::GameCube);
    fifo_page(cupid::Console::Wii);
    memory_error_signal(cupid::Console::GameCube);
    memory_error_signal(cupid::Console::Wii);
    cpu_device_read_widths(cupid::Console::GameCube);
    cpu_device_read_widths(cupid::Console::Wii);
    cpu_ram_and_host_bounds(cupid::Console::GameCube);
    cpu_ram_and_host_bounds(cupid::Console::Wii);
    strict_page_table_writes(cupid::Console::GameCube);
    strict_page_table_writes(cupid::Console::Wii);
    instruction_backing_reads(cupid::Console::GameCube);
    instruction_backing_reads(cupid::Console::Wii);
    instruction_byte_reads(cupid::Console::GameCube);
    instruction_byte_reads(cupid::Console::Wii);
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
