// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/types.hpp"
#include <array>
#include <memory>

namespace cupid::cpu {
struct State;

enum class ExecutionBackend { Interpreter, NativeInteger };

struct NativeIntegerStatistics {
  u64 executed = 0, fallback = 0, compiled_operations = 0, cache_hits = 0;
  u64 capacity_fallbacks = 0, cache_entries = 0, executable_bytes = 0;
};

// Executes one fetched integer operation. The caller owns all guest fetch,
// exceptions and timing. One CPU thread owns an instance and its code pages.
class NativeInteger {
public:
  static bool supported() noexcept;
  explicit NativeInteger(unsigned capacity = 1024);
  ~NativeInteger();
  NativeInteger(const NativeInteger &) = delete;
  NativeInteger &operator=(const NativeInteger &) = delete;

  bool execute(State &state, u32 opcode) {
    auto &hint = hints_[(opcode ^ (opcode >> 11) ^ (opcode >> 21)) & 2047];
    if (hint.opcode == opcode && hint.operation) {
      invoke_operation(state, hint.operation);
      ++statistics_.executed;
      ++statistics_.cache_hits;
      return true;
    }
    return execute_other(state, opcode);
  }

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
    u32 opcode = 0;
    void (*operation)(State *) = nullptr;
  };

  std::array<Hint, 2048> hints_{};
  NativeIntegerStatistics statistics_{};
  bool execute_other(State &state, u32 opcode);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cupid::cpu
