// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/interpreter.hpp"
#include "cupid/dsp/engine.hpp"
#include "cupid/hw/devices.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace cupid;

void send(dsp::Engine &engine, u32 value) {
  engine.write16(0, static_cast<u16>(value >> 16));
  engine.write16(2, static_cast<u16>(value));
  CHECK((engine.read16(0) & 0x8000) == 0);
  CHECK(engine.read16(2) == static_cast<u16>(value));
}

u32 receive(dsp::Engine &engine) {
  return (u32(engine.read16(4)) << 16) | engine.read16(6);
}

void parameter(dsp::Engine &engine, u32 command, u32 value) {
  send(engine, command);
  send(engine, value);
}

std::array<u8, 32> synthetic_hash_fixture(u32 hash) {
  std::array<u8, 32> bytes{};
  for (unsigned bit = 0; bit < 32; ++bit) {
    if (!(hash & (1u << bit)))
      continue;
    const unsigned distance = (bit * 11) % 32;
    bytes[distance == 0 ? 0 : 32 - distance] ^= 1;
  }
  return bytes;
}

void mail_tests() {
  dsp::MailQueue queue;
  unsigned interrupts = 0;
  queue.interrupt = [&] { ++interrupts; };
  queue.push(0x81234567, true);
  CHECK(interrupts == 1 && queue.pending());
  CHECK(queue.read_high() == 0x8123 && queue.read_high() == 0x8123);
  queue.push(0x89abcdef, true);
  CHECK(interrupts == 1);
  CHECK(queue.read_low() == 0x4567 && interrupts == 2);
  queue.halt(true);
  CHECK(queue.read_high() == 0x0123 && queue.read_low() == 0x4567 && queue.pending());
  queue.halt(false);
  CHECK(queue.read_high() == 0x89ab && queue.read_low() == 0xcdef);
  CHECK(queue.read_high() == 0x09ab && !queue.pending());
  queue.push(0x80000001);
  queue.clear();
  CHECK(!queue.pending() && queue.read_high() == 0x09ab && queue.read_low() == 0xcdef);
}

void bootstrap_tests(Console console) {
  Memory memory(console);
  dsp::Engine engine(memory, console);
  CHECK(engine.read16(0x0a) == 0x804 && receive(engine) == 0);
  engine.write16(0x0a, 0x800);
  CHECK(receive(engine) == 0x8071feed);
  CHECK(engine.read16(4) == 0x0071);
  send(engine, 0x12345678);
  CHECK(receive(engine) == 0xfeee5678);
  engine.write16(0x0a, 0x8ad);
  CHECK((engine.read16(0x0a) & 0xc07) == 0x804);
  CHECK(receive(engine) == 0x7eee5678);
  engine.write16(0x0a, 0x800);
  CHECK(receive(engine) == 0x8071feed);
  engine.write16(0x0a, 0);
  CHECK(engine.program_hash() == 1 && engine.read16(0x0a) == 0x400);
  CHECK(receive(engine) == 0x80544348);
  engine.advance(1559);
  CHECK((engine.read16(0x0a) & 0x400) != 0);
  engine.advance(1);
  CHECK((engine.read16(0x0a) & 0x400) == 0);
  send(engine, 0xaabbccdd);
  CHECK(engine.read16(4) == 0x0054);
  CHECK_THROWS(engine.write16(4, 1));
  CHECK_THROWS(engine.read16(8));
  engine.write16(0x0a, 1);
  CHECK(engine.program_hash() == 0 && receive(engine) == 0x8071feed);
}

