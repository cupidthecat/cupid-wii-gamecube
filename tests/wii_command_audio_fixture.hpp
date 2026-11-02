// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "command_audio_fixture.hpp"
#include <algorithm>

namespace command_audio_fixture {
struct WiiFrame {
  std::vector<u8> ram = std::vector<u8>(0x20000);
  std::array<u8, 4096> samples{};
  std::array<s16, 2048> coefficients{};
  std::vector<u16> commands;
  u32 sample_address;

  void put16(u32 address, u16 value) {
    ram[address] = u8(value >> 8);
    ram[address + 1] = u8(value);
  }

  void put32(u32 address, u32 value) {
    put16(address, u16(value >> 16));
    put16(address + 2, u16(value));
  }

  WiiFrame(u32 profile, unsigned index, bool secondary)
      : sample_address(secondary ? 0x10008000 : 0x8000) {
    const bool old = profile == 0xfa450138 || profile == 0x7699af32;
    const bool filters = profile == 0x347112ba || profile == 0x4cc52064;
    u32 seed = 0x4e16d8a7 ^ (index * 0x9e3779b9u);
    const auto random = [&] {
      seed ^= seed << 13;
      seed ^= seed >> 17;
      seed ^= seed << 5;
      return seed;
    };
    for (unsigned i = 0; i < coefficients.size(); ++i)
      coefficients[i] = s16((i * 3947) ^ 0x68ab);
    for (auto &byte : samples)
      byte = u8(random());
    for (unsigned i = 0; i < samples.size(); i += 8)
      samples[i] &= 0x7f;
    for (unsigned c = 0; c < 20; ++c) {
      put32(0x2000 + c * 6, u32(s32(random() % 2000001) - 1000000));
      put16(0x2004 + c * 6, u16(random()));
    }
    std::array<u16, 155> voice{};
    voice[4] = u16((index / 3) % 3);
    voice[5] = u16(index % 4);
    voice[6] = u16(random());
    voice[7] = u16(random());
    voice[8] = 1;
    voice[9] = u16((index >> 2) & 1);
    voice[58] = u16(random());
    voice[59] = u16(random());
    voice[60] = u16(index & 1);
    voice[61] = index % 3 == 0 ? 0 : index % 3 == 1 ? 0x19 : 0x0a;
    const u32 start = voice[61] == 0      ? sample_address * 2 + 2
                      : voice[61] == 0x19 ? sample_address
                                          : sample_address / 2;
    const u32 end = start + (voice[61] == 0 ? 125 : 63);
    voice[62] = voice[66] = u16(start >> 16);
    voice[63] = voice[67] = u16(start);
    voice[64] = u16(end >> 16);
    voice[65] = u16(end);
    voice[84] = 0x800;
    for (unsigned i = 68; i < 84; ++i)
      voice[i] = u16(s16(int(random() % 4097) - 2048));
    for (unsigned i : {86u, 87u, 96u, 97u})
      voice[i] = u16(random());
    voice[88] = u16((index / 9) % 4);
    voice[89] = u16(random());
    voice[90] = u16(random());
    for (unsigned i = 91; i < 95; ++i)
      voice[i] = u16(random());
    voice[98] = u16((index >> 3) & 1);
    voice[99] = u16(random());
    voice[100] = u16(random() % 32768);
    voice[101] = u16(random() % 32768);
    voice[102] = u16((index >> 4) & 1);
    for (unsigned i = 103; i < 112; ++i)
      voice[i] = u16(random());
    for (unsigned i = 10; i < 34; ++i)
      voice[i] = u16(random());
    voice[57] = 0x1a00;
    if (index & 16) {
      voice[53] = voice[55] = 1;
      put16(0x1a00, 58);
      put16(0x1a02, u16(random()));
      put16(0x1a04, 7);
      put16(0x1a06, u16(random()));
    }
    voice[112] = u16((index >> 1) & 1);
    voice[113] = u16(random());
    for (unsigned i = 114; i < 143; ++i)
      voice[i] = u16(random());
    voice[143] = u16(index % 3);
    for (unsigned i = 144; i < 153; ++i)
      voice[i] = u16(random());
    for (unsigned i = 0; i < voice.size(); ++i) {
      if ((!old && i >= 53 && i < 58) || (!filters && i >= 106 && i < 112))
        continue;
      const unsigned physical = !filters && i >= 112 ? i - 6 : i - (!old && i >= 58 ? 5 : 0);
      put16(0x1800 + physical * 2, voice[i]);
    }
    commands = {0, 0, 0x2000, 4, 0, 0x1800};
    if (old)
      commands.push_back(5);
    commands.push_back(u16(12 + old));
    if (!old && profile != 0xd9c4bf34)
      commands.push_back(u16(random()));
    const auto append = [&](std::initializer_list<u16> words) {
      commands.insert(commands.end(), words.begin(), words.end());
    };
    append({1, 0x4000, 1, 0x3000});
    for (unsigned group = 0; group < 3; ++group)
      append({u16(5 + old + group), 0x8000, 1, u16(group * 0x1000), 1, 0xa000});
    append({u16(13 + old), 1, 0x6000, 1, 0x6100, 1, 0x6200, 1, 0x6300, u16(14 + old)});
    for (unsigned i = 0; i < commands.size(); ++i)
      put16(0x1000 + i * 2, commands[i]);
    if (!secondary)
      std::copy(samples.begin(), samples.end(), ram.begin() + sample_address);
  }
};
} // namespace command_audio_fixture
