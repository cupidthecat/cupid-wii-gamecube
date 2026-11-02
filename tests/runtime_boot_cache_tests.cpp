// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/boot.hpp"
#include "test_support.hpp"
#include <chrono>
#include <fstream>

using namespace cupid;

namespace {
class LoaderDisc {
public:
  LoaderDisc() {
    path = std::filesystem::temp_directory_path() /
           ("cupid-cache-loader-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".iso");
    std::vector<u8> bytes(0x4000);
    bytes[0] = 'G';
    bytes[1] = 'T';
    bytes[2] = 'S';
    bytes[3] = 'E';
    const auto word = [&](unsigned offset, u32 value) {
      for (unsigned i = 0; i < 4; ++i)
        bytes.at(offset + i) = u8(value >> (24 - i * 8));
    };
    word(0x1c, 0xc2339f3d);
    word(0x2450, 0x81200000);
    word(0x2454, 0x100);
    const auto code = [&](unsigned offset, std::initializer_list<u32> instructions) {
      for (u32 instruction : instructions) {
        word(0x2460 + offset, instruction);
        offset += 4;
      }
    };
    // Entry publishes three callbacks through cacheable guest pointers.
    code(0, {0x3cc08120, 0x60c60040, 0x90c30000, 0x38c60040, 0x90c40000, 0x38c60040, 0x90c50000,
             0x39400000, 0x3ce08130, 0x39001234, 0x91070000, 0x4e800020});
    code(0x40, {0x4e800020});
    // Main requests one original four-byte payload, then reports completion.
    code(0x80,
         {0x2c0a0000, 0x4082002c, 0x39400001, 0x3cc08000, 0x60c65000, 0x90c30000, 0x38c00004,
          0x90c40000, 0x38c03000, 0x90c50000, 0x38600001, 0x4e800020, 0x38600000, 0x4e800020});
    code(0xc0, {0x3c608000, 0x60635000, 0x4e800020});
    word(0x3000, 0x60000000);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    CHECK(file.good());
  }

  ~LoaderDisc() {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }

  std::filesystem::path path;
};
} // namespace

int main() {
  return run_tests([] {
    LoaderDisc fixture;
    for (bool cache : {false, true})
      for (auto backend :
           {cpu::ExecutionBackend::Interpreter, cpu::ExecutionBackend::NativeInteger}) {
        if (backend == cpu::ExecutionBackend::NativeInteger && !cpu::NativeInteger::supported())
          continue;
        Memory memory(Console::GameCube);
        cpu::Interpreter processor(memory);
        processor.set_execution_backend(backend);
        processor.set_data_cache_enabled(cache);
        disc::Image image(fixture.path);
        runtime::BootInfo boot;
        unsigned steps = 0;
        runtime::boot_disc(
            memory, processor, image,
            [&] {
              CHECK(++steps < 100);
              processor.step();
            },
            boot);
        CHECK(boot.stage == "game-entry" && boot.entry == 0x80005000);
        CHECK(boot.loaded_sections == 1 && boot.loaded_bytes == 0x104);
        CHECK(memory.read32(0x5000) == 0x60000000);
        CHECK(processor.state().pc == 0x80005000);
        CHECK(processor.read_data_word(0x80003100) == 0x81200040);
        CHECK(memory.read32(0x3100) == (cache ? 0u : 0x81200040u));
        CHECK(processor.read_data_word(0x81300000) == 0x4e800020);
        processor.set_data_cache_enabled(false);
        CHECK(memory.read32(0x01300000) == 0x4e800020);
      }
  });
}