void upload_tests(Console console) {
  Memory memory(console);
  dsp::Engine engine(memory, console);
  engine.write16(0x0a, 0x800);
  CHECK(receive(engine) == 0x8071feed);
  const auto bytes = synthetic_hash_fixture(0x65d6cc6f);
  CHECK(dsp::firmware_hash(bytes) == 0x65d6cc6f);
  memory.copy_in(0x2000, bytes);
  parameter(engine, 0x80f3a001, 0x80002000);
  parameter(engine, 0x80f3a002, 0xaaaa0020);
  parameter(engine, 0x80f3c002, 0xbbbb0123);
  parameter(engine, 0x80f3b002, 0xcccc0456);
  parameter(engine, 0x80f3d001, 0xdddd0100);
  CHECK(engine.program_hash() == 0x65d6cc6f);
  unsigned interrupts = 0;
  engine.interrupt = [&] { ++interrupts; };
  const u64 period = console == Console::Wii ? 729000 : 486000;
  engine.advance(period - 1);
  CHECK(interrupts == 0);
  engine.advance(1);
  CHECK(interrupts == 1);
  CHECK(receive(engine) == 0xdcd10000);
  engine.advance(period);
  CHECK(interrupts == 1);
  send(engine, 0xff000000);
  CHECK(engine.program_hash() == 0 && receive(engine) == 0x8071feed);
  memory.copy_in(0x2000, synthetic_hash_fixture(0x12345678));
  parameter(engine, 0x80f3a001, 0x2000);
  parameter(engine, 0x80f3a002, 32);
  parameter(engine, 0x80f3c002, 0x100);
  parameter(engine, 0x80f3b002, 0x200);
  send(engine, 0x80f3d001);
  bool failed = false;
  try {
    send(engine, 0x300);
  } catch (const dsp::UnsupportedMicrocode &error) {
    failed = true;
    CHECK(error.hash == 0x12345678 && error.descriptor.source == 0x2000);
    CHECK(error.descriptor.length == 32 && error.descriptor.entry == 0x300);
    CHECK(std::string(error.what()).find("hash=0x12345678") != std::string::npos);
  }
  CHECK(failed && engine.program_hash() == 0);
}

void mmio_tests() {
  Memory memory(Console::GameCube);
  dsp::Engine engine(memory, Console::GameCube);
  hw::Devices devices(Console::GameCube);
  devices.dsp_read = [&](u32 offset) { return engine.read16(offset); };
  devices.dsp_write = [&](u32 offset, u16 value) { engine.write16(offset, value); };
  engine.interrupt = [&] { devices.dsp_interrupt(); };
  devices.write(0x0c00500a, 0x8ac, 2);
  CHECK((devices.read(0x0c00500a, 2) & 0xc07) == 0x804);
  devices.write(0x0c00500a, 0x800, 2);
  CHECK(devices.read(0x0c005004, 2) == 0x8071);
  CHECK(devices.read(0x0c005006, 2) == 0xfeed);
  devices.write(0x0c00500a, 0x100, 2);
  CHECK((devices.read(0x0c00500a, 2) & 0x400) != 0);
  engine.advance(1560);
  CHECK((devices.read(0x0c00500a, 2) & 0x400) == 0);
  devices.dsp_interrupt();
  CHECK((devices.read(0x0c00500a, 2) & 0x80) != 0);
  devices.write(0x0c00500a, 0x180, 2);
  CHECK((devices.read(0x0c00500a, 2) & 0x80) == 0);
}

void advance_timing_tests(Console console) {
  Memory memory(console);
  dsp::Engine engine(memory, console);
  const u64 period = console == Console::Wii ? 729000 : 486000;
  engine.write16(0x0a, 0x800);
  CHECK(receive(engine) == 0x8071feed);
  engine.advance(period - 1);
  memory.copy_in(0x2000, synthetic_hash_fixture(0x65d6cc6f));
  parameter(engine, 0x80f3a001, 0x2000);
  parameter(engine, 0x80f3a002, 32);
  parameter(engine, 0x80f3d001, 0);
  unsigned interrupts = 0;
  engine.interrupt = [&] { ++interrupts; };
  engine.advance(0);
  CHECK(interrupts == 0);
  engine.advance(1);
  CHECK(interrupts == 1);
  engine.advance(3 * period + period / 2);
  CHECK(interrupts == 2);
  engine.advance(period / 2 - 1);
  CHECK(interrupts == 2);
  engine.advance(1);
  CHECK(interrupts == 3);

  // A callback failure consumes the elapsed time and retains the next boundary.
  engine.interrupt = [&] {
    ++interrupts;
    CHECK(engine.read16(0x0a) == 0x800);
    throw std::runtime_error("Authored interrupt callback failure");
  };
  CHECK_THROWS(engine.advance(period));
  CHECK(interrupts == 4);
  engine.interrupt = [&] { ++interrupts; };
  engine.advance(period - 1);
  CHECK(interrupts == 4);
  engine.advance(1);
  CHECK(interrupts == 5);
  CHECK(receive(engine) == 0xdcd10000);
  engine.advance(period);
  CHECK(interrupts == 5);

  engine.write16(0x0a, 0);
  engine.advance(1559);
  CHECK((engine.read16(0x0a) & 0x400) != 0);
  engine.advance(1);
  CHECK((engine.read16(0x0a) & 0x400) == 0);
  engine.advance(std::numeric_limits<u64>::max() - (8 * period + 1560));
  CHECK(engine.program_hash() == 1 && engine.read16(0x0a) == 0);
  CHECK_THROWS(engine.advance(1));
  CHECK(engine.program_hash() == 1 && engine.read16(0x0a) == 0 && interrupts == 5);
  engine.advance(0);
  CHECK(interrupts == 5);
}

