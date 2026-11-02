// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/elf.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cupid::runtime {
namespace {
[[noreturn]] void invalid(const char *reason) {
  throw std::runtime_error(std::string("ELF: ") + reason);
}

void require(bool condition, const char *reason) {
  if (!condition)
    invalid(reason);
}

void range(u64 offset, u64 count, u64 size, const char *reason) {
  require(offset <= size && count <= size - offset, reason);
}

u32 field(std::span<const u8> file, std::size_t offset, unsigned width = 4) {
  range(offset, width, file.size(), "truncated field");
  u32 result = 0;
  for (unsigned i = 0; i < width; ++i)
    result = (result << 8) | file[offset + i];
  return result;
}

struct LoadSegment {
  u32 file_offset, physical, file_size, memory_size, flags;
};
} // namespace

BootInfo load_elf(Memory &memory, std::span<const u8> file) {
  require(file.size() >= 52, "header is shorter than 52 bytes");
  constexpr std::array<u8, 4> magic{0x7f, 'E', 'L', 'F'};
  require(std::equal(magic.begin(), magic.end(), file.begin()), "incorrect file magic");
  require(file[4] == 1 && file[5] == 2, "requires 32-bit big-endian encoding");
  require(file[6] == 1 && field(file, 20) == 1, "unsupported ELF version");
  require(field(file, 16, 2) == 2, "relocatable and shared ELF files are unsupported");
  require(field(file, 18, 2) == 20, "requires the PowerPC machine type");
  require(field(file, 40, 2) == 52, "invalid ELF header size");
  const u32 entry = field(file, 24), program_offset = field(file, 28),
            section_offset = field(file, 32);
  const u32 program_stride = field(file, 42, 2), program_count = field(file, 44, 2);
  const u32 section_stride = field(file, 46, 2), section_count = field(file, 48, 2),
            strings = field(file, 50, 2);
  require(program_count != 0, "has no program segments");
  require(program_stride == 32, "invalid program header size");
  require(section_count == 0 || section_stride == 40, "invalid section header size");
  range(program_offset, static_cast<u64>(program_count) * program_stride, file.size(),
        "program table is outside its file");
  range(section_offset, static_cast<u64>(section_count) * section_stride, file.size(),
        "section table is outside its file");
  require(strings == 0 || strings < section_count, "invalid section name table index");
  require(!(entry & 3), "entry point is not instruction aligned");
  memory.range(Memory::boot_physical(entry), 4);
  for (u32 index = 0; index < section_count; ++index) {
    const std::size_t base = static_cast<std::size_t>(section_offset) + index * 40;
    if (field(file, base + 4) != 8)
      range(field(file, base + 16), field(file, base + 20), file.size(),
            "section data is outside its file");
  }
  std::vector<LoadSegment> loads;
  bool executable_entry = false;
  for (u32 index = 0; index < program_count; ++index) {
    const std::size_t base = static_cast<std::size_t>(program_offset) + index * 32;
    const u32 kind = field(file, base), file_offset = field(file, base + 4),
              virtual_address = field(file, base + 8);
    const u32 physical_address = field(file, base + 12), file_size = field(file, base + 16),
              memory_size = field(file, base + 20);
    const u32 flags = field(file, base + 24), alignment = field(file, base + 28);
    range(file_offset, file_size, file.size(), "segment data is outside its file");
    require(file_size <= memory_size, "segment file size exceeds its memory size");
    if (kind != 1)
      continue;
    require(alignment <= 1 || !(alignment & (alignment - 1)),
            "segment alignment is not a power of two");
    require(alignment <= 1 || virtual_address % alignment == file_offset % alignment,
            "segment file and virtual address alignment disagree");
    require(static_cast<u64>(virtual_address) + memory_size <= 0x100000000ull,
            "segment virtual address wraps");
    // Behavioral reference: ElfReader::LoadIntoMemory uses nonzero LMA, otherwise VMA.
    const u32 address = physical_address ? physical_address | 0x80000000u : virtual_address;
    require(static_cast<u64>(address) + memory_size <= 0x100000000ull,
            "segment load address wraps");
    const u32 physical = Memory::boot_physical(address);
    memory.range(physical, memory_size);
    const u32 entry_physical = Memory::boot_physical(entry);
    if ((flags & 1) && entry_physical >= physical &&
        static_cast<u64>(entry_physical) + 4 <= static_cast<u64>(physical) + file_size)
      executable_entry = true;
    if (memory_size)
      loads.push_back({file_offset, physical, file_size, memory_size, flags});
  }
  require(executable_entry, "entry point is outside loaded executable bytes");
  BootInfo result{entry, 0, static_cast<unsigned>(loads.size()), "executable-loaded"};
  for (const auto &segment : loads) {
    auto destination = memory.range(segment.physical, segment.memory_size);
    std::copy_n(file.begin() + segment.file_offset, segment.file_size, destination.begin());
    std::fill(destination.begin() + segment.file_size, destination.end(), 0);
    result.loaded_bytes += segment.file_size;
  }
  return result;
}
} // namespace cupid::runtime
