// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/cache.hpp"
#include <algorithm>

namespace cupid::cpu {
void InstructionCache::reset() {
  for (auto &set : sets_)
    for (auto &line : set)
      line.valid = false;
  replacement_ = {};
}

void InstructionCache::invalidate(u32 effective) {
  for (auto &line : sets_[(effective >> 5) & 127])
    line.valid = false;
}

u32 InstructionCache::read(Bus &bus, u32 physical, bool enabled, bool locked) {
  if (!enabled) {
    u32 value = 0;
    bus.read_instruction_words(physical, std::span<u32>(&value, 1));
    return value;
  }
  std::array<u8, 4> bytes{};
  unsigned copied = 0;
  while (copied < bytes.size()) {
    const u32 cursor = physical + copied;
    const unsigned offset = cursor & 31u;
    const unsigned count = std::min(4u - copied, 32u - offset);
    auto *line = obtain(bus, cursor, locked);
    if (line) {
      for (unsigned n = 0; n < count; ++n) {
        const unsigned index = offset + n;
        bytes[copied + n] = static_cast<u8>(line->words[index / 4u] >> (24u - (index & 3u) * 8u));
      }
    } else if (!bus.read_instruction_bytes(cursor, std::span(bytes).subspan(copied, count))) {
      throw BusFault(cursor, false, "Locked instruction-cache miss has no defined backing", true);
    }
    copied += count;
  }
  return (u32(bytes[0]) << 24) | (u32(bytes[1]) << 16) | (u32(bytes[2]) << 8) | bytes[3];
}

InstructionCache::Line *InstructionCache::obtain(Bus &bus, u32 physical, bool locked) {
  const u32 address = bus.instruction_cache_key(physical) & ~31u;
  const unsigned set_index = (address >> 5) & 127;
  auto &set = sets_[set_index];
  unsigned way = 8;
  for (unsigned n = 0; n < 8; ++n)
    if (set[n].valid && set[n].address == address) {
      way = n;
      break;
    }
  if (way == 8 && locked)
    return nullptr;
  if (way == 8) {
    for (unsigned n = 0; n < 8; ++n)
      if (!set[n].valid) {
        way = n;
        break;
      }
    if (way == 8) {
      unsigned node = 0;
      way = 0;
      for (unsigned level = 0; level < 3; ++level) {
        const unsigned branch = (replacement_[set_index] >> node) & 1;
        way = (way << 1) | branch;
        node = node * 2 + 1 + branch;
      }
    }
    Line loaded = set[way];
    loaded.address = address;
    bus.read_instruction_words(physical & ~31u, loaded.words);
    loaded.valid = true;
    set[way] = loaded;
  }
  unsigned node = 0;
  for (unsigned level = 0; level < 3; ++level) {
    const unsigned branch = (way >> (2 - level)) & 1;
    const u8 bit = static_cast<u8>(1u << node);
    replacement_[set_index] =
        static_cast<u8>((replacement_[set_index] & ~bit) | (branch ? 0 : bit));
    node = node * 2 + 1 + branch;
  }
  return &set[way];
}
} // namespace cupid::cpu