struct AudioFixture {
  Memory memory;
  dsp::Engine engine;
  std::vector<u8> aram = std::vector<u8>(4096);
  Console console;
  unsigned interrupts = 0;
  bool light;

  explicit AudioFixture(Console mode, u32 hash = 0)
      : memory(mode), engine(memory, mode), console(mode), light(hash == 0x24b22038) {
    if (!hash)
      hash = mode == Console::GameCube ? 0x56d36052 : 0xd643001f;
    engine.interrupt = [&] { ++interrupts; };
    engine.aram_read = [&](u32 offset, std::span<u8> output) {
      if (offset > aram.size() || output.size() > aram.size() - offset)
        throw std::runtime_error("Synthetic ARAM read is out of range");
      std::copy_n(aram.begin() + offset, output.size(), output.begin());
    };
    engine.write16(0x0a, 0x800);
    CHECK(receive(engine) == 0x8071feed);
    memory.copy_in(0x2000, synthetic_hash_fixture(hash));
    parameter(engine, 0x80f3a001, 0x80002000);
    parameter(engine, 0x80f3a002, 32);
    parameter(engine, 0x80f3d001, 0);
    CHECK(engine.program_hash() == hash);
    if (light) {
      CHECK(receive(engine) == 0x88881111);
    } else {
      CHECK(receive(engine) == 0xdcd10000);
      CHECK(receive(engine) == 0xf3551111);
      CHECK(interrupts == 1);
    }
    for (u32 phase = 0; phase < 64; ++phase)
      memory.write16(0x3000 + phase * 8 + 6, 0x7fff);
  }

  void packet(std::initializer_list<u32> values) {
    if (!light)
      send(engine, static_cast<u32>(values.size()));
    for (const u32 value : values)
      send(engine, value);
  }

  void ack(u16 sync) {
    if (light) {
      CHECK(receive(engine) == (0x80000000 | (2 * ((sync >> 8) & 127) + 0x62)));
    } else {
      CHECK(receive(engine) == 0xdcd10004);
      CHECK(receive(engine) == (0xf3550000 | sync));
    }
  }

  void configure(unsigned count = 1, u32 reverb = 0) {
    packet({0x81000000 | count, 0x4000, 0x3000, 0x3800, reverb});
    ack(0x8100);
    if (console == Console::Wii) {
      packet({0x8e000000, 0x80008000});
      ack(0x8e00);
    }
  }

  void word(unsigned index, u16 value, unsigned voice = 0) {
    memory.write16(0x4000 + voice * (light ? 0x100 : 0x180) + index * 2, value);
  }

  u16 word(unsigned index) {
    return memory.read16(0x4000 + index * 2);
  }

  void voice(bool constant = true, unsigned type = 16) {
    word(0, 1);
    word(2, 0x1000);
    word(4, 1);
    word(6, constant);
    word(8, 0x0d00);
    word(9, 0x7fff);
    word(10, 0x7fff);
    word(12, 0x0d60);
    word(13, 0x4000);
    word(14, 0x4000);
    if (light) {
      word(0x1b, 12000);
      word(0x40, static_cast<u16>(type));
    } else {
      word(0x33, 12000);
      word(0x80, static_cast<u16>(type));
    }
  }

  void start() {
    if (console == Console::Wii)
      packet({0x82011000, 0x5000, 0x6000, 0, 0});
    else
      packet({0x82011000, 0x5000, 0x6000});
    if (light)
      ack(1);
  }

  void synchronize(u16 mask = 0x8000) {
    send(engine, 0);
    if (!light) {
      if (console == Console::GameCube) {
        send(engine, u32(mask) << 16);
        send(engine, 0);
      } else {
        send(engine, mask);
      }
      ack(0xff00);
      CHECK(receive(engine) == 0xdcd10005);
    }
  }

  void sample_bytes(std::span<const u8> bytes) {
    if (console == Console::Wii)
      memory.copy_in(0x8000, bytes);
    else
      std::copy(bytes.begin(), bytes.end(), aram.begin());
  }
};

