// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/elf.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <vector>

using namespace cupid;

namespace {
using Bytes = std::vector<u8>;

void put(Bytes &bytes, std::size_t offset, u32 value, unsigned width = 4) {
  for (unsigned i = 0; i < width; ++i)
    bytes.at(offset + i) = static_cast<u8>(value >> ((width - i - 1) * 8));
}

Bytes executable() {
  Bytes file(0x180);
  file[0] = 0x7f;
  file[1] = 'E';
  file[2] = 'L';
  file[3] = 'F';
  file[4] = 1;
  file[5] = 2;
  file[6] = 1;
  put(file, 16, 2, 2);
  put(file, 18, 20, 2);
  put(file, 20, 1);
  put(file, 24, 0x80001000);
  put(file, 28, 52);
  put(file, 40, 52, 2);
  put(file, 42, 32, 2);
  put(file, 44, 2, 2);
  const std::array<u32, 5> guest{0x38600007, 0x3c808000, 0x60842000, 0x90640000, 0x4e800020};
  for (std::size_t i = 0; i < guest.size(); ++i)
    put(file, 0x100 + i * 4, guest[i]);
  const std::array<u32, 8> code{1, 0x100, 0x80001000, 0x1000, 20, 64, 5, 4};
  const std::array<u32, 8> data{1, 0x120, 0x80002000, 0, 8, 32, 6, 4};
  for (std::size_t i = 0; i < 8; ++i) {
    put(file, 52 + i * 4, code[i]);
    put(file, 84 + i * 4, data[i]);
  }
  put(file, 0x120, 0xdeadbeef);
  put(file, 0x124, 0x11223344);
  return file;
}

void execute_test() {
  Memory memory(Console::GameCube);
  std::fill(memory.range(0x1000, 64).begin(), memory.range(0x1000, 64).end(), 0xee);
  std::fill(memory.range(0x2000, 32).begin(), memory.range(0x2000, 32).end(), 0xee);
  const auto file = executable();
  const auto loaded = runtime::load_elf(memory, file);
  CHECK(loaded.entry == 0x80001000);
  CHECK(loaded.loaded_bytes == 28);
  CHECK(loaded.loaded_sections == 2);
  CHECK(memory.read32(0x1000) == 0x38600007);
  CHECK(memory.read32(0x2000) == 0xdeadbeef);
  CHECK(memory.read32(0x2004) == 0x11223344);
  for (u32 offset = 20; offset < 64; ++offset)
    CHECK(memory.read8(0x1000 + offset) == 0);
  for (u32 offset = 8; offset < 32; ++offset)
    CHECK(memory.read8(0x2000 + offset) == 0);
  cpu::Interpreter interpreter(memory);
  interpreter.configure_boot();
  interpreter.state().pc = loaded.entry;
  interpreter.state().spr[cpu::spr::lr] = 0;
  for (int instruction = 0; instruction < 5; ++instruction)
    interpreter.step();
  CHECK(interpreter.state().pc == 0);
  CHECK(interpreter.state().gpr[3] == 7);
  CHECK(memory.read32(0x2000) == 7);
}

void invalid_tests() {
  Memory memory(Console::GameCube);
  const auto original = executable();
  auto fail = [&](Bytes file) {
    memory.write32(0x1000, 0xaabbccdd);
    memory.write32(0x2000, 0x12345678);
    CHECK_THROWS(runtime::load_elf(memory, file));
    CHECK(memory.read32(0x1000) == 0xaabbccdd);
    CHECK(memory.read32(0x2000) == 0x12345678);
  };
  for (std::size_t length :
       {std::size_t{0}, std::size_t{16}, std::size_t{51}, std::size_t{83}, std::size_t{0x108}}) {
    auto file = original;
    file.resize(length);
    fail(file);
  }
  for (const auto &mutation : std::array<std::array<u32, 3>, 21>{{{0, 0, 1},
                                                                  {4, 2, 1},
                                                                  {5, 1, 1},
                                                                  {6, 0, 1},
                                                                  {16, 1, 2},
                                                                  {18, 3, 2},
                                                                  {20, 2, 4},
                                                                  {40, 51, 2},
                                                                  {42, 31, 2},
                                                                  {44, 0, 2},
                                                                  {28, 0xffffffff, 4},
                                                                  {32, 0xffffffff, 4},
                                                                  {50, 1, 2},
                                                                  {24, 0x80001002, 4},
                                                                  {24, 0x80002000, 4},
                                                                  {24, 0x80001014, 4},
                                                                  {52 + 4, 0xfffffff0, 4},
                                                                  {52 + 16, 65, 4},
                                                                  {52 + 28, 3, 4},
                                                                  {52 + 8, 0x80001002, 4},
                                                                  {84 + 12, 0x03000000, 4}}}) {
    auto file = original;
    put(file, mutation[0], mutation[1], mutation[2]);
    fail(file);
  }
  auto wrapped = original;
  put(wrapped, 84 + 8, 0xfffffff0);
  put(wrapped, 84 + 20, 32);
  fail(wrapped);
  auto bad_physical = original;
  put(bad_physical, 84 + 12, 0x7ffffff0);
  put(bad_physical, 84 + 20, 32);
  fail(bad_physical);
  auto inconsistent = original;
  put(inconsistent, 52 + 12, 0x3000);
  fail(inconsistent);
}

void sections_and_order_tests() {
  Memory memory(Console::GameCube);
  auto file = executable();
  put(file, 32, 0x130);
  put(file, 46, 40, 2);
  put(file, 48, 2, 2);
  file.resize(0x180);
  put(file, 0x130 + 40 + 4, 8);
  put(file, 0x130 + 40 + 16, 0xffffffff);
  put(file, 0x130 + 40 + 20, 0x80);
  CHECK(runtime::load_elf(memory, file).loaded_sections == 2); // SHT_NOBITS occupies no file bytes.
  put(file, 0x130 + 40 + 4, 1);
  CHECK_THROWS(runtime::load_elf(memory, file));
  file = executable();
  put(file, 84 + 8, 0x80001010);
  put(file, 84 + 12, 0x1010);
  runtime::load_elf(memory, file);
  CHECK(memory.read32(0x1010) == 0xdeadbeef);
  CHECK(memory.read32(0x1014) == 0x11223344);
  CHECK(memory.read32(0x1018) == 0);
  file = executable();
  put(file, 52 + 12, 0);
  CHECK(runtime::load_elf(memory, file).entry == 0x80001000);
  put(file, 24, 0xc0001000);
  CHECK(runtime::load_elf(memory, file).entry == 0xc0001000);
  // Non-loadable metadata segments are bounds checked but do not touch RAM.
  file = executable();
  put(file, 44, 3, 2);
  put(file, 116, 4);
  put(file, 120, 0x150);
  put(file, 128, 0xffffffff);
  put(file, 132, 4);
  put(file, 136, 4);
  CHECK(runtime::load_elf(memory, file).loaded_sections == 2);
}

void wii_tests() {
  Memory memory(Console::Wii);
  auto file = executable();
  put(file, 24, 0x90001000);
  put(file, 52 + 8, 0x90001000);
  put(file, 52 + 12, 0x10001000);
  CHECK(runtime::load_elf(memory, file).entry == 0x90001000);
  CHECK(memory.read32(0x10001000) == 0x38600007);
  Memory smaller(Console::GameCube);
  CHECK_THROWS(runtime::load_elf(smaller, file));
}
} // namespace

int main() {
  return run_tests([] {
    execute_test();
    invalid_tests();
    sections_and_order_tests();
    wii_tests();
  });
}
