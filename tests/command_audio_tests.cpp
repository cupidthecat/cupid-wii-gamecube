// SPDX-License-Identifier: GPL-2.0-or-later
#include "command_audio_fixture.hpp"
#include "cupid/dsp/command_audio.hpp"
#include "test_support.hpp"
#include <array>
#include <tuple>
#include <vector>

using namespace cupid;

namespace {
struct Fixture {
  Memory memory{Console::GameCube};
  std::array<u8, 4096> aram{};
  std::vector<std::tuple<u32, bool, u32>> mails;
  dsp::CommandAudioProgram program;

  explicit Fixture(u32 hash = 0x3ad3b7ac, std::span<const s16> coefficients = {})
      : program(
            memory, Console::GameCube, hash,
            [&](u32 mail, bool irq, u32 delay) { mails.emplace_back(mail, irq, delay); },
            [&](u32 address, std::span<u8> output) {
              for (auto &byte : output)
                byte = aram[address++ & 4095];
            },
            coefficients) {
    program.initialize();
  }

  void commands(std::initializer_list<u16> words) {
    unsigned i = 0;
    for (u16 value : words)
      memory.write16(0x1000 + 2 * i++, value);
    CHECK(!program.handle_mail(0xbabe0000 | u32(words.size())));
    CHECK(!program.handle_mail(0x1000));
  }