void audio_constant_tests(Console console) {
  AudioFixture fixture(console);
  fixture.configure();
  fixture.voice();
  fixture.memory.write16(0x5000, 0x1234);
  fixture.start();
  CHECK(fixture.memory.read16(0x5000) == 0x1234);
  if (console == Console::GameCube) {
    send(fixture.engine, 0);
    send(fixture.engine, 0x80000000);
    CHECK(fixture.memory.read16(0x5000) == 0x1234);
    send(fixture.engine, 0);
    fixture.ack(0xff00);
    CHECK(receive(fixture.engine) == 0xdcd10005);
  } else {
    fixture.synchronize();
  }
  for (unsigned i = 0; i < 80; ++i) {
    CHECK(fixture.memory.read16(0x5000 + i * 2) == 5999);
    CHECK(fixture.memory.read16(0x6000 + i * 2) == 3000);
  }
  CHECK(fixture.word(4) == 1 && fixture.word(10) == 0x7fff);
  fixture.word(0x33, static_cast<u16>(-12000));
  if (console == Console::Wii)
    fixture.packet({0x82011000, 0x5000, 0x6000, 0, 0});
  else
    fixture.packet({0x82011000, 0x5000, 0x6000});
  // New commands wait until the CPU continues the completed task.
  CHECK(fixture.memory.read16(0x5000) == 5999);
  send(fixture.engine, 0xcdd10003);
  fixture.synchronize(0);
  CHECK(fixture.memory.read16(0x5000) == 0);
  send(fixture.engine, 0xcdd10003);
  fixture.start();
  fixture.synchronize();
  CHECK(fixture.memory.read16(0x5000) == static_cast<u16>(-6000));
}

void audio_pcm_tests(Console console, bool sixteen_bit) {
  AudioFixture fixture(console);
  fixture.configure();
  fixture.voice(false, sixteen_bit ? 16 : 8);
  fixture.word(0x81, 1);
  fixture.word(0x89, 2);
  fixture.word(0x8b, 5);
  const std::array<s16, 5> source{1000, -2000, 3000, -4000, 5000};
  std::vector<u8> bytes(sixteen_bit ? 10 : 5);
  for (unsigned i = 0; i < 5; ++i) {
    if (sixteen_bit) {
      bytes[2 * i] = static_cast<u8>(static_cast<u16>(source[i]) >> 8);
      bytes[2 * i + 1] = static_cast<u8>(source[i]);
    } else {
      bytes[i] = static_cast<u8>(i == 0 ? 1 : i == 1 ? -2 : i == 2 ? 3 : i == 3 ? -4 : 5);
    }
  }
  fixture.sample_bytes(bytes);
  fixture.start();
  fixture.synchronize();
  CHECK(fixture.memory.read16(0x5000) == 0);
  for (unsigned i = 1; i < 80; ++i) {
    const unsigned position = i - 1 < 5 ? i - 1 : 2 + (i - 6) % 3;
    const s16 raw = sixteen_bit ? source[position] : std::bit_cast<s8>(bytes[position]) * 256;
    const s32 resampled = s32(raw) * 32767 >> 15;
    const s16 expected = static_cast<s16>(resampled * 32767 >> 16);
    CHECK(fixture.memory.read16(0x5000 + i * 2) == static_cast<u16>(expected));
  }
  CHECK(fixture.word(1) == 0 && fixture.word(4) == 0);
  CHECK(fixture.word(0x3b) == 0 && fixture.word(0x39) == (sixteen_bit ? 10 : 5));
}

void audio_afc_tests(Console console, bool high_quality) {
  AudioFixture fixture(console);
  fixture.configure();
  fixture.voice(false, high_quality ? 9 : 5);
  fixture.word(0x8b, 80);
  const unsigned width = high_quality ? 9 : 5;
  std::vector<u8> encoded(width * 5);
  for (unsigned block = 0; block < 5; ++block) {
    encoded[block * width] = 0x30;
    std::fill_n(encoded.begin() + block * width + 1, width - 1,
                static_cast<u8>(high_quality ? 0x1f : 0x6c));
  }
  fixture.sample_bytes(encoded);
  fixture.start();
  fixture.synchronize();
  for (unsigned i = 0; i < 80; ++i) {
    const std::array<s16, 4> low{32, -64, -32, 0};
    const s16 raw = !i ? 0 : high_quality ? ((i - 1) % 2 ? -8 : 8) : low[(i - 1) % 4];
    const s32 resampled = s32(raw) * 32767 >> 15;
    CHECK(fixture.memory.read16(0x5000 + i * 2) == static_cast<u16>(resampled * 32767 >> 16));
  }
  CHECK(fixture.word(0x39) == width * 5 && fixture.word(0x3b) == 0);
  CHECK(fixture.word(0x66) == (high_quality ? 8 : static_cast<u16>(-32)));
  CHECK(fixture.word(0x67) == (high_quality ? static_cast<u16>(-8) : 0));
}

