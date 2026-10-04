// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/platform/input_replay.hpp"
#include "cupid/runtime/machine.hpp"
#include "test_support.hpp"
#include <array>
#include <fstream>
#include <random>
#include <sstream>

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
  });
}