  void resume() {
    CHECK(!program.handle_mail(0x12340003));
  }
};

void mailbox_and_commands() {
  Fixture f;
  CHECK(f.mails == (std::vector<std::tuple<u32, bool, u32>>{{0xdcd10000, true, 0}}));
  f.memory.write32(0x2000, 40000);
  f.memory.write16(0x2004, 1);
  f.memory.write32(0x2006, u32(-40000));
  f.memory.write16(0x200a, 0xffff);
  f.memory.write16(0x2010, 100);
  f.commands({0, 0, 0x2000, 14, 0, 0x4000, 0, 0x5000, 15});
  CHECK(f.memory.read32(0x5000) == 0x80007fff);
  CHECK(f.memory.read32(0x527c) == 0x80007fff);
  CHECK(f.memory.read32(0x4000) == 0 && f.memory.read32(0x427c) == 0);
  CHECK(f.mails.back() == std::tuple(u32(0xdcd10002), true, u32(2500)));
  CHECK(!f.program.handle_mail(0x00000000));
  CHECK(f.mails.back() == std::tuple(u32(0xdcd10001), true, u32(0)));
  f.commands({10, 11, 12, 8, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15});
  auto reset = f.program.handle_mail(0xcdd10002);
  CHECK(reset && reset->hash == 0 && !reset->preserve);
}

void buffer_volume_and_compressor() {
  Fixture f;
  for (unsigned i = 0; i < 480; ++i)
    f.memory.write32(0x3000 + i * 4, 32768);
  f.commands({0, 0, 0x2000, 1, 0,      0x3000, 0x8000, 0x4000, 0xffff, 6,      0, 0x5000,
              4, 0, 0x6000, 0, 0x7000, 5,      0,      0x8000, 0,      0x7000, 15});
  CHECK(f.memory.read32(0x5000) == 32768 && f.memory.read32(0x577c) == 32768);
  CHECK(f.memory.read32(0x6000) == 16384 && f.memory.read32(0x677c) == 16384);
  CHECK(f.memory.read32(0x8000) == 65535 && f.memory.read32(0x877c) == 65535);
  f.resume();
  for (unsigned i = 0; i < 160; ++i) {
    f.memory.write32(0x3000 + i * 4, 24000);
    f.memory.write16(0x9000 + i * 2, 0x4000);
    f.memory.write16(0x9000 + 12 * 320 + i * 2, 0x2000);
  }
  f.commands({7, 0, 0x3000, 18, 20000, 2, 0, 0x9000, 6, 0, 0x5000, 15});
  CHECK(f.memory.read32(0x5000) == 12000 && f.memory.read32(0x5280) == 12000);
  f.resume();
  f.commands({18, 20000, 2, 0, 0x9000, 6, 0, 0x5000, 15});
  CHECK(f.memory.read32(0x5000) == 3000);
}

void pcm_voice(u32 hash, bool loop) {
  Fixture f(hash);
  std::array<u16, 122> voice{};
  voice[4] = 2;
  voice[6] = 3;
  voice[7] = 1;
  voice[8] = loop ? 1 : 0;
  voice[9] = voice[11] = 0x8000;
  voice[50] = 0x7fff;
  voice[55] = loop ? 1 : 0;
  voice[56] = 0x0a;
  voice[60] = 15;
  voice[79] = 0x800;
  for (unsigned i = 0; i < 16; ++i) {
    const u16 value = u16(s16(int(i) * 2000 - 15000));
    f.aram[i * 2] = u8(value >> 8);
    f.aram[i * 2 + 1] = u8(value);
  }
  const bool compact = hash == 0x4e8a8b21;
  for (unsigned i = 0; i < voice.size(); ++i) {
    if (compact && i >= 93 && i < 97)
      continue;
    f.memory.write16(0x2000 + 2 * (i - (compact && i >= 97 ? 4 : 0)), voice[i]);
  }
  f.commands({2, 0, 0x2000, 3, 14, 0, 0x4000, 0, 0x5000, 15});
  for (unsigned i = 0; i < 160; ++i) {
    const s32 raw = loop || i < 16 ? int(i & 15) * 2000 - 15000 : 0;
    const u16 expected = u16((raw * 32767) >> 15);
    CHECK(f.memory.read16(0x5000 + 4 * i) == expected);
    CHECK(f.memory.read16(0x5002 + 4 * i) == expected);
  }
  CHECK(f.memory.read16(0x2000 + 14) == (loop ? 1 : 0));
  const unsigned counter = compact ? 93 : 97;
  CHECK(f.memory.read16(0x2000 + counter * 2) == (loop ? 10 : 0));
}

void voice_frame_regressions() {
  Memory memory(Console::GameCube);
  for (bool with_coefficients : {false, true}) {
    for (u32 hash :
         {0x3ad3b7acu, 0x3daf59b9u, 0x4e8a8b21u, 0x07f88145u, 0xe2136399u, 0x3389a79eu}) {
      u64 digest = 14695981039346656037ull;
      for (unsigned index = 0; index < 144; ++index) {
        command_audio_fixture::Frame frame(hash, index);
        memory.copy_in(0, frame.ram);
        std::vector<std::tuple<u32, bool, u32>> mails;
        dsp::CommandAudioProgram program(
            memory, Console::GameCube, hash,
            [&](u32 mail, bool irq, u32 delay) { mails.emplace_back(mail, irq, delay); },
            [&](u32 address, std::span<u8> output) {
              for (auto &byte : output)
                byte = frame.aram[address++ & 4095];
            },
            with_coefficients ? std::span<const s16>(frame.coefficients) : std::span<const s16>{});
        program.initialize();
        CHECK(!program.handle_mail(0xbabe0000 | u32(frame.commands.size())));
        CHECK(!program.handle_mail(0x1000));
        CHECK(mails == (std::vector<std::tuple<u32, bool, u32>>{{0xdcd10000, true, 0},
                                                                {0xdcd10002, true, 2500}}));
        digest = command_audio_fixture::digest(digest, memory.range(0, frame.ram.size()));
      }
      // These complete-RAM digests were obtained from the pinned source, then
      // checked byte-for-byte against the implementation for every frame.
      const bool compact = hash == 0x4e8a8b21;
      const u64 expected = with_coefficients
                               ? (compact ? 0x2295855a14ce23b8ull : 0xfe00a69aa2f4b8a3ull)
                               : (compact ? 0xf4a9337a6e221123ull : 0xa543b296a45cf20bull);
      CHECK(digest == expected);
    }
  }
}

void malformed_lists() {
  Fixture f;
  f.commands({15});
  f.resume();
  CHECK(!f.program.handle_mail(0xbabe0200));
  // Oversized copies leave the previous, terminated command list intact.
  CHECK(!f.program.handle_mail(0xffffffff));
  Fixture old(0x4e8a8b21);
  CHECK_THROWS(old.commands({18, 0, 0, 0, 0x2000, 15}));
  Fixture cyclic;
  cyclic.memory.write32(0x2000, 0x2000);
  CHECK_THROWS(cyclic.commands({2, 0, 0x2000, 3, 15}));
}
} // namespace

int main() {
  return run_tests([] {
    mailbox_and_commands();
    buffer_volume_and_compressor();
    for (u32 hash : {0x3ad3b7acu, 0x3daf59b9u, 0x4e8a8b21u, 0x07f88145u, 0xe2136399u, 0x3389a79eu})
      for (bool loop : {false, true})
        pcm_voice(hash, loop);
    malformed_lists();
    voice_frame_regressions();
  });
}
