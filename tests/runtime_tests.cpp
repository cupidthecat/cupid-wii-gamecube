// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/machine.hpp"
#include "test_support.hpp"
#include <array>
#include <sstream>

int main() {
  return run_tests([] {
    using namespace cupid;
    runtime::Machine machine(Console::GameCube);
    machine.load_executable(runtime::self_test_dol());
    for (unsigned i = 0; i < 7; ++i)
      machine.step();
    CHECK(machine.cpu().state().gpr[5] == 16);
    CHECK(machine.cpu().state().gpr[7] == 16);
    CHECK(machine.memory().read32(0x5000) == 16);
    CHECK(machine.cpu().state().pc == 0x8000401c);
    machine.set_limits({8, 0, 0});
    machine.step();
    CHECK_THROWS(machine.step());
    auto malformed = runtime::self_test_dol();
    malformed[0x90] = 0xff;
    Memory memory(Console::GameCube);
    memory.write32(0x4000, 0xdeadbeef);
    CHECK_THROWS(runtime::load_dol(memory, malformed));
    CHECK(memory.read32(0x4000) == 0xdeadbeef);
    malformed = runtime::self_test_dol();
    malformed[0xe3] = 1;
    CHECK_THROWS(runtime::load_dol(memory, malformed));
    CHECK_THROWS(runtime::load_dol(memory, std::span<const u8>(malformed).first(128)));
    std::array<u8, 8> yuv{16, 128, 235, 128, 81, 90, 145, 240};
    memory.copy_in(0x1000, yuv);
    auto frame = platform::decode_xfb(memory, {0x1000, 2, 2, 4, 0, 0});
    CHECK(frame.rgba.size() == 16);
    CHECK(frame.rgba[0] == 0);
    CHECK(frame.rgba[3] == 255);
    CHECK(frame.rgba[4] == 254);
    CHECK(frame.rgba[8] == 254);
    CHECK(frame.rgba[9] == 0);
    CHECK(frame.rgba[10] == 0);
    CHECK_THROWS(platform::decode_xfb(memory, {0x1000, 3, 2, 6, 0, 0}));
    CHECK_THROWS(platform::decode_xfb(memory, {Memory::mem1_size - 4, 2, 2, 4, 0, 0}));
    std::ostringstream state;
    machine.write_state(state, "test-limit");
    CHECK(state.str().find("\"instructions\":8") != std::string::npos);
    platform::Output output({true, false, {}, {}});
    output.present(memory, {0x1000, 2, 2, 4, 0, 0});
    CHECK(output.presented_frames() == 1);
    CHECK(output.last_frame_hash() == platform::frame_hash(frame.rgba));
  });
}
