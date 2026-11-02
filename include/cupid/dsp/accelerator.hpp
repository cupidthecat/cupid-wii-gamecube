// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/types.hpp"
#include <functional>
#include <span>

namespace cupid::dsp {
struct AcceleratorState {
  u32 start = 0, end = 0, current = 0;
  u16 format = 0, predictor = 0, input = 0;
  s16 gain = 0, history1 = 0, history2 = 0;
  bool stopped = false;
};

class SampleAccelerator {
public:
  const AcceleratorState &state() const noexcept {
    return state_;
  }

  static bool readable(u8 reg) noexcept;
  static bool writable(u8 reg) noexcept;
  u16 read(u8 reg, std::span<const s16, 16> coefficients);
  void write(u8 reg, u16 value);
  u16 read_raw();
  void write_raw(u16 value);
  u16 read_sample(std::span<const s16, 16> coefficients);

  std::function<u8(u32)> memory_read;
  std::function<void(u32, u8)> memory_write;
  std::function<void(unsigned)> exception;

private:
  AcceleratorState state_;
  u16 current_sample();
  u8 byte(u32 address);
  void raise(unsigned code);
};
} // namespace cupid::dsp
