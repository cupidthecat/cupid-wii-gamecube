// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/platform/input_replay.hpp"
#include "cupid/runtime/machine.hpp"
#include "test_support.hpp"
#include <array>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

namespace {
using namespace cupid;

void invalid(const std::string &text) {
  std::istringstream input(text);
  CHECK_THROWS(platform::InputReplay{input});
}

std::vector<u8> polling_program() {
  constexpr std::array<u32, 10> code{
      0x3c60cc00, // lis r3,0xcc00
      0x60636400, // ori r3,r3,0x6400
      0x3cc08000, // lis r6,0x8000
      0x3c804000, // lis r4,0x4000: SI direct command
      0x90830080, // stw r4,0x80(r3)
      0x38800701, // li r4,0x701: request eight input bytes
      0x90830034, // stw r4,0x34(r3)
      0x80a30080, // lwz r5,0x80(r3)
      0x90a66000, // stw r5,0x6000(r6)
      0x4bffffe8  // repeat the actual SI request
  };
  const auto section_size = (code.size() * 4 + 31) & ~std::size_t{31};
  std::vector<u8> dol(0x100 + section_size);
  const auto word = [&](std::size_t offset, u32 value) {
    for (unsigned i = 0; i < 4; ++i)
      dol[offset + i] = static_cast<u8>(value >> (24 - i * 8));
  };
  word(0, 0x100);
  word(0x48, 0x80004000);
  word(0x90, static_cast<u32>(code.size() * 4));
  word(0xe0, 0x80004000);
  for (unsigned i = 0; i < code.size(); ++i)
    word(0x100 + i * 4, code[i]);
  return dol;
}

void guest_input(Console console) {
  std::random_device random;
  const auto path =
      std::filesystem::temp_directory_path() /
      ("cupid-replay-" + std::to_string(random()) + "-" + std::to_string(random()) + ".txt");
  CHECK(!std::filesystem::exists(path));
  std::ofstream file(path);
  file << "instruction 0 pad0.a=1 pad0.stick-x=200\n"
          "instruction 10 pad0.a=0 pad0.b=1 pad0.stick-x=20\n";
  file.close();
  CHECK(file);
  runtime::Machine machine(console);
  platform::Output output({true, false, {}, {}});
  machine.set_output(&output);
  machine.set_input_replay(path);
  machine.load_executable(polling_program());
  for (unsigned i = 0; i < 9; ++i)
    machine.step();
  CHECK(machine.memory().read32(0x6000) == 0x0180c880);
  for (unsigned i = 0; i < 7; ++i)
    machine.step();
  CHECK(machine.memory().read32(0x6000) == 0x02801480);
  CHECK(machine.cpu().state().instructions == 16);
  std::filesystem::remove(path);
}

struct ReplayFile {
  std::filesystem::path path;

  ReplayFile() {
    std::random_device random;
    path =
        std::filesystem::temp_directory_path() / ("cupid-follow-guest-" + std::to_string(random()) +
                                                  "-" + std::to_string(random()) + ".txt");
    CHECK(!std::filesystem::exists(path));
  }

  ~ReplayFile() {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }

  void write(const std::string &text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    file.close();
    CHECK(file);
  }
};

void unchanged_cpu(const cpu::State &before, const cpu::State &after) {
  CHECK(before.gpr == after.gpr && before.spr == after.spr && before.sr == after.sr);
  for (std::size_t index = 0; index < before.fpr.size(); ++index)
    CHECK(before.fpr[index].bits == after.fpr[index].bits);
  CHECK(before.pc == after.pc && before.msr == after.msr && before.cr == after.cr);
  CHECK(before.fpscr == after.fpscr && before.last_pc == after.last_pc);
  CHECK(before.last_opcode == after.last_opcode && before.cycles == after.cycles);
  CHECK(before.instructions == after.instructions && before.timebase == after.timebase);
}

void follow_guest_input(Console console) {
  ReplayFile replay;
  replay.write("instruction 0 pad0.a=1 pad0.stick-x=200\n"
               "instruction 4200 pad0.a=0 pad0.b=1 pad0.stick-x=20\n");
  runtime::Machine machine(console);
  machine.load_executable(polling_program());
  machine.set_limits({0, 0, 0});
  auto &state = machine.cpu().state();
  state.fpr[0].bits = {0x7ff123456789abcdull, 0x8000000000000000ull};
  auto before = state;
  std::vector<std::string> diagnostics;
  machine.set_input_replay_follow(replay.path, [&](const std::string &text) {
    unchanged_cpu(before, state);
    diagnostics.push_back(text);
  });
  unchanged_cpu(before, state);
  CHECK(diagnostics.size() == 1);
  const auto step = [&] {
    before = state;
    machine.step();
  };
  const auto advance = [&](u64 instructions) {
    while (state.instructions < instructions)
      step();
  };
  const auto poll = [&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    step();
  };
  advance(9);
  CHECK(machine.memory().read32(0x6000) == 0x0180c880);
  advance(4096);
  const std::string invalid_replay = "instruction 0 pad0.a=0\nfield 8 remote0.ir0-x=2000\n";
  replay.write(invalid_replay);
  poll();
  CHECK(diagnostics.size() == 2 && diagnostics.back().find("rejected") != std::string::npos);
  advance(4120);
  CHECK(machine.memory().read32(0x6000) == 0x0180c880);
  advance(4230);
  CHECK(machine.memory().read32(0x6000) == 0x02801480);

  advance(8192);
  const std::string replacement = "instruction 0 pad0.x=1 pad0.stick-x=99\n";
  replay.write(replacement);
  poll();
  CHECK(diagnostics.size() == 3 && diagnostics.back().find("version 2") != std::string::npos);
  advance(8220);
  CHECK(machine.memory().read32(0x6000) == 0x04806380);

  advance(12288);
  replay.write(invalid_replay);
  poll();
  CHECK(diagnostics.size() == 4 && diagnostics.back().find("rejected") != std::string::npos);
  advance(12320);
  CHECK(machine.memory().read32(0x6000) == 0x04806380);
  advance(16384);
  replay.write(replacement);
  poll();
  CHECK(diagnostics.size() == 5 && diagnostics.back().find("recovered") != std::string::npos);
  std::ostringstream followed;
  machine.write_state(followed, "test");
  CHECK(followed.str().find("\"input_replay_updates\":2,") != std::string::npos);
  CHECK(followed.str().find("\"input_replay_rejections\":2,") != std::string::npos);

  auto missing = replay.path;
  missing += ".missing";
  CHECK(!std::filesystem::exists(missing));
  before = state;
  CHECK_THROWS(machine.set_input_replay_follow(missing));
  unchanged_cpu(before, state);
  replay.write(invalid_replay);
  CHECK_THROWS(machine.set_input_replay(replay.path));
  unchanged_cpu(before, state);
  replay.write("instruction 0 pad0.b=1 pad0.stick-x=40\n");
  machine.set_input_replay(replay.path);
  unchanged_cpu(before, state);
  advance(state.instructions + 20);
  CHECK(machine.memory().read32(0x6000) == 0x02802880);
  std::ostringstream one_shot;
  machine.write_state(one_shot, "test");
  CHECK(one_shot.str().find("\"input_replay_updates\":0,") != std::string::npos);
  CHECK(one_shot.str().find("\"input_replay_rejections\":0,") != std::string::npos);
  CHECK(diagnostics.size() == 5);
}
} // namespace

