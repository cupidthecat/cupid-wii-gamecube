// SPDX-License-Identifier: GPL-2.0-or-later
#include "buffer_audio_fixture.hpp"
#include "cupid/dsp/buffer_audio.hpp"
#include "test_support.hpp"
#include <tuple>
using namespace cupid;

namespace {
void complete_buffer_frames() {
  constexpr std::array<std::array<u64, 16>, 2> expected{
      {{0xdedb99d818270cb8ull, 0xdedb99d818270cb8ull, 0x7342a33d6b15fd93ull, 0x7342a33d6b15fd93ull,
        0x7342a33d6b15fd93ull, 0x22add03fcbafcb08ull, 0x22add03fcbafcb08ull, 0x22add03fcbafcb08ull,
        0x22add03fcbafcb08ull, 0x7d5fba71586be9ceull, 0xabc1ebede908f48cull, 0x78e7795b8406ed6aull,
        0x78e7795b8406ed6aull, 0x78e7795b8406ed6aull, 0x78e7795b8406ed6aull, 0x78e7795b8406ed6aull},
       {0xe84d1acc8c286650ull, 0xe84d1acc8c286650ull, 0x19a5eb8a973e28daull, 0x19a5eb8a973e28daull,
        0x19a5eb8a973e28daull, 0x8148393f97854196ull, 0x8148393f97854196ull, 0x8148393f97854196ull,
        0x8148393f97854196ull, 0xc98cec76a2170cfeull, 0x8c47dd9c7d6f65bcull, 0xb3ea38c7077739eaull,
        0xb3ea38c7077739eaull, 0xb3ea38c7077739eaull, 0xb3ea38c7077739eaull,
        0xb3ea38c7077739eaull}}};
  for (unsigned variant = 0; variant < 3; ++variant) {
    const Console console = variant ? Console::Wii : Console::GameCube;
    const bool secondary = variant == 2;
    Memory memory(console);
    for (unsigned profile = 0; profile < buffer_audio_fixture::profiles.size(); ++profile) {
      u64 digest = 14695981039346656037ull;
      for (unsigned index = 0; index < 128; ++index) {
        buffer_audio_fixture::Frame frame(profile, index, secondary);
        memory.copy_in(0, frame.ram);
        if (secondary)
          memory.copy_in(0x10000000, frame.ram);
        std::vector<std::pair<u32, bool>> mails;
        dsp::BufferAudioProgram program(
            memory, console, buffer_audio_fixture::profiles[profile],
            [&](u32 value, bool irq) { mails.emplace_back(value, irq); }, [] { return false; },
            [] {},
            [&](u32 address, std::span<u8> output) {
              for (auto &byte : output)
                byte = frame.ram[address++ & 0xffff];
            });
        program.initialize();
        CHECK(mails == (std::vector<std::pair<u32, bool>>{{0xdcd10000, false}}));
        mails.clear();
        CHECK(!program.handle_mail(frame.accelerated ? 0xface0080 : 0x123));
        CHECK(!program.handle_mail(0x1000));
        for (unsigned repeat = 0; repeat < 3; ++repeat) {
          CHECK(!program.handle_mail(frame.accelerated
                                         ? (repeat ? 0xface0020 : 0xface0010)
                                         : (repeat ? 0x222 : (index % 3 == 0 ? 0x112 : 0x111))));
          CHECK(!program.handle_mail(frame.accelerated ? 0xface0100 : 0x666));
          CHECK(mails ==
                (std::vector<std::pair<u32, bool>>{{0xdcd10004, true}, {0xdcd10004, true}}));
          mails.clear();
          digest = buffer_audio_fixture::digest(digest, memory.range(0, frame.ram.size()));
        }
      }
      // Each source-qualified digest covers three frames in 128 retained tasks.
      CHECK(digest == expected[secondary ? 1 : 0][profile]);
    }
  }
}

void runtime_selection_and_resume() {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Memory memory(console), expected(console);
    for (unsigned profile = 0; profile < buffer_audio_fixture::profiles.size(); ++profile) {
      const u32 hash = buffer_audio_fixture::profiles[profile];
      buffer_audio_fixture::Frame frame(profile, 26, false);
      memory.copy_in(0, frame.ram);
      expected.copy_in(0, frame.ram);
      const auto aram = [&](u32 address, std::span<u8> bytes) {
        for (auto &byte : bytes)
          byte = frame.ram[address++ & 0xffff];
      };
      dsp::BufferAudioProgram direct(
          expected, console, hash, [](u32, bool) {}, [] { return false; }, [] {}, aram);
      direct.initialize();
      direct.handle_mail(frame.accelerated ? 0xface0080 : 0x123);
      direct.handle_mail(0x1000);
      direct.handle_mail(frame.accelerated ? 0xface0010 : 0x111);
      direct.handle_mail(frame.accelerated ? 0xface0100 : 0x666);
      dsp::Engine engine(memory, console);
      engine.aram_read = aram;
      unsigned interrupts = 0;
      engine.interrupt = [&] { ++interrupts; };
      const auto send = [&](u32 value) {
        engine.write16(0, u16(value >> 16));
        engine.write16(2, u16(value));
      };
      const auto receive = [&] { return (u32(engine.read16(4)) << 16) | engine.read16(6); };
      const auto code = [&](u32 value) {
        std::array<u8, 32> bytes{};
        for (unsigned bit = 0; bit < 32; ++bit)
          if (value & (1u << bit)) {
            const unsigned distance = bit * 11 % 32;
            bytes[distance ? 32 - distance : 0] ^= 1;
          }
        memory.copy_in(0x30000, bytes);
      };
      engine.write16(0x0a, 0x800);
      CHECK(receive() == 0x8071feed);
      code(hash);
      send(0x80f3a001);
      send(0x30000);
      send(0x80f3a002);
      send(32);
      send(0x80f3d001);
      send(0);
      CHECK(engine.program_hash() == hash && interrupts == 0);
      engine.advance(console == Console::Wii ? 729000 : 486000);
      CHECK(interrupts == 1 && receive() == 0xdcd10000);
      send(frame.accelerated ? 0xface0080 : 0x123);
      send(0x1000);
      send(frame.accelerated ? 0xface0010 : 0x111);
      send(frame.accelerated ? 0xface0100 : 0x666);
      CHECK(receive() == 0xdcd10004);
      CHECK(receive() == 0xdcd10004);
      CHECK(std::equal(expected.range(0, frame.ram.size()).begin(),
                       expected.range(0, frame.ram.size()).end(),
                       memory.range(0, frame.ram.size()).begin()));
      const auto swap = [&](u32 next) {
        code(next);
        send(0xcdd10001);
        for (u32 word : std::array<u32, 10>{0, 0, 0, 0x30000, 32, 0, 0, 0, 0, 0})
          send(word);
        CHECK(engine.program_hash() == next);
      };
      swap(0x3ad3b7ac);
      CHECK(receive() == 0xdcd10000);
      memory.write16(0x20000, 15);
      send(0xbabe0001);
      send(0x20000);
      CHECK(receive() == 0xdcd10002);
      swap(hash);
      CHECK((engine.read16(4) & 0x8000) == 0);
      send(frame.accelerated ? 0xface0100 : 0x666);
      CHECK(receive() == 0xdcd10004);
      CHECK(std::equal(expected.range(0, frame.ram.size()).begin(),
                       expected.range(0, frame.ram.size()).end(),
                       memory.range(0, frame.ram.size()).begin()));
    }
  }
}

