// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/dsp/engine.hpp"
#include "cupid/gfx/command_processor.hpp"
#include "cupid/hw/devices.hpp"
#include "cupid/ios/ipc.hpp"
#include "cupid/platform/input_replay.hpp"
#include "cupid/platform/input_replay_follow.hpp"
#include "cupid/platform/output.hpp"
#include "cupid/runtime/boot.hpp"
#include "cupid/runtime/fonts.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>

namespace cupid::runtime {
struct RunLimits {
  u64 instructions = 1000000;
  u64 frames = 0;
  double seconds = 10;
};

class LimitReached : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class Machine {
public:
  explicit Machine(Console console);

  Memory &memory() {
    return memory_;
  }

  cpu::Interpreter &cpu() {
    return cpu_;
  }

  hw::Devices &devices() {
    return devices_;
  }

  gfx::CommandProcessor &graphics() {
    return graphics_;
  }

  const BootInfo &boot_info() const {
    return boot_;
  }

  void set_output(platform::Output *output);
  void set_limits(const RunLimits &limits);
  void set_trace(const std::filesystem::path &path, u64 count, bool after_boot = false,
                 u64 start = 0);
  void set_user_directory(const std::filesystem::path &path);
  void set_input_replay(const std::filesystem::path &path);
  void set_input_replay_follow(const std::filesystem::path &path,
                               std::function<void(const std::string &)> diagnostic = {});
  void install_bios(const Bios &bios);
  void install_fonts(const IplFonts &fonts);
  void load_executable(std::span<const u8> file, bool elf = false);
  void load_disc(const std::filesystem::path &path);
  void step();
  void run();
  void persist();
  void write_state(std::ostream &out, const std::string &outcome) const;

private:
  void refresh_input_replay();

  Console console_;
  Memory memory_;
  cpu::Interpreter cpu_;
  hw::Devices devices_;
  gfx::CommandProcessor graphics_;
  dsp::Engine dsp_;
  ios::Ipc ios_;
  std::unique_ptr<disc::Image> image_;
  platform::Output *output_ = nullptr;
  BootInfo boot_;
  RunLimits limits_;
  std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
  u64 step_attempts_ = 0, last_poll_ = 0, trace_count_ = 0;
  u64 trace_emitted_ = 0;
  u64 trace_start_ = 0;
  bool trace_after_boot_ = false;
  std::ofstream trace_;
  std::filesystem::path user_directory_;
  bool shutdown_requested_ = false;
  std::unique_ptr<platform::InputReplay> input_replay_;
  std::unique_ptr<platform::InputReplayFollower> input_replay_follower_;
  std::chrono::steady_clock::time_point last_input_follow_poll_{};
  std::function<void(const std::string &)> input_follow_diagnostic_;
};
} // namespace cupid::runtime
