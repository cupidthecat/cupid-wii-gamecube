// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/dsp/command_audio.hpp"
#include "test_support.hpp"
#include "wii_command_audio_fixture.hpp"
#include <tuple>

using namespace cupid;

namespace {
constexpr std::array<u32, 6> profiles{0xfa450138, 0x7699af32, 0xd9c4bf34,
                                      0xadbc06bd, 0x347112ba, 0x4cc52064};

void voice_frames() {
  constexpr std::array<std::array<u64, 6>, 4> expected{{
      {0xfed5fe472f2a894full, 0xfed5fe472f2a894full, 0xe042f880b5ef177cull, 0xf877b94b2da2cea3ull,
       0x9984ad74ebf4ca5dull, 0x9984ad74ebf4ca5dull},
      {0x9e284134504bc9d5ull, 0x9e284134504bc9d5ull, 0xb630cfa3b1050feaull, 0xeb3378e41abe13fdull,
       0x6d64fd9bd10c6b87ull, 0x6d64fd9bd10c6b87ull},
      {0xc19cc7ae3af275b4ull, 0xc19cc7ae3af275b4ull, 0x1778eca40bd40d7bull, 0x2949b0866eb16f86ull,
       0x24d799b667c60a4full, 0x24d799b667c60a4full},
      {0x6b161b1d62a85e62ull, 0x6b161b1d62a85e62ull, 0xdc7a3bf143f74f11ull, 0x9f943a7a4d9845c4ull,
       0x5372d8b5f15f6fcdull, 0x5372d8b5f15f6fcdull},
  }};
  Memory memory(Console::Wii);
  for (unsigned variant = 0; variant < expected.size(); ++variant) {
    const bool coefficients = (variant & 2) != 0, secondary = (variant & 1) != 0;
    for (unsigned profile = 0; profile < profiles.size(); ++profile) {
      u64 digest = 14695981039346656037ull;
      for (unsigned index = 0; index < 144; ++index) {
        command_audio_fixture::WiiFrame frame(profiles[profile], index, secondary);
        memory.copy_in(0, frame.ram);
        if (secondary)
          memory.copy_in(frame.sample_address, frame.samples);
        std::vector<std::tuple<u32, bool, u32>> mails;
        dsp::CommandAudioProgram program(
            memory, Console::Wii, profiles[profile],
            [&](u32 mail, bool irq, u32 delay) { mails.emplace_back(mail, irq, delay); }, {},
            coefficients ? std::span<const s16>(frame.coefficients) : std::span<const s16>{});
        program.initialize();
        CHECK(!program.handle_mail(0xbabe0000 | u32(frame.commands.size())));
        CHECK(!program.handle_mail(0x1000));
        CHECK(mails == (std::vector<std::tuple<u32, bool, u32>>{{0xdcd10000, true, 0},
                                                                {0xdcd10004, true, 0},
                                                                {0xdcd10002, true, 2500}}));
        digest = command_audio_fixture::digest(digest, memory.range(0, frame.ram.size()));
        if (secondary)
          CHECK(std::equal(frame.samples.begin(), frame.samples.end(),
                           memory.range(frame.sample_address, frame.samples.size()).begin()));
      }
      // Each digest covers the full RAM region in 144 source-qualified frames.
      CHECK(digest == expected[variant][profile]);
    }
  }
}

void stereo_commands() {
  Memory memory(Console::Wii);
  dsp::CommandAudioProgram program(memory, Console::Wii, 0xadbc06bd, [](u32, bool, u32) {}, {});
  const auto run = [&](std::initializer_list<u16> words) {
    unsigned index = 0;
    for (u16 word : words)
      memory.write16(0x1000 + 2 * index++, word);
    CHECK(!program.handle_mail(0xbabe0000 | u32(words.size())));
    CHECK(!program.handle_mail(0x1000));
  };
  program.initialize();
  memory.write32(0x2000, 10000);
  memory.write32(0x2006, u32(-20000));
  for (unsigned i = 0; i < 96; ++i) {
    memory.write32(0x3000 + i * 4, 1000);
    memory.write32(0x3180 + i * 4, 2000);
  }
  run({0, 0, 0x2000, 1, 0, 0x3000, 2, 0, 0x3000, 3, 0, 0x3000, 11, 0x8000, 0, 0x4000, 0, 0x5000,
       14});
  CHECK(memory.read16(0x5000) == u16(-22000));
  CHECK(memory.read16(0x5002) == 11000);
  CHECK(!program.handle_mail(0xcdd10003));
  run({0xffff, 11, 0, 0, 0x4000, 0, 0x5000, 14});
  CHECK(memory.read16(0x5000) == u16((-22000 * 32426) >> 15));
  CHECK(memory.read16(0x5002) == u16((11000 * 32426) >> 15));
  CHECK(memory.read32(0x5000 + 95 * 4) == 0);
}

void engine_coefficients_and_sync() {
  for (u32 hash : profiles) {
    Memory memory(Console::Wii), expected(Console::Wii);
    command_audio_fixture::WiiFrame frame(hash, 26, true);
    for (auto *ram : {&memory, &expected}) {
      ram->copy_in(0, frame.ram);
      ram->copy_in(frame.sample_address, frame.samples);
    }
    dsp::CommandAudioProgram direct(
        expected, Console::Wii, hash, [](u32, bool, u32) {}, {}, frame.coefficients);
    direct.initialize();
    direct.handle_mail(0xbabe0000 | u32(frame.commands.size()));
    direct.handle_mail(0x1000);
    dsp::Engine engine(memory, Console::Wii);
    std::array<u8, 4096> coefficient_bytes{};
    for (unsigned i = 0; i < frame.coefficients.size(); ++i) {
      coefficient_bytes[i * 2] = u8(u16(frame.coefficients[i]) >> 8);
      coefficient_bytes[i * 2 + 1] = u8(frame.coefficients[i]);
    }
    engine.install_audio_coefficients(coefficient_bytes);
    unsigned interrupts = 0;
    engine.interrupt = [&] { ++interrupts; };
    const auto send = [&](u32 value) {
      engine.write16(0, u16(value >> 16));
      engine.write16(2, u16(value));
    };
    const auto receive = [&] { return (u32(engine.read16(4)) << 16) | engine.read16(6); };
    engine.write16(0x0a, 0x800);
    CHECK(receive() == 0x8071feed);
    std::array<u8, 32> code{};
    for (unsigned bit = 0; bit < 32; ++bit)
      if (hash & (1u << bit)) {
        const unsigned distance = bit * 11 % 32;
        code[distance ? 32 - distance : 0] ^= 1;
      }
    memory.copy_in(0x30000, code);
    send(0x80f3a001);
    send(0x30000);
    send(0x80f3a002);
    send(32);
    send(0x80f3d001);
    send(0);
    CHECK(engine.program_hash() == hash && interrupts == 1);
    CHECK(receive() == 0xdcd10000);
    send(0xbabe0000 | u32(frame.commands.size()));
    send(0x1000);
    CHECK(interrupts == 2);
    CHECK(receive() == 0xdcd10004 && interrupts == 3);
    CHECK(receive() == 0xdcd10002 && interrupts == 3);
    CHECK(std::equal(expected.range(0, frame.ram.size()).begin(),
                     expected.range(0, frame.ram.size()).end(),
                     memory.range(0, frame.ram.size()).begin()));
  }
}
} // namespace

int main() {
  return run_tests([] {
    stereo_commands();
    voice_frames();
    engine_coefficients_and_sync();
  });
}
