// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cupid::dsp {
namespace {
constexpr std::array<u32, 9> legacy_profiles{0x8d69a19b, 0xcc2fd441, 0xa81582e2,
                                             0xdbbeeb61, 0xbad876ef, 0xfa9c576f,
                                             0x614dd145, 0x5dbf8bf1, 0x373a950e};
constexpr std::array<u32, 7> accelerated_profiles{0x008366af, 0x078066ab, 0x5ad4d933, 0x84c680a9,
                                                  0xa02a6131, 0x2e5e4100, 0x002e5e41};

template <std::size_t Size> bool contains(const std::array<u32, Size> &values, u32 value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}
} // namespace

bool BufferAudioProgram::supports(u32 hash) noexcept {
  return contains(legacy_profiles, hash) || contains(accelerated_profiles, hash);
}

BufferAudioProgram::Impl::Impl(Memory &ram, Console mode, u32 hash,
                               std::function<void(u32, bool)> mail, std::function<bool()> has_mail,
                               std::function<void()> irq,
                               std::function<void(u32, std::span<u8>)> aram)
    : memory(ram), console(mode), program_hash(hash),
      accelerated(contains(accelerated_profiles, hash)),
      new_flags(accelerated ? hash != 0x008366af && hash != 0x078066ab
                            : hash != 0x8d69a19b && hash != 0xcc2fd441),
      swap_channels(accelerated ? hash != 0x008366af
                                : std::find(legacy_profiles.begin(), legacy_profiles.begin() + 5,
                                            hash) != legacy_profiles.begin() + 5),
      send(std::move(mail)), pending(std::move(has_mail)), interrupt(std::move(irq)),
      read_aram(std::move(aram)) {
  if (!supports(hash))
    throw std::invalid_argument("Unsupported buffer-audio profile");
  accelerator.memory_read = [this](u32 address) {
    if (console == Console::Wii) {
      if (address & 0x10000000)
        return memory.range(0x10000000 | (address & 0x3ffffff), 1)[0];
      const u32 physical = address & 0x1ffffff;
      return physical < Memory::mem1_size ? memory.range(physical, 1)[0] : u8(0);
    }
    if (!read_aram)
      throw std::runtime_error("Buffer audio requires connected ARAM reads");
    u8 value = 0;
    read_aram(address & 0xffffff, std::span<u8>(&value, 1));
    return value;
  };
  accelerator.exception = [this](unsigned exception) {
    if (exception != 5)
      return;
    const auto state = accelerator.state();
    accelerator.write(0xdb, u16(state.history1));
    accelerator.write(0xdc, u16(state.history2));
    accelerator.write(0xda, state.predictor);
  };
}

std::span<u8> BufferAudioProgram::Impl::backing(u32 address, std::size_t bytes) {
  return memory.range(address & 0x3fffffff, bytes);
}

void BufferAudioProgram::Impl::load_parameter() {
  const auto bytes = backing(parameter_address, accelerated ? 44 : 64);
  std::copy(bytes.begin(), bytes.end(), parameter.begin());
}

void BufferAudioProgram::Impl::store_parameter() {
  auto bytes = backing(parameter_address, accelerated ? 44 : 64);
  std::copy_n(parameter.begin(), bytes.size(), bytes.begin());
}

void BufferAudioProgram::Impl::send_samples() {
  const unsigned count = accelerated ? 192 : 2048;
  auto bytes = backing(integer(0), count * 2);
  for (unsigned i = 0; i < count; ++i) {
    bytes[i * 2] = u8(output[i] >> 8);
    bytes[i * 2 + 1] = u8(output[i]);
  }
  send(0xdcd10004, true);
}

BufferAudioProgram::BufferAudioProgram(Memory &memory, Console console, u32 hash,
                                       std::function<void(u32, bool)> send,
                                       std::function<bool()> pending,
                                       std::function<void()> interrupt,
                                       std::function<void(u32, std::span<u8>)> read_aram)
    : impl_(std::make_unique<Impl>(memory, console, hash, std::move(send), std::move(pending),
                                   std::move(interrupt), std::move(read_aram))) {
}

BufferAudioProgram::~BufferAudioProgram() = default;

void BufferAudioProgram::initialize() {
  impl_->send(0xdcd10000, false);
}

void BufferAudioProgram::update() {
  if (impl_->pending && impl_->pending() && impl_->interrupt)
    impl_->interrupt();
}

u32 BufferAudioProgram::hash() const noexcept {
  return impl_->program_hash;
}

std::optional<AudioProgram::Switch> BufferAudioProgram::handle_mail(u32 value) {
  auto &state = *impl_;
  if (state.uploading) {
    state.upload_words[state.upload_index++] = value;
    if (state.upload_index != state.upload_words.size())
      return std::nullopt;
    state.uploading = false;
    state.upload_index = 0;
    const auto &words = state.upload_words;
    if (u16(words[1]) || u16(words[8]))
      throw std::runtime_error("DSP task data-memory upload/download is not implemented");
    BootDescriptor boot{words[3], u16(words[4]), u16(words[5]), u16(words[8]), u16(words[6])};
    return AudioProgram::Switch{firmware_hash(state.backing(boot.source, boot.length)), boot, true};
  }
  if (std::exchange(state.next_address, false)) {
    state.parameter_address = value;
    return std::nullopt;
  }
  if ((value >> 16) == 0xcdd1) {
    if (value == 0xcdd10001)
      state.uploading = true;
    else if (value == 0xcdd10002)
      return AudioProgram::Switch{0, {}, false};
    return std::nullopt;
  }
  const u32 command = state.accelerated ? value : value & 0xffff;
  if (command == (state.accelerated ? 0xface0080u : 0x0123u)) {
    state.next_address = true;
    return std::nullopt;
  }
  const bool first = command == (state.accelerated ? 0xface0010u : 0x0111u);
  const bool next = command == (state.accelerated ? 0xface0020u : 0x0222u);
  const bool download = !state.accelerated && command == 0x0112;
  if (first || next || download) {
    state.load_parameter();
    if (first)
      state.output.fill(0);
    else if (download) {
      const auto bytes = state.backing(state.integer(0), state.output.size() * 2);
      for (unsigned i = 0; i < state.output.size(); ++i)
        state.output[i] = u16((u16(bytes[i * 2]) << 8) | bytes[i * 2 + 1]);
    }
    if (state.accelerated)
      state.mix_accelerated();
    else
      state.mix_legacy();
  } else if (command == (state.accelerated ? 0xface0100u : 0x0666u)) {
    state.send_samples();
  } else if (command == (state.accelerated ? 0xfacedeadu : 0x0999u)) {
    state.send(state.program_hash == 0x8d69a19b ? 0xdcd10004 : 0xdcd10003, true);
  } else if (!state.accelerated) {
    if (command == 0x0777)
      state.send(0, false);
    else if (command == 0x0888)
      state.send(0x22640000, false);
    else
      state.send(0xdcd10004, true);
  }
  return std::nullopt;
}
} // namespace cupid::dsp
