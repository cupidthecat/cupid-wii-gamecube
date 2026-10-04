// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/audio/disc_stream.hpp"
#include "cupid/runtime/machine.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <vector>

using namespace cupid;

namespace {
constexpr u32 di = 0x0c006000, ai = 0x0c006c00;

void decoder() {
  audio::DiscAdpcm decoder;
  std::array<u8, 32> block{};
  std::array<s16, 56> samples{};
  block[0] = 0;
  block[1] = 1;
  std::fill(block.begin() + 4, block.end(), u8(0x87));
  decoder.decode(block, samples);
  for (unsigned n = 0; n < 28; ++n)
    CHECK(samples[n * 2] == 28672 && samples[n * 2 + 1] == -16384);
  block[0] = 0x10;
  block[1] = 0x11;
  std::fill(block.begin() + 4, block.end(), u8(0));
  decoder.decode(block, samples);
  CHECK(samples[0] == 26880 && samples[1] == -15360);
  decoder.reset();
  decoder.decode(block, samples);
  CHECK(std::all_of(samples.begin(), samples.end(), [](s16 s) { return s == 0; }));
  // Reserved predictors use zero prediction; the lower nibble still scales the sample.
  block[0] = 0xf0;
  block[1] = 0xff;
  std::fill(block.begin() + 4, block.end(), u8(0x87));
  decoder.decode(block, samples);
  CHECK(samples[0] == 28672 && samples[1] == -1);
}

void stream_timing() {
  for (u64 clock : {486000000ull, 729000000ull}) {
    for (u32 divisor : {2248u, 2250u, 3372u, 3375u}) {
      const unsigned maximum = divisor >= 3000 ? 4 : 6;
      audio::DiscStream stream(clock, divisor);
      const u64 initial = clock * 6 * 28 * divisor / 108000000;
      const u64 period = clock * maximum * 28 * divisor / 108000000;
      unsigned reads = 0, packets = 0;
      std::vector<u64> offsets;
      std::vector<s16> pcm;
      stream.read = [&](u64 offset, std::span<u8> out) {
        ++reads;
        offsets.push_back(offset);
        CHECK(out.size() == maximum * 32);
        std::fill(out.begin(), out.end(), u8(0));
        for (unsigned block = 0; block < maximum; ++block)
          std::fill(out.begin() + block * 32 + 4, out.begin() + (block + 1) * 32, u8(0x21));
      };
      stream.samples = [&](std::span<const s16> samples, u32 rate) {
        CHECK(rate == divisor);
        ++packets;
        pcm.assign(samples.begin(), samples.end());
      };
      u32 value = 99;
      CHECK(stream.command(0, 0x8000, 0x1000) == 0x52001);
      CHECK(stream.status(0, value) == 0x52001 && value == 99);
      stream.configure(true, 10);
      CHECK(stream.command(7, 0, 0) == 0x52401);
      CHECK(stream.status(7, value) == 0x52401 && value == 99);
      CHECK(stream.command(0, 0x8000, 0x1000) == 0);
      CHECK(stream.status(0, value) == 0 && value == 1);
      stream.advance(initial - 1, divisor, true);
      CHECK(reads == 0 && packets == 0 && stream.next_event() == 1);
      stream.advance(1, divisor, true);
      CHECK(reads == 1 && packets == 1 && pcm.size() == 336);
      CHECK(std::all_of(pcm.begin(), pcm.end(), [](s16 s) { return s == 0; }));
      CHECK(offsets[0] == 0x8000 && stream.next_event() == period);
      stream.advance(period, divisor, true);
      CHECK(reads == 2 && packets == 2 && pcm.size() == maximum * 56);
      for (unsigned n = 0; n < maximum * 28; ++n)
        CHECK(pcm[n * 2] == 4096 && pcm[n * 2 + 1] == 8192);
      CHECK(offsets[1] == 0x8000 + maximum * 32);
      // An AI pause delivers queued audio once, then silence without moving the disc head.
      stream.advance(period * 2, divisor, false);
      CHECK(reads == 2 && packets == 4);
      CHECK(std::all_of(pcm.begin(), pcm.end(), [](s16 s) { return s == 0; }));
      stream.advance(period, divisor, true);
      CHECK(reads == 3 && packets == 5);
      CHECK(offsets[2] == 0x8000 + maximum * 64);
      CHECK_THROWS(stream.advance(1, 0, true));
      CHECK(stream.command(1, 0, 0) == 0);
      CHECK(stream.status(0, value) == 0 && value == 0);
    }
  }
}

void track_boundaries() {
  constexpr u64 clock = 486000000;
  constexpr u32 divisor = 2248;
  constexpr u64 period = clock * 6 * 28 * divisor / 108000000;
  audio::DiscStream stream(clock, divisor);
  std::vector<std::pair<u64, std::size_t>> reads;
  std::vector<std::vector<s16>> packets;
  stream.read = [&](u64 address, std::span<u8> bytes) {
    reads.emplace_back(address, bytes.size());
    std::fill(bytes.begin(), bytes.end(), u8(0));
    for (std::size_t n = 0; n < bytes.size(); n += 32) {
      bytes[n] = 0x10;
      std::fill(bytes.begin() + n + 4, bytes.begin() + n + 32, u8(0x11));
    }
  };
  stream.samples = [&](std::span<const s16> samples, u32) {
    packets.emplace_back(samples.begin(), samples.end());
  };
  stream.configure(true, 10);
  CHECK(stream.command(0, 0x8000, 192) == 0);
  stream.advance(period, divisor, true);
  CHECK(stream.command(0, 0x10000, 384) == 0);
  stream.advance(period, divisor, true);
  CHECK(reads.size() == 2 && reads[0].first == 0x8000 && reads[1].first == 0x80c0);
  u32 status = 0;
  CHECK(stream.status(2, status) == 0 && status == 0x4000);
  CHECK(stream.status(3, status) == 0 && status == 384);
  CHECK(stream.status(1, status) == 0 && status == 0x4000);
  // Scheduling changes tracks before the queued contiguous chunk is decoded.
  stream.advance(period, divisor, true);
  CHECK(reads[2].first == 0x100c0);
  CHECK(packets[1][0] == 4096 && packets[2][0] == 4096);
  CHECK(stream.command(0, 0, 0) == 0);
  CHECK(stream.command(0, 0x18000, 192) == 0); // ignored while stop-at-end is armed
  stream.advance(period, divisor, true);
  CHECK(stream.status(0, status) == 0 && status == 0);
  CHECK(reads.size() == 3);
  CHECK(stream.status(2, status) == 0 && status == 0x4000);
  // An immediate empty callback preserves liveness at an exact track boundary.
  CHECK(stream.next_event() == period);
  stream.reset();
  CHECK(stream.status(0, status) == 0x52001);
}

void read_failure() {
  audio::DiscStream stream(486000000, 2248);
  const u64 period = stream.next_event();
  unsigned reads = 0, packets = 0;
  stream.configure(true, 10);
  CHECK(stream.command(0, 0x8000, 1920) == 0);
  stream.read = [&](u64, std::span<u8> bytes) {
    if (++reads == 1)
      throw std::out_of_range("Authored disc read failure");
    std::fill(bytes.begin(), bytes.end(), u8(0));
  };
  stream.samples = [&](std::span<const s16>, u32) { ++packets; };
  stream.advance(period, 2248, true);
  CHECK(reads == 1 && packets == 1);
  stream.advance(period, 2248, true);
  CHECK(reads == 1 && packets == 1);
  stream.advance(period, 2248, true);
  CHECK(reads == 2 && packets == 2);
}

void stream_drive_errors() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    hw::Devices devices(console);
    const u32 divisor = console == Console::Wii ? 2250 : 2248;
    const u64 period = devices.ticks_per_second() * 6 * 28 * divisor / 108000000;
    devices.disc_read = [](u64, std::span<u8>) {
      throw std::out_of_range("Authored streaming disc read failure");
    };
    devices.prepare_disc_boot(true, 10);
    // Invalid stream commands consume the configuration period once enabled.
    CHECK(devices.disc_immediate(0xe1050000).error == 0x52401);
    CHECK(devices.disc_immediate(0xe401000a).error == 0x52402);
    devices.prepare_disc_boot(true, 10);
    CHECK(!devices.disc_immediate(0xe1000000, 0x2000, 1920).failed);
    devices.write(ai, 3, 4);
    devices.advance(period);
    CHECK(devices.disc_immediate(0xe0000000).immediate == 0);
    devices.advance(period);
    CHECK(devices.disc_immediate(0xe0000000).immediate == 0x31100);
    CHECK(devices.disc_immediate(0xe0000000).immediate == 0);
    // Streaming completion does not change the normal DI transfer interrupt bits.
    CHECK((devices.read(di, 4) & 0x14) == 0);
    devices.advance(period);
    devices.reset_disc(true);
    devices.advance(period);
    CHECK(devices.disc_immediate(0xe0000000).immediate == 0x05031100);
  }
}

