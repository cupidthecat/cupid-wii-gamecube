// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/platform/output.hpp"
#include "cupid/runtime/machine.hpp"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace machine_speaker_transport {
using namespace cupid;
constexpr u32 ipc_base = 0x0d000000, request = 0x10000, transfer = 0x17000;
constexpr u64 dac_period = 182250; // Eight stereo frames at the Wii reset rate of 32 kHz.

void require(bool condition, const char *reason) {
  if (!condition)
    throw std::runtime_error(reason);
}

std::vector<u8> idle_program() {
  // An ordinary divide/branch loop supplies real CPU ticks to every device.
  constexpr std::array<u32, 4> code{0x3880007b, 0x38a00007, 0x7c642bd6, 0x4bfffffc};
  std::vector<u8> dol(0x120);
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

struct Fixture {
  runtime::Machine machine{Console::Wii};
  std::unique_ptr<platform::Output> output;
  std::filesystem::path root;
  u32 fd = 0;
  u64 dma_hash = 0, dma_first_tick = 0;
  unsigned sample_port = 0;

  explicit Fixture(bool speakers) {
    const auto parent = std::filesystem::canonical(std::filesystem::temp_directory_path());
    root = parent / ("cupid-machine-speaker-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(root), "Temporary fixture directory already exists");
    std::ofstream replay(root / "input.txt");
    replay << "instruction 0 remote0.connected=1 remote1.connected=1 "
              "remote2.connected=1 remote3.connected=1\n";
    replay.close();
    require(bool(replay), "Unable to write authored input replay");
    machine.set_user_directory(root / "user");
    machine.set_input_replay(root / "input.txt");
    machine.set_limits({0, 0, 0});
    machine.load_executable(idle_program());
    set_output(speakers);
    machine.step();
    prepare(1);
    constexpr char path[] = "/dev/usb/oh1/57e/305";
    machine.memory().copy_in(0x11000, {reinterpret_cast<const u8 *>(path), sizeof(path)});
    machine.memory().write32(request + 12, 0x11000);
    machine.memory().write32(request + 16, 3);
    const s32 opened = complete();
    require(opened >= 0, "Unable to open guest Bluetooth transport");
    fd = u32(opened);
  }

  ~Fixture() {
    machine.set_output(nullptr);
    output.reset();
    std::error_code error;
    const auto target = std::filesystem::canonical(root, error);
    const auto parent = std::filesystem::canonical(std::filesystem::temp_directory_path(), error);
    if (!error && target.parent_path() == parent &&
        target.filename().string().starts_with("cupid-machine-speaker-"))
      std::filesystem::remove_all(target, error);
  }

  void set_output(bool speakers) {
    machine.set_output(nullptr);
    output.reset();
    platform::OutputOptions options;
    options.headless = true;
    options.audio = true;
    options.remote_speakers = speakers;
    output = std::make_unique<platform::Output>(options);
    dma_hash = output->audio_hash();
    dma_first_tick = machine.devices().cycles() / dac_period;
    machine.set_output(output.get());
    require(output->remote_speaker_processing() == speakers, "Incorrect host speaker setting");
  }

  void prepare(u32 command) {
    auto bytes = machine.memory().range(request, 64);
    std::fill(bytes.begin(), bytes.end(), u8(0));
    machine.memory().write32(request, command);
    machine.memory().write32(request + 8, fd);
  }

  void wait_control(u32 bits) {
    const u64 start = machine.cpu().state().cycles;
    while ((machine.memory().read32(ipc_base + 4) & bits) == 0) {
      machine.step();
      require(machine.cpu().state().cycles - start < 20000000,
              "Guest IPC request did not complete");
      require(machine.cpu().state().spr[cpu::spr::dar] == 0, "Authored guest program incurred DSI");
      require(machine.cpu().state().spr[cpu::spr::dsisr] == 0,
              "Authored guest program recorded DSISR");
    }
  }

  s32 complete() {
    machine.memory().write32(ipc_base, request);
    machine.memory().write32(ipc_base + 4, 0x31);
    wait_control(2);
    machine.memory().write32(ipc_base + 4, 0x32);
    machine.memory().write32(ipc_base + 0x30, 0x40000000);
    wait_control(4);
    require(machine.memory().read32(ipc_base + 8) == request, "Reply reached the wrong request");
    require(machine.memory().read32(request) == 8, "Reply did not update the guest command");
    const s32 result = s32(machine.memory().read32(request + 4));
    machine.memory().write32(ipc_base + 4, 0x34);
    machine.memory().write32(ipc_base + 0x30, 0x40000000);
    return result;
  }

  s32 ioctlv(u32 operation, std::initializer_list<std::pair<u32, u32>> inputs,
             std::pair<u32, u32> payload) {
    prepare(7);
    auto &memory = machine.memory();
    memory.write32(request + 12, operation);
    memory.write32(request + 16, u32(inputs.size()));
    memory.write32(request + 20, 1);
    memory.write32(request + 24, 0x14000);
    u32 cursor = 0x14000;
    for (const auto &[address, size] : inputs) {
      memory.write32(cursor, address);
      memory.write32(cursor + 4, size);
      cursor += 8;
    }
    memory.write32(cursor, payload.first);
    memory.write32(cursor + 4, payload.second);
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
    const s32 result = ioctlv(
        0, {{0x15000, 1}, {0x15001, 1}, {0x15002, 2}, {0x15004, 2}, {0x15006, 2}, {0x15008, 1}},
        {transfer, u32(bytes.size())});
    require(result == s32(bytes.size()), "HCI command framing was rejected");
  }

  void acl(unsigned port, u16 cid, std::span<const u8> payload) {
    std::vector<u8> bytes{u8(port), 0x21,        u8(payload.size() + 4), 0, u8(payload.size()), 0,
                          u8(cid),  u8(cid >> 8)};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    machine.memory().copy_in(transfer, bytes);
    machine.memory().write8(0x15000, 2);
    machine.memory().write16(0x15002, u16(bytes.size()));
    require(ioctlv(1, {{0x15000, 1}, {0x15002, 2}}, {transfer, u32(bytes.size())}) == 0,
            "Guest ACL packet was rejected");
  }

  std::vector<u8> input(u8 endpoint) {
    machine.memory().write8(0x15000, endpoint);
    machine.memory().write16(0x15002, 512);
    const s32 length =
        ioctlv(endpoint == 0x81 ? 2 : 1, {{0x15000, 1}, {0x15002, 2}}, {0x13000, 512});
    require(length > 0 && length <= 512, "Guest USB input length was invalid");
    const auto bytes = machine.memory().range(0x13000, u32(length));
    return {bytes.begin(), bytes.end()};
  }

  void connect(unsigned port) {
    const std::array<u8, 16> connect{5,        4,    13,   0x11, 2, 0x19, 0x79, 0,
                                     u8(port), 0x18, 0xcc, 0,    0, 0,    0,    0};
    hci(connect);
    require(input(0x81)[0] == 0x0f, "Missing HCI command status");
    auto response = input(0x81);
    require(response.size() == 13 && response[0] == 3 && response[2] == 0 && response[3] == port &&
                response[4] == 1,
            "The requested Remote did not connect");
    const std::array<u8, 8> channel{2, 1, 4, 0, 0x13, 0, 0x61, 0};
    acl(port, 1, channel);
    response = input(0x82);
    require(response.size() == 20 && response[8] == 3 && response[12] == 0x40 &&
                response[14] == 0x61 && response[16] == 0,
            "The guest interrupt channel was not accepted");
    const std::array<u8, 12> config{4, 2, 8, 0, 0x40, 0, 0, 0, 1, 2, 185, 0};
    acl(port, 1, config);
    response = input(0x82);
    require(response[8] == 5 && response[12] == 0x61, "Missing channel configuration response");
    require(input(0x81)[0] == 0x13, "Missing completed-packet event");
    response = input(0x82);
    require(response.size() == 20 && response[8] == 4 && response[12] == 0x61,
            "Missing peer configuration request");
    const std::array<u8, 10> configured{5, response[9], 6, 0, 0x40, 0, 0, 0, 0, 0};
    acl(port, 1, configured);
  }

  void hid(unsigned port, std::span<const u8> report) {
    std::vector<u8> payload{0xa2};
    payload.insert(payload.end(), report.begin(), report.end());
    acl(port, 0x40, payload);
  }

  void configure_speaker(unsigned port, bool hid_status) {
    std::array<u8, 22> setup{0x16, 4, 0xa2, 0, 9, 1, 1};
    hid(port, setup);
    setup = {0x16, 4, 0xa2, 0, 2, 4, 0x40, 0xb8, 0x0b, 0xff};
    hid(port, setup);
    hid(port, std::array<u8, 2>{0x14, u8(hid_status ? 6 : 2)});
    hid(port, std::array<u8, 2>{0x19, 0});
  }

  void samples(unsigned port) {
    sample_port = port;
    std::array<u8, 22> report;
    report.fill(0x40);
    report[0] = 0x18;
    report[1] = 20 << 3;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned packet = 0; packet < 80; ++packet) {
      hid(port, report);
      // PCM8 at divisor 13,500 is 8 kHz: each 20-sample packet spans 2.5 ms.
      std::this_thread::sleep_until(start + std::chrono::microseconds(2500 * (packet + 1)));
    }
  }

  platform::HostAudioStats wait_audio(bool nonzero) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    platform::HostAudioStats stats;
    do {
      stats = output->host_audio_stats();
      if (stats.mixed_frames >= 2048 && (!nonzero || stats.nonzero_samples > 0))
        return stats;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    std::ostringstream message;
    message << "Host speaker callback did not reach the expected state: port=" << sample_port
            << " expected_nonzero=" << nonzero << " mixed_frames=" << stats.mixed_frames
            << " nonzero_samples=" << stats.nonzero_samples
            << " left_nonzero=" << stats.left_nonzero << " right_nonzero=" << stats.right_nonzero
            << " dropped_granules=" << stats.dropped_granules;
    throw std::runtime_error(message.str());
  }

  void check_dma_silence() {
    // The idle Wii program leaves DMA disabled at its reset 32-kHz clock.
    // Speaker transport advances guest time, so account for every silent tick,
    // including the retained partial period when replacing the host output.
    const u64 samples = (machine.devices().cycles() / dac_period - dma_first_tick) * 16;
    u64 expected_hash = dma_hash;
    for (u64 byte = 0; byte < samples * 2; ++byte)
      expected_hash *= 1099511628211ULL;
    require(output->audio_samples() == samples && output->audio_nonzero_samples() == 0 &&
                output->audio_peak() == 0 && output->audio_hash() == expected_hash,
            "Speaker transport changed the scheduled main DAC silence");
  }
};

void checks() {
  require(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0, "Unable to select dummy audio backend");
  platform::OutputOptions invalid;
  invalid.remote_speakers = true;
  bool rejected = false;
  try {
    platform::Output forbidden(invalid);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "Speaker setting without host audio was accepted");
  {
    Fixture fixture(false);
    fixture.connect(0);
    fixture.configure_speaker(0, true);
    fixture.samples(0);
    require(fixture.wait_audio(false).nonzero_samples == 0,
            "Default-disabled host processing emitted speaker samples");
    fixture.check_dma_silence();
    fixture.set_output(true);
    // Host processing is separate from the speaker bit in the HID status report.
    fixture.configure_speaker(0, false);
    fixture.samples(0);
    const auto stats = fixture.wait_audio(true);
    require(stats.left_nonzero > 0 && stats.right_nonzero > 0,
            "Enabled speaker samples did not reach both host channels");
    fixture.check_dma_silence();
  }
  for (unsigned port = 1; port < 4; ++port) {
    Fixture fixture(true);
    fixture.connect(port);
    fixture.configure_speaker(port, false);
    fixture.samples(port);
    const auto stats = fixture.wait_audio(true);
    require(stats.nonzero_samples > 0 && stats.left_nonzero > 0 && stats.right_nonzero > 0,
            "A nonzero Remote port did not reach the host audio channel");
    fixture.check_dma_silence();
  }
}
} // namespace machine_speaker_transport
