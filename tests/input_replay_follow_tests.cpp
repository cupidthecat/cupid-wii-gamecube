// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/platform/input_replay_follow.hpp"
#include "test_support.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
class FixtureDirectory {
public:
  FixtureDirectory() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    path =
        std::filesystem::temp_directory_path() / ("cupid-input-follow-" + std::to_string(suffix));
    if (!std::filesystem::create_directory(path))
      throw std::runtime_error("Authored fixture directory already exists");
  }

  ~FixtureDirectory() {
    std::error_code error;
    const auto actual = std::filesystem::canonical(path, error);
    if (error)
      return;
    const auto parent = std::filesystem::canonical(std::filesystem::temp_directory_path(), error);
    if (!error && actual.parent_path() == parent &&
        actual.filename().string().starts_with("cupid-input-follow-"))
      std::filesystem::remove_all(actual, error);
  }

  void write(const std::string &text) {
    std::ofstream file(path / "replay.txt", std::ios::binary);
    file << text;
    file.close();
    if (!file)
      throw std::runtime_error("Cannot create authored input replay");
  }

  std::filesystem::path path;
};
} // namespace

int main() {
  return run_tests([] {
    using namespace cupid;
    FixtureDirectory fixture;
    const auto path = fixture.path / "replay.txt";
    platform::InputReplayFollower follow(path);
    CHECK(!follow.refresh());
    CHECK(!follow.error().empty());
    CHECK(follow.accepted_updates() == 0);
    CHECK(follow.status() == platform::InputReplayFollowStatus::Rejected);
    CHECK(follow.rejected_updates() == 1);
    CHECK(!follow.refresh());
    CHECK(follow.rejected_updates() == 1);
    fixture.write("instruction 0 pad0.a=1 remote0.a=1\nfield 20 pad0.a=0 remote0.a=0\n");
    auto accepted = follow.refresh();
    CHECK(accepted != nullptr);
    CHECK(follow.error().empty());
    CHECK(follow.accepted_updates() == 1);
    CHECK(follow.status() == platform::InputReplayFollowStatus::Accepted);
    CHECK(accepted->update(0, 0));
    CHECK((accepted->state().pads[0].buttons & 0x100) != 0);
    CHECK((accepted->state().remotes[0].buttons & hw::RemoteA) != 0);
    CHECK(!follow.refresh());
    CHECK(follow.accepted_updates() == 1);
    CHECK(follow.status() == platform::InputReplayFollowStatus::Unchanged);
    fixture.write("instruction 0 pad0.a=0\nfield 8 remote0.ir0-x=2000\n");
    CHECK(!follow.refresh());
    CHECK(!follow.error().empty());
    CHECK(follow.accepted_updates() == 1);
    CHECK(follow.rejected_updates() == 2);
    CHECK(!follow.refresh());
    CHECK(follow.rejected_updates() == 2);
    fixture.write("instruction 0 pad0.a=0\nfield 8 remote0.ir0-x=2001\n");
    CHECK(!follow.refresh());
    CHECK(follow.rejected_updates() == 3);
    CHECK(!accepted->update(1, 19));
    CHECK((accepted->state().pads[0].buttons & 0x100) != 0);
    CHECK(accepted->update(1, 20));
    CHECK((accepted->state().pads[0].buttons & 0x100) == 0);
    CHECK((accepted->state().remotes[0].buttons & hw::RemoteA) == 0);
    std::filesystem::remove(path);
    CHECK(!follow.refresh());
    CHECK(!follow.error().empty());
    CHECK(follow.accepted_updates() == 1);
    fixture.write("instruction 0 pad0.a=1 remote0.a=1\nfield 20 pad0.a=0 remote0.a=0\n");
    CHECK(!follow.refresh());
    CHECK(follow.error().empty());
    const auto stamp = std::filesystem::last_write_time(path);
    fixture.write("instruction 0 pad0.b=1 remote0.b=1\nfield 20 pad0.b=0 remote0.b=0\n");
    std::filesystem::last_write_time(path, stamp);
    auto changed = follow.refresh();
    CHECK(changed != nullptr);
    CHECK(follow.accepted_updates() == 2);
    CHECK(changed->update(0, 0));
    CHECK((changed->state().pads[0].buttons & 0x200) != 0);
    CHECK((changed->state().remotes[0].buttons & hw::RemoteB) != 0);
    CHECK((accepted->state().pads[0].buttons & 0x200) == 0);
    fixture.write("# An empty valid source deliberately releases controls.\n");
    auto empty = follow.refresh();
    CHECK(empty != nullptr);
    CHECK(empty->update(100, 100));
    CHECK(empty->state().pads[0].buttons == 0);
    CHECK(empty->state().remotes[0].buttons == 0);
    CHECK(follow.accepted_updates() == 3);
    fixture.write("instruction 0 pad0.stick-x=300\n");
    CHECK(!follow.refresh());
    CHECK(!follow.error().empty());
    CHECK(follow.accepted_updates() == 3);
    fixture.write("instruction 9 pad0.a=1\ninstruction 8 pad0.a=0\n");
    CHECK(!follow.refresh());
    CHECK(!follow.error().empty());
    std::filesystem::remove(path);
    std::filesystem::create_directory(path);
    CHECK(!follow.refresh());
    CHECK(!follow.error().empty());
    std::filesystem::remove(path);
    fixture.write("");
    std::filesystem::resize_file(path, platform::InputReplayFollower::maximum_file_size + 1);
    CHECK(!follow.refresh());
    CHECK(follow.error().find("4 MiB") != std::string::npos);
    CHECK(follow.accepted_updates() == 3);
  });
}
