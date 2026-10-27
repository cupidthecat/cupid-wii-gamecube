// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/machine.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace cupid;
constexpr u32 request = 0x10000, transfer = 0x17000, pending = 0x18000;

bool same_cpu(const cpu::State &a, const cpu::State &b) {
  if (a.gpr != b.gpr || a.spr != b.spr || a.sr != b.sr || a.pc != b.pc || a.msr != b.msr ||
      a.cr != b.cr || a.fpscr != b.fpscr || a.last_pc != b.last_pc ||
      a.last_opcode != b.last_opcode || a.cycles != b.cycles || a.instructions != b.instructions ||
      a.timebase != b.timebase)
    return false;
  for (unsigned i = 0; i < a.fpr.size(); ++i)
    if (a.fpr[i].bits != b.fpr[i].bits)
      return false;
  return true;
}

std::vector<u8> program() {
  // The authored PPC loop issues each queued IPC request and acknowledges both
  // hardware phases. Waiting instructions supply ordinary cycles to IOS/devices.
  constexpr std::array<u32, 28> code{
      0x3c80cd00, 0x3ca08001, 0x60a58000, 0x80c50000, 0x2c060000, 0x4182fff8, 0x90c40000,
      0x38e00031, 0x90e40004, 0x80e40004, 0x70e80002, 0x4182fff8, 0x38e00032, 0x90e40004,
      0x3ce04000, 0x90e40030, 0x80e40004, 0x70e80004, 0x4182fff8, 0x81040008, 0x91050004,
      0x38e00034, 0x90e40004, 0x3ce04000, 0x90e40030, 0x38e00000, 0x90e50000, 0x4bffffa0};
  std::vector<u8> dol(0x100 + ((code.size() * 4 + 31) & ~std::size_t(31)));
  const auto word = [&](unsigned offset, u32 value) {
    for (unsigned i = 0; i < 4; ++i)
      dol[offset + i] = u8(value >> (24 - i * 8));
  };
  word(0, 0x100);
  word(0x48, 0x80004000);
  word(0x90, u32(code.size() * 4));
  word(0xe0, 0x80004000);
  for (unsigned i = 0; i < code.size(); ++i)
    word(0x100 + i * 4, code[i]);
  return dol;
}

