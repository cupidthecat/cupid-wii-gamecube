// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/hw/devices.hpp"
#include "cupid/hw/remote.hpp"
#include <array>
#include <filesystem>
#include <istream>
#include <string>
#include <vector>

namespace cupid::platform {
struct InputState {
  std::array<hw::PadState, 4> pads;
  std::array<hw::RemoteState, 4> remotes;
};

class InputReplay {
public:
  explicit InputReplay(std::istream &input);
  explicit InputReplay(const std::filesystem::path &path);

  bool needs_update(u64 instructions, u64 fields) const {
    return !initialized_ || ((pending_ & 1) && instructions >= next_[0]) ||
           ((pending_ & 2) && fields >= next_[1]);
  }

  bool update(u64 instructions, u64 fields);
  const InputState &state() const;
  void reset();

private:
  struct Action {
    std::string name;
    int value = 0;
  };

  struct Event {
    u64 when = 0;
    unsigned line = 0;
    std::vector<Action> actions;
  };

  void read(std::istream &input);
  void update_deadlines();
  static void apply(InputState &state, const Action &action);
  std::array<std::vector<Event>, 2> events_;
  std::array<std::size_t, 2> positions_{};
  std::array<u64, 2> next_{};
  unsigned pending_ = 0;
  InputState state_;
  bool initialized_ = false;
};
} // namespace cupid::platform
