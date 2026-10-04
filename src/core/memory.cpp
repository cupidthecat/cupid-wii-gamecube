// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/memory.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>

namespace cupid {
namespace {
[[noreturn]] void bad_address(u32 address, bool write) {
  std::ostringstream reason;
  reason << "Unmapped physical " << (write ? "write" : "read") << " at 0x" << std::hex << address;
  throw BusFault(address, write, reason.str());
}
} // namespace

Memory::Memory(Console console)
    : mem1_(mem1_size), mem2_(console == Console::Wii ? mem2_size : 0), cache_(cache_size) {
}

void Memory::clear() {
  for (auto *bytes : {&mem1_, &mem2_, &cache_})
    std::fill(bytes->begin(), bytes->end(), 0);
}

u32 Memory::boot_physical(u32 address) {
  if ((address & 0xc0000000) == 0x80000000 || (address & 0xc0000000) == 0xc0000000)
    return address & 0x3fffffff;
  return address;
}

std::span<const u8> Memory::range(u32 address, std::size_t length) const {
  const std::vector<u8> *bytes = nullptr;
  u32 base = 0;
  if (address < mem1_size)
    bytes = &mem1_;
  else if (address >= 0x10000000 && address < 0x10000000 + mem2_.size()) {
    bytes = &mem2_;
    base = 0x10000000;
  } else if (address >= 0xe0000000 && address < 0xe0000000 + cache_size) {
    bytes = &cache_;
    base = 0xe0000000;
  }
  if (!bytes || length > bytes->size() - (address - base))
    bad_address(address, false);
  return std::span<const u8>(*bytes).subspan(address - base, length);
}

std::span<u8> Memory::range(u32 address, std::size_t length) {
  auto span = static_cast<const Memory &>(*this).range(address, length);
  return {const_cast<u8 *>(span.data()), span.size()};
}

void Memory::copy_in(u32 physical, std::span<const u8> data) {
  try {
    auto target = range(physical, data.size());
    std::copy(data.begin(), data.end(), target.begin());
  } catch (const BusFault &) {
    bad_address(physical, true);
  }
}

void Memory::copy_out(u32 physical, std::span<u8> data) const {
  auto source = range(physical, data.size());
  std::copy(source.begin(), source.end(), data.begin());
}

void Memory::locked_cache_dma(u32 memory_address, u32 cache_address, u32 bytes, bool load) {
  if (!bytes || bytes > 4096 || ((bytes | memory_address | cache_address) & 31))
    throw std::invalid_argument(
        "Locked-cache DMA requires aligned addresses and one to 128 blocks");
  const auto region = memory_address & 0x1f000000;
  if (region == 0x08000000 || region == 0x0c000000) {
    for (u32 offset = 0; offset < bytes; offset += 4) {
      const auto locked = 0xe0000000 | ((cache_address + offset) & (cache_size - 1));
      if (load)
        write32(locked, read32(memory_address + offset));
      else
        write32(memory_address + offset, read32(locked));
    }
    return;
  }
  auto locked = range(0xe0000000 | (cache_address & (cache_size - 1)), bytes);
  auto ram = range(memory_address & 0x3fffffff, bytes);
  auto source = load ? ram : locked;
  auto destination = load ? locked : ram;
  std::memmove(destination.data(), source.data(), bytes);
}

u64 Memory::read(u32 address, unsigned width) {
  if (mmio_handles && mmio_handles(address)) {
    const bool efb_double = width == 8 && address >= 0x08000000 && address < 0x0c000000;
    if (!mmio_read || (width > 4 && !efb_double))
      bad_address(address, false);
    return mmio_read(address, width);
  }
  auto bytes = range(address, width);
  u64 result = 0;
  for (u8 byte : bytes)
    result = (result << 8) | byte;
  return result;
}

void Memory::write(u32 address, u64 value, unsigned width) {
  if ((address & ~u32{0x1f}) == 0x0c008000) {
    if (!fifo_write)
      throw BusFault(address, true, "Graphics FIFO has no command processor", true);
    for (unsigned i = width; i > 0; --i)
      fifo_write(static_cast<u8>(value >> ((i - 1) * 8)));
    return;
  }
  if (mmio_handles && mmio_handles(address)) {
    if (width == 8 && address >= 0x08000000 && address < 0x0c000000)
      throw BusFault(address, true, "CPU EFB writes are not implemented", true);
    if (!mmio_write || width > 4)
      bad_address(address, true);
    mmio_write(address, static_cast<u32>(value), width);
    return;
  }
  try {
    auto bytes = range(address, width);
    for (unsigned i = 0; i < width; ++i)
      bytes[i] = static_cast<u8>(value >> ((width - i - 1) * 8));
  } catch (const BusFault &) {
    bad_address(address, true);
  }
}

u8 Memory::read8(u32 a) {
  return static_cast<u8>(read(a, 1));
}

u16 Memory::read16(u32 a) {
  return static_cast<u16>(read(a, 2));
}

u32 Memory::read32(u32 a) {
  return static_cast<u32>(read(a, 4));
}

u64 Memory::read64(u32 a) {
  return read(a, 8);
}

void Memory::write8(u32 a, u8 v) {
  write(a, v, 1);
}

void Memory::write16(u32 a, u16 v) {
  write(a, v, 2);
}

void Memory::write32(u32 a, u32 v) {
  write(a, v, 4);
}

void Memory::write64(u32 a, u64 v) {
  write(a, v, 8);
}
} // namespace cupid