class Fixture {
public:
  Fixture() : machine(Console::Wii) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    root = std::filesystem::temp_directory_path() / ("cupid-follow-hid-" + std::to_string(suffix));
    CHECK(std::filesystem::create_directory(root));
    machine.set_user_directory(root / "user");
    machine.set_limits({0, 0, 0});
    machine.load_executable(program());
    write(held_source());
    before_step = machine.cpu().state();
    machine.set_input_replay_follow(root / "input.txt", [&](const std::string &message) {
      CHECK(attempted_steps % 4096 == 0);
      CHECK(same_cpu(before_step, machine.cpu().state()));
      CHECK(!message.empty());
      diagnostics.push_back(message);
    });
    CHECK(diagnostics.size() == 1);
    prepare(1);
    constexpr char path[] = "/dev/usb/oh1/57e/305";
    machine.memory().copy_in(0x11000, {reinterpret_cast<const u8 *>(path), sizeof(path)});
    machine.memory().write32(request + 12, 0x11000);
    machine.memory().write32(request + 16, 3);
    const s32 opened = complete();
    CHECK(opened >= 0);
    fd = u32(opened);
  }

  ~Fixture() {
    std::error_code error;
    const auto actual = std::filesystem::canonical(root, error);
    if (error)
      return;
    const auto parent = std::filesystem::canonical(std::filesystem::temp_directory_path(), error);
    if (!error && actual.parent_path() == parent &&
        actual.filename().string().starts_with("cupid-follow-hid-"))
      std::filesystem::remove_all(actual, error);
  }

  static std::string held_source() {
    return "instruction 0 remote0.connected=1 nunchuk0.connected=1 remote0.a=1 remote0.b=1 "
           "remote0.ir0-x=111 remote0.ir0-y=222 remote0.ir0-size=4 "
           "remote0.ir1-x=239 remote0.ir1-y=222 remote0.ir1-size=4\n";
  }

  static std::string released_source() {
    return "instruction 0 remote0.connected=1 nunchuk0.connected=1 remote0.a=0 remote0.b=0 "
           "remote0.ir0-x=333 remote0.ir0-y=444 remote0.ir0-size=4 "
           "remote0.ir1-x=461 remote0.ir1-y=444 remote0.ir1-size=4\n";
  }

  void write(const std::string &text) {
    std::ofstream file(root / "input.txt", std::ios::binary);
    file << text;
    file.close();
    CHECK(bool(file));
  }

  void step() {
    if (attempted_steps % 4096 == 0)
      before_step = machine.cpu().state();
    machine.step();
    ++attempted_steps;
    CHECK(machine.cpu().state().spr[cpu::spr::dar] == 0);
    CHECK(machine.cpu().state().spr[cpu::spr::dsisr] == 0);
  }

  void wait_diagnostics(std::size_t count) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (diagnostics.size() < count) {
      CHECK(std::chrono::steady_clock::now() < deadline);
      step();
    }
    CHECK(diagnostics.size() == count);
  }

  std::string state() const {
    std::ostringstream result;
    machine.write_state(result, "authored-input-follow");
    return result.str();
  }

  void prepare(u32 command) {
    auto bytes = machine.memory().range(request, 64);
    std::fill(bytes.begin(), bytes.end(), u8(0));
    machine.memory().write32(request, command);
    machine.memory().write32(request + 8, fd);
  }

  s32 complete() {
    auto &memory = machine.memory();
    CHECK(memory.read32(pending) == 0);
    memory.write32(pending + 4, 0);
    memory.write32(pending, request);
    const u64 start = machine.cpu().state().cycles;
    while (memory.read32(pending) != 0) {
      step();
      CHECK(machine.cpu().state().cycles - start < 30000000);
    }
    CHECK(memory.read32(pending + 4) == request);
    CHECK(memory.read32(request) == 8);
    return s32(memory.read32(request + 4));
  }

  s32 ioctlv(u32 operation, std::initializer_list<std::pair<u32, u32>> inputs,
             std::pair<u32, u32> payload) {
    prepare(7);
    auto &memory = machine.memory();
    memory.write32(request + 12, operation);
    memory.write32(request + 16, u32(inputs.size()));
    memory.write32(request + 20, 1);
    memory.write32(request + 24, 0x14000);
    u32 at = 0x14000;
    for (const auto &[address, size] : inputs) {
      memory.write32(at, address);
      memory.write32(at + 4, size);
      at += 8;
    }
    memory.write32(at, payload.first);
    memory.write32(at + 4, payload.second);
    return complete();
  }

  void hci(std::span<const u8> bytes) {
    auto &memory = machine.memory();
    memory.copy_in(transfer, bytes);
    memory.write8(0x15000, 0x20);
    memory.write8(0x15001, 0);
    memory.write16(0x15002, 0);
    memory.write16(0x15004, 0);
    memory.write16(0x15006, u16(bytes.size() << 8));
    memory.write8(0x15008, 0);
    CHECK(
        ioctlv(0,
               {{0x15000, 1}, {0x15001, 1}, {0x15002, 2}, {0x15004, 2}, {0x15006, 2}, {0x15008, 1}},
               {transfer, u32(bytes.size())}) == s32(bytes.size()));
  }

  void acl(u16 cid, std::span<const u8> payload) {
    std::vector<u8> bytes{0,       0x21,        u8(payload.size() + 4), 0, u8(payload.size()), 0,
                          u8(cid), u8(cid >> 8)};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    machine.memory().copy_in(transfer, bytes);
    machine.memory().write8(0x15000, 2);
    machine.memory().write16(0x15002, u16(bytes.size()));
    CHECK(ioctlv(1, {{0x15000, 1}, {0x15002, 2}}, {transfer, u32(bytes.size())}) == 0);
  }

  std::vector<u8> input(u8 endpoint) {
    machine.memory().write8(0x15000, endpoint);
    machine.memory().write16(0x15002, 512);
    const s32 size = ioctlv(endpoint == 0x81 ? 2 : 1, {{0x15000, 1}, {0x15002, 2}}, {0x13000, 512});
    CHECK(size > 0 && size <= 512);
    const auto bytes = machine.memory().range(0x13000, u32(size));
    return {bytes.begin(), bytes.end()};
  }

  void connect() {
    hci(std::array<u8, 16>{5, 4, 13, 0x11, 2, 0x19, 0x79, 0, 0, 0x18, 0xcc, 0, 0, 0, 0, 0});
    CHECK(input(0x81)[0] == 0x0f);
    auto response = input(0x81);
    CHECK(response.size() == 13 && response[0] == 3 && response[2] == 0 && response[3] == 0 &&
          response[4] == 1);
    acl(1, std::array<u8, 8>{2, 1, 4, 0, 0x13, 0, 0x61, 0});
    response = input(0x82);
    CHECK(response.size() == 20 && response[8] == 3 && response[12] == 0x40 &&
          response[14] == 0x61 && response[16] == 0);
    acl(1, std::array<u8, 12>{4, 2, 8, 0, 0x40, 0, 0, 0, 1, 2, 185, 0});
    response = input(0x82);
    CHECK(response[8] == 5 && response[12] == 0x61);
    CHECK(input(0x81)[0] == 0x13);
    response = input(0x82);
    CHECK(response.size() == 20 && response[8] == 4 && response[12] == 0x61);
    acl(1, std::array<u8, 10>{5, response[9], 6, 0, 0x40, 0, 0, 0, 0, 0});
    response = input(0x82);
    CHECK(response.size() >= 10 && response[8] == 0xa1 && response[9] == 0x20);
  }

  void hid(std::span<const u8> bytes) {
    std::vector<u8> payload{0xa2};
    payload.insert(payload.end(), bytes.begin(), bytes.end());
    acl(0x40, payload);
  }

  void select_basic_ir() {
    hid(std::array<u8, 2>{0x15, 0});
    CHECK(input(0x81)[0] == 0x13);
    const auto status = input(0x82);
    CHECK(status.size() >= 10 && status[8] == 0xa1 && status[9] == 0x20);
    hid(std::array<u8, 2>{0x13, 6});
    hid(std::array<u8, 22>{0x16, 4, 0xb0, 0, 0x33, 1, 1});
    hid(std::array<u8, 22>{0x16, 4, 0xb0, 0, 0x30, 1, 8});
    hid(std::array<u8, 3>{0x12, 4, 0x36});
    // Completed-packet HCI events have priority over ACL data in this transport.
    CHECK(input(0x81)[0] == 0x13);
  }

  void expect_report(bool held, u16 x, u16 y) {
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
      const auto bytes = input(0x82);
      if (bytes.size() < 22 || bytes[8] != 0xa1 || bytes[9] != 0x36)
        continue;
      const u16 x0 = u16(bytes[12] | (((bytes[14] >> 4) & 3) << 8));
      const u16 y0 = u16(bytes[13] | (((bytes[14] >> 6) & 3) << 8));
      const u16 x1 = u16(bytes[15] | ((bytes[14] & 3) << 8));
      const u16 y1 = u16(bytes[16] | (((bytes[14] >> 2) & 3) << 8));
      if ((bytes[11] & 0x0c) == (held ? 0x0c : 0) && x0 == x && y0 == y && x1 == x + 128 && y1 == y)
        return;
    }
    throw std::runtime_error("Authored guest HID read did not observe the expected input");
  }

  runtime::Machine machine;
  std::vector<std::string> diagnostics;