void light_audio_tests() {
  AudioFixture fixture(Console::GameCube, 0x24b22038);
  fixture.configure();
  fixture.voice();
  // Small voice blocks provide the ramp step directly.
  fixture.word(9, 0);
  fixture.word(13, 0);
  fixture.start();
  const unsigned interrupts = fixture.interrupts;
  fixture.synchronize();
  CHECK(fixture.interrupts == interrupts + 1);
  CHECK(fixture.memory.read16(0x5000) == 5999);
  CHECK(fixture.word(10) == 0x7fff && fixture.word(0x1b) == 12000);
  fixture.packet({0x83000000});
  CHECK((fixture.engine.read16(4) & 0x8000) == 0);
}

void audio_failure_tests() {
  AudioFixture fixture(Console::GameCube);
  CHECK_THROWS(send(fixture.engine, 64));
  fixture.configure();
  fixture.voice(false, 2);
  fixture.start();
  send(fixture.engine, 0);
  send(fixture.engine, 0x80000000);
  CHECK_THROWS(send(fixture.engine, 0));
  AudioFixture bad(Console::GameCube);
  bad.configure();
  bad.voice(false);
  bad.word(0x81, 1);
  bad.start();
  send(bad.engine, 0);
  send(bad.engine, 0x80000000);
  CHECK_THROWS(send(bad.engine, 0));
  AudioFixture missing(Console::GameCube);
  CHECK_THROWS(missing.packet({0x81000001, 0x4000}));
  AudioFixture bounds(Console::Wii);
  bounds.configure();
  CHECK_THROWS(bounds.packet({0x82011000, 0x017ffff0, 0x6000, 0, 0}));
}

void audio_resume_tests() {
  AudioFixture fixture(Console::GameCube);
  fixture.configure();
  fixture.voice();
  auto swap = [&](u32 hash) {
    fixture.memory.copy_in(0x2100, synthetic_hash_fixture(hash));
    send(fixture.engine, 0xcdd10001);
    for (u32 mail : std::array<u32, 10>{0, 0, 0, 0x80002100, 32, 0, 0, 0, 0, 0})
      send(fixture.engine, mail);
    CHECK(fixture.engine.program_hash() == hash);
  };
  swap(0x86840740);
  CHECK(receive(fixture.engine) == 0xdcd10000);
  CHECK(receive(fixture.engine) == 0xf3551111);
  swap(0x56d36052);
  CHECK((fixture.engine.read16(4) & 0x8000) == 0);
  fixture.engine.advance(486000);
  CHECK(receive(fixture.engine) == 0xdcd10001);
  fixture.start();
  fixture.synchronize();
  CHECK(fixture.memory.read16(0x5000) == 5999);
}

void audio_filter_reverb_tests() {
  AudioFixture fixture(Console::GameCube);
  fixture.configure(1, 0x7000);
  fixture.voice();
  // First reverb channel: pre-filter, a one-frame ring and a front-left destination.
  fixture.memory.write16(0x7000, 1);
  fixture.memory.write16(0x7002, 1);
  fixture.memory.write32(0x7004, 0x7200);
  fixture.memory.write16(0x7008, 0x0d00);
  fixture.memory.write16(0x700a, 0x7fff);
  fixture.memory.write16(0x701e, 0x7fff);
  for (unsigned i = 0; i < 80; ++i)
    fixture.memory.write16(0x7200 + i * 2, 1000);
  fixture.word(0xa8, 128);
  fixture.start();
  fixture.synchronize();
  CHECK(fixture.memory.read16(0x5000) == 5999);
  CHECK(fixture.memory.read16(0x5002) == 6997);
  CHECK(fixture.word(0x68) == 12000 && fixture.word(0x69) == 12000);
  CHECK(fixture.memory.read16(0x7200) == 0 && fixture.memory.read16(0x7202) == 999);
  AudioFixture broken(Console::GameCube);
  broken.configure(0, 0x7000);
  broken.memory.write16(0x7000, 1);
  CHECK_THROWS(broken.start());
}

