// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/platform/input_replay_follow.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace cupid::platform {
InputReplayFollower::InputReplayFollower(std::filesystem::path path) : path_(std::move(path)) {
}

std::unique_ptr<InputReplay> InputReplayFollower::refresh() {
  std::string bytes;
  try {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path_, error) || error)
      throw std::runtime_error("Input replay source is not a readable regular file");
    const auto size = std::filesystem::file_size(path_, error);
    if (error)
      throw std::runtime_error("Cannot determine input replay file size");
    if (size > maximum_file_size)
      throw std::runtime_error("Input replay file exceeds 4 MiB");
    const auto modified = std::filesystem::last_write_time(path_, error);
    if (error)
      throw std::runtime_error("Cannot inspect input replay modification time");
    std::ifstream file(path_, std::ios::binary);
    if (!file)
      throw std::runtime_error("Cannot read input replay file");
    bytes.resize(static_cast<std::size_t>(size), '\0');
    if (size) {
      file.read(bytes.data(), static_cast<std::streamsize>(size));
      if (file.gcount() != static_cast<std::streamsize>(size) || file.fail())
        throw std::runtime_error("Input replay file changed or failed during reading");
    }
    if (file.peek() != std::char_traits<char>::eof() || file.bad())
      throw std::runtime_error("Input replay file changed or failed during reading");
    if (std::filesystem::file_size(path_, error) != size || error)
      throw std::runtime_error("Input replay file changed during reading");
    if (std::filesystem::last_write_time(path_, error) != modified || error)
      throw std::runtime_error("Input replay file changed during reading");
    if (accepted_updates_ && bytes == accepted_bytes_) {
      error_.clear();
      rejected_bytes_.clear();
      status_ = InputReplayFollowStatus::Unchanged;
      return nullptr;
    }
    std::istringstream input(bytes);
    auto replay = std::make_unique<InputReplay>(input);
    accepted_bytes_ = std::move(bytes);
    ++accepted_updates_;
    error_.clear();
    rejected_bytes_.clear();
    status_ = InputReplayFollowStatus::Accepted;
    return replay;
  } catch (const std::exception &failure) {
    if (status_ != InputReplayFollowStatus::Rejected || error_ != failure.what() ||
        rejected_bytes_ != bytes)
      ++rejected_updates_;
    error_ = failure.what();
    rejected_bytes_ = std::move(bytes);
    status_ = InputReplayFollowStatus::Rejected;
    return nullptr;
  }
}

InputReplayFollowStatus InputReplayFollower::status() const {
  return status_;
}

const std::string &InputReplayFollower::error() const {
  return error_;
}

u64 InputReplayFollower::accepted_updates() const {
  return accepted_updates_;
}

u64 InputReplayFollower::rejected_updates() const {
  return rejected_updates_;
}
} // namespace cupid::platform
