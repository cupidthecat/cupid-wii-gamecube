// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/dsp/audio.hpp"
#include <memory>

namespace cupid::dsp {
// Mailbox-driven PCM buffer tasks used by guest audio libraries.
class BufferAudioProgram {
public:
  static bool supports(u32 hash) noexcept;
  BufferAudioProgram(Memory &memory, Console console, u32 hash, std::function<void(u32, bool)> send,
                     std::function<bool()> pending, std::function<void()> interrupt,
                     std::function<void(u32, std::span<u8>)> read_aram);
  ~BufferAudioProgram();
  BufferAudioProgram(const BufferAudioProgram &) = delete;
  BufferAudioProgram &operator=(const BufferAudioProgram &) = delete;
  void initialize();
  void update();
  std::optional<AudioProgram::Switch> handle_mail(u32 value);
  u32 hash() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cupid::dsp
