// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/memory.hpp"
#include "cupid/cpu/interpreter.hpp"
#include "cupid/hw/devices.hpp"
#include "cupid/hw/remote.hpp"
#include "cupid/ios/ipc.hpp"
#include "cupid/ios/sysconf.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace cupid;

namespace {
constexpr u32 ipc_base = 0x0d000000, request = 0x10000, path_buffer = 0x11000;
constexpr u32 data_buffer = 0x12000, output_buffer = 0x13000;

struct Fixture {
  Memory memory{Console::Wii};
  hw::Devices devices{Console::Wii};
  ios::Ipc ipc{memory, devices, Console::Wii};

  Fixture() {
    devices.write(0x0c003004, 0x4000, 4);
  }

  void text(u32 address, const std::string &value) {
    for (unsigned i = 0; i < value.size(); ++i)
      memory.write8(address + i, u8(value[i]));
    memory.write8(address + u32(value.size()), 0);
  }

  void prepare(u32 command, u32 fd = 0) {
    auto data = memory.range(request, 64);
    std::fill(data.begin(), data.end(), u8(0));
    memory.write32(request, command);
    memory.write32(request + 8, fd);
  }

  void submit(u32 address = request) {
    devices.write(ipc_base, address, 4);
    devices.write(ipc_base + 4, 0x31, 4);
    ipc.advance(6000);
    CHECK((devices.read(ipc_base + 4, 4) & 3) == 2);
  }

  void clear_ack() {
    devices.write(ipc_base + 4, 0x32, 4);
    devices.write(ipc_base + 0x30, 0x40000000, 4);
  }

  void clear_reply() {
    devices.write(ipc_base + 4, 0x34, 4);
    devices.write(ipc_base + 0x30, 0x40000000, 4);
  }

  s32 complete(u64 elapsed = 100000000) {
    submit();
    clear_ack();
    ipc.advance(elapsed);
    if (memory.read32(request) != 8)
      std::cerr << "Pending IPC request: command=" << memory.read32(request)
                << " operation=" << memory.read32(request + 12)
                << " transfer=" << unsigned(memory.read8(0x17000)) << " cycles=" << ipc.cycles()
                << '\n';
    CHECK(memory.read32(request) == 8);
    CHECK(devices.read(ipc_base + 8, 4) == request);
    CHECK(devices.read(ipc_base + 4, 4) & 4);
    const s32 result = s32(memory.read32(request + 4));
    clear_reply();
    return result;
  }

  s32 open(const std::string &path, u32 mode = 3) {
    prepare(1);
    text(path_buffer, path);
    memory.write32(request + 12, path_buffer);
    memory.write32(request + 16, mode);
    return complete();
  }

