// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/types.hpp"
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>

namespace cupid {
class Memory;

namespace audio {
struct RemoteSpeakerPacket;
} // namespace audio

namespace hw {
class Devices;
struct RemoteState;
} // namespace hw

namespace ios {

bool initialize_ios_memory(Memory &memory, u32 version, bool reload = false);

class Ipc {
public:
  Ipc(Memory &memory, hw::Devices &devices, Console console);
  ~Ipc();
  Ipc(const Ipc &) = delete;
  Ipc &operator=(const Ipc &) = delete;
  void set_user_directory(const std::filesystem::path &directory);
  void set_version(u32 version);
  void set_partition_open(bool open);
  void set_shutdown_handler(std::function<void()> handler);
  void press_reset_button();
  void press_power_button();
  void set_remote(unsigned index, const hw::RemoteState &state);
  void set_remote_rumble_handler(std::function<void(unsigned, bool)> handler);
  void set_remote_speaker_processing(unsigned index, bool enabled, float pan = 0);
  void set_remote_speaker_handler(
      std::function<void(unsigned, const audio::RemoteSpeakerPacket &)> handler);
  // Parses the selected disc's original metadata; does not authenticate signatures.
  bool set_title_context(std::span<const u8> ticket, std::span<const u8> tmd,
                         u64 partition_offset = 0);
  u32 read(u32 address);
  void write(u32 address, u32 value);

  void advance(u64 cycles) {
    if (cycles > std::numeric_limits<u64>::max() - now_)
      throw std::overflow_error("IPC clock overflow");
    const u64 end = now_ + cycles;
    if (!pending_event_ || end < next_event_) {
      now_ = end;
      return;
    }
    advance_events(end);
  }

  u64 cycles() const;
  std::function<void(u64, std::span<u8>)> disc_read;
  std::function<void(u64, std::span<u8>)> partition_read;

private:
  void advance_events(u64 end);
  u64 now_ = 0, next_event_ = 0;
  bool pending_event_ = false;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace ios
} // namespace cupid
