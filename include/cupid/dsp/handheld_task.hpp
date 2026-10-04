// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/dsp/engine.hpp"
#include <array>
#include <functional>
#include <optional>

namespace cupid::dsp {
class HandheldTask {
public:
  struct Transition {
    u32 hash;
    BootDescriptor boot;
    bool preserve;
  };

  HandheldTask(Memory &memory, std::function<void(u32)> send, std::function<bool()> pending,
               std::function<void()> interrupt);
  void initialize();
  void update();
  std::optional<Transition> handle_mail(u32 value);

private:
  enum class Phase { Request, Address, NextTask, Upload };
  Memory &memory_;
  std::function<void(u32)> send_;
  std::function<bool()> pending_;
  std::function<void()> interrupt_;
  Phase phase_ = Phase::Request;
  std::array<u32, 10> descriptor_{};
  unsigned next_word_ = 0;
};
} // namespace cupid::dsp