  s32 ioctlv(u32 fd, u32 operation, std::initializer_list<std::pair<u32, u32>> input,
             std::initializer_list<std::pair<u32, u32>> output, u64 elapsed = 100000000) {
    prepare(7, fd);
    memory.write32(request + 12, operation);
    memory.write32(request + 16, u32(input.size()));
    memory.write32(request + 20, u32(output.size()));
    memory.write32(request + 24, 0x14000);
    u32 cursor = 0x14000;
    for (const auto &list : {input, output})
      for (const auto &[address, size] : list) {
        memory.write32(cursor, address);
        memory.write32(cursor + 4, size);
        cursor += 8;
      }
    return complete(elapsed);
  }
};

void memory_values() {
  Memory memory(Console::Wii);
  memory.write32(0, 0x12345678);
  CHECK(ios::initialize_ios_memory(memory, 30));
  CHECK(memory.read32(0) == 0);
  CHECK(memory.read32(0x28) == 0x1800000);
  CHECK(memory.read32(0x3140) == 0x1e0a10);
  CHECK(memory.read32(0x3144) == 0x40308);
  CHECK(memory.read32(0x3130) == 0x935e0000);
  CHECK(memory.read32(0x3134) == 0x93600000);
  CHECK(memory.read32(0x315c) == 0xdeadbeef);
  memory.write32(0x3144, 0x1234);
  memory.write32(0, 0xcafebabe);
  CHECK(ios::initialize_ios_memory(memory, 28, true));
  CHECK(memory.read32(0) == 0xcafebabe);
  CHECK(memory.read32(0x3144) == 0x1234);
  CHECK(memory.read32(0x3130) == 0x937e0000);
  CHECK(memory.read32(0x314c) == 0x93820000);
  CHECK(ios::initialize_ios_memory(memory, 9));
  CHECK(memory.read32(0x3140) == 0x9040a);
  CHECK(memory.read32(0x3130) == 0x933e0000);
  CHECK(memory.read32(0x3148) == 0xdeadbeef);
  CHECK(ios::initialize_ios_memory(memory, 257));
  CHECK(memory.read32(0x3158) == 0xcafebabe);
  CHECK(memory.read32(0x3160) == 0xdeadbeef);
  CHECK(!ios::initialize_ios_memory(memory, 999));
}

void wii_register_mirrors() {
  Fixture fixture;
  fixture.memory.mmio_handles = [&](u32 address) { return fixture.devices.handles(address); };
  fixture.memory.mmio_read = [&](u32 address, unsigned size) {
    return fixture.devices.read(address, size);
  };
  fixture.memory.mmio_write = [&](u32 address, u32 value, unsigned size) {
    fixture.devices.write(address, value, size);
  };
  cpu::Interpreter interpreter(fixture.memory, Console::Wii);
  interpreter.configure_boot();
  interpreter.state().pc = 0x80001000;
  interpreter.state().gpr[4] = 0xcd800000;
  interpreter.state().gpr[5] = 0xaabbccdd;
  fixture.memory.write32(0x1000, 0x80a40180);
  interpreter.step();
  CHECK(interpreter.state().pc == 0x80001004);
  CHECK(interpreter.state().gpr[5] == 0);
  CHECK(fixture.devices.handles(0x0d800180));
  CHECK(fixture.devices.read(0x0d800180, 4) == 0);
  fixture.devices.write(0x0d8000c0, 0x20, 4);
  CHECK(fixture.devices.read(0x0d0000c0, 4) == 0x20);
  fixture.devices.write(0x0d806808, 0x1234, 4);
  CHECK(fixture.devices.read(0x0d006808, 4) == 0x1234);
  fixture.devices.write(0x0d806408, 0x5678, 4);
  CHECK(fixture.devices.read(0x0d006408, 4) == 0x5678);
  fixture.devices.write(0x0d80600c, 0x9abc, 4);
  CHECK(fixture.devices.read(0x0d00600c, 4) == 0x9abc);
  fixture.devices.write(0x0d806c04, 0xdef0, 4);
  CHECK(fixture.devices.read(0x0d006c04, 4) == 0xdef0);
  CHECK_THROWS(fixture.devices.read(0x0d802000, 4));
  CHECK_THROWS(fixture.devices.read(0x0d80302c, 4));
  CHECK_THROWS(fixture.devices.read(0x0d004000, 4));
  CHECK_THROWS(fixture.devices.read(0x0d80500a, 2));
  hw::Devices gamecube(Console::GameCube);
  CHECK(!gamecube.handles(0x0d800180));
}

void handshake() {
  Fixture fixture;
  fixture.prepare(2, 99);
  fixture.devices.write(ipc_base, request, 4);
  fixture.devices.write(ipc_base + 4, 0x31, 4);
  fixture.ipc.advance(5999);
  CHECK((fixture.devices.read(ipc_base + 4, 4) & 3) == 1);
  CHECK(fixture.memory.read32(request) == 2);
  fixture.ipc.advance(1);
  CHECK((fixture.devices.read(ipc_base + 4, 4) & 3) == 2);
  CHECK(fixture.memory.read32(request) == 8);
  CHECK(fixture.memory.read32(request + 4) == 0xfffffffc);
  CHECK(fixture.memory.read32(request + 8) == 2);
  CHECK(!fixture.devices.external_interrupt_pending());
  fixture.ipc.advance(1199);
  CHECK(!fixture.devices.external_interrupt_pending());
  fixture.ipc.advance(1);
  CHECK(fixture.devices.external_interrupt_pending());
  fixture.clear_ack();
  CHECK(!fixture.devices.external_interrupt_pending());
  fixture.ipc.advance(5400);
  CHECK(fixture.devices.read(ipc_base + 8, 4) == request);
  CHECK(fixture.devices.read(ipc_base + 4, 4) & 4);
  CHECK(!fixture.devices.external_interrupt_pending());
  fixture.ipc.advance(1200);
  CHECK(fixture.devices.external_interrupt_pending());
  fixture.clear_reply();
  CHECK(!fixture.devices.external_interrupt_pending());
  fixture.devices.write(ipc_base + 0x34, 0x40000000, 4);
  CHECK(fixture.devices.read(ipc_base + 4, 4) == 0);
  CHECK_THROWS(fixture.devices.read(ipc_base + 0x30, 4));
  CHECK_THROWS(fixture.devices.read(ipc_base + 0x34, 4));
  CHECK(fixture.devices.read(ipc_base + 0x64, 4) == 0xffffffff);
}

void system_management() {
  Fixture fixture;
  const u32 event_fd = u32(fixture.open("/dev/stm/eventhook"));
  const u32 immediate_fd = u32(fixture.open("/dev/stm/immediate"));
  constexpr u32 hook_request = 0x15000;
  const auto install = [&] {
    fixture.prepare(6, event_fd);
    fixture.memory.write32(request + 12, 0x1000);
    fixture.memory.write32(request + 24, output_buffer);
    fixture.memory.write32(request + 28, 4);
    fixture.memory.write32(output_buffer, 0xaabbccdd);
    const auto bytes = fixture.memory.range(request, 64);
    fixture.memory.copy_in(hook_request, bytes);
    fixture.submit(hook_request);
    fixture.clear_ack();
    fixture.ipc.advance(1000000);
    CHECK(fixture.memory.read32(hook_request) == 6);
    CHECK(fixture.memory.read32(output_buffer) == 0xaabbccdd);
    CHECK((fixture.devices.read(ipc_base + 4, 4) & 4) == 0);
  };
  const auto ioctl = [&](u32 operation) {
    fixture.prepare(6, immediate_fd);
    fixture.memory.write32(request + 12, operation);
    return fixture.complete();
  };
  const auto receive_event = [&](u32 event) {
    fixture.ipc.advance(48000);
    CHECK(fixture.memory.read32(hook_request) == 8);
    CHECK(fixture.memory.read32(hook_request + 4) == 0);
    CHECK(fixture.memory.read32(output_buffer) == event);
    CHECK(fixture.devices.read(ipc_base + 8, 4) == hook_request);
    fixture.clear_reply();
  };
  CHECK(ioctl(0x3002) == -6);
  install();
  fixture.prepare(6, event_fd);
  fixture.memory.write32(request + 12, 0x1000);
  fixture.memory.write32(request + 24, output_buffer);
  fixture.memory.write32(request + 28, 4);
  CHECK(fixture.complete() == -2);
  fixture.ipc.press_reset_button();
  receive_event(0x20000);
  install();
  fixture.ipc.press_power_button();
  receive_event(0x800);
  fixture.ipc.press_reset_button();
  fixture.ipc.advance(1000000);
  CHECK((fixture.devices.read(ipc_base + 4, 4) & 4) == 0);
  install();
  fixture.prepare(6, immediate_fd);
  fixture.memory.write32(request + 12, 0x3002);
  fixture.submit();
  fixture.clear_ack();
  receive_event(0);
  fixture.ipc.advance(48000);
  CHECK(fixture.devices.read(ipc_base + 8, 4) == request);
  CHECK(fixture.memory.read32(request + 4) == 0);
  fixture.clear_reply();
  bool stopped = false;
  fixture.ipc.set_shutdown_handler([&] { stopped = true; });
  CHECK(ioctl(0x2003) == 0);
  CHECK(stopped);
  CHECK(ioctl(0x2001) == 0);
  CHECK(ioctl(0x5001) == 0);
  CHECK(ioctl(0x6002) == 0);
  CHECK(ioctl(0xbeef) == -9);
  fixture.prepare(6, immediate_fd);
  fixture.memory.write32(request + 12, 0x4001);
  CHECK_THROWS(fixture.submit());
}

void network_scheduler_queries() {
  Fixture fixture;
  const u32 fd = u32(fixture.open("/dev/net/kd/request", 1));
  const auto ioctl = [&](u32 operation, u32 output, u32 size) {
    fixture.prepare(6, fd);
    fixture.memory.write32(request + 12, operation);
    fixture.memory.write32(request + 24, output);
    fixture.memory.write32(request + 28, size);
    return fixture.complete();
  };
  CHECK(ioctl(4, output_buffer, 12) == 0);
  CHECK(fixture.memory.read32(output_buffer) == 0);
  CHECK(fixture.memory.read32(output_buffer + 4) == 1);
  CHECK(fixture.memory.read32(output_buffer + 8) == 2);
  CHECK(ioctl(4, output_buffer + 1, 12) == -4);
  CHECK(ioctl(4, output_buffer, 11) == -4);
  CHECK(ioctl(0x1e, output_buffer, 1024) == 0);
  CHECK(fixture.memory.read32(output_buffer) == 0);
  CHECK(fixture.memory.read32(output_buffer + 4) == 0xffffffff);
  CHECK(fixture.memory.read32(output_buffer + 8) == 0);
  CHECK(ioctl(0x1e, output_buffer, 15) == -4);
  const u32 time_fd = u32(fixture.open("/dev/net/kd/time", 0));
  fixture.devices.set_rtc_seconds(1234);
  fixture.prepare(6, time_fd);
  fixture.memory.write32(request + 12, 0x14);
  fixture.memory.write32(request + 24, output_buffer);
  fixture.memory.write32(request + 28, 12);
  CHECK(fixture.complete() == 0);
  CHECK(fixture.memory.read64(output_buffer + 4) == 946684800 + 1234);
  fixture.devices.advance(729000000);
  fixture.prepare(6, time_fd);
  fixture.memory.write32(request + 12, 0x14);
  fixture.memory.write32(request + 24, output_buffer);
  fixture.memory.write32(request + 28, 12);
  CHECK(fixture.complete() == 0);
  CHECK(fixture.memory.read64(output_buffer + 4) == 946684800 + 1235);
  fixture.prepare(6, fd);
  fixture.memory.write32(request + 12, 0x80);
  CHECK_THROWS(fixture.submit());
}

void network_storage_permissions() {
  Fixture fixture;
  const auto task_temp = std::filesystem::canonical(std::filesystem::temp_directory_path());
  const auto task_root =
      task_temp / ("cupid-ios-test-network-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture.ipc.set_user_directory(task_root);
  const auto config = task_root / "wii/shared2/wc24/nwc24msg.cfg";
  std::filesystem::create_directories(config.parent_path());
  const std::string original = "Existing configuration must survive a failed read.";
  {
    std::ofstream stream(config, std::ios::binary);
    stream.write(original.data(), std::streamsize(original.size()));
  }
  const u32 fs = u32(fixture.open("/dev/fs"));
  fixture.prepare(6, fs);
  fixture.memory.write32(request + 12, 5);
  fixture.memory.write32(request + 16, data_buffer);
  fixture.memory.write32(request + 20, 74);
  fixture.text(data_buffer + 6, "/shared2/wc24/nwc24msg.cfg");
  fixture.memory.write8(data_buffer + 70, 3);
  fixture.memory.write8(data_buffer + 71, 3);
  // KD uses UID/GID 12, so an existing write-only node must not count as missing.
  fixture.memory.write8(data_buffer + 72, 2);
  CHECK(fixture.complete() == 0);
  const u32 kd = u32(fixture.open("/dev/net/kd/request"));
  fixture.prepare(6, kd);
  fixture.memory.write32(request + 12, 0x0f);
  fixture.memory.write32(request + 24, output_buffer);
  fixture.memory.write32(request + 28, 16);
  CHECK(fixture.complete() == 0);
  CHECK(s32(fixture.memory.read32(output_buffer)) == -16);
  {
    std::ifstream stream(config, std::ios::binary);
    std::string retained(original.size(), '\0');
    stream.read(retained.data(), std::streamsize(retained.size()));
    CHECK(retained == original);
    CHECK(std::filesystem::file_size(config) == original.size());
  }
  CHECK(!std::filesystem::exists(config.parent_path() / "nwc24msg.cbk"));
  const auto resolved = std::filesystem::canonical(task_root);
  CHECK(resolved.parent_path() == task_temp);
  CHECK(resolved.filename().string().starts_with("cupid-ios-test-network-"));
  std::filesystem::remove_all(resolved);
}

void system_configuration_storage() {
  const auto task_temp = std::filesystem::canonical(std::filesystem::temp_directory_path());
  const auto task_root =
      task_temp / ("cupid-ios-sysconf-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto target = task_root / "wii/shared2/sys/SYSCONF";
  const auto backup = task_root / "wii-sysconf-backup.bin";
  auto load = [](const std::filesystem::path &path) {
    std::vector<u8> bytes(std::size_t(std::filesystem::file_size(path)));
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()));
    CHECK(bool(input));
    return bytes;
  };
  auto write = [](const std::filesystem::path &path, std::span<const u8> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    CHECK(bool(output));
  };
  auto entry = [](std::span<const u8> bytes, const std::string &name) {
    auto read16 = [&](std::size_t at) { return unsigned((u16(bytes[at]) << 8) | bytes[at + 1]); };
    for (unsigned i = 0; i < read16(4); ++i) {
      const std::size_t at = read16(6 + i * 2);
      const std::size_t name_size = (bytes[at] & 31) + 1;
      if (std::string(reinterpret_cast<const char *>(bytes.data() + at + 1), name_size) == name) {
        const unsigned type = bytes[at] >> 5;
        return at + 1 + name_size + (type == 1 ? 2u : type == 2 ? 1u : 0u);
      }
    }
    throw std::runtime_error("Missing system configuration fixture entry");
  };
  Fixture fixture;
  fixture.ipc.set_user_directory(task_root);
  const auto initialized = load(target);
  CHECK(initialized.size() == 16384);
  CHECK(initialized[entry(initialized, "BT.DINF")] == 5);
  CHECK(!std::filesystem::exists(backup));
  const u32 fs = u32(fixture.open("/dev/fs"));
  for (const std::string &path :
       {std::string("/shared2/sys"), std::string("/shared2/sys/SYSCONF")}) {
    fixture.prepare(6, fs);
    fixture.memory.write32(request + 12, 6);
    fixture.memory.write32(request + 16, data_buffer);
    fixture.memory.write32(request + 20, 64);
    fixture.memory.write32(request + 24, output_buffer);
    fixture.memory.write32(request + 28, 74);
    fixture.text(data_buffer, path);
    CHECK(fixture.complete() == 0);
    CHECK(fixture.memory.read32(output_buffer) == 0x1000);
    CHECK(fixture.memory.read16(output_buffer + 4) == 1);
    for (unsigned mode = 0; mode < 3; ++mode)
      CHECK(fixture.memory.read8(output_buffer + 70 + mode) == 3);
  }
  const u32 fd = u32(fixture.open("/shared2/sys/SYSCONF", 1));
  fixture.prepare(3, fd);
  fixture.memory.write32(request + 12, 0x20000);
  fixture.memory.write32(request + 16, 16384);
  CHECK(fixture.complete() == 16384);
  const auto guest_bytes = fixture.memory.range(0x20000, 16384);
  CHECK(std::equal(initialized.begin(), initialized.end(), guest_bytes.begin()));
  fixture.prepare(2, fd);
  CHECK(fixture.complete() == 0);
  fixture.ipc.set_user_directory(task_root);
  CHECK(load(target) == initialized && !std::filesystem::exists(backup));

  auto original = initialized;
  const auto peers = entry(original, "BT.DINF"), language = entry(original, "IPL.LNG");
  std::fill_n(original.begin() + peers, 1121, u8(0));
  original[language] = 4;
  write(target, original);
  fixture.ipc.set_user_directory(task_root);
  CHECK(load(backup) == original);
  const auto replacement = load(target);
  CHECK(replacement == ios::update_bluetooth_peers(original));
  CHECK(replacement[entry(replacement, "IPL.LNG")] == 4);
  CHECK(replacement[entry(replacement, "BT.DINF")] == 5);
  original[language] = 3;
  write(target, original);
  fixture.ipc.set_user_directory(task_root);
  CHECK(load(backup) != original && load(backup)[language] == 4);
  CHECK(load(target)[entry(load(target), "IPL.LNG")] == 3);

  const std::vector<u8> malformed{1, 2, 3};
  write(target, malformed);
  CHECK_THROWS(fixture.ipc.set_user_directory(task_root));
  CHECK(load(target) == malformed && load(backup)[language] == 4);
  write(target, {});
  CHECK_THROWS(fixture.ipc.set_user_directory(task_root));
  CHECK(std::filesystem::file_size(target) == 0 && load(backup)[language] == 4);
  write(target, replacement);
  auto invalid_config = replacement;
  invalid_config[0] = 'X';
  write(target, invalid_config);
  CHECK_THROWS(fixture.ipc.set_user_directory(task_root));
  CHECK(load(target) == invalid_config);
  write(target, original);
  const auto pending = std::filesystem::path(target.string() + ".pending");
  std::filesystem::create_directory(pending);
  CHECK_THROWS(fixture.ipc.set_user_directory(task_root));
  CHECK(load(target) == original && std::filesystem::is_directory(pending));
  std::filesystem::remove(pending);

  // Some Windows hosts disallow creating symbolic links; test the path when available.
  const auto external = task_root / "external.bin";
  write(external, initialized);
  std::filesystem::remove(target);
  std::error_code link_error;
  std::filesystem::create_symlink(external, target, link_error);
  if (!link_error) {
    CHECK_THROWS(fixture.ipc.set_user_directory(task_root));
    CHECK(load(external) == initialized && std::filesystem::is_symlink(target));
    std::filesystem::remove(target);
  }
  write(target, original);
  std::filesystem::create_symlink(external, pending, link_error);
  if (!link_error) {
    CHECK_THROWS(fixture.ipc.set_user_directory(task_root));
    CHECK(load(external) == initialized && load(target) == original);
  }
  const auto resolved = std::filesystem::canonical(task_root);
  CHECK(resolved.parent_path() == task_temp);
  CHECK(resolved.filename().string().starts_with("cupid-ios-sysconf-"));
  std::filesystem::remove_all(resolved);
}

void bluetooth_controller() {
  Fixture fixture;
  const u32 fd = u32(fixture.open("/dev/usb/oh1/57e/305"));
  constexpr u32 command_buffer = 0x17000;
  const auto control = [&](std::initializer_list<u8> bytes) {
    fixture.memory.copy_in(command_buffer, std::span<const u8>(bytes.begin(), bytes.size()));
    fixture.memory.write8(0x15000, 0x20);
    fixture.memory.write8(0x15001, 0);
    fixture.memory.write16(0x15002, 0);
    fixture.memory.write16(0x15004, 0);
    fixture.memory.write16(0x15006, u16(bytes.size() << 8));
    fixture.memory.write8(0x15008, 0);
    return fixture.ioctlv(
        fd, 0, {{0x15000, 1}, {0x15001, 1}, {0x15002, 2}, {0x15004, 2}, {0x15006, 2}, {0x15008, 1}},
        {{command_buffer, u32(bytes.size())}});
  };
  const auto read_event = [&](u16 capacity = 64) {
    fixture.memory.write8(0x15000, 0x81);
    fixture.memory.write16(0x15002, capacity);
    return fixture.ioctlv(fd, 2, {{0x15000, 1}, {0x15002, 2}}, {{output_buffer, capacity}});
  };
  CHECK(control({1, 0x10, 0}) == 3);
  CHECK(read_event() == 14);
  const auto version = fixture.memory.range(output_buffer, 14);
  CHECK(std::equal(
      version.begin(), version.end(),
      std::array<u8, 14>{0x0e, 12, 1, 1, 0x10, 0, 3, 0xa7, 0x40, 3, 0x0f, 0, 0x0e, 0x43}.begin()));
  CHECK(control({5, 0x10, 0}) == 3);
  CHECK(read_event() == 13);
  CHECK(fixture.memory.read8(output_buffer + 6) == 0x53);
  CHECK(fixture.memory.read8(output_buffer + 9) == 10);
  CHECK(control({5, 0x0c, 2, 1, 0}) == 5);
  CHECK(read_event() == 6);
  const std::array<u8, 6> filter_complete{0x0e, 4, 1, 5, 0x0c, 0};
  CHECK(std::equal(filter_complete.begin(), filter_complete.end(),
                   fixture.memory.range(output_buffer, 6).begin()));
  CHECK(control({5, 0x0c, 1, 0}) == 4);
  CHECK(read_event() == 6);
  CHECK(std::equal(filter_complete.begin(), filter_complete.end(),
                   fixture.memory.range(output_buffer, 6).begin()));
  CHECK(control({0x1a, 0x0c, 1, 3}) == 4);
  CHECK(read_event() == 6);
  CHECK(control({0x19, 0x0c, 0}) == 3);
  CHECK(read_event() == 7);
  CHECK(fixture.memory.read8(output_buffer + 6) == 3);
  CHECK(control({3, 0x0c, 0}) == 3);
  CHECK(read_event() == 6);
  CHECK(control({0x19, 0x0c, 0}) == 3);
  CHECK(read_event() == 7);
  CHECK(fixture.memory.read8(output_buffer + 6) == 3);
  // An absent peer cannot complete acceptance or report a successful role change.
  CHECK(control({9, 4, 7, 0x11, 2, 0x19, 0x79, 0, 0, 0}) == 10);
  CHECK(read_event() == 6);
  CHECK(fixture.memory.read8(output_buffer) == 0x0f);
  CHECK(read_event() == 13);
  CHECK(fixture.memory.read8(output_buffer) == 3);
  CHECK(fixture.memory.read8(output_buffer + 2) == 8);
  CHECK(control({0x0d, 0x0c, 7, 0, 0, 0, 0, 0, 0, 1}) == 10);
  CHECK(read_event(128) == 113);
  CHECK(fixture.memory.read8(output_buffer) == 0x15);
  CHECK(fixture.memory.read8(output_buffer + 1) == 111);
  CHECK(fixture.memory.read8(output_buffer + 2) == 5);
  const auto key_span = fixture.memory.range(output_buffer, 113);
  const std::vector<u8> registered_keys(key_span.begin(), key_span.end());
  for (unsigned peer = 0; peer < 5; ++peer) {
    const u32 base = output_buffer + 3 + peer * 22;
    const std::array<u8, 6> expected{0x11, 2, 0x19, 0x79, 0, u8(peer)};
    CHECK(std::equal(expected.begin(), expected.end(), fixture.memory.range(base, 6).begin()));
    for (unsigned byte = 0; byte < 16; ++byte)
      CHECK(fixture.memory.read8(base + 6 + byte) == 0xa0 + peer);
  }
  CHECK(read_event() == 10);
  const std::array<u8, 10> key_complete{0x0e, 8, 1, 0x0d, 0x0c, 0, 255, 0, 5, 0};
  CHECK(std::equal(key_complete.begin(), key_complete.end(),
                   fixture.memory.range(output_buffer, 10).begin()));
  CHECK(control({0x0b, 4, 22, 0x11, 2, 0x19, 0x79, 0,  0,  1,  2,  3, 4,
                 5,    6, 7,  8,    9, 10,   11,   12, 13, 14, 15, 16}) == 25);
  CHECK(read_event() == 12);
  const std::array<u8, 12> key_reply{0x0e, 10, 1, 0x0b, 4, 0, 0x11, 2, 0x19, 0x79, 0, 0};
  CHECK(std::equal(key_reply.begin(), key_reply.end(),
                   fixture.memory.range(output_buffer, 12).begin()));
  CHECK(control({0x0c, 4, 6, 0x11, 2, 0x19, 0x79, 0, 0}) == 9);
  CHECK(read_event() == 12);
  CHECK(fixture.memory.read8(output_buffer + 3) == 0x0c);
  CHECK(std::equal(key_reply.begin() + 5, key_reply.end(),
                   fixture.memory.range(output_buffer + 5, 7).begin()));
  CHECK(control({0x12, 0x0c, 7, 0x11, 2, 0x19, 0x79, 0, 0, 0}) == 10);
  CHECK(read_event() == 8);
  const std::array<u8, 8> delete_complete{0x0e, 6, 1, 0x12, 0x0c, 0, 0, 0};
  CHECK(std::equal(delete_complete.begin(), delete_complete.end(),
                   fixture.memory.range(output_buffer, 8).begin()));
  CHECK(control({0x0d, 0x0c, 7, 0, 0, 0, 0, 0, 0, 1}) == 10);
  CHECK(read_event(128) == 113);
  CHECK(std::equal(registered_keys.begin(), registered_keys.end(),
                   fixture.memory.range(output_buffer, 113).begin()));
  CHECK(read_event() == 10);
  CHECK(control({0x4f, 0xfc, 1, 0}) == 4);
  CHECK(read_event() == 6);
  const std::array<u8, 6> vendor_complete{0x0e, 4, 1, 0x4f, 0xfc, 0};
  CHECK(std::equal(vendor_complete.begin(), vendor_complete.end(),
                   fixture.memory.range(output_buffer, 6).begin()));

  fixture.prepare(7, fd);
  fixture.memory.write32(request + 12, 2);
  fixture.memory.write32(request + 16, 2);
  fixture.memory.write32(request + 20, 1);
  fixture.memory.write32(request + 24, 0x14000);
  fixture.memory.write8(0x15000, 0x81);
  fixture.memory.write16(0x15002, 64);
  for (unsigned i = 0; i < 3; ++i) {
    fixture.memory.write32(0x14000 + i * 8, i == 0 ? 0x15000 : i == 1 ? 0x15002 : output_buffer);
    fixture.memory.write32(0x14004 + i * 8, i == 0 ? 1 : i == 1 ? 2 : 64);
  }
  constexpr u32 pending = 0x16000;
  fixture.memory.copy_in(pending, fixture.memory.range(request, 64));
  fixture.submit(pending);
  fixture.clear_ack();
  fixture.ipc.advance(1000000);
  CHECK(fixture.memory.read32(pending) == 7);
  CHECK((fixture.devices.read(ipc_base + 4, 4) & 4) == 0);
  fixture.prepare(2, fd);
  CHECK(fixture.complete() == 0);
  hw::RemoteState state;
  fixture.ipc.set_remote(0, state);
  CHECK_THROWS(fixture.ipc.set_remote(4, state));
}

void bluetooth_reconnect_cooldown() {
  Fixture fixture;
  const u32 fd = u32(fixture.open("/dev/usb/oh1/57e/305"));
  constexpr u32 transfer = 0x17000;
  const auto control = [&](std::initializer_list<u8> bytes) {
    fixture.memory.copy_in(transfer, std::span<const u8>(bytes.begin(), bytes.size()));
    fixture.memory.write8(0x15000, 0x20);
    fixture.memory.write8(0x15001, 0);
    fixture.memory.write16(0x15002, 0);
    fixture.memory.write16(0x15004, 0);
    fixture.memory.write16(0x15006, u16(bytes.size() << 8));
    fixture.memory.write8(0x15008, 0);
    return fixture.ioctlv(
        fd, 0, {{0x15000, 1}, {0x15001, 1}, {0x15002, 2}, {0x15004, 2}, {0x15006, 2}, {0x15008, 1}},
        {{transfer, u32(bytes.size())}}, 100000);
  };
  const auto input = [&] {
    fixture.memory.write8(0x15000, 0x81);
    fixture.memory.write16(0x15002, 64);
    const s32 length =
        fixture.ioctlv(fd, 2, {{0x15000, 1}, {0x15002, 2}}, {{output_buffer, 64}}, 100000);
    CHECK(length > 0);
    const auto bytes = fixture.memory.range(output_buffer, u32(length));
    return std::vector<u8>(bytes.begin(), bytes.end());
  };
  const auto pending_input = [&] {
    fixture.prepare(7, fd);
    fixture.memory.write32(request + 12, 2);
    fixture.memory.write32(request + 16, 2);
    fixture.memory.write32(request + 20, 1);
    fixture.memory.write32(request + 24, 0x14000);
    fixture.memory.write8(0x15000, 0x81);
    fixture.memory.write16(0x15002, 64);
    for (unsigned i = 0; i < 3; ++i) {
      fixture.memory.write32(0x14000 + i * 8, i == 0 ? 0x15000 : i == 1 ? 0x15002 : output_buffer);
      fixture.memory.write32(0x14004 + i * 8, i == 0 ? 1 : i == 1 ? 2 : 64);
    }
    fixture.submit();
    fixture.clear_ack();
  };
  const u64 period = fixture.devices.ticks_per_second() / 200;
  const auto ticks = [&](unsigned count) {
    // The device's first update is scheduled when its open request dispatches.
    const u64 phase = (fixture.ipc.cycles() - 6000) % period;
    fixture.ipc.advance(period - phase + u64(count - 1) * period);
  };
  const auto completed_request = [&] {
    fixture.ipc.advance(100000);
    CHECK(fixture.memory.read32(request) == 8);
    CHECK(fixture.memory.read32(request + 4) == 12);
    const std::array<u8, 12> expected{4, 10, 0x11, 2, 0x19, 0x79, 0, 0, 0, 4, 0x48, 1};
    CHECK(std::equal(expected.begin(), expected.end(),
                     fixture.memory.range(output_buffer, 12).begin()));
    fixture.clear_reply();
  };
  const auto accept = [&] {
    CHECK(control({9, 4, 7, 0x11, 2, 0x19, 0x79, 0, 0, 1}) == 10);
    CHECK(input() == std::vector<u8>({0x0f, 4, 0, 1, 9, 4}));
    CHECK(input() == std::vector<u8>({3, 11, 0, 0, 1, 0x11, 2, 0x19, 0x79, 0, 0, 1, 0}));
  };
  const auto disconnect = [&] {
    CHECK(control({6, 4, 3, 0, 1, 0x13}) == 6);
    CHECK(input() == std::vector<u8>({0x0f, 4, 0, 1, 6, 4}));
    CHECK(input() == std::vector<u8>({5, 4, 0, 0, 1, 0x13}));
  };
  hw::RemoteState state;
  state.connected = true;
  fixture.ipc.set_remote(0, state);
  CHECK(control({0x1a, 0x0c, 1, 2}) == 4);
  CHECK(input() == std::vector<u8>({0x0e, 4, 1, 0x1a, 0x0c, 0}));
  ticks(1);
  CHECK(input() == std::vector<u8>({4, 10, 0x11, 2, 0x19, 0x79, 0, 0, 0, 4, 0x48, 1}));
  accept();
  disconnect();
  state.buttons = hw::RemoteA;
  fixture.ipc.set_remote(0, state);
  pending_input();
  ticks(199);
  CHECK(fixture.memory.read32(request) == 7);
  ticks(1);
  completed_request();
  accept();
  disconnect();
  state.buttons = 0;
  fixture.ipc.set_remote(0, state);
  pending_input();
  ticks(250);
  CHECK(fixture.memory.read32(request) == 7);
  state.buttons = hw::RemoteA;
  fixture.ipc.set_remote(0, state);
  ticks(1);
  completed_request();
  // A named guest disconnect completes even when the known peer is already inactive.
  disconnect();
}

void bluetooth_vendor_transport() {
  Fixture fixture;
  const u32 fd = u32(fixture.open("/dev/usb/oh1/57e/305"));
  // Authored bytes match the observed packet geometry without including game data.
  std::array<u8, 187> upload{0x4c, 0xfc, 184};
  for (unsigned i = 3; i < upload.size(); ++i)
    upload[i] = u8((i * 13) ^ 0x47);
  fixture.memory.copy_in(0x17000, upload);
  fixture.memory.write8(0x15000, 0x20);
  fixture.memory.write8(0x15001, 0);
  fixture.memory.write16(0x15002, 0);
  fixture.memory.write16(0x15004, 0);
  fixture.memory.write16(0x15006, u16(upload.size() << 8));
  fixture.memory.write8(0x15008, 0);
  const auto control = [&] {
    return fixture.ioctlv(
        fd, 0, {{0x15000, 1}, {0x15001, 1}, {0x15002, 2}, {0x15004, 2}, {0x15006, 2}, {0x15008, 1}},
        {{0x17000, u32(upload.size())}});
  };
  CHECK(control() == s32(upload.size()));
  CHECK(std::equal(upload.begin(), upload.end(),
                   fixture.memory.range(0x17000, upload.size()).begin()));
  fixture.memory.write8(0x15000, 0x81);
  fixture.memory.write16(0x15002, 64);
  CHECK(fixture.ioctlv(fd, 2, {{0x15000, 1}, {0x15002, 2}}, {{output_buffer, 64}}) == 6);
  const std::array<u8, 6> complete{0x0e, 4, 1, 0x4c, 0xfc, 0};
  CHECK(
      std::equal(complete.begin(), complete.end(), fixture.memory.range(output_buffer, 6).begin()));
  fixture.memory.write8(0x17000, 0x50);
  fixture.memory.write8(0x15000, 0x20);
  fixture.memory.write16(0x15002, 0);
  try {
    control();
    CHECK(false);
  } catch (const BusFault &fault) {
    CHECK(fault.unsupported_reason);
    CHECK(std::string(fault.what()).find("64592") != std::string::npos);
  }
}

void bluetooth_remote_transport() {
  Fixture fixture;
  const u32 fd = u32(fixture.open("/dev/usb/oh1/57e/305"));
  hw::RemoteState state;
  state.connected = true;
  state.buttons = hw::RemoteA | hw::RemotePlus;
  fixture.ipc.set_remote(0, state);
  bool rumbling = false;
  fixture.ipc.set_remote_rumble_handler([&](unsigned index, bool active) {
    CHECK(index == 0);
    rumbling = active;
  });
  constexpr u32 transfer = 0x17000;
  const auto control = [&](std::initializer_list<u8> bytes) {
    fixture.memory.copy_in(transfer, std::span<const u8>(bytes.begin(), bytes.size()));
    fixture.memory.write8(0x15000, 0x20);
    fixture.memory.write8(0x15001, 0);
    fixture.memory.write16(0x15002, 0);
    fixture.memory.write16(0x15004, 0);
    fixture.memory.write16(0x15006, u16(bytes.size() << 8));
    fixture.memory.write8(0x15008, 0);
    return fixture.ioctlv(
        fd, 0, {{0x15000, 1}, {0x15001, 1}, {0x15002, 2}, {0x15004, 2}, {0x15006, 2}, {0x15008, 1}},
        {{transfer, u32(bytes.size())}}, 100000);
  };
  const auto input = [&](u8 endpoint) {
    fixture.memory.write8(0x15000, endpoint);
    fixture.memory.write16(0x15002, 512);
    const s32 length = fixture.ioctlv(fd, endpoint == 0x81 ? 2 : 1, {{0x15000, 1}, {0x15002, 2}},
                                      {{output_buffer, 512}}, 100000);
    CHECK(length > 0);
    const auto bytes = fixture.memory.range(output_buffer, u32(length));
    return std::vector<u8>(bytes.begin(), bytes.end());
  };
  const auto output = [&](std::initializer_list<u8> bytes) {
    fixture.memory.copy_in(transfer, std::span<const u8>(bytes.begin(), bytes.size()));
    fixture.memory.write8(0x15000, 2);
    fixture.memory.write16(0x15002, u16(bytes.size()));
    return fixture.ioctlv(fd, 1, {{0x15000, 1}, {0x15002, 2}}, {{transfer, u32(bytes.size())}},
                          100000);
  };
  CHECK(control({1, 4, 5, 0x33, 0x8b, 0x9e, 4, 0}) == 8);
  CHECK(input(0x81) == std::vector<u8>({0x0f, 4, 0, 1, 1, 4}));
  CHECK(input(0x81) ==
        std::vector<u8>({2, 15, 1, 0x11, 2, 0x19, 0x79, 0, 0, 1, 0, 0, 0, 4, 0x48, 0x18, 0x38}));
  CHECK(input(0x81) == std::vector<u8>({1, 2, 0, 1}));
  CHECK(control({2, 4, 0}) == 3);
  CHECK(input(0x81) == std::vector<u8>({0x0e, 4, 1, 2, 4, 0}));
  CHECK(control({5, 4, 13, 0x11, 2, 0x19, 0x79, 0, 0, 0x18, 0xcc, 0, 0, 0, 0, 0}) == 16);
  CHECK(input(0x81) == std::vector<u8>({0x0f, 4, 0, 1, 5, 4}));
  CHECK(input(0x81) == std::vector<u8>({3, 11, 0, 0, 1, 0x11, 2, 0x19, 0x79, 0, 0, 1, 0}));
  CHECK(control({0x0f, 4, 4, 0, 1, 0x18, 0xcc}) == 7);
  CHECK(input(0x81) == std::vector<u8>({0x0f, 4, 0, 1, 0x0f, 4}));
  CHECK(input(0x81) == std::vector<u8>({0x1d, 5, 0, 0, 1, 0x18, 0xcc}));
  CHECK(control({0x0d, 8, 4, 0, 1, 5, 0}) == 7);
  CHECK(input(0x81) == std::vector<u8>({0x0f, 4, 0, 1, 0x0d, 8}));
  CHECK(control({3, 8, 10, 0, 1, 0x60, 9, 0x80, 4, 4, 0, 1, 0}) == 13);
  CHECK(input(0x81) == std::vector<u8>({0x0f, 4, 0, 1, 3, 8}));
  CHECK(input(0x81) == std::vector<u8>({0x14, 6, 0, 0, 1, 2, 0x60, 9}));
  CHECK(control({0x37, 0x0c, 4, 0, 1, 0x40, 0x1f}) == 7);
  CHECK(input(0x81) == std::vector<u8>({0x0e, 6, 1, 0x37, 0x0c, 0, 0, 1}));
  CHECK(control({1, 4, 5, 0x33, 0x8b, 0x9e, 4, 0}) == 8);
  CHECK(input(0x81) == std::vector<u8>({0x0f, 4, 0, 1, 1, 4}));
  CHECK(input(0x81) == std::vector<u8>({1, 2, 0, 0}));
  // Start the serial endpoint fixture at a known controller tick. Completion
  // events must be drained before bulk input, as they are by concurrent guest polls.
  const u64 controller_period = fixture.devices.ticks_per_second() / 200;
  fixture.ipc.advance(controller_period - fixture.ipc.cycles() % controller_period);
  // Guest opens a HID interrupt channel with its own CID0x61; peer allocates0x40.
  CHECK(output({0, 0x21, 12, 0, 8, 0, 1, 0, 2, 1, 4, 0, 0x13, 0, 0x61, 0}) == 0);
  auto packet = input(0x82);
  CHECK(packet ==
        std::vector<u8>({0, 0x21, 16, 0, 12, 0, 1, 0, 3, 1, 8, 0, 0x40, 0, 0x61, 0, 0, 0, 0, 0}));
  CHECK(output({0, 0x21, 12, 0, 8, 0, 1, 0, 2, 5, 4, 0, 0x13, 0, 0x62, 0}) == 0);
  CHECK(input(0x82) ==
        std::vector<u8>({0, 0x21, 16, 0, 12, 0, 1, 0, 3, 5, 8, 0, 0, 0, 0x62, 0, 3, 0, 0, 0}));
  CHECK(output({0, 0x21, 16, 0, 12, 0, 1, 0, 4, 2, 8, 0, 0x40, 0, 0, 0, 1, 2, 185, 0}) == 0);
  packet = input(0x82);
  CHECK(packet[8] == 5 && packet[12] == 0x61);
  fixture.ipc.advance(3645000);
  packet = input(0x81);
  CHECK(packet[0] == 0x13 && packet[1] == 21 && packet[2] == 5);
  CHECK(packet[19] == 4 && packet[20] == 1 && packet[21] == 0 && packet[22] == 0);
  packet = input(0x82);
  CHECK(packet ==
        std::vector<u8>({0, 0x21, 16, 0, 12, 0, 1, 0, 4, 4, 8, 0, 0x61, 0, 0, 0, 1, 2, 185, 0}));
  CHECK(output({0, 0x21, 14, 0, 10, 0, 1, 0, 5, 4, 6, 0, 0x40, 0, 0, 0, 0, 0}) == 0);
  std::vector<audio::RemoteSpeakerPacket> speaker_packets;
  fixture.ipc.set_remote_speaker_handler(
      [&](unsigned index, const audio::RemoteSpeakerPacket &samples) {
        CHECK(index == 0);
        speaker_packets.push_back(samples);
      });
  CHECK_THROWS(fixture.ipc.set_remote_speaker_processing(4, true));
  const auto speaker_output = [&](std::span<const u8> report) {
    std::vector<u8> bytes{0, 0x21, u8(report.size() + 5), 0, u8(report.size() + 1), 0, 0x40,
                          0, 0xa2};
    bytes.insert(bytes.end(), report.begin(), report.end());
    fixture.memory.copy_in(transfer, bytes);
    fixture.memory.write8(0x15000, 2);
    fixture.memory.write16(0x15002, u16(bytes.size()));
    CHECK(fixture.ioctlv(fd, 1, {{0x15000, 1}, {0x15002, 2}}, {{transfer, u32(bytes.size())}},
                         100000) == 0);
  };
  const auto speaker_ack = [&](u8 operation) {
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
      const auto response = input(0x82);
      if (response[9] != 0x22)
        continue;
      CHECK(response ==
            std::vector<u8>({0, 0x21, 10, 0, 6, 0, 0x61, 0, 0xa1, 0x22, 0x10, 0x08, operation, 0}));
      return;
    }
    CHECK(false);
  };
  std::array<u8, 22> speaker_setup{0x16, 4, 0xa2, 0, 9, 1, 1};
  speaker_output(speaker_setup);
  speaker_ack(0x16);
  speaker_setup = {0x16, 4, 0xa2, 0, 2, 4, 0x40, 0xb8, 0x0b, 0xff};
  speaker_output(speaker_setup);
  speaker_ack(0x16);
  speaker_output(std::array<u8, 2>{0x14, 6});
  speaker_ack(0x14);
  const std::array<u8, 22> speaker_data{0x18, 8, 0x80};
  speaker_output(speaker_data);
  CHECK(speaker_packets.empty());
  fixture.ipc.set_remote_speaker_processing(0, true);
  speaker_output(speaker_data);
  CHECK(speaker_packets.size() == 1 && speaker_packets[0].count == 1);
  CHECK(speaker_packets[0].mono[0] == -32768 && speaker_packets[0].rate_divisor == 13500);
  CHECK(speaker_packets[0].left_volume == 255 && speaker_packets[0].right_volume == 255);
  fixture.ipc.set_remote_speaker_processing(0, false);
  CHECK(output({0, 0x21, 8, 0, 4, 0, 0x40, 0, 0xa2, 0x12, 3, 0x31}) == 0);
  CHECK(rumbling);
  speaker_ack(0x12);
  fixture.ipc.advance(3645000);
  packet = input(0x81);
  CHECK(packet[0] == 0x13);
  packet = input(0x82);
  CHECK(packet ==
        std::vector<u8>({0, 0x21, 11, 0, 7, 0, 0x61, 0, 0xa1, 0x31, 0x10, 0x08, 128, 128, 154}));
  CHECK(output({0, 0x21, 7, 0, 3, 0, 0x40, 0, 0xa2, 0x10, 0}) == 0);
  CHECK(!rumbling);
  // Closing USB endpoints does not destroy established links or queued events.
  CHECK(control({1, 0x10, 0}) == 3);
  fixture.prepare(2, fd);
  CHECK(fixture.complete(100000) == 0);
  fixture.prepare(1);
  fixture.text(path_buffer, "/dev/usb/oh1/57e/305");
  fixture.memory.write32(request + 12, path_buffer);
  fixture.memory.write32(request + 16, 3);
  CHECK(fixture.complete(100000) == s32(fd));
  packet = input(0x81);
  CHECK(packet ==
        std::vector<u8>({0x0e, 12, 1, 1, 0x10, 0, 3, 0xa7, 0x40, 3, 0x0f, 0, 0x0e, 0x43}));
  CHECK(control({0x19, 0x0c, 0}) == 3);
  CHECK(input(0x81) == std::vector<u8>({0x0e, 5, 1, 0x19, 0x0c, 0, 0}));
  CHECK(control({0x1b, 4, 2, 0, 1}) == 5);
  CHECK(input(0x81) == std::vector<u8>({0x0f, 4, 0, 1, 0x1b, 4}));
  CHECK(input(0x81) == std::vector<u8>({0x0b, 11, 0, 0, 1, 0xbc, 2, 4, 0x38, 8, 0, 0, 0}));
  fixture.ipc.advance(controller_period);
  CHECK(input(0x82) ==
        std::vector<u8>({0, 0x21, 11, 0, 7, 0, 0x61, 0, 0xa1, 0x31, 0x10, 0x08, 128, 128, 154}));
  state.nunchuk.attached = true;
  fixture.ipc.set_remote(0, state);
  fixture.ipc.advance(controller_period);
  packet = input(0x82);
  CHECK(packet ==
        std::vector<u8>({0, 0x21, 12, 0, 8, 0, 0x61, 0, 0xa1, 0x20, 0x10, 0x08, 6, 0, 0, 0xff}));
  const std::array<u8, 22> encryption_enable{0x16, 4, 0xa4, 0, 0xf0, 1, 0xaa};
  speaker_output(encryption_enable);
  speaker_ack(0x16);
  // Authored calibration request has the observed guest geometry, with a zero key record.
  speaker_output(std::array<u8, 7>{0x17, 4, 0xa4, 0, 0x20, 0, 32});
  packet = input(0x82);
  CHECK(packet.size() == 31 && packet[9] == 0x21 && packet[12] == 0xf0);
  CHECK(packet[13] == 0 && packet[14] == 0x20);
  const std::array<u8, 16> calibration{0x80, 0x80, 0x80, 0,    0xb3, 0xb3, 0xb3, 0,
                                       0xe0, 0x20, 0x80, 0xe0, 0x20, 0x80, 0xee, 0x43};
  for (unsigned i = 0; i < calibration.size(); ++i)
    CHECK(packet[15 + i] == u8(u8(calibration[i] - 0x17) ^ 0x17));
  fixture.ipc.advance(controller_period);
  CHECK(input(0x81)[0] == 0x13);
  packet = input(0x82);
  CHECK(packet.size() == 31 && packet[9] == 0x21 && packet[12] == 0xf0);
  CHECK(packet[13] == 0 && packet[14] == 0x30);
  CHECK(std::all_of(packet.begin() + 15, packet.end(), [](u8 value) { return value == 0xfe; }));
  state.connected = false;
  fixture.ipc.set_remote(0, state);
  fixture.ipc.advance(3645000);
  packet = input(0x81);
  CHECK(packet[0] == 5 && packet[5] == 0x13);
}