void audio_end_tests() {
  AudioFixture fixture(Console::GameCube);
  fixture.configure();
  fixture.voice(false, 9);
  fixture.word(0x8b, 96);
  std::array<u8, 54> encoded{};
  fixture.sample_bytes(encoded);
  fixture.start();
  fixture.synchronize();
  CHECK(fixture.word(0x3b) == 16 && fixture.word(1) == 0);
  send(fixture.engine, 0xcdd10003);
  fixture.start();
  fixture.synchronize();
  CHECK(fixture.word(1) == 1 && fixture.word(0x3b) == 16 && fixture.word(0x39) == 54);
  for (unsigned i = 0; i < 80; ++i)
    CHECK(fixture.memory.read16(0x5000 + i * 2) == 0);
  AudioFixture truncated(Console::GameCube);
  truncated.configure();
  truncated.voice(false, 33);
  truncated.word(0x3b, 16);
  truncated.word(0x8d, 0x8000);
  truncated.start();
  send(truncated.engine, 0);
  send(truncated.engine, 0x80000000);
  CHECK_THROWS(send(truncated.engine, 0));
}

void audio_position_tests() {
  AudioFixture fixture(Console::Wii);
  fixture.configure();
  fixture.voice();
  for (u32 i = 0; i < 128; ++i)
    fixture.memory.write16(0x3400 + i * 2, 0x7fff);
  fixture.configure();
  fixture.word(0x2c, 1);
  fixture.word(0x2a, 0x7fff);
  fixture.word(0x2b, 0x7fff);
  fixture.start();
  fixture.synchronize();
  CHECK(fixture.memory.read16(0x5000) == 5999);
  CHECK(fixture.memory.read16(0x6000) == 5999);
  CHECK(fixture.word(0x2a) == 0x7fff);
}

void audio_requested_history_tests(Console console, bool light) {
  for (u16 requested : {u16(1), u16(20), u16(80), u16(0xffff)}) {
    for (bool constant : {false, true}) {
      const u32 hash = light ? 0x24b22038 : 0;
      AudioFixture baseline(console, hash);
      AudioFixture flagged(console, hash);
      for (auto *fixture : {&baseline, &flagged}) {
        fixture->configure();
        fixture->voice(constant, 16);
        fixture->word(light ? 0x41 : 0x81, 1);
        fixture->word(light ? 0x4b : 0x8b, 128);
        for (unsigned i = 0; i < (light ? 16u : 20u); ++i)
          fixture->word((light ? 0x30 : 0x40) + i, static_cast<u16>(0x9000 + i));
        std::array<u8, 256> source{};
        for (unsigned i = 0; i < 128; ++i) {
          const u16 sample = static_cast<u16>(s32(i) * 257 - 16000);
          source[2 * i] = static_cast<u8>(sample >> 8);
          source[2 * i + 1] = static_cast<u8>(sample);
        }
        fixture->sample_bytes(source);
      }
      flagged.word(7, requested);
      for (unsigned frame = 0; frame < 2; ++frame) {
        if (frame && !light) {
          send(baseline.engine, 0xcdd10003);
          send(flagged.engine, 0xcdd10003);
        }
        baseline.start();
        flagged.start();
        baseline.synchronize();
        flagged.synchronize();
        CHECK(flagged.word(7) == requested && baseline.word(7) == 0);
        CHECK(flagged.interrupts == baseline.interrupts);
        // The named compatibility behavior retains the request without adding a transfer.
        // Compare output and all voice/table state, accounting only for that retained field.
        const auto expected = baseline.memory.range(0x3000, 0x6000);
        const auto actual = flagged.memory.range(0x3000, 0x6000);
        for (unsigned i = 0; i < actual.size(); ++i) {
          if (i != 0x100e && i != 0x100f)
            CHECK(actual[i] == expected[i]);
        }
      }
    }
  }
}

