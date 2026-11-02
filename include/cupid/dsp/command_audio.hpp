// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/dsp/audio.hpp"
#include <memory>
#include <optional>

namespace cupid::dsp {
// Command-list audio tasks with guest-resident voice state.
class CommandAudioProgram {
public:
  static bool supports(u32 hash) noexcept;
  CommandAudioProgram(Memory &memory, Console console, u32 hash,
                      std::function<void(u32, bool, u32)> send,
                      std::function<void(u32, std::span<u8>)> read_aram,
                      std::span<const s16> coefficients = {});
  ~CommandAudioProgram();
  CommandAudioProgram(const CommandAudioProgram &) = delete;
  CommandAudioProgram &operator=(const CommandAudioProgram &) = delete;
  void initialize();
  std::optional<AudioProgram::Switch> handle_mail(u32 value);
  void update();
  u32 hash() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cupid::dsp