namespace usage_profile {
constexpr u32 request = 0x135e0080, vectors = 0x135e1020, path_buffer = 0x135e1040;
constexpr u32 clusters_buffer = 0x135e1080, inodes_buffer = 0x135e10a0;

struct Fixture {
  Memory memory{Console::Wii};
  hw::Devices devices{Console::Wii};
  ios::Ipc ipc{memory, devices, Console::Wii};

  void text(const std::string &value) {
    auto output = memory.range(path_buffer, 64);
    std::fill(output.begin(), output.end(), u8(0));
    std::copy(value.begin(), value.end(), output.begin());
  }

  s32 complete() {
    devices.write(0x0d000000, request, 4);
    devices.write(0x0d000004, 0x31, 4);
    ipc.advance(6000);
    CHECK((devices.read(0x0d000004, 4) & 3) == 2);
    devices.write(0x0d000004, 0x32, 4);
    devices.write(0x0d000030, 0x40000000, 4);
    ipc.advance(100000000);
    CHECK(memory.read32(request) == 8);
    const s32 result = s32(memory.read32(request + 4));
    devices.write(0x0d000004, 0x34, 4);
    devices.write(0x0d000030, 0x40000000, 4);
    return result;
  }

  s32 open_fs() {
    auto data = memory.range(request, 64);
    std::fill(data.begin(), data.end(), u8(0));
    memory.write32(request, 1);
    text("/dev/fs");
    memory.write32(request + 12, path_buffer);
    memory.write32(request + 16, 3);
    return complete();
  }

