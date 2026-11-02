// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/types.hpp"
#include <array>
#include <span>
#include <vector>

namespace buffer_audio_fixture {
using namespace cupid;
constexpr std::array<u32, 16> profiles{
    0x8d69a19b, 0xcc2fd441, 0xa81582e2, 0xdbbeeb61, 0xbad876ef, 0xfa9c576f, 0x614dd145, 0x5dbf8bf1,
    0x373a950e, 0x008366af, 0x078066ab, 0x5ad4d933, 0x84c680a9, 0xa02a6131, 0x2e5e4100, 0x002e5e41};

struct Frame {
  std::vector<u8> ram = std::vector<u8>(0x10000);
  bool accelerated;
  u32 sample_base;

  void put16(u32 address, u16 value) {
    ram[address] = u8(value >> 8);
    ram[address + 1] = u8(value);
  }

  void put32(u32 address, u32 value) {
    put16(address, u16(value >> 16));
    put16(address + 2, u16(value));
  }

  Frame(unsigned profile, unsigned index, bool secondary)
      : accelerated(profile >= 9), sample_base(secondary ? 0x10000000 : 0) {
    u32 seed = 0x8472fe19 ^ (index * 0x9e3779b9u);
    const auto random = [&] {
      seed ^= seed << 13;
      seed ^= seed >> 17;
      seed ^= seed << 5;
      return seed;
    };
    for (auto &byte : ram)
      byte = u8(random());
    const bool modern = accelerated ? profile >= 11 : profile >= 2;
    const unsigned format = index % (modern ? 8 : 4);
    const u32 delay = index % 7 == 0
                          ? std::array<u32, 8>{0, 1, 95, 96, 97, 1023, 1024, 1025}[(index / 7) % 8]
                          : 0;
    if (accelerated) {
      u32 flags = format | 0x40000000;
      if (index % 29 == 0)
        flags |= modern ? 8 : 4;
      if (index % 31 == 0)
        flags &= ~0x40000000u;
      const u32 begin = (format & 2) ? (sample_base + 0x8000) / 2 : sample_base + 0x8000;
      put32(0x1000, 0x5000);
      put32(0x1004, index % 19 == 0 ? 0 : begin);
      put32(0x1008, begin + 63);
      put32(0x100c, begin + index % 16);
      put16(0x1010, u16(random()));
      put16(0x1012, u16(random()));
      put16(0x1014, u16(random()));
      put32(0x1016, std::array<u32, 6>{0, 0x4000, 0x8000, 0x10000, 0x20000, 0x40000}[index % 6]);
      put16(0x101a, u16(random()));
      put16(0x101c, u16(random()));
      put16(0x101e, u16(random()));
      put16(0x1020, u16(random() % 513));
      put16(0x1022, u16(random() % 513));
      put32(0x1024, delay);
      put32(0x1028, flags);
    } else {
      const std::array<unsigned, 8> widths{1, 2, 2, 4, 1, 2, 2, 4};
      u32 flags = (widths[format] << 16) | format;
      if (index & 8)
        flags |= modern ? 0x100 : 4;
      if (index % 29 == 0)
        flags |= modern ? 0x200 : 0x20;
      put32(0x1000, 0x4000);
      put32(0x1004, delay);
      put32(0x1008, flags);
      put32(0x100c, index % 11 == 0 ? 0 : sample_base + 0x8000 + index % 32);
      put32(0x1010, sample_base + 0x8100);
      put32(0x1014, std::array<u32, 6>{0, 8000, 24000, 48000, 96000, 192000}[index % 6]);
      put16(0x1018, u16(random()));
      put16(0x101a, u16(random()));
      put32(0x101c, random() % 96001);
      put16(0x1020, u16(random() % 513));
      put16(0x1022, u16(random() % 513));
      put32(0x1024, index % 13 == 0 ? 0 : sample_base + 0xa000);
      put32(0x1028, sample_base + 0xa100);
      put16(0x102c, u16(random() % 513));
      put16(0x102e, u16(random() % 513));
    }
  }
};

inline u64 digest(u64 state, std::span<const u8> bytes) {
  for (u8 byte : bytes)
    state = (state ^ byte) * 1099511628211ull;
  return state;
}
} // namespace buffer_audio_fixture