private:
  std::filesystem::path root;
  cpu::State before_step;
  u64 attempted_steps = 0;
  u32 fd = 0;
};
} // namespace

int main() {
  return run_tests([] {
    Fixture fixture;
    fixture.connect();
    fixture.select_basic_ir();
    fixture.expect_report(true, 111, 222);
    fixture.expect_report(true, 111, 222);
    fixture.write(Fixture::released_source());
    fixture.wait_diagnostics(2);
    fixture.expect_report(false, 333, 444);
    fixture.expect_report(false, 333, 444);
    CHECK(fixture.state().find("\"input_replay_updates\":2") != std::string::npos);
    fixture.write("instruction 0 remote0.a=1 remote0.b=1\nfield 4 remote0.ir0-x=2000\n");
    fixture.wait_diagnostics(3);
    fixture.expect_report(false, 333, 444);
    fixture.expect_report(false, 333, 444);
    CHECK(fixture.state().find("\"input_replay_rejections\":1") != std::string::npos);
    CHECK(fixture.state().find("\"input_replay_updates\":2") != std::string::npos);
    fixture.write(Fixture::released_source());
    fixture.wait_diagnostics(4);
    fixture.expect_report(false, 333, 444);
    CHECK(fixture.state().find("\"input_replay_updates\":2") != std::string::npos);
    CHECK(fixture.state().find("\"input_replay_rejections\":1") != std::string::npos);
    CHECK(fixture.diagnostics.size() == 4);
  });
}