void split_and_rate_changes() {
  for (u64 clock : {486000000ull, 729000000ull}) {
    audio::DiscStream bulk(clock, 2248), split(clock, 2248);
    std::array<std::vector<u64>, 2> reads;
    std::array<std::vector<s16>, 2> pcm;
    for (unsigned which = 0; which < 2; ++which) {
      auto &stream = which == 0 ? bulk : split;
      stream.configure(true, 10);
      CHECK(stream.command(0, 0x8000, 1920) == 0);
      stream.read = [&, which](u64 offset, std::span<u8> bytes) {
        reads[which].push_back(offset);
        for (std::size_t n = 0; n < bytes.size(); ++n)
          bytes[n] = u8((offset + n) * 7);
      };
      stream.samples = [&, which](std::span<const s16> samples, u32) {
        pcm[which].insert(pcm[which].end(), samples.begin(), samples.end());
      };
    }
    constexpr std::array<u32, 8> rates{2248, 3372, 2250, 3375, 2248, 2250, 3372, 3375};
    for (unsigned interval = 0; interval < rates.size(); ++interval) {
      const u64 amount = clock / 100 + interval * 71;
      bulk.advance(amount, rates[interval], interval % 3 != 1);
      u64 remaining = amount;
      while (remaining) {
        const u64 piece = std::min<u64>(remaining, 7777);
        split.advance(piece, rates[interval], interval % 3 != 1);
        remaining -= piece;
      }
      CHECK(reads[0] == reads[1] && pcm[0] == pcm[1]);
      CHECK(bulk.next_event() == split.next_event());
    }
  }
}

