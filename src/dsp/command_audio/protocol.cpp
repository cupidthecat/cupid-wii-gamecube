// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cupid::dsp {
bool CommandAudioProgram::supports(u32 hash) noexcept {
  constexpr std::array<u32, 6> profiles{0x3ad3b7ac, 0x3daf59b9, 0x4e8a8b21,
                                        0x07f88145, 0xe2136399, 0x3389a79e};
  return std::find(profiles.begin(), profiles.end(), hash) != profiles.end();
}

CommandAudioProgram::Impl::Impl(Memory &ram, Console mode, u32 hash,
                                std::function<void(u32, bool, u32)> mail,
                                std::function<void(u32, std::span<u8>)> aram,
                                std::span<const s16> table)
    : memory(ram), console(mode), program_hash(hash), send(std::move(mail)),
      read_aram(std::move(aram)) {
  if (!supports(hash))
    throw std::invalid_argument("Unsupported command-list audio profile");
  if (!table.empty() && table.size() != coefficients.size())
    throw std::invalid_argument("DSP resampling table must contain 2048 coefficients");
  have_coefficients = !table.empty();
  if (have_coefficients)
    std::copy(table.begin(), table.end(), coefficients.begin());
  accelerator.memory_read = [this](u32 address) {
    if (console == Console::Wii) {
      if (address & 0x10000000)
        return memory.range(0x10000000 | (address & 0x3ffffff), 1)[0];
      const u32 physical = address & 0x1ffffff;
      return physical < Memory::mem1_size ? memory.range(physical, 1)[0] : u8(0);
    }
    if (!read_aram)
      throw std::runtime_error("Command-list audio requires ARAM reads");
    u8 value = 0;
    read_aram(address & 0xffffff, std::span<u8>(&value, 1));
    return value;
  };
  accelerator.exception = [this](unsigned exception) {
    if (exception != 5 || !active_voice)
      return;
    auto &voice = *active_voice;
    if (!voice[55]) {
      voice[7] = 0;
      return;
    }
    accelerator.write(0xda, voice[90]);
    if (voice[8] != 1) {
      accelerator.write(0xdb, voice[91]);
      accelerator.write(0xdc, voice[92]);
    } else {
      const auto &state = accelerator.state();
      const u16 first = u16(state.history1), second = u16(state.history2);
      accelerator.write(0xdb, first);
      accelerator.write(0xdc, second);
      ++voice[97];
    }
  };
}

CommandAudioProgram::CommandAudioProgram(Memory &memory, Console console, u32 hash,
                                         std::function<void(u32, bool, u32)> send,
                                         std::function<void(u32, std::span<u8>)> read_aram,
                                         std::span<const s16> coefficients)
    : impl_(std::make_unique<Impl>(memory, console, hash, std::move(send), std::move(read_aram),
                                   coefficients)) {
}

CommandAudioProgram::~CommandAudioProgram() = default;

void CommandAudioProgram::initialize() {
  impl_->send(0xdcd10000, true, 0);
}

u32 CommandAudioProgram::hash() const noexcept {
  return impl_->program_hash;
}

void CommandAudioProgram::update() {
  if (std::exchange(impl_->resume, false))
    impl_->send(0xdcd10001, true, 0);
}

std::optional<AudioProgram::Switch> CommandAudioProgram::handle_mail(u32 value) {
  auto &state = *impl_;
  if (state.upload) {
    state.upload_words[state.upload_index++] = value;
    if (state.upload_index != state.upload_words.size())
      return std::nullopt;
    state.upload = false;
    state.upload_index = 0;
    const auto &words = state.upload_words;
    if (u16(words[1]) || u16(words[8]))
      throw std::runtime_error("DSP task data-memory upload/download is not implemented");
    BootDescriptor boot{words[3], u16(words[4]), u16(words[5]), u16(words[8]), u16(words[6])};
    const u32 next_hash = firmware_hash(state.backing(boot.source, boot.length));
    state.resume = true;
    return AudioProgram::Switch{next_hash, boot, true};
  }
  switch (state.phase) {
  case Impl::Phase::Size:
    if ((value >> 16) == 0xbabe) {
      state.command_size = u16(value);
      state.phase = Impl::Phase::Address;
    }
    break;
  case Impl::Phase::Address:
    state.copy_list(value, state.command_size);
    state.execute_list();
    state.command_size = 0;
    state.send(0xdcd10002, true, 2500);
    state.phase = Impl::Phase::Task;
    break;
  case Impl::Phase::Task:
    switch (u16(value)) {
    case 0:
      state.send(0xdcd10001, true, 0);
      state.phase = Impl::Phase::Size;
      break;
    case 1:
      state.upload = true;
      state.phase = Impl::Phase::Size;
      break;
    case 2:
      return AudioProgram::Switch{0, {}, false};
    case 3:
      state.phase = Impl::Phase::Size;
      break;
    default:
      break;
    }
    break;
  }
  return std::nullopt;
}
} // namespace cupid::dsp
