// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/types.hpp"
#include <array>
#include <span>
#include <vector>

namespace command_audio_fixture {
using namespace cupid;

struct Frame {
  std::vector<u8> ram = std::vector<u8>(0x20000);
  std::array<u8, 4096> aram{};
  std::array<s16, 2048> coefficients{};
  static constexpr std::array<u16, 26> commands{
      0, 0, 0x2000, 2, 0,      0x1800, 3,      14, 1, 0x4000, 1, 0x3000, 6,
      1, 0, 4,      1, 0x1000, 1,      0x8000, 5,  1, 0x2000, 1, 0x8000, 15};

  void put16(u32 address, u16 value) {
    ram[address] = u8(value >> 8);
    ram[address + 1] = u8(value);
  }

  void put32(u32 address, u32 value) {
    put16(address, u16(value >> 16));
    put16(address + 2, u16(value));
  }

  Frame(u32 profile, unsigned index) {
    u32 seed = 0x587eab31 ^ (index * 0x9e3779b9u);
    const auto random = [&] {
      seed ^= seed << 13;
      seed ^= seed >> 17;
      seed ^= seed << 5;
      return seed;
    };
    for (unsigned i = 0; i < coefficients.size(); ++i)
      coefficients[i] = s16((i * 3947) ^ 0x68ab);
    for (auto &byte : aram)
      byte = u8(random());
    for (unsigned i = 0; i < aram.size(); i += 8)
      aram[i] &= 0x7f;
    for (unsigned c = 0; c < 9; ++c) {
      put32(0x2000 + c * 6, u32(s32(random() % 2000001) - 1000000));
      put16(0x2004 + c * 6, u16(random()));
    }
    std::array<u16, 122> voice{};
    voice[4] = u16((index / 3) % 3);
    voice[5] = u16(index % 4);
    voice[6] = u16(random());
    voice[7] = 1;
    voice[8] = u16((index >> 2) & 1);
    voice[50] = u16(random());
    voice[51] = u16(random());
    voice[55] = u16(index & 1);
    voice[56] = index % 3 == 0 ? 0 : index % 3 == 1 ? 0x19 : 0x0a;
    voice[58] = voice[56] == 0 ? 2 : 0;
    voice[60] = voice[56] == 0 ? 127 : 63;
    voice[62] = voice[58];
    voice[79] = 0x800;
    for (unsigned i = 63; i < 79; ++i)
      voice[i] = u16(s16(int(random() % 4097) - 2048));
    for (unsigned i : {81u, 82u, 91u, 92u})
      voice[i] = u16(random());
    voice[83] = u16((index / 9) % 4);
    voice[84] = u16(random());
    voice[85] = u16(random());
    for (unsigned i = 86; i < 90; ++i)
      voice[i] = u16(random());
    voice[93] = u16((index >> 3) & 1);
    voice[94] = u16(random());
    voice[95] = u16(random() % 32768);
    voice[96] = u16(random() % 32768);
    for (unsigned i = 9; i < 27; ++i)
      voice[i] = u16(random());
    voice[40] = 0x1900;
    if (index & 16) {
      voice[34] = voice[36] = 1;
      put16(0x1900, 50);
      put16(0x1902, u16(random()));
      put16(0x1904, 6);
      put16(0x1906, u16(random()));
    }
    const bool compact = profile == 0x4e8a8b21;
    for (unsigned i = 0; i < voice.size(); ++i) {
      if (compact && i >= 93 && i < 97)
        continue;
      put16(0x1800 + 2 * (i - (compact && i >= 97 ? 4 : 0)), voice[i]);
    }
    for (unsigned i = 0; i < commands.size(); ++i)
      put16(0x1000 + i * 2, commands[i]);
  }
};

inline u64 digest(u64 state, std::span<const u8> bytes) {
  for (u8 byte : bytes)
    state = (state ^ byte) * 1099511628211ull;
  return state;
}
} // namespace command_audio_fixture