  void prepare_usage(const std::string &path) {
    auto data = memory.range(request, 64);
    std::fill(data.begin(), data.end(), u8(0));
    memory.write32(request, 7);
    memory.write32(request + 8, 2);
    memory.write32(request + 12, 12);
    memory.write32(request + 16, 1);
    memory.write32(request + 20, 2);
    memory.write32(request + 24, vectors);
    memory.write32(vectors, path_buffer);
    memory.write32(vectors + 4, 64);
    memory.write32(vectors + 8, clusters_buffer);
    memory.write32(vectors + 12, 4);
    memory.write32(vectors + 16, inodes_buffer);
    memory.write32(vectors + 20, 4);
    memory.write32(clusters_buffer, 0x12345678);
    memory.write32(inodes_buffer, 0x87654321);
    text(path);
  }

  void expect_failure(const std::string &path, s32 error) {
    prepare_usage(path);
    CHECK(complete() == error);
    CHECK(memory.read32(clusters_buffer) == 0x12345678);
    CHECK(memory.read32(inodes_buffer) == 0x87654321);
  }
};

void write_file(const std::filesystem::path &path, std::uintmax_t size) {
  std::ofstream stream(path, std::ios::binary);
  CHECK(stream.good());
  if (size) {
    stream.seekp(std::streamoff(size - 1));
    stream.put('x');
  }
  stream.close();
  CHECK(std::filesystem::file_size(path) == size);
}

void usage() {
  Fixture fixture;
  const auto parent = std::filesystem::canonical(std::filesystem::temp_directory_path());
  const auto root =
      parent / ("cupid-usage-check-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture.ipc.set_user_directory(root);
  const auto directory = root / "wii" / "tmp" / "usage";
  std::filesystem::create_directories(directory / "nested");
  write_file(directory / "empty.bin", 0);
  write_file(directory / "one.bin", 1);
  write_file(directory / "exact.bin", 16384);
  write_file(directory / "nested" / "over.bin", 16385);
  write_file(directory / "filename-too-long.bin", 90000);
  std::filesystem::create_directory(directory / "invalid-long-directory");
  write_file(directory / "invalid-long-directory" / "large.bin", 65536);
  CHECK(fixture.open_fs() == 0);
  CHECK(fixture.open_fs() == 1);
  CHECK(fixture.open_fs() == 2);
  fixture.prepare_usage("/tmp/usage");
  CHECK(fixture.complete() == 0);
  CHECK(fixture.memory.read32(clusters_buffer) == 4);
  CHECK(fixture.memory.read32(inodes_buffer) == 6);
  fixture.prepare_usage("/tmp/usage/nested");
  CHECK(fixture.complete() == 0);
  CHECK(fixture.memory.read32(clusters_buffer) == 2);
  CHECK(fixture.memory.read32(inodes_buffer) == 2);
  std::filesystem::create_directory(directory / "empty");
  fixture.prepare_usage("/tmp/usage/empty");
  CHECK(fixture.complete() == 0);
  CHECK(fixture.memory.read32(clusters_buffer) == 0);
  CHECK(fixture.memory.read32(inodes_buffer) == 1);
  std::filesystem::remove(directory / "empty");
  fixture.expect_failure("/tmp/missing", -106);
  fixture.expect_failure("/tmp/usage/one.bin", -101);
  fixture.expect_failure("/tmp/usage/", -101);
  fixture.expect_failure("/tmp/../escape", -101);
  fixture.expect_failure("/tmp/CON.bin", -101);
  for (u32 offset : {16u, 20u}) {
    fixture.prepare_usage("/tmp/usage");
    fixture.memory.write32(request + offset, 0);
    CHECK(fixture.complete() == -101);
    CHECK(fixture.memory.read32(clusters_buffer) == 0x12345678);
    CHECK(fixture.memory.read32(inodes_buffer) == 0x87654321);
  }
  for (u32 offset : {4u, 12u, 20u})
    for (u32 size : {0u, 3u, 5u, 63u, 65u}) {
      fixture.prepare_usage("/tmp/usage");
      fixture.memory.write32(vectors + offset, size);
      CHECK(fixture.complete() == -101);
      CHECK(fixture.memory.read32(clusters_buffer) == 0x12345678);
      CHECK(fixture.memory.read32(inodes_buffer) == 0x87654321);
    }
  fixture.prepare_usage("/tmp/usage");
  fixture.memory.write32(vectors + 8, 0);
  CHECK(fixture.complete() == -101);
  CHECK(fixture.memory.read32(inodes_buffer) == 0x87654321);
  fixture.prepare_usage("/tmp/usage");
  std::fill(fixture.memory.range(path_buffer, 64).begin(),
            fixture.memory.range(path_buffer, 64).end(), u8('x'));
  CHECK(fixture.complete() == -101);
  CHECK(fixture.memory.read32(clusters_buffer) == 0x12345678);
  CHECK(fixture.memory.read32(inodes_buffer) == 0x87654321);
  std::error_code link_error;
  const auto external = root / "external";
  std::filesystem::create_directory(external);
  write_file(external / "private.bin", 65536);
  std::filesystem::create_directory_symlink(external, directory / "link", link_error);
  if (!link_error) {
    fixture.prepare_usage("/tmp/usage");
    CHECK(fixture.complete() == 0);
    CHECK(fixture.memory.read32(clusters_buffer) == 4);
    CHECK(fixture.memory.read32(inodes_buffer) == 6);
    fixture.expect_failure("/tmp/usage/link", -101);
  }
  // This operation deliberately has no caller UID/GID permission check.
  // First prove the non-root handle cannot use the same directory's ordinary read operation.
  const u32 parameters = 0x16000;
  std::fill(fixture.memory.range(parameters, 74).begin(),
            fixture.memory.range(parameters, 74).end(), u8(0));
  const std::string protected_path = "/tmp/usage";
  for (unsigned i = 0; i < protected_path.size(); ++i)
    fixture.memory.write8(parameters + 6 + i, u8(protected_path[i]));
  std::fill(fixture.memory.range(request, 64).begin(), fixture.memory.range(request, 64).end(),
            u8(0));
  fixture.memory.write32(request, 6);
  fixture.memory.write32(request + 8, 2);
  fixture.memory.write32(request + 12, 5);
  fixture.memory.write32(request + 16, parameters);
  fixture.memory.write32(request + 20, 74);
  CHECK(fixture.complete() == 0);
  std::fill(fixture.memory.range(0x16000, 0x2a4).begin(),
            fixture.memory.range(0x16000, 0x2a4).end(), u8(0));
  std::fill(fixture.memory.range(0x17000, 0x1e4).begin(),
            fixture.memory.range(0x17000, 0x1e4).end(), u8(0));
  fixture.memory.write32(0x16000, 0x10001);
  fixture.memory.write32(0x17000, 0x10001);
  fixture.memory.write64(0x16000 + 0x1dc, 0x0001000054455354ull);
  fixture.memory.write64(0x17000 + 0x18c, 0x0001000054455354ull);
  CHECK(fixture.ipc.set_title_context(fixture.memory.range(0x16000, 0x2a4),
                                      fixture.memory.range(0x17000, 0x1e4)));
  fixture.memory.write32(request, 2);
  fixture.memory.write32(request + 8, 2);
  CHECK(fixture.complete() == 0);
  CHECK(fixture.open_fs() == 2);
  fixture.prepare_usage("/tmp/usage");
  fixture.memory.write32(request + 12, 4);
  fixture.memory.write32(request + 20, 1);
  CHECK(fixture.complete() == -102);
  fixture.prepare_usage("/tmp/usage");
  CHECK(fixture.complete() == 0);
  CHECK(fixture.memory.read32(clusters_buffer) == 4);
  CHECK(fixture.memory.read32(inodes_buffer) == 6);
  write_file(root / "wii" / "host-only", 100000);
  fixture.prepare_usage("/");
  CHECK(fixture.complete() == 0);
  const u32 initial_clusters = fixture.memory.read32(clusters_buffer);
  const u32 initial_inodes = fixture.memory.read32(inodes_buffer);
  write_file(root / "wii" / "more-host", 200000);
  fixture.prepare_usage("/");
  CHECK(fixture.complete() == 0);
  CHECK(fixture.memory.read32(clusters_buffer) == initial_clusters);
  CHECK(fixture.memory.read32(inodes_buffer) == initial_inodes);
  write_file(directory / "limit.bin", (u64(0x7bc0) + 20) * 16384);
  fixture.prepare_usage("/tmp/usage");
  CHECK(fixture.complete() == 0);
  CHECK(fixture.memory.read32(clusters_buffer) == 0x7bc0);
  CHECK(fixture.memory.read32(inodes_buffer) == 7);
  std::filesystem::create_directory(directory / "many");
  for (unsigned i = 0; i < 0x1800; ++i)
    write_file(directory / "many" / ("n" + std::to_string(i)), 0);
  fixture.prepare_usage("/tmp/usage/many");
  CHECK(fixture.complete() == 0);
  CHECK(fixture.memory.read32(clusters_buffer) == 0);
  CHECK(fixture.memory.read32(inodes_buffer) == 0x17ff);
  const auto resolved = std::filesystem::canonical(root);
  CHECK(resolved.parent_path() == parent);
  CHECK(resolved.filename().string().starts_with("cupid-usage-check-"));
  std::filesystem::remove_all(resolved);
}
} // namespace usage_profile

void filesystem() {
  Fixture fixture;
  const auto task_temp = std::filesystem::canonical(std::filesystem::temp_directory_path());
  const auto task_root =
      task_temp / ("cupid-ios-test-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture.ipc.set_user_directory(task_root);
  std::filesystem::create_directory(task_root / "wii" / "tmp");
  {
    std::ofstream file(task_root / "wii" / "tmp" / "save.bin", std::ios::binary);
    file.write("abc", 3);
  }
  const s32 descriptor = fixture.open("/tmp/save.bin");
  CHECK(descriptor == 0);
  fixture.prepare(3, u32(descriptor));
  fixture.memory.write32(request + 12, data_buffer);
  fixture.memory.write32(request + 16, 10);
  CHECK(fixture.complete() == 3);
  CHECK(fixture.memory.read8(data_buffer) == 'a');
  CHECK(fixture.memory.read8(data_buffer + 2) == 'c');
  fixture.prepare(5, u32(descriptor));
  fixture.memory.write32(request + 12, 0xfffffffe);
  fixture.memory.write32(request + 16, 2);
  CHECK(fixture.complete() == 1);
  fixture.prepare(4, u32(descriptor));
  fixture.text(data_buffer, "XYZ");
  fixture.memory.write32(request + 12, data_buffer);
  fixture.memory.write32(request + 16, 3);
  CHECK(fixture.complete() == 3);
  const s32 second = fixture.open("/tmp/save.bin", 1);
  CHECK(second == 1);
  fixture.prepare(3, u32(second));
  fixture.memory.write32(request + 12, data_buffer);
  fixture.memory.write32(request + 16, 10);
  CHECK(fixture.complete() == 4);
  CHECK(fixture.memory.read32(data_buffer) == 0x6158595a);
  fixture.prepare(4, u32(second));
  fixture.memory.write32(request + 12, data_buffer);
  fixture.memory.write32(request + 16, 1);
  CHECK(fixture.complete() == -102);
  fixture.prepare(5, u32(second));
  fixture.memory.write32(request + 12, 5);
  CHECK(fixture.complete() == -101);
  fixture.prepare(6, u32(second));
  fixture.memory.write32(request + 12, 11);
  fixture.memory.write32(request + 24, output_buffer);
  fixture.memory.write32(request + 28, 8);
  CHECK(fixture.complete() == 0);
  CHECK(fixture.memory.read32(output_buffer) == 4);
  CHECK(fixture.memory.read32(output_buffer + 4) == 4);
  CHECK(fixture.open("/../escape.bin") == -101);
  CHECK(fixture.open("/tmp/CON.bin") == -101);
  CHECK(fixture.open("/tmp/nul") == -101);
  CHECK(fixture.open("/tmp/LPT1.dat") == -101);
  CHECK(fixture.open("/tmp/missing.bin") == -106);
  const s32 fs = fixture.open("/dev/fs");
  CHECK(fs == 2);
  fixture.prepare(6, u32(fs));
  fixture.memory.write32(request + 12, 9);
  auto params = fixture.memory.range(data_buffer, 74);
  std::fill(params.begin(), params.end(), u8(0));
  fixture.text(data_buffer + 6, "/tmp/new.bin");
  fixture.memory.write8(data_buffer + 70, 3);
  fixture.memory.write8(data_buffer + 71, 3);
  fixture.memory.write8(data_buffer + 72, 3);
  fixture.memory.write32(request + 16, data_buffer);
  fixture.memory.write32(request + 20, 74);
  CHECK(fixture.complete() == 0);
  CHECK(std::filesystem::file_size(task_root / "wii" / "tmp" / "new.bin") == 0);
  fixture.prepare(2, 0);
  CHECK(fixture.complete() == 0);
  fixture.prepare(2, 1);
  CHECK(fixture.complete() == 0);
  fixture.prepare(2, 2);
  CHECK(fixture.complete() == 0);
  CHECK(fixture.open("/tmp/new.bin") == 0);
  // Remove only the unique fixture directory created under the system temporary directory.
  CHECK(task_root.parent_path() == task_temp);
  CHECK(task_root.filename().string().starts_with("cupid-ios-test-"));
  std::filesystem::remove_all(task_root);
}

void disc_service() {
  Fixture fixture;
  const s32 descriptor = fixture.open("/dev/di");
  CHECK(descriptor == 0);
  u64 seen = 0;
  fixture.ipc.disc_read = [&](u64 offset, std::span<u8> out) {
    seen = offset;
    CHECK(out.size() == 32);
    std::fill(out.begin(), out.end(), u8(0x5a));
  };
  fixture.prepare(6, u32(descriptor));
  fixture.memory.write32(request + 12, 0x8d);
  fixture.memory.write32(request + 16, data_buffer);
  fixture.memory.write32(request + 20, 32);
  fixture.memory.write32(request + 24, output_buffer);
  fixture.memory.write32(request + 28, 32);
  fixture.memory.write32(data_buffer + 4, 32);
  fixture.memory.write32(data_buffer + 8, 2);
  fixture.submit();
  CHECK(seen == 8);
  CHECK(fixture.memory.read32(request) == 6);
  fixture.ipc.advance(218699);
  CHECK(fixture.memory.read32(output_buffer) == 0);
  fixture.ipc.advance(1);
  CHECK(fixture.memory.read32(output_buffer) == 0x5a5a5a5a);
  CHECK(fixture.memory.read32(request + 4) == 1);
  CHECK(fixture.memory.read32(request) == 8);
  fixture.clear_ack();
  fixture.ipc.advance(49200);
  fixture.clear_reply();
  fixture.prepare(6, u32(descriptor));
  fixture.memory.write32(request + 12, 0x71);
  fixture.memory.write32(request + 16, data_buffer);
  fixture.memory.write32(request + 20, 32);
  fixture.memory.write32(request + 24, output_buffer);
  fixture.memory.write32(request + 28, 32);
  CHECK(fixture.complete() == 32);
  fixture.ipc.set_partition_open(true);
  fixture.ipc.partition_read = [](u64 offset, std::span<u8> out) {
    CHECK(offset == 8);
    std::fill(out.begin(), out.end(), u8(0x7b));
  };
  fixture.prepare(6, u32(descriptor));
  fixture.memory.write32(request + 12, 0x71);
  fixture.memory.write32(request + 16, data_buffer);
  fixture.memory.write32(request + 20, 32);
  fixture.memory.write32(request + 24, output_buffer);
  fixture.memory.write32(request + 28, 32);
  CHECK(fixture.complete() == 1);
  CHECK(fixture.memory.read32(output_buffer) == 0x7b7b7b7b);
  auto read = [&](u32 operation, u32 length, u32 position, u32 destination, u32 size) {
    fixture.prepare(6, u32(descriptor));
    fixture.memory.write32(request + 12, operation);
    fixture.memory.write32(request + 16, data_buffer);
    fixture.memory.write32(request + 20, 32);
    fixture.memory.write32(request + 24, destination);
    fixture.memory.write32(request + 28, size);
    fixture.memory.write32(data_buffer + 4, length);
    fixture.memory.write32(data_buffer + 8, position);
    return fixture.complete();
  };
  CHECK(read(0x71, 31, 2, output_buffer, 32) == 128);
  CHECK(read(0x71, 32, 2, output_buffer + 1, 32) == 128);
  CHECK(read(0x71, 32, 2, output_buffer, 31) == 32);
  CHECK(read(0x8d, 32, 2, output_buffer, 31) == 16);
  CHECK(read(0x8d, 32, 0x14000, output_buffer, 32) == 32);
  CHECK(seen == 8);
  fixture.ipc.disc_read = [](u64 offset, std::span<u8>) {
    CHECK(offset == 0x118280000ull);
    throw std::out_of_range("Disc block exceeds image extent");
  };
  CHECK(read(0x8d, 32, 0x460a0000, output_buffer, 32) == 2);
  CHECK(read(0xe0, 0, 0, output_buffer, 4) == 1);
  CHECK(fixture.memory.read32(output_buffer) == 0x52100);
  CHECK(read(0xe0, 0, 0, output_buffer, 4) == 1);
  CHECK(fixture.memory.read32(output_buffer) == 0);
}

void unfinished_service() {
  Fixture fixture;
  const s32 descriptor = fixture.open("/dev/es");
  CHECK(descriptor == 0);
  fixture.prepare(7, u32(descriptor));
  bool caught = false;
  try {
    fixture.submit();
  } catch (const BusFault &fault) {
    caught = fault.unsupported_reason;
  }
  CHECK(caught);
}

void title_metadata() {
  Fixture fixture;
  const u32 descriptor = u32(fixture.open("/dev/es"));
  CHECK(fixture.ioctlv(descriptor, 0x20, {}, {{output_buffer, 8}}) == -1017);
  CHECK(fixture.open("/dev/es") == 1);
  CHECK(fixture.open("/dev/es") == 2);
  CHECK(fixture.open("/dev/es") == -1016);
  const u64 title_id = 0x0001000054455354ull;
  fixture.memory.write32(0x16000, 0x10001);
  fixture.memory.write32(0x17000, 0x10001);
  // These buffers describe a test ticket and a TMD with one real-sized content record.
  fixture.memory.write64(0x16000 + 0x1dc, title_id);
  fixture.memory.write64(0x17000 + 0x18c, title_id);
  fixture.memory.write64(0x17000 + 0x184, 0x10000001eull);
  fixture.memory.write16(0x17000 + 0x1de, 1);
  fixture.memory.write32(0x17000 + 0x1e4, 0x11223344);
  fixture.memory.write16(0x17000 + 0x1e8, 7);
  fixture.memory.write16(0x17000 + 0x1ea, 1);
  fixture.memory.write64(0x17000 + 0x1ec, 0x123456789ull);
  fixture.memory.write64(0x16000 + 0x1d0, 0x1234567890abcdefull);
  CHECK(fixture.ipc.set_title_context(fixture.memory.range(0x16000, 0x2a4),
                                      fixture.memory.range(0x17000, 0x208), 0x50000));
  CHECK(fixture.ioctlv(descriptor, 0x20, {}, {{output_buffer, 8}}) == 0);
  CHECK(fixture.memory.read64(output_buffer) == title_id);
  CHECK(fixture.ioctlv(descriptor, 0x39, {}, {{output_buffer, 4}}) == 0);
  CHECK(fixture.memory.read32(output_buffer) == 0x208);
  fixture.memory.write32(data_buffer, 0x208);
  CHECK(fixture.ioctlv(descriptor, 0x3a, {{data_buffer, 4}}, {{output_buffer, 0x208}}) == 0);
  CHECK(std::equal(fixture.memory.range(0x17000, 0x208).begin(),
                   fixture.memory.range(0x17000, 0x208).end(),
                   fixture.memory.range(output_buffer, 0x208).begin()));
  CHECK(fixture.ioctlv(descriptor, 0x19, {{0, 0}}, {{output_buffer, 4}}) == 0);
  CHECK(fixture.memory.read32(output_buffer) == 0x6c);
  fixture.memory.write32(data_buffer, 0x6c);
  CHECK(fixture.ioctlv(descriptor, 0x1a, {{0, 0}, {data_buffer, 4}}, {{output_buffer, 0x6c}}) == 0);
  CHECK(fixture.memory.read64(output_buffer + 4) == 0x10000001eull);
  CHECK(fixture.memory.read64(output_buffer + 12) == title_id);
  CHECK(fixture.memory.read32(output_buffer + 0x5c) == 0x11223344);
  CHECK(fixture.memory.read64(output_buffer + 0x64) == 0x123456789ull);
  CHECK(fixture.ioctlv(descriptor, 0x1b, {{0, 0}}, {{output_buffer, 0xd8}}) == 0);
  CHECK(fixture.memory.read64(output_buffer + 4) == 0x1234567890abcdefull);
  CHECK(fixture.memory.read64(output_buffer + 16) == title_id);
  fixture.memory.write64(data_buffer, 0x1234567890abcdefull);
  fixture.memory.write32(output_buffer, 0xaabbccdd);
  CHECK(fixture.ioctlv(descriptor, 0x16, {{data_buffer, 8}}, {{0, 0}, {output_buffer, 4}}) == 0);
  CHECK(fixture.memory.read32(output_buffer) == 0);
  fixture.memory.write32(output_buffer + 0x100, 0xaabbccdd);
  CHECK(fixture.ioctlv(descriptor, 0x16, {{data_buffer, 8}},
                       {{output_buffer + 0x100, 64}, {output_buffer, 4}}) == 0);
  CHECK(fixture.memory.read32(output_buffer + 0x100) == 0xaabbccdd);
  CHECK(fixture.ioctlv(descriptor, 0x16, {{data_buffer, 7}}, {{0, 0}, {output_buffer, 4}}) ==
        -1017);
  CHECK(fixture.ioctlv(descriptor, 0x16, {{data_buffer, 8}}, {{0, 0}, {output_buffer, 8}}) ==
        -1017);
  CHECK(fixture.ioctlv(descriptor, 0x16, {{data_buffer, 8}},
                       {{output_buffer + 0x100, 7}, {output_buffer, 4}}) == -1017);
  fixture.memory.write64(data_buffer, title_id);
  CHECK(fixture.ioctlv(descriptor, 0x12, {{data_buffer, 8}}, {{output_buffer, 4}}) == 0);
  // A disc ticket is not an installed ticket. Missing installed tickets yield zero views.
  CHECK(fixture.memory.read32(output_buffer) == 0);
  fixture.memory.write64(data_buffer, 0x0001000054455354ull);
  CHECK(fixture.ioctlv(descriptor, 0x1d, {{data_buffer, 8}}, {{output_buffer, 64}}) == 0);
  CHECK(fixture.memory.read32(output_buffer) == 0x2f746974);
  CHECK(fixture.ioctlv(descriptor, 0x3a, {{data_buffer, 4}}, {{output_buffer, 4}}) == -1017);
  CHECK(fixture.ioctlv(descriptor, 0x1c, {}, {}) == -1017);
  CHECK(fixture.ioctlv(descriptor, 0x20, {}, {{0, 8}}) == -1017);
  CHECK(fixture.ioctlv(descriptor, 0x20, {}, {{0x1800000, 8}}) == -1017);
  fixture.memory.write64(0x16000 + 0x1dc, title_id + 1);
  CHECK(!fixture.ipc.set_title_context(fixture.memory.range(0x16000, 0x2a4),
                                       fixture.memory.range(0x17000, 0x208)));
  CHECK(fixture.ioctlv(descriptor, 0x20, {}, {{output_buffer, 8}}) == -1017);
  fixture.memory.write64(0x16000 + 0x1dc, title_id);
  fixture.memory.write32(0x16000 + 0x264, 1);
  CHECK(fixture.ipc.set_title_context(fixture.memory.range(0x16000, 0x2a4),
                                      fixture.memory.range(0x17000, 0x208)));
  fixture.memory.write64(data_buffer, 0x1234567890abcdefull);
  CHECK_THROWS(fixture.ioctlv(descriptor, 0x16, {{data_buffer, 8}}, {{0, 0}, {output_buffer, 4}}));
}

void private_metadata(const std::filesystem::path &ticket_path,
                      const std::filesystem::path &tmd_path, u64 partition_offset) {
  auto load = [](const std::filesystem::path &path, unsigned maximum) {
    const auto size = std::filesystem::file_size(path);
    CHECK(size <= maximum);
    std::vector<u8> bytes(std::size_t(size), u8{});
    std::ifstream stream(path, std::ios::binary);
    stream.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()));
    CHECK(bool(stream));
    return bytes;
  };
  const auto ticket = load(ticket_path, 0x2a4), tmd = load(tmd_path, 0x49e4);
  Fixture fixture;
  fixture.memory.copy_in(0x18000, tmd);
  fixture.memory.copy_in(0x20000, ticket);
  CHECK(fixture.ipc.set_title_context(ticket, tmd, partition_offset));
  const auto descriptor = u32(fixture.open("/dev/es"));
  CHECK(fixture.ioctlv(descriptor, 0x20, {}, {{output_buffer, 8}}) == 0);
  CHECK(fixture.memory.read64(output_buffer) == fixture.memory.read64(0x1818c));
  CHECK(fixture.ioctlv(descriptor, 0x39, {}, {{output_buffer, 4}}) == 0);
  CHECK(fixture.memory.read32(output_buffer) == tmd.size());
  fixture.memory.write32(data_buffer, u32(tmd.size()));
  CHECK(fixture.ioctlv(descriptor, 0x3a, {{data_buffer, 4}}, {{output_buffer, u32(tmd.size())}}) ==
        0);
  CHECK(
      std::equal(tmd.begin(), tmd.end(), fixture.memory.range(output_buffer, tmd.size()).begin()));
  CHECK(fixture.ioctlv(descriptor, 0x1b, {{0, 0}}, {{output_buffer, 0xd8}}) == 0);
  CHECK(std::equal(ticket.begin() + 0x1d0, ticket.end(),
                   fixture.memory.range(output_buffer + 4, 0xd4).begin()));
  fixture.memory.write64(data_buffer, fixture.memory.read64(0x201d0));
  CHECK(fixture.ioctlv(descriptor, 0x16, {{data_buffer, 8}}, {{0, 0}, {output_buffer, 4}}) == 0);
  CHECK(fixture.memory.read32(output_buffer) == 0);
  const auto disc = u32(fixture.open("/dev/di"));
  fixture.prepare(6, disc);
  fixture.memory.write32(request + 12, 0x71);
  fixture.memory.write32(request + 16, data_buffer);
  fixture.memory.write32(request + 20, 32);
  fixture.memory.write32(request + 24, output_buffer);
  fixture.memory.write32(request + 28, 32);
  fixture.memory.write32(data_buffer + 4, 32);
  CHECK(fixture.complete() == 32);
  fixture.memory.write32(data_buffer + 4, u32(partition_offset >> 2));
  CHECK(fixture.ioctlv(disc, 0x8b, {{data_buffer, 32}, {0, 0}, {0, 0}},
                       {{output_buffer, u32(tmd.size())}, {0x24000, 4}}) == 1);
  CHECK(fixture.memory.read32(0x24000) == 0);
  CHECK(
      std::equal(tmd.begin(), tmd.end(), fixture.memory.range(output_buffer, tmd.size()).begin()));
}

