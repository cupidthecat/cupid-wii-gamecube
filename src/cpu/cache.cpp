// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/cache.hpp"
#include <algorithm>

namespace cupid::cpu {
namespace {
struct Replacement {
  u8 mask = 0, value = 0;
};

constexpr auto replacement_updates = [] {
  std::array<Replacement, 8> updates{};
  for (unsigned way = 0; way < updates.size(); ++way) {
    unsigned node = 0;
    for (unsigned level = 0; level < 3; ++level) {
      const unsigned branch = (way >> (2 - level)) & 1;
      const u8 bit = static_cast<u8>(1u << node);
      updates[way].mask |= bit;
      if (!branch)
        updates[way].value |= bit;
      node = node * 2 + 1 + branch;
    }
  }
  return updates;
}();

constexpr auto replacement_victims = [] {
  std::array<u8, 128> victims{};
  for (unsigned state = 0; state < victims.size(); ++state) {
    unsigned node = 0, way = 0;
    for (unsigned level = 0; level < 3; ++level) {
      const unsigned branch = (state >> node) & 1;
      way = (way << 1) | branch;
      node = node * 2 + 1 + branch;
    }
    victims[state] = static_cast<u8>(way);
  }
  return victims;
}();
} // namespace

void InstructionCache::reset() {
  for (auto &set : sets_)
    for (auto &line : set)
      line.valid = false;
  replacement_ = {};
  lookup_.fill(8);
  recent_way_ = 8;
}

void InstructionCache::invalidate(u32 effective) {
  for (auto &line : sets_[(effective >> 5) & 127])
    line.valid = false;
}

u32 InstructionCache::read_locked_miss(Bus &bus, u32 physical) {
  std::array<u8, 4> bytes{};
  if (!bus.read_instruction_bytes(physical, bytes))
    throw BusFault(physical, false, "Locked instruction-cache miss has no defined backing", true);
  return (u32(bytes[0]) << 24) | (u32(bytes[1]) << 16) | (u32(bytes[2]) << 8) | bytes[3];
}

u32 InstructionCache::read_unaligned(Bus &bus, u32 physical, bool locked) {
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

InstructionCache::Line *InstructionCache::obtain_other(Bus &bus, u32 physical, u32 address,
                                                       bool locked) {
  const unsigned set_index = (address >> 5) & 127;
  auto &set = sets_[set_index];
  auto &hint = lookup_[(address >> 5) & (lookup_.size() - 1)];
  unsigned way = hint;
  if (way >= 8 || !set[way].valid || set[way].address != address) {
    way = 8;
    for (unsigned n = 0; n < 8; ++n)
      if (set[n].valid && set[n].address == address) {
        way = n;
        break;
      }
  }
  if (way == 8 && locked)
    return nullptr;
  if (way == 8) {
    for (unsigned n = 0; n < 8; ++n)
      if (!set[n].valid) {
        way = n;
        break;
      }
    if (way == 8)
      way = replacement_victims[replacement_[set_index]];
    Line loaded = set[way];
    loaded.address = address;
    bus.read_instruction_words(physical & ~31u, loaded.words);
    loaded.valid = true;
    set[way] = loaded;
  }
  hint = static_cast<u8>(way);
  const auto update = replacement_updates[way];
  replacement_[set_index] =
      static_cast<u8>((replacement_[set_index] & ~update.mask) | update.value);
  recent_address_ = address;
  recent_way_ = static_cast<u8>(way);
  return &set[way];
}
} // namespace cupid::cpu