void audio_retained_fir_tests(Console console, bool light) {
  for (u16 count : {u16(1), u16(2), u16(20), u16(31)}) {
    for (bool constant : {false, true}) {
      for (unsigned filter = 0; filter < 3; ++filter) {
        AudioFixture baseline(console, light ? 0x24b22038 : 0);
        AudioFixture flagged(console, light ? 0x24b22038 : 0);
        const unsigned read_only = light ? 0x40 : 0x80;
        for (auto *fixture : {&baseline, &flagged}) {
          fixture->configure();
          fixture->voice(constant);
          fixture->word(read_only + 1, 1);
          fixture->word(read_only + 11, 128);
          for (unsigned i = 0; i < 20; ++i) {
            fixture->word(read_only + 0x10 + i, u16(0x1000 + i * 17));
            if (!light)
              fixture->word(0x40 + i, u16(0x9000 + i));
          }
          if (filter == 1)
            fixture->word(read_only + 0x28, 128);
          if (filter == 2) {
            fixture->word(read_only + 4, 32);
            fixture->word(read_only + 0x24, 0x4000);
            fixture->word(read_only + 0x25, 0x1000);
            fixture->word(read_only + 0x26, 0x0800);
            fixture->word(read_only + 0x27, 0x0400);
          }
          std::array<u8, 256> source{};
          for (unsigned i = 0; i < 128; ++i) {
            const u16 sample = u16(s32(i) * 257 - 16000);
            source[2 * i] = u8(sample >> 8);
            source[2 * i + 1] = u8(sample);
          }
          fixture->sample_bytes(source);
        }
        flagged.word(read_only + 4, u16(flagged.word(read_only + 4) | count));
        for (unsigned frame = 0; frame < 2; ++frame) {
          if (frame && !light) {
            send(baseline.engine, 0xcdd10003);
            send(flagged.engine, 0xcdd10003);
          }
          baseline.start();
          flagged.start();
          baseline.synchronize();
          flagged.synchronize();
          CHECK((flagged.word(read_only + 4) & 31) == count);
          CHECK(flagged.interrupts == baseline.interrupts);
          const auto expected = baseline.memory.range(0x3000, 0x6000);
          const auto actual = flagged.memory.range(0x3000, 0x6000);
          for (unsigned i = 0; i < actual.size(); ++i) {
            if (i != 0x1001 + 2 * (read_only + 4))
              CHECK(actual[i] == expected[i]);
          }
          CHECK(std::any_of(actual.begin() + 0x2000, actual.begin() + 0x20a0,
                            [](u8 byte) { return byte != 0; }));
        }
      }
    }
  }
}

void audio_parameter_command_tests(Console console) {
  AudioFixture fixture(console);
  fixture.configure();
  fixture.voice();
  for (u32 parameter : {0u, 1u, 0x80000000u, 0xffffffffu}) {
    const auto state = fixture.memory.range(0x3000, 0x6000);
    const std::vector<u8> before(state.begin(), state.end());
    if (console == Console::Wii)
      fixture.packet({0x8d120001, parameter});
    else
      fixture.packet({0x8d120001});
    fixture.ack(0x8d12);
    CHECK((fixture.engine.read16(4) & 0x8000) == 0);
    CHECK(std::equal(before.begin(), before.end(), state.begin()));
  }
  fixture.start();
  if (console == Console::Wii)
    fixture.packet({0x8d340001, 0x12345678});
  else
    fixture.packet({0x8d340001});
  CHECK((fixture.engine.read16(4) & 0x8000) == 0);
  fixture.synchronize();
  CHECK(fixture.memory.read16(0x5000) == 5999);
  CHECK((fixture.engine.read16(4) & 0x8000) == 0);
  const auto output = fixture.memory.range(0x3000, 0x6000);
  const std::vector<u8> before(output.begin(), output.end());
  send(fixture.engine, 0xcdd10003);
  fixture.ack(0x8d34);
  CHECK((fixture.engine.read16(4) & 0x8000) == 0);
  CHECK(std::equal(before.begin(), before.end(), output.begin()));
  if (console == Console::Wii) {
    AudioFixture truncated(console);
    CHECK_THROWS(truncated.packet({0x8d120001}));
  }
}