void protocol_controls() {
  Memory memory(Console::GameCube);
  for (unsigned profile = 0; profile < buffer_audio_fixture::profiles.size(); ++profile) {
    const u32 hash = buffer_audio_fixture::profiles[profile];
    bool pending = false;
    unsigned irq = 0;
    std::vector<std::pair<u32, bool>> mails;
    dsp::BufferAudioProgram program(
        memory, Console::GameCube, hash,
        [&](u32 value, bool interrupt) { mails.emplace_back(value, interrupt); },
        [&] { return pending; }, [&] { ++irq; }, {});
    program.initialize();
    program.update();
    CHECK(irq == 0);
    pending = true;
    program.update();
    CHECK(irq == 1);
    mails.clear();
    CHECK(!program.handle_mail(0xcdd1ffff));
    CHECK(mails.empty());
    CHECK(!program.handle_mail(0x12349999));
    CHECK(mails.size() == (profile < 9 ? 1u : 0u));
    mails.clear();
    CHECK(!program.handle_mail(profile < 9 ? 0x999 : 0xfacedead));
    CHECK(mails == (std::vector<std::pair<u32, bool>>{
                       {hash == 0x8d69a19b ? 0xdcd10004u : 0xdcd10003u, true}}));
    mails.clear();
    if (profile < 9) {
      CHECK(!program.handle_mail(0x777));
      CHECK(!program.handle_mail(0x888));
      CHECK(mails == (std::vector<std::pair<u32, bool>>{{0, false}, {0x22640000, false}}));
    }
    const auto reset = program.handle_mail(0xcdd10002);
    CHECK(reset && reset->hash == 0 && !reset->preserve);
  }
}
} // namespace

int main() {
  return run_tests([] {
    complete_buffer_frames();
    protocol_controls();
    runtime_selection_and_resume();
  });
}
