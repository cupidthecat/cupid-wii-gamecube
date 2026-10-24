// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/dsp/engine.hpp"
#include <memory>
#include <optional>

namespace cupid::dsp {

// Mail-driven DSP audio tasks operate on guest parameter blocks and guest RAM.
class AudioProgram {
public:
  struct Switch {
    u32 hash;
    BootDescriptor boot;
    bool preserve;
  };

  static bool supports(u32 hash);
  AudioProgram(Memory &memory, Console console, u32 hash, std::function<void(u32, bool)> send,
               std::function<void(u32, std::span<u8>)> read_aram, std::function<void()> interrupt);
  ~AudioProgram();
  AudioProgram(const AudioProgram &) = delete;
  AudioProgram &operator=(const AudioProgram &) = delete;
  void initialize();
  std::optional<Switch> handle_mail(u32 value);
  void update();
  u32 hash() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace cupid::dsp