void device_commands() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    hw::Devices devices(console);
    const u64 delay = 300 * (devices.ticks_per_second() / 1000000);
    devices.disc_read = [](u64, std::span<u8> bytes) {
      std::fill(bytes.begin(), bytes.end(), u8(0));
    };
    devices.dma_write = [](u32, std::span<const u8>) {};
    auto issue = [&](u32 command, u32 start = 0, u32 length = 0) {
      devices.write(di, 0x54, 4);
      devices.write(di + 8, command, 4);
      devices.write(di + 12, start, 4);
      devices.write(di + 16, length, 4);
      devices.write(di + 28, 1, 4);
      CHECK(devices.read(di + 28, 4) & 1);
      devices.advance(delay);
      CHECK((devices.read(di + 28, 4) & 1) == 0);
      return devices.read(di + 32, 4);
    };
    auto error = [&] { return issue(0xe0000000); };
    issue(0xe1000000, 0x2000, 1920);
    CHECK((devices.read(di, 4) & 4) != 0 && error() == 0x52001);
    issue(0xe401000a);
    CHECK(devices.read(di, 4) & 16);
    issue(0xe1000000, 0x2000, 1920);
    CHECK(devices.read(di, 4) & 16);
    CHECK(issue(0xe2000000) == 1);
    CHECK(issue(0xe2020000) == 0x2000);
    CHECK(issue(0xe2030000) == 1920);
    issue(0xe401000a);
    CHECK(error() == 0x52402);
    issue(0xe2050000);
    CHECK(error() == 0x52401);
    issue(0xab000000, 0x12345);
    CHECK(devices.read(di, 4) & 16);
    issue(0xe1010000);
    CHECK(issue(0xe2000000) == 0);
    devices.reset_disc(true);
    issue(0xe401000a);
    CHECK(error() == 0x05020401);
    devices.write(di + 24, 32, 4);
    issue(0xa8000040);
    issue(0xe401000a);
    CHECK(devices.read(di, 4) & 16);
    issue(0xa8000040);
    issue(0xe401000a);
    CHECK(error() == 0x52402);
    devices.reset_disc(false);
    issue(0xe2000000);
    CHECK(error() == 0x02062800);
    devices.prepare_disc_boot(true, 0);
    issue(0xe1000000, 0x2000, 1920);
    CHECK(devices.read(di, 4) & 16);
    issue(0xe3000000);
    issue(0xe2000000);
    CHECK(error() == 0x04020400);
  }
}

