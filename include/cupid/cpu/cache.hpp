// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/bus.hpp"
#include <array>
#include <bit>

namespace cupid::cpu {
class InstructionCache {
public:
  u32 read(Bus &bus, u32 physical, bool enabled, bool locked) {
    if (!enabled) {
      u32 value = 0;
      bus.read_instruction_words(physical, std::span<u32>(&value, 1));
      return value;
    }
    if ((physical & 3) == 0) {
      if (auto *line = obtain(bus, physical, locked))
        return line->words[(physical >> 2) & 7];
      return read_locked_miss(bus, physical);
    }
    return read_unaligned(bus, physical, locked);
  }

  void invalidate(u32 effective);
  void reset();

  // Repeated hits in the recent line leave its replacement bits unchanged.
  // The caller has established a stable RAM identity without a backing read.
  std::span<const u32> recent_words(u32 key, u32 physical) const noexcept {
    if ((physical & 3) || recent_way_ >= 8 || recent_address_ != (key & ~31u))
      return {};
    const auto &line = sets_[(key >> 5) & 127][recent_way_];
    if (!line.valid || line.address != (key & ~31u))
      return {};
    return std::span<const u32>(line.words).subspan((physical >> 2) & 7);
  }

  // An admission hint only. Address translation and stable bus identity must
  // still be established before these words can execute as guest instructions.
  unsigned recent_integer_count(u32 offset) const noexcept {
    if (recent_way_ >= 8)
      return 0;
    const auto &line = sets_[(recent_address_ >> 5) & 127][recent_way_];
    if (!line.valid)
      return 0;
    return std::countr_one(static_cast<unsigned>(line.integer_mask) >> ((offset >> 2) & 7));
  }

  bool recent_cache_flush_run(u32 offset) const noexcept {
    if (recent_way_ >= 8)
      return false;
    const auto &line = sets_[(recent_address_ >> 5) & 127][recent_way_];
    return line.valid && (line.cache_flush_mask & (1u << ((offset >> 2) & 7)));
  }

  bool register_branch_run_hint(u32 offset) const noexcept {
    if (recent_way_ >= 8)
      return false;
    const auto &line = sets_[(recent_address_ >> 5) & 127][recent_way_];
    if (!line.valid)
      return false;
    const unsigned shift = (offset >> 2) & 7;
    const auto matches = [&](const Resident &first, u32 address) {
      const unsigned prefix = std::countr_one(first.integer_mask);
      if (!prefix)
        return false;
      if (prefix < first.words.size())
        return (first.branch_mask & (1u << prefix)) && (first.words[prefix] >> 26) == 16;
      if (prefix == 8)
        return false;
      const auto following = inspect(address + 32, address + 32);
      const unsigned next = std::countr_one(following.integer_mask);
      return prefix + next < 8 && next < following.words.size() &&
             (following.branch_mask & (1u << next)) && (following.words[next] >> 26) == 16;
    };
    const Resident first{std::span<const u32>(line.words).subspan(shift),
                         static_cast<unsigned>(line.integer_mask) >> shift, 0,
                         static_cast<unsigned>(line.scalar_branch_mask) >> shift, 0};
    if (matches(first, recent_address_))
      return true;
    // A conditional branch in the following line can return to its prefix.
    return shift >= 6 && line.scalar_branch_mask &&
           matches(inspect(recent_address_ - 32 + shift * 4, recent_address_ - 32 + shift * 4),
                   recent_address_ - 32);
  }

  struct Resident {
    std::span<const u32> words;
    unsigned integer_mask = 0, scalar_mask = 0, branch_mask = 0, scalar_start_mask = 0;
  };

  // Inspection never allocates, fills, or changes replacement state. A caller
  // commits each line hit only when an instruction from that line executes.
  Resident inspect(u32 key, u32 physical) const noexcept;
  void commit_hit(u32 key) noexcept;

  bool scalar_run_hint(u32 offset) const noexcept {
    if (recent_way_ >= 8)
      return false;
    const auto &recent = sets_[(recent_address_ >> 5) & 127][recent_way_];
    if (!recent.valid)
      return false;
    const unsigned shift = (offset >> 2) & 7;
    if (recent.scalar_start_mask & (1u << shift))
      return true;
    // A terminal branch can return to a RAM operation in the preceding line.
    if (recent.scalar_branch_mask && ((offset >> 2) & 7) >= 6)
      return (inspect(recent_address_ - 32, recent_address_ - 32).scalar_start_mask &
              (1u << shift)) != 0;
    return false;
  }

private:
  struct Line {
    std::array<u32, 8> words{};
    u32 address = 0;
    bool valid = false;
    u8 integer_mask = 0;
    u8 cache_flush_mask = 0;
    u8 scalar_mask = 0, scalar_branch_mask = 0, scalar_start_mask = 0;
  };

  Line *obtain(Bus &bus, u32 physical, bool locked) {
    const u32 address = bus.instruction_cache_key(physical) & ~31u;
    const unsigned set_index = (address >> 5) & 127;
    auto &set = sets_[set_index];
    // Repeating the last hit leaves its replacement bits unchanged. Every
    // allocation and other hit replaces the hint; invalidation clears validity.
    if (recent_way_ < 8 && recent_address_ == address && set[recent_way_].valid)
      return &set[recent_way_];
    return obtain_other(bus, physical, address, locked);
  }

  Line *obtain_other(Bus &bus, u32 physical, u32 address, bool locked);
  u32 read_locked_miss(Bus &bus, u32 physical);
  u32 read_unaligned(Bus &bus, u32 physical, bool locked);

  std::array<std::array<Line, 8>, 128> sets_{};
  std::array<u8, 128> replacement_{};
  // A host lookup hint. Tags and validity in sets_ remain authoritative.
  std::array<u8, 4096> lookup_{};
  u32 recent_address_ = 0;
  u8 recent_way_ = 8;
};
} // namespace cupid::cpu