void filesystem_metadata() {
  const auto task_temp = std::filesystem::canonical(std::filesystem::temp_directory_path());
  const auto task_root =
      task_temp / ("cupid-ios-metadata-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(task_root);
  Fixture fixture;
  fixture.ipc.set_user_directory(task_root);
  const u64 title = 0x000100004d455441ull;
  fixture.memory.write32(0x16000, 0x10001);
  fixture.memory.write32(0x17000, 0x10001);
  fixture.memory.write64(0x16000 + 0x1dc, title);
  fixture.memory.write64(0x17000 + 0x18c, title);
  fixture.memory.write16(0x17000 + 0x198, 0x4444);
  fixture.memory.write16(0x17000 + 0x1de, 2);
  fixture.memory.write32(0x17000 + 0x1e4, 1);
  fixture.memory.write32(0x17000 + 0x208, 2);
  fixture.memory.write16(0x17000 + 0x20e, 0x8000);
  for (unsigned i = 0; i < 20; ++i)
    fixture.memory.write8(0x17000 + 0x218 + i, u8(0xa0 + i));
  CHECK(fixture.ipc.set_title_context(fixture.memory.range(0x16000, 0x2a4),
                                      fixture.memory.range(0x17000, 0x22c)));
  const u32 descriptor = u32(fixture.open("/dev/fs"));
  auto create = [&](const std::string &path, u32 operation) {
    fixture.prepare(6, descriptor);
    fixture.memory.write32(request + 12, operation);
    fixture.memory.write32(request + 16, data_buffer);
    fixture.memory.write32(request + 20, 74);
    std::fill(fixture.memory.range(data_buffer, 74).begin(),
              fixture.memory.range(data_buffer, 74).end(), u8(0));
    fixture.memory.write32(data_buffer, 0x7777);
    fixture.memory.write16(data_buffer + 4, 0x7777);
    fixture.text(data_buffer + 6, path);
    fixture.memory.write8(data_buffer + 70, 3);
    fixture.memory.write8(data_buffer + 71, 1);
    fixture.memory.write8(data_buffer + 73, 0x5a);
    return fixture.complete();
  };
  auto get_attribute = [&](Fixture &test, u32 fd, const std::string &path) {
    test.prepare(6, fd);
    test.memory.write32(request + 12, 6);
    test.memory.write32(request + 16, data_buffer);
    test.memory.write32(request + 20, 64);
    test.memory.write32(request + 24, output_buffer);
    test.memory.write32(request + 28, 74);
    test.text(data_buffer, path);
    return test.complete();
  };
  CHECK(create("/tmp/alpha", 9) == 0);
  CHECK(get_attribute(fixture, descriptor, "/tmp/alpha") == 0);
  CHECK(fixture.memory.read32(output_buffer) == 0x1001);
  CHECK(fixture.memory.read16(output_buffer + 4) == 0x4444);
  CHECK(fixture.memory.read8(output_buffer + 70) == 3);
  CHECK(fixture.memory.read8(output_buffer + 71) == 1);
  CHECK(fixture.memory.read8(output_buffer + 72) == 0);
  CHECK(fixture.memory.read8(output_buffer + 73) == 0x5a);
  CHECK(fixture.memory.read32(output_buffer + 6) == 0);
  auto set_attribute = [&](u32 uid) {
    fixture.prepare(6, descriptor);
    fixture.memory.write32(request + 12, 5);
    fixture.memory.write32(request + 16, data_buffer);
    fixture.memory.write32(request + 20, 74);
    fixture.memory.write32(data_buffer, uid);
    fixture.memory.write16(data_buffer + 4, 0x4455);
    fixture.text(data_buffer + 6, "/tmp/alpha");
    fixture.memory.write8(data_buffer + 70, 1);
    fixture.memory.write8(data_buffer + 71, 0);
    fixture.memory.write8(data_buffer + 72, 0);
    fixture.memory.write8(data_buffer + 73, 0x77);
    return fixture.complete();
  };
  CHECK(set_attribute(0x1002) == -102);
  CHECK(set_attribute(0x1001) == 0);
  CHECK(fixture.open("/tmp/alpha", 3) == -102);
  CHECK(fixture.open("/sys/uid.sys", 1) == -102);
  CHECK(create("/tmp/beta", 9) == 0);
  fixture.text(data_buffer, "/tmp");
  fixture.memory.write32(0x15000, 2);
  CHECK(fixture.ioctlv(descriptor, 4, {{data_buffer, 64}, {0x15000, 4}},
                       {{output_buffer, 26}, {0x15004, 4}}) == 0);
  CHECK(fixture.memory.read32(0x15004) == 2);
  CHECK(fixture.memory.read32(output_buffer) == 0x62657461);
  CHECK(fixture.memory.read8(output_buffer + 4) == 0);
  CHECK(fixture.memory.read32(output_buffer + 5) == 0x616c7068);
  CHECK(fixture.memory.read8(output_buffer + 9) == 'a');
  CHECK(fixture.ioctlv(descriptor, 4, {{data_buffer, 64}}, {{output_buffer, 4}}) == 0);
  CHECK(fixture.memory.read32(output_buffer) == 2);
  auto rename = [&](const std::string &from, const std::string &to) {
    fixture.prepare(6, descriptor);
    fixture.memory.write32(request + 12, 8);
    fixture.memory.write32(request + 16, data_buffer);
    fixture.memory.write32(request + 20, 128);
    fixture.text(data_buffer, from);
    fixture.text(data_buffer + 64, to);
    return fixture.complete();
  };
  CHECK(rename("/tmp/beta", "/shared2/other") == -101);
  CHECK(rename("/tmp/beta", "/shared2/beta") == 0);
  CHECK(!std::filesystem::exists(task_root / "wii" / "tmp" / "beta"));
  CHECK(std::filesystem::is_regular_file(task_root / "wii" / "shared2" / "beta"));
  CHECK(create("/tmp/sub", 3) == 0);
  CHECK(create("/tmp/sub/test.bin", 9) == 0);
  CHECK(fixture.open("/tmp/sub/test.bin") == 1);
  auto erase = [&] {
    fixture.prepare(6, descriptor);
    fixture.memory.write32(request + 12, 7);
    fixture.memory.write32(request + 16, data_buffer);
    fixture.memory.write32(request + 20, 64);
    fixture.text(data_buffer, "/tmp/sub");
    return fixture.complete();
  };
  CHECK(erase() == -111);
  fixture.prepare(2, 1);
  CHECK(fixture.complete() == 0);
  CHECK(erase() == 0);
  CHECK(!std::filesystem::exists(task_root / "wii" / "tmp" / "sub"));
  Fixture loaded;
  loaded.ipc.set_user_directory(task_root);
  CHECK(loaded.ipc.set_title_context(fixture.memory.range(0x16000, 0x2a4),
                                     fixture.memory.range(0x17000, 0x22c)));
  CHECK(loaded.open("/dev/fs") == 0);
  CHECK(get_attribute(loaded, 0, "/tmp/alpha") == 0);
  CHECK(loaded.memory.read32(output_buffer) == 0x1001);
  CHECK(loaded.memory.read16(output_buffer + 4) == 0x4455);
  CHECK(loaded.memory.read8(output_buffer + 70) == 1);
  CHECK(loaded.memory.read8(output_buffer + 73) == 0x77);
  CHECK(loaded.open("/tmp/alpha", 3) == -102);
  CHECK(loaded.open("/tmp/alpha", 1) == 1);
  CHECK(loaded.open("/dev/es") == 2);
  CHECK(loaded.ioctlv(2, 0x0e, {}, {{output_buffer, 4}}) == 0);
  CHECK(loaded.memory.read32(output_buffer) == 1);
  CHECK(loaded.ioctlv(2, 0x0c, {}, {{output_buffer, 4}}) == 0);
  CHECK(loaded.memory.read32(output_buffer) == 0);
  loaded.memory.write32(data_buffer, 8);
  CHECK(loaded.ioctlv(2, 0x0f, {{data_buffer, 4}}, {{output_buffer, 64}}) == 0);
  CHECK(loaded.memory.read64(output_buffer) == title);
  loaded.memory.write64(data_buffer, title);
  CHECK(loaded.ioctlv(2, 0x10, {{data_buffer, 8}}, {{output_buffer, 4}}) == 0);
  CHECK(loaded.memory.read32(output_buffer) == 0);
  const auto content = task_root / "wii" / "title" / "00010000" / "4d455441" / "content";
  {
    std::ofstream stream(content / "00000001.app", std::ios::binary);
    stream.put('A');
  }
  CHECK(loaded.ioctlv(2, 0x10, {{data_buffer, 8}}, {{output_buffer, 4}}) == 0);
  CHECK(loaded.memory.read32(output_buffer) == 1);
  {
    std::ofstream map(task_root / "wii" / "shared1" / "content.map", std::ios::binary);
    map.write("00000003", 8);
    const auto hash = fixture.memory.range(0x17218, 20);
    map.write(reinterpret_cast<const char *>(hash.data()), 20);
  }
  CHECK(loaded.ioctlv(2, 0x10, {{data_buffer, 8}}, {{output_buffer, 4}}) == 0);
  CHECK(loaded.memory.read32(output_buffer) == 1);
  {
    std::ofstream stream(task_root / "wii" / "shared1" / "00000003.app", std::ios::binary);
    stream.put('B');
  }
  CHECK(loaded.ioctlv(2, 0x10, {{data_buffer, 8}}, {{output_buffer, 4}}) == 0);
  CHECK(loaded.memory.read32(output_buffer) == 2);
  loaded.memory.write32(0x15000, 2);
  CHECK(loaded.ioctlv(2, 0x11, {{data_buffer, 8}, {0x15000, 4}}, {{output_buffer, 8}}) == 0);
  CHECK(loaded.memory.read32(output_buffer) == 1);
  CHECK(loaded.memory.read32(output_buffer + 4) == 2);
  std::filesystem::create_directory(task_root / "wii" / "ticket" / "00010000");
  {
    std::ofstream stream(task_root / "wii" / "ticket" / "00010000" / "4d455441.tik",
                         std::ios::binary);
    const auto bytes = fixture.memory.range(0x16000, 0x2a4);
    stream.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
  }
  CHECK(loaded.ioctlv(2, 0x0c, {}, {{output_buffer, 4}}) == 0);
  CHECK(loaded.memory.read32(output_buffer) == 1);
  CHECK(loaded.ioctlv(2, 0x12, {{data_buffer, 8}}, {{output_buffer, 4}}) == 0);
  CHECK(loaded.memory.read32(output_buffer) == 1);
  CHECK(std::filesystem::file_size(task_root / "wii" / "sys" / "uid.sys") == 24);
  CHECK(task_root.parent_path() == task_temp);
  CHECK(task_root.filename().string().starts_with("cupid-ios-metadata-"));
  std::filesystem::remove_all(task_root);
}
} // namespace

int main(int argc, char **argv) {
  return run_tests([&] {
    memory_values();
    wii_register_mirrors();
    handshake();
    system_management();
    network_scheduler_queries();
    network_storage_permissions();
    system_configuration_storage();
    bluetooth_controller();
    bluetooth_reconnect_cooldown();
    bluetooth_vendor_transport();
    bluetooth_remote_transport();
    filesystem();
    usage_profile::usage();
    disc_service();
    unfinished_service();
    title_metadata();
    filesystem_metadata();
    if (argc == 3 || argc == 4)
      private_metadata(argv[1], argv[2], argc == 4 ? std::stoull(argv[3], nullptr, 0) : 0);
  });
}