void guest_stream() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    runtime::Machine machine(console);
    platform::Output output({true, false, {}, {}});
    machine.set_output(&output);
    machine.devices().disc_read = [](u64 position, std::span<u8> bytes) {
      CHECK(position >= 0x8000 && bytes.size() == 192);
      std::fill(bytes.begin(), bytes.end(), u8(0));
      for (unsigned block = 0; block < 6; ++block)
        std::fill(bytes.begin() + block * 32 + 4, bytes.begin() + block * 32 + 32, u8(0x21));
    };
    machine.devices().prepare_disc_boot(true, 10);
    std::vector<u32> program;
    auto store = [&](u32 address, u32 value) {
      address |= 0xc0000000;
      program.push_back(0x3c600000 | (address >> 16));
      program.push_back(0x60630000 | (address & 65535));
      program.push_back(0x3c800000 | (value >> 16));
      program.push_back(0x60840000 | (value & 65535));
      program.push_back(0x90830000);
    };
    store(ai + 4, 0x80ff);
    store(ai, 3);
    store(di + 8, 0xe1000000);
    store(di + 12, 0x2000);
    store(di + 16, 19200);
    store(di + 28, 1);
    for (std::size_t i = 0; i < program.size(); ++i)
      machine.memory().write32(0x1000 + u32(i) * 4, program[i]);
    machine.memory().write32(0x1000 + u32(program.size()) * 4, 0x48000000);
    machine.cpu().configure_boot();
    machine.cpu().state().pc = 0x80001000;
    machine.set_limits({10000000, 0, 0});
    for (std::size_t i = 0; i < program.size(); ++i)
      machine.step();
    const u64 period = machine.devices().ticks_per_second() * 6 * 28 *
                       (console == Console::Wii ? 2250u : 2248u) / 108000000;
    machine.devices().advance(period * 2);
    const auto stats = output.disc_audio_stats();
    CHECK(stats.samples == 672 && stats.nonzero_samples == 336 && stats.peak == 8192);
    CHECK(output.audio_samples() == 0);
    CHECK(machine.cpu().state().spr[cpu::spr::dar] == 0);
  }
}
} // namespace

int main() {
  return run_tests([] {
    decoder();
    stream_timing();
    track_boundaries();
    read_failure();
    stream_drive_errors();
    split_and_rate_changes();
    device_commands();
    guest_stream();
  });
}
