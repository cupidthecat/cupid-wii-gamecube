// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/data_cache.hpp"
#include <algorithm>

namespace cupid::cpu {
DataCache::Line *DataCache::obtain(Bus &bus, u32 physical, bool locked) {
  physical &= ~31u;
  const u32 key = bus.instruction_cache_key(physical);
  const unsigned index = (key >> 5) & 127;
  auto &set = sets_[index];
  unsigned way = 0;
  while (way < set.size() && !(set[way].valid && set[way].key == key))
    ++way;
  if (way == set.size()) {
    if (locked)
      return nullptr;
    for (way = 0; way < set.size() && set[way].valid; ++way) {
    }
    if (way == set.size()) {
      unsigned node = 0;
      way = 0;
      for (unsigned level = 0; level < 3; ++level) {
        const unsigned branch = (replacement_[index] >> node) & 1;
        way = (way << 1) | branch;
        node = node * 2 + 1 + branch;
      }
    }
    auto &line = set[way];
    if (line.valid && line.dirty)
      (void)bus.write_data_cache_bytes(line.physical, line.bytes);
    // A rejected backing copy retains this way's bytes, including a replaced
    // dirty line. The requested tag is installed regardless of copy success.
    (void)bus.read_data_cache_bytes(physical, line.bytes);
    line.key = key;
    line.physical = physical;
    line.valid = true;
    line.dirty = false;
  }
  unsigned node = 0;
  for (unsigned level = 0; level < 3; ++level) {
    const unsigned branch = (way >> (2 - level)) & 1;
    const u8 bit = u8(1u << node);
    replacement_[index] = u8((replacement_[index] & ~bit) | (branch ? 0 : bit));
    node = node * 2 + 1 + branch;
  }
  return &set[way];
}

void DataCache::read(Bus &bus, u32 physical, std::span<u8> bytes, bool locked) {
  while (!bytes.empty()) {
    const unsigned offset = physical & 31;
    const auto size = std::min<std::size_t>(bytes.size(), 32 - offset);
    auto *line = obtain(bus, physical, locked);
    if (line)
      std::copy_n(line->bytes.begin() + offset, size, bytes.begin());
    else if (!bus.read_data_cache_bytes(physical, bytes.first(size)))
      throw BusFault(physical, false, "Locked data-cache miss has no defined backing", true);
    physical += u32(size);
    bytes = bytes.subspan(size);
  }
}

void DataCache::write(Bus &bus, u32 physical, std::span<const u8> bytes, bool locked) {
  while (!bytes.empty()) {
    const unsigned offset = physical & 31;
    const auto size = std::min<std::size_t>(bytes.size(), 32 - offset);
    if (auto *line = obtain(bus, physical, locked)) {
      std::copy_n(bytes.begin(), size, line->bytes.begin() + offset);
      line->dirty = true;
    } else {
      (void)bus.write_data_cache_bytes(physical, bytes.first(size));
    }
    physical += u32(size);
    bytes = bytes.subspan(size);
  }
}

void DataCache::store(Bus &bus, u32 physical) {
  if (auto *line = obtain(bus, physical, true)) {
    if (line->dirty)
      (void)bus.write_data_cache_bytes(physical & ~31u, line->bytes);
    line->dirty = false;
  }
}

void DataCache::invalidate(Bus &bus, u32 physical) {
  if (auto *line = obtain(bus, physical, true))
    line->valid = line->dirty = false;
}

void DataCache::flush(Bus &bus, u32 physical) {
  if (auto *line = obtain(bus, physical, true)) {
    if (line->dirty)
      (void)bus.write_data_cache_bytes(physical & ~31u, line->bytes);
    line->valid = line->dirty = false;
  }
}

void DataCache::flush_all(Bus &bus) {
  for (auto &set : sets_)
    for (auto &line : set)
      if (line.valid && line.dirty)
        (void)bus.write_data_cache_bytes(line.physical, line.bytes);
  reset();
}

void DataCache::reset() {
  for (auto &set : sets_)
    for (auto &line : set)
      line.valid = line.dirty = false;
  replacement_ = {};
}
} // namespace cupid::cpu
