// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/types.hpp"
#include <array>
#include <memory>
#include <span>

namespace cupid::cpu {
struct State;

enum class ExecutionBackend { Interpreter, NativeInteger };

struct NativeIntegerStatistics {
  u64 executed = 0, fallback = 0, compiled_operations = 0, cache_hits = 0;
  u64 capacity_fallbacks = 0, cache_entries = 0, executable_bytes = 0;
  u64 cached_fallbacks = 0;
  u64 block_runs = 0, block_executed = 0, block_compilations = 0, block_cache_hits = 0;
  u64 block_replacements = 0, block_cache_entries = 0, block_executable_bytes = 0;
  u64 block_compilation_failures = 0;
};

// Executes one fetched integer operation. The caller owns all guest fetch,
// exceptions and timing. One CPU thread owns an instance and its code pages.
class NativeInteger {
public:
  static bool supported() noexcept;
  static bool can_execute(u32 opcode) noexcept;

  static bool can_group(u32 opcode) noexcept {
    const unsigned primary = opcode >> 26;
    if (primary == 0 || primary == 7 || primary >= 32)
      return false;
    if (primary == 31) {
      const unsigned arithmetic = (opcode >> 1) & 511;
      if (arithmetic == 11 || arithmetic == 75 || arithmetic == 235)
        return false;
    }
    return can_execute(opcode);
  }

  explicit NativeInteger(unsigned capacity = 1024, unsigned block_capacity = 256);
  ~NativeInteger();
  NativeInteger(const NativeInteger &) = delete;
  NativeInteger &operator=(const NativeInteger &) = delete;

  bool execute(State &state, u32 opcode) {
    auto &hint = hints_[(opcode ^ (opcode >> 11) ^ (opcode >> 21)) & 2047];
    if (hint.opcode == opcode) {
      if (hint.operation) {
        invoke_operation(state, hint.operation);
        ++statistics_.executed;
        ++statistics_.cache_hits;
        return true;
      }
      if (hint.kind != Hint::Kind::Empty) {
        ++statistics_.fallback;
        ++statistics_.cached_fallbacks;
        if (hint.kind == Hint::Kind::Capacity)
          ++statistics_.capacity_fallbacks;
        return false;
      }
    }
    return execute_other(state, opcode);
  }

  // Resource-free entry for a partially executed RAM prefix. A missing hint
  // leaves guest state and statistics unchanged; the caller can interpret it.
  bool execute_cached(State &state, u32 opcode) {
    const auto &hint = hints_[(opcode ^ (opcode >> 11) ^ (opcode >> 21)) & 2047];
    if (hint.opcode != opcode || !hint.operation)
      return false;
    invoke_operation(state, hint.operation);
    ++statistics_.executed;
    ++statistics_.cache_hits;
    return true;
  }

  void record_fallback(u32 count) noexcept {
    statistics_.fallback += count;
  }

  // Fetch, interrupt and timing guards belong to the caller. Resource failures
  // return false before any guest register changes.
  bool execute_sequence(State &state, std::span<const u32> opcodes);

  void reset();
  NativeIntegerStatistics statistics() const noexcept;

private:
  // Emitted entries have no compiler function-type prefix. Exclude only this
  // intentional call from Clang's prefix check; other sanitizers stay enabled.
#if defined(__clang__)
  __attribute__((no_sanitize("function")))
#endif
  static void
  invoke_operation(State &state, void (*operation)(State *)) {
    operation(&state);
  }

  struct Hint {
    enum class Kind : u32 { Empty, Emitted, Unsupported, Capacity };
    u32 opcode = 0;
    Kind kind = Kind::Empty;
    void (*operation)(State *) = nullptr;
  };

  std::array<Hint, 2048> hints_{};
  NativeIntegerStatistics statistics_{};
  bool execute_other(State &state, u32 opcode);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cupid::cpu
