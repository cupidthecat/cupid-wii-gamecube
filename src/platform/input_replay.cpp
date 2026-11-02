// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/platform/input_replay.hpp"
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace cupid::platform {
namespace {
template <typename T> T number(std::string_view text) {
  T value = 0;
  int base = 10;
  if (text.starts_with("0x")) {
    text.remove_prefix(2);
    base = 16;
  }
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    throw std::runtime_error("Invalid input replay integer");
  return value;
}

void check_range(int value, int lower, int upper) {
  if (value < lower || value > upper)
    throw std::runtime_error("Input replay value is outside its action's range");
}
} // namespace

InputReplay::InputReplay(std::istream &input) {
  read(input);
}

InputReplay::InputReplay(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("Cannot open input replay file");
  read(input);
}

void InputReplay::reset() {
  positions_ = {};
  state_ = {};
  state_.pads[0].connected = true;
  state_.remotes[0].connected = true;
  state_.remotes[0].nunchuk.attached = true;
  initialized_ = false;
  update_deadlines();
}

void InputReplay::update_deadlines() {
  pending_ = 0;
  for (unsigned kind = 0; kind < 2; ++kind) {
    if (positions_[kind] < events_[kind].size()) {
      next_[kind] = events_[kind][positions_[kind]].when;
      pending_ |= 1u << kind;
    }
  }
}

void InputReplay::read(std::istream &input) {
  std::string text;
  unsigned line = 0;
  while (std::getline(input, text)) {
    ++line;
    if (text.size() > 4096 || line > 100000)
      throw std::runtime_error("Input replay exceeds its size limit");
    text.erase(text.find('#') == std::string::npos ? text.size() : text.find('#'));
    std::istringstream record(text);
    std::string counter, time, action;
    if (!(record >> counter))
      continue;
    try {
      if ((counter != "instruction" && counter != "field") || !(record >> time))
        throw std::runtime_error("Expected instruction or field, then a counter");
      const unsigned kind = counter == "field" ? 1 : 0;
      Event event{number<u64>(time), line, {}};
      if (!events_[kind].empty() && event.when < events_[kind].back().when)
        throw std::runtime_error("Counters must be nondecreasing within each counter type");
      while (record >> action) {
        const auto separator = action.find('=');
        if (separator == std::string::npos)
          throw std::runtime_error("Expected action=value");
        Action parsed{action.substr(0, separator), number<int>(action.substr(separator + 1))};
        InputState checked;
        apply(checked, parsed);
        event.actions.push_back(std::move(parsed));
      }
      if (event.actions.empty())
        throw std::runtime_error("Replay event has no actions");
      events_[kind].push_back(std::move(event));
    } catch (const std::exception &error) {
      throw std::runtime_error("Input replay line " + std::to_string(line) + ": " + error.what());
    }
  }
  if (input.bad())
    throw std::runtime_error("Cannot read input replay file");
  reset();
}

void InputReplay::apply(InputState &state, const Action &action) {
  const auto separator = action.name.find('.');
  if (separator == std::string::npos || separator == 0)
    throw std::runtime_error("Input action requires a device and port");
  std::string_view device(action.name.data(), separator),
      name(action.name.data() + separator + 1, action.name.size() - separator - 1);
  const char digit = device.back();
  if (digit < '0' || digit > '3')
    throw std::runtime_error("Input port must be 0 through 3");
  device.remove_suffix(1);
  const unsigned port = static_cast<unsigned>(digit - '0');
  const int value = action.value;
  auto boolean = [&](bool &target) {
    check_range(value, 0, 1);
    target = value != 0;
  };
  auto byte = [&](u8 &target) {
    check_range(value, 0, 255);
    target = static_cast<u8>(value);
  };
  auto flag = [&](u16 &target, u16 mask) {
    check_range(value, 0, 1);
    target = static_cast<u16>(value ? target | mask : target & ~mask);
  };
  if (device == "pad") {
    auto &pad = state.pads[port];
    if (name == "connected")
      return boolean(pad.connected);
    const std::array<std::pair<std::string_view, u8 *>, 8> axes{{{"stick-x", &pad.stick_x},
                                                                 {"stick-y", &pad.stick_y},
                                                                 {"c-x", &pad.cstick_x},
                                                                 {"c-y", &pad.cstick_y},
                                                                 {"l-trigger", &pad.left_trigger},
                                                                 {"r-trigger", &pad.right_trigger},
                                                                 {"analog-a", &pad.analog_a},
                                                                 {"analog-b", &pad.analog_b}}};
    for (auto [key, target] : axes)
      if (name == key)
        return byte(*target);
    const std::array<std::pair<std::string_view, u16>, 12> buttons{{{"a", 0x100},
                                                                    {"b", 0x200},
                                                                    {"x", 0x400},
                                                                    {"y", 0x800},
                                                                    {"start", 0x1000},
                                                                    {"l", 0x40},
                                                                    {"r", 0x20},
                                                                    {"z", 0x10},
                                                                    {"left", 1},
                                                                    {"right", 2},
                                                                    {"down", 4},
                                                                    {"up", 8}}};
    for (auto [key, mask] : buttons)
      if (name == key)
        return flag(pad.buttons, mask);
  } else if (device == "remote" || device == "nunchuk") {
    auto &remote = state.remotes[port];
    auto &extension = remote.nunchuk;
    const bool nunchuk = device == "nunchuk";
    if (name == "connected")
      return boolean(nunchuk ? extension.attached : remote.connected);
    if (name == "accel-x" || name == "accel-y" || name == "accel-z") {
      check_range(value, 0, 1023);
      (nunchuk ? extension.acceleration : remote.acceleration)[name.back() - 'x'] =
          static_cast<u16>(value);
      return;
    }
    if (nunchuk) {
      if (name == "c")
        return boolean(extension.c);
      if (name == "z")
        return boolean(extension.z);
      if (name == "stick-x" || name == "stick-y")
        return byte(name == "stick-x" ? extension.stick_x : extension.stick_y);
    } else {
      if (name == "battery")
        return byte(remote.battery);
      if (name.size() >= 5 && name.starts_with("ir") && name[2] >= '0' && name[2] <= '3' &&
          name[3] == '-') {
        auto &point = remote.infrared[static_cast<unsigned>(name[2] - '0')];
        const auto component = name.substr(4);
        if (component == "x" || component == "y") {
          check_range(value, -1, component == "x" ? 1023 : 767);
          (component == "x" ? point.x : point.y) = value == -1 ? 0xffff : static_cast<u16>(value);
          return;
        }
        if (component == "size") {
          check_range(value, -1, 15);
          point.size = value == -1 ? 0xff : static_cast<u8>(value);
          return;
        }
      }
      const std::array<std::pair<std::string_view, u16>, 11> buttons{{{"a", hw::RemoteA},
                                                                      {"b", hw::RemoteB},
                                                                      {"one", hw::RemoteOne},
                                                                      {"two", hw::RemoteTwo},
                                                                      {"plus", hw::RemotePlus},
                                                                      {"minus", hw::RemoteMinus},
                                                                      {"home", hw::RemoteHome},
                                                                      {"left", hw::RemoteLeft},
                                                                      {"right", hw::RemoteRight},
                                                                      {"down", hw::RemoteDown},
                                                                      {"up", hw::RemoteUp}}};
      for (auto [key, mask] : buttons)
        if (name == key)
          return flag(remote.buttons, mask);
    }
  }
  throw std::runtime_error("Unknown input replay action: " + action.name);
}

bool InputReplay::update(u64 instructions, u64 fields) {
  if (!needs_update(instructions, fields))
    return false;
  const std::array<u64, 2> counters{instructions, fields};
  bool changed = !initialized_;
  initialized_ = true;
  for (;;) {
    unsigned selected = 2;
    for (unsigned kind = 0; kind < 2; ++kind) {
      if (positions_[kind] == events_[kind].size())
        continue;
      const auto &event = events_[kind][positions_[kind]];
      if (event.when <= counters[kind] &&
          (selected == 2 || event.line < events_[selected][positions_[selected]].line))
        selected = kind;
    }
    if (selected == 2)
      break;
    for (const auto &action : events_[selected][positions_[selected]++].actions)
      apply(state_, action);
    changed = true;
  }
  update_deadlines();
  return changed;
}

const InputState &InputReplay::state() const {
  return state_;
}
} // namespace cupid::platform