void guest_audio_dma_tests(Console console) {
  AudioFixture fixture(console);
  hw::Devices devices(console);
  fixture.engine.aram_read = [&](u32 address, std::span<u8> output) {
    devices.read_aram(address, output);
  };
  fixture.memory.mmio_handles = [&](u32 address) { return devices.handles(address); };
  fixture.memory.mmio_read = [&](u32 address, unsigned width) {
    return devices.read(address, width);
  };
  fixture.memory.mmio_write = [&](u32 address, u32 value, unsigned width) {
    devices.write(address, value, width);
  };
  devices.dsp_read = [&](u32 offset) { return fixture.engine.read16(offset); };
  devices.dsp_write = [&](u32 offset, u16 value) { fixture.engine.write16(offset, value); };
  devices.dma_read = [&](u32 address, std::span<u8> output) {
    fixture.memory.copy_out(address, output);
  };
  devices.dma_write = [&](u32 address, std::span<const u8> input) {
    fixture.memory.copy_in(address, input);
  };
  fixture.configure();
  fixture.voice(false, 16);
  fixture.word(0x8b, 80);
  for (unsigned i = 0; i < 80; ++i)
    fixture.memory.write16((console == Console::Wii ? 0x8000 : 0xa000) + i * 2, 12000);
  if (console == Console::GameCube) {
    devices.write(0x0c005020, 0xa000, 4);
    devices.write(0x0c005024, 0, 4);
    devices.write(0x0c005028, 160, 4);
    devices.advance(246 * 5);
  }
  fixture.start();
  fixture.synchronize();
  std::vector<s16> output;
  devices.audio_samples = [&](std::span<const s16> samples, u32 rate) {
    CHECK(rate == (console == Console::Wii ? 48000u : 48042u));
    output.insert(output.end(), samples.begin(), samples.end());
  };
  // Original guest code interleaves the planar output and programs the audio DMA registers.
  auto instruction = [](u32 op, u32 target, u32 base, u16 immediate) {
    return (op << 26) | (target << 21) | (base << 16) | immediate;
  };
  const std::array<u32, 16> program{instruction(40, 7, 3, 0),     instruction(40, 8, 4, 0),
                                    instruction(44, 7, 5, 0),     instruction(44, 8, 5, 2),
                                    instruction(14, 3, 3, 2),     instruction(14, 4, 4, 2),
                                    instruction(14, 5, 5, 4),     instruction(14, 6, 6, 0xffff),
                                    instruction(11, 0, 6, 0),     0x4082ffdc,
                                    instruction(14, 11, 0, 0),    instruction(36, 11, 12, 0),
                                    instruction(36, 9, 10, 0x30), instruction(14, 9, 0, 0x800a),
                                    instruction(44, 9, 10, 0x36), 0x4e800020};
  for (unsigned i = 0; i < program.size(); ++i)
    fixture.memory.write32(0x1000 + i * 4, program[i]);
  cpu::Interpreter cpu(fixture.memory, console);
  cpu.state().pc = 0x1000;
  cpu.state().gpr[3] = 0x5000;
  cpu.state().gpr[4] = 0x6000;
  cpu.state().gpr[5] = cpu.state().gpr[9] = 0x9000;
  cpu.state().gpr[6] = 80;
  cpu.state().gpr[10] = 0x0c005000;
  cpu.state().gpr[12] = 0x0c006c00;
  for (unsigned i = 0; i < 806; ++i)
    cpu.step();
  CHECK(cpu.state().pc == 0 && output.empty());
  devices.advance(10 * (console == Console::Wii ? 121500 : 80928));
  CHECK(output.size() == 160);
  CHECK(output[0] == 0 && output[1] == 0);
  for (unsigned i = 1; i < 80; ++i)
    CHECK(output[2 * i] == 5999 && output[2 * i + 1] == 2999);
}

} // namespace

int main() {
  return run_tests([] {
    mail_tests();
    bootstrap_tests(Console::GameCube);
    bootstrap_tests(Console::Wii);
    upload_tests(Console::GameCube);
    upload_tests(Console::Wii);
    mmio_tests();
    advance_timing_tests(Console::GameCube);
    advance_timing_tests(Console::Wii);
    audio_constant_tests(Console::GameCube);
    audio_constant_tests(Console::Wii);
    audio_pcm_tests(Console::GameCube, false);
    audio_pcm_tests(Console::GameCube, true);
    audio_pcm_tests(Console::Wii, false);
    audio_pcm_tests(Console::Wii, true);
    audio_afc_tests(Console::GameCube, false);
    audio_afc_tests(Console::GameCube, true);
    audio_afc_tests(Console::Wii, false);
    audio_afc_tests(Console::Wii, true);
    light_audio_tests();
    audio_failure_tests();
    audio_resume_tests();
    audio_filter_reverb_tests();
    audio_end_tests();
    audio_position_tests();
    audio_requested_history_tests(Console::GameCube, false);
    audio_requested_history_tests(Console::Wii, false);
    audio_requested_history_tests(Console::GameCube, true);
    audio_retained_fir_tests(Console::GameCube, false);
    audio_retained_fir_tests(Console::Wii, false);
    audio_retained_fir_tests(Console::GameCube, true);
    audio_parameter_command_tests(Console::GameCube);
    audio_parameter_command_tests(Console::Wii);
    guest_audio_dma_tests(Console::GameCube);
    guest_audio_dma_tests(Console::Wii);
  });
}
