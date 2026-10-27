// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/memory.hpp"
#include "cupid/ios/network.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <map>
#include <string>

using namespace cupid;

namespace {
constexpr u32 input = 0x10000, output = 0x11000;

struct Fixture {
  Memory memory{Console::Wii};
  ios::Network network{memory};
  u64 clock = 1000000000;
  std::map<std::string, std::vector<u8>> files;
  bool writable = true;
  std::string failing_path;

  Fixture() {
    network.unix_time = [this] { return clock; };
    network.read_file = [this](std::string_view path) -> std::optional<std::vector<u8>> {
      const auto it = files.find(std::string(path));
      if (it == files.end())
        return std::nullopt;
      return it->second;
    };
    network.write_file = [this](std::string_view path, std::span<const u8> bytes) {
      if (!writable || path == failing_path)
        return false;
      files[std::string(path)] = {bytes.begin(), bytes.end()};
      return true;
    };
    std::fill(memory.range(output, 1024).begin(), memory.range(output, 1024).end(), u8(0xa5));
  }

  s32 request(u32 op, u32 in_size = 0, u32 out_size = 32) {
    return network.ioctl(ios::NetworkDevice::Request, op, input, in_size, output, out_size);
  }

  s32 time(u32 op, u32 in_size = 0, u32 out_size = 32) {
    return network.ioctl(ios::NetworkDevice::Time, op, input, in_size, output, out_size);
  }
};

void clock_tests() {
  Fixture f;
  CHECK(f.time(0x14) == 0);
  CHECK(f.memory.read32(output) == 0);
  CHECK(f.memory.read64(output + 4) == f.clock);
  CHECK(f.memory.read32(output + 12) == 0xa5a5a5a5);
  f.memory.write64(input, f.clock - 100);
  f.memory.write32(input + 8, 0);
  CHECK(f.time(0x15, 12) == 0);
  CHECK(f.network.adjusted_utc() == f.clock + 100);
  f.clock += 7;
  CHECK(f.time(0x14) == 0);
  CHECK(f.memory.read64(output + 4) == 1000000107);
  f.memory.write32(input, 999999990);
  f.memory.write32(input + 4, 0);
  CHECK(f.time(0x17, 8) == 0);
  CHECK(f.time(0x18) == 0);
  CHECK(f.memory.read64(output + 4) == 117);
  CHECK(f.time(0x16, 0, 4) == -9);
  CHECK(f.memory.read32(output) == 0);
  CHECK_THROWS(f.time(0x19));
  f.memory.write64(input, f.clock + 1000);
  f.memory.write32(input + 8, 0);
  CHECK(f.time(0x15, 12) == 0);
  CHECK(f.network.adjusted_utc() == f.clock - 1000);
  const auto previous = f.network.adjusted_utc();
  f.memory.write32(input + 8, 1);
  CHECK_THROWS(f.time(0x15, 12));
  CHECK(f.network.adjusted_utc() == previous);
  f.network.unix_time = {};
  CHECK_THROWS(f.network.adjusted_utc());
}

void range_tests() {
  Fixture f;
  for (const auto device : {ios::NetworkDevice::Request, ios::NetworkDevice::Time}) {
    const u32 op = device == ios::NetworkDevice::Time ? 0x14 : 4;
    CHECK(f.network.ioctl(device, op, 0, 0, 0, 32) == -4);
    CHECK(f.network.ioctl(device, op, 0, 0, output + 1, 32) == -4);
    CHECK(f.network.ioctl(device, op, 0, 0, output, 11) == -4);
    CHECK(f.network.ioctl(device, op, 0, 0, Memory::mem1_size - 4, 32) == -4);
    CHECK(f.network.ioctl(device, op, 0, 0, 0xfffffff0, 32) == -4);
    CHECK(f.memory.read32(output) == 0xa5a5a5a5);
  }
  CHECK(f.time(0x15, 11) == -4);
  CHECK(f.time(0x17, 7) == -4);
  CHECK(f.network.ioctl(ios::NetworkDevice::Time, 0x15, input + 1, 12, output, 12) == -4);
  unsigned mmio = 0;
  f.memory.mmio_handles = [](u32 address) { return address == 0x0d000000; };
  f.memory.mmio_write = [&](u32, u32, unsigned) { ++mmio; };
  CHECK(f.network.ioctl(ios::NetworkDevice::Time, 0x14, 0, 0, 0x0d000000, 12) == -4);
  CHECK(mmio == 0);
  CHECK(f.network.ioctl(ios::NetworkDevice::Time, 0x14, 0, 0, 0x10001000, 12) == 0);
  CHECK(f.memory.read64(0x10001004) == f.clock);
}

void scheduler_tests() {
  Fixture f;
  CHECK(f.request(4) == 0);
  CHECK(f.memory.read32(output) == 0);
  CHECK(f.memory.read32(output + 4) == 1);
  CHECK(f.memory.read32(output + 8) == 2);
  CHECK(f.request(0x1e, 0, 1024) == 0);
  CHECK(f.memory.read32(output) == 0);
  CHECK(f.memory.read32(output + 4) == 0xffffffff);
  CHECK(f.memory.read32(output + 252) == 0);
  CHECK(f.memory.read32(output + 256) == 0xa5a5a5a5);
  CHECK(f.request(1) == 0);
  CHECK(f.network.scheduler_suspended());
  CHECK(f.request(3) == 0);
  CHECK(!f.network.scheduler_suspended());
  CHECK(f.network.ioctl(ios::NetworkDevice::Request, 2, 0, 0, 0, 0) == 0);
  CHECK(f.network.scheduler_suspended());
  CHECK_THROWS(f.request(6));
  CHECK_THROWS(f.request(0x99));
}

void error_tests() {
  Fixture f;
  CHECK(f.request(0x0a) == 0);
  CHECK(s32(f.memory.read32(output)) == -14);
  CHECK(f.request(0x1e, 0, 256) == 0);
  CHECK(s32(f.memory.read32(output + 8)) == -102214);
  CHECK(f.memory.read32(output + 20) == 1);
  CHECK(s32(f.memory.read32(output + 128)) == -102214);
  CHECK(f.request(0x0e, 12) == 0);
  CHECK(s32(f.memory.read32(output)) == -14);
  auto &list = f.files["/shared2/wc24/nwc24dl.bin"];
  list.resize(128 + 120 * 16 + 120 * 512);
  list[0] = 'W';
  list[1] = 'c';
  list[2] = 'D';
  list[3] = 'l';
  list[7] = 1;
  f.memory.write16(input + 6, 120);
  CHECK(f.request(0x0e, 12) == 0);
  CHECK(s32(f.memory.read32(output)) == -3);
  f.memory.write16(input + 6, 0);
  CHECK(f.request(0x0e, 12) == 0);
  CHECK(s32(f.memory.read32(output)) == -13);
  list[128 + 120 * 16 + 15] = 1;
  CHECK_THROWS(f.request(0x0e, 12));
  for (unsigned i = 0; i < 40; ++i)
    CHECK(f.request(0x0a) == 0);
  CHECK(f.request(0x1e, 0, 256) == 0);
  CHECK(f.memory.read32(output + 20) == 44);
  CHECK(s32(f.memory.read32(output + 128)) == -102214);
}

std::vector<u8> encoded_settings() {
  constexpr std::string_view text = "AREA=USA\r\nMODEL=RVL\r\n";
  std::vector<u8> bytes(256);
  u32 key = 0x73b5dbfa;
  for (unsigned i = 0; i < bytes.size(); ++i) {
    bytes[i] = u8((i < text.size() ? u8(text[i]) : 0) ^ u8(key));
    key = std::rotl(key, 1);
  }
  return bytes;
}

void config_tests() {
  Fixture f;
  CHECK(f.request(0x0f) == 0);
  CHECK(s32(f.memory.read32(output)) == -1);
  CHECK(f.memory.read64(output + 4) == 0);
  CHECK(f.memory.read32(output + 12) == 0);
  const auto initial = f.files.at("/shared2/wc24/nwc24msg.cfg");
  CHECK(initial.size() == 1024);
  CHECK(initial[0] == 'W' && initial[1] == 'c' && initial[2] == 'C' && initial[3] == 'f');
  CHECK(initial[7] == 8);
  f.files["/title/00000001/00000002/data/setting.txt"] = encoded_settings();
  f.network.device_id = [] { return std::optional<u32>(0x12345678); };
  CHECK(f.request(0x0f) == 0);
  CHECK(s32(f.memory.read32(output)) == 0);
  const u64 id = f.memory.read64(output + 4);
  CHECK(id == 6382198605942757ull);
  CHECK(ios::make_wc24_id(0x12345678, 1, 1, 1) == 5819243048935396ull);
  CHECK(ios::make_wc24_id(0x12345678, 31, 1, 1) == 3567449677688293ull);
  CHECK_THROWS(ios::make_wc24_id(0x12345678, 32, 1, 1));
  CHECK_THROWS(ios::make_wc24_id(0x12345678, 0, 8, 1));
  CHECK_THROWS(ios::make_wc24_id(0x12345678, 0, 1, 8));
  CHECK(f.memory.read32(output + 12) == 1);
  const auto saved = f.files.at("/shared2/wc24/nwc24msg.cfg");
  CHECK(saved != initial);
  CHECK(saved == f.files.at("/shared2/wc24/nwc24msg.cbk"));
  f.network.reset_storage();
  CHECK(f.request(0x0f) == 0);
  CHECK(s32(f.memory.read32(output)) == -35);
  CHECK(f.memory.read64(output + 4) == id);
  CHECK(f.files.at("/shared2/wc24/nwc24msg.cfg") == saved);
  CHECK_THROWS(f.request(0x10));
  f.files["/shared2/wc24/nwc24msg.cfg"][8] ^= 1;
  f.network.reset_storage();
  CHECK(f.request(0x0f) == 0);
  CHECK(s32(f.memory.read32(output)) == -14);
  CHECK(f.files.at("/shared2/wc24/nwc24msg.cfg")[8] == u8(saved[8] ^ 1));
  Fixture readonly;
  readonly.writable = false;
  CHECK(readonly.request(0x0f) == 0);
  CHECK(s32(readonly.memory.read32(output)) == -19);
  CHECK(readonly.files.empty());

  Fixture failed_commit;
  CHECK(failed_commit.request(0x0f) == 0);
  const auto prior = failed_commit.files.at("/shared2/wc24/nwc24msg.cfg");
  failed_commit.files["/title/00000001/00000002/data/setting.txt"] = encoded_settings();
  failed_commit.network.device_id = [] { return std::optional<u32>(0x12345678); };
  failed_commit.failing_path = "/shared2/wc24/nwc24msg.cfg";
  CHECK(failed_commit.request(0x0f) == 0);
  CHECK(s32(failed_commit.memory.read32(output)) == -19);
  CHECK(failed_commit.memory.read64(output + 4) == 0);
  CHECK(failed_commit.memory.read32(output + 12) == 0);
  CHECK(failed_commit.files.at("/shared2/wc24/nwc24msg.cfg") == prior);
  CHECK(failed_commit.files.at("/shared2/wc24/nwc24msg.cbk") != prior);
  failed_commit.failing_path.clear();
  CHECK(failed_commit.request(0x0f) == 0);
  CHECK(failed_commit.memory.read64(output + 4) == 6382198605942757ull);

  Fixture unreadable;
  const std::vector<u8> private_config(1024, 0x5a);
  unreadable.files["/shared2/wc24/nwc24msg.cfg"] = private_config;
  unreadable.network.read_file = [](std::string_view) -> std::optional<std::vector<u8>> {
    throw ios::NetworkStorageError(-16);
  };
  CHECK(unreadable.request(0x0f) == 0);
  CHECK(s32(unreadable.memory.read32(output)) == -16);
  CHECK(unreadable.files.at("/shared2/wc24/nwc24msg.cfg") == private_config);
  CHECK(unreadable.files.size() == 1);
}
} // namespace

int main() {
  return run_tests([] {
    clock_tests();
    range_tests();
    scheduler_tests();
    error_tests();
    config_tests();
  });
}
