// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/memory.hpp"
#include "cupid/cpu/interpreter.hpp"
#include "cupid/disc/image.hpp"
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace cupid::runtime {
struct BootInfo {
  u32 entry = 0;
  u64 loaded_bytes = 0;
  unsigned loaded_sections = 0;
  std::string stage = "uninitialized";
};

struct Bios {
  std::vector<u8> decoded;
  u32 crc32 = 0;
  bool pal = false;
  std::string revision;
};

Bios load_bios(const std::filesystem::path &path);
std::vector<u8> read_file(const std::filesystem::path &path, u64 limit);
BootInfo load_dol(Memory &memory, std::span<const u8> file);
// The caller's step function provides timing, interrupt delivery, trace and run limits.
void boot_disc(Memory &memory, cpu::Interpreter &cpu, disc::Image &image,
               const std::function<void()> &step, BootInfo &info);
void initialize_boot_memory(Memory &memory, Console console, bool pal = false);
std::vector<u8> self_test_dol();
} // namespace cupid::runtime
