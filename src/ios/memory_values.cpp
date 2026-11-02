// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/memory.hpp"
#include "cupid/ios/ipc.hpp"
#include <algorithm>
#include <array>

namespace cupid::ios {
namespace {
struct Version {
  u16 major;
  u32 revision;
  u32 date;
};

constexpr std::array<Version, 41> versions{
    {{4, 0x40003, 0x81006},    {9, 0x9040a, 0x030110},   {11, 0xb000a, 0x102506},
     {12, 0xc020e, 0x030110},  {13, 0xd0408, 0x030110},  {14, 0xe0408, 0x030110},
     {15, 0xf0408, 0x030110},  {17, 0x110408, 0x030110}, {20, 0x14000c, 0x102506},
     {21, 0x15040f, 0x030110}, {22, 0x16050e, 0x030110}, {28, 0x1c070f, 0x030110},
     {30, 0x1e0a10, 0x40308},  {31, 0x1f0e18, 0x030110}, {33, 0x210e18, 0x030110},
     {34, 0x220e18, 0x030110}, {35, 0x230e18, 0x030110}, {36, 0x240e18, 0x030110},
     {37, 0x25161f, 0x030110}, {38, 0x26101c, 0x030110}, {40, 0x280911, 0x022308},
     {41, 0x290e17, 0x030110}, {43, 0x2b0e17, 0x030110}, {45, 0x2d0e17, 0x030110},
     {46, 0x2e0e17, 0x030110}, {48, 0x30101c, 0x030110}, {50, 0x321319, 0x101008},
     {51, 0x331219, 0x071108}, {52, 0x34161d, 0x101008}, {53, 0x35161f, 0x030110},
     {55, 0x37161f, 0x030110}, {56, 0x38161e, 0x030110}, {57, 0x39171f, 0x030110},
     {58, 0x3a1820, 0x030110}, {59, 0x3b2421, 0x101811}, {60, 0x3c181e, 0x112408},
     {61, 0x3d161e, 0x030110}, {62, 0x3e191e, 0x022712}, {70, 0x461a1f, 0x060309},
     {80, 0x501b20, 0x030310}, {257, 0x707, 0x82209}}};
} // namespace

bool initialize_ios_memory(Memory &memory, u32 version, bool reload) {
  const auto selected =
      std::find_if(versions.begin(), versions.end(),
                   [version](const Version &entry) { return entry.major == version; });
  if (selected == versions.end())
    return false;
  constexpr u32 placeholder = 0xdeadbeef;
  const u32 mem2_end = version < 28 ? 0x93400000 : (version == 28 ? 0x93800000 : 0x93600000);
  const u32 ipc_begin = mem2_end - 0x20000;
  const bool reserved = version >= 28 && version != 257;
  const std::array<std::pair<u32, u32>, 10> retained{
      {{0x3118, 0x04000000},
       {0x311c, 0x04000000},
       {0x3120, mem2_end},
       {0x3124, 0x90000800},
       {0x3128, ipc_begin},
       {0x3130, ipc_begin},
       {0x3134, mem2_end},
       {0x3140, selected->revision},
       {0x3148, reserved ? mem2_end : placeholder},
       {0x314c, reserved ? mem2_end + 0x20000 : placeholder}}};
  if (!reload) {
    auto low = memory.range(0, 0x3fff);
    std::fill(low.begin(), low.end(), u8(0));
    const std::array<std::pair<u32, u32>, 20> bootstrap{
        {{0x28, 0x01800000},
         {0x30, 0},
         {0x34, 0x81800000},
         {0xf0, 0x01800000},
         {0x3100, 0x01800000},
         {0x3104, 0x01800000},
         {0x3108, 0x81800000},
         {0x310c, 0},
         {0x3110, 0x81800000},
         {0x3114, placeholder},
         {0x312c, placeholder},
         {0x3138, 0x11},
         {0x313c, placeholder},
         {0x3144, selected->date},
         {0x3150, placeholder},
         {0x3154, placeholder},
         {0x3158, version == 257 ? 0xcafebabe : 0xff01},
         {0x315c, placeholder},
         {0x3160, version == 257 ? placeholder : 0},
         {0x3130, ipc_begin}}};
    for (const auto &[address, value] : bootstrap)
      memory.write32(address, value);
  }
  for (const auto &[address, value] : retained)
    memory.write32(address, value);
  return true;
}
} // namespace cupid::ios
