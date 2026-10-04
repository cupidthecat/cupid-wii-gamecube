// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/cache.hpp"

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
  const u32 address = bus.instruction_cache_key(physical) & ~31u;
  const unsigned set_index = (address >> 5) & 127;
  auto &set = sets_[set_index];
  unsigned way = 8;
  for (unsigned n = 0; n < 8; ++n)
    if (set[n].valid && set[n].address == address) {
      way = n;
      break;
    }
  if (way == 8 && locked) {
    u32 value = 0;
    if (!bus.read_instruction_words(physical, std::span<u32>(&value, 1)))
      throw BusFault(physical, false, "Locked instruction-cache miss has no defined backing", true);
    return value;
  }
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
  return set[way].words[(physical >> 2) & 7];
}
} // namespace cupid::cpu
