// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/platform/input_replay.hpp"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace cupid::platform {
enum class InputReplayFollowStatus { Unchanged, Accepted, Rejected };

class InputReplayFollower {
public:
  static constexpr std::size_t maximum_file_size = 4 * 1024 * 1024;
  explicit InputReplayFollower(std::filesystem::path path);
  // The caller retains its existing replay unless a complete replacement is returned.
  std::unique_ptr<InputReplay> refresh();
  InputReplayFollowStatus status() const;
  const std::string &error() const;
  u64 accepted_updates() const;
  u64 rejected_updates() const;

private:
  std::filesystem::path path_;
  std::string accepted_bytes_, rejected_bytes_, error_;
  InputReplayFollowStatus status_ = InputReplayFollowStatus::Unchanged;
  u64 accepted_updates_ = 0, rejected_updates_ = 0;
};
} // namespace cupid::platform