int main() {
  return run_tests([] {
    std::istringstream input(
        "# Both counters are absolute from the new machine.\n"
        "instruction 0 pad0.a=1 pad1.connected=1 pad1.stick-y=255 remote0.ir0-x=480 "
        "remote0.ir0-y=320 remote0.ir0-size=4 nunchuk0.stick-x=220\n"
        "field 2 remote0.a=1 remote0.accel-x=768 nunchuk0.c=1\n"
        "instruction 10 pad0.a=0 remote0.accel-x=512\n"
        "field 3 remote0.a=0 remote0.ir0-x=-1 remote0.ir0-y=-1 remote0.ir0-size=-1\n");
    platform::InputReplay replay(input);
    CHECK(replay.update(0, 0));
    CHECK(replay.state().pads[0].connected && replay.state().pads[0].buttons == 0x100);
    CHECK(replay.state().pads[1].connected && replay.state().pads[1].stick_y == 255);
    CHECK(replay.state().remotes[0].connected && replay.state().remotes[0].nunchuk.attached);
    CHECK(replay.state().remotes[0].nunchuk.stick_x == 220);
    CHECK(replay.state().remotes[0].infrared[0].x == 480);
    CHECK(!replay.update(1, 0));
    CHECK(replay.update(9, 2));
    CHECK(replay.state().remotes[0].buttons == hw::RemoteA);
    CHECK(replay.state().remotes[0].acceleration[0] == 768);
    CHECK(replay.state().remotes[0].nunchuk.c);
    CHECK(replay.update(10, 2));
    CHECK(replay.state().pads[0].buttons == 0);
    CHECK(replay.state().remotes[0].acceleration[0] == 512);
    CHECK(replay.update(11, 3));
    CHECK(replay.state().remotes[0].buttons == 0);
    CHECK(replay.state().remotes[0].infrared[0].x == 0xffff);
    CHECK(replay.state().remotes[0].infrared[0].size == 0xff);
    CHECK(!replay.update(100, 100));
    replay.reset();
    CHECK(replay.update(0, 0));
    CHECK(replay.state().pads[0].buttons == 0x100);

    std::istringstream tied("field 1 remote0.b=1\ninstruction 5 remote0.b=0\n");
    platform::InputReplay simultaneous(tied);
    CHECK(simultaneous.update(5, 1));
    CHECK(simultaneous.state().remotes[0].buttons == 0);
    invalid("frame 1 pad0.a=1");
    invalid("instruction -1 pad0.a=1");
    invalid("instruction 1");
    invalid("instruction 1 pad0.a");
    invalid("instruction 1 pad4.a=1");
    invalid("instruction 1 pad0.a=2");
    invalid("instruction 1 pad0.stick-x=256");
    invalid("instruction 1 remote0.accel-x=1024");
    invalid("instruction 1 remote0.ir0-y=768");
    invalid("instruction 1 nunchuk0.accel-y=-1");
    invalid("instruction 1 remote0.unknown=0");
    invalid("instruction 2 pad0.a=1\ninstruction 1 pad0.a=0");
    invalid("instruction 18446744073709551616 pad0.a=0");
    guest_input(Console::GameCube);
    guest_input(Console::Wii);
    follow_guest_input(Console::GameCube);
    follow_guest_input(Console::Wii);
  });
}
