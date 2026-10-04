// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/boot.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace cupid::runtime {
namespace {
u32 word(std::span<const u8> bytes, std::size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 4)
    throw std::runtime_error("Truncated boot header");
  return (u32(bytes[offset]) << 24) | (u32(bytes[offset + 1]) << 16) |
         (u32(bytes[offset + 2]) << 8) | bytes[offset + 3];
}

void put(std::span<u8> bytes, std::size_t offset, u32 value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes[offset + i] = static_cast<u8>(value >> (24 - i * 8));
}

u32 crc(std::span<const u8> bytes) {
  u32 result = 0xffffffff;
  for (auto byte : bytes) {
    result ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit)
      result = (result >> 1) ^ ((result & 1) ? 0xedb88320 : 0);
  }
  return ~result;
}

void decode_rom(std::span<u8> bytes) {
  std::array<u16, 3> feedback{0x2953, 0xd9c2, 0x3ff1};
  u8 previous = 1;
  for (auto &byte : bytes) {
    u8 stream = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
      const unsigned a = feedback[0] & 1, b = feedback[1] & 1, c = feedback[2] & 1;
      const unsigned a1 = (feedback[0] >> 1) & 1, b1 = (feedback[1] >> 1) & 1;
      previous ^= static_cast<u8>(a1 ^ c ^ (b | b1) ^ ((a ^ b1 ^ c) & (a ^ b)));
      if (a == b)
        feedback[2] = static_cast<u16>((feedback[2] >> 1) ^ (c ? 0xb3d0 : 0));
      if (!a)
        feedback[1] = static_cast<u16>((feedback[1] >> 1) ^ (b ? 0xfb10 : 0));
      feedback[0] = static_cast<u16>((feedback[0] >> 1) ^ (a ? 0xa740 : 0));
      stream = static_cast<u8>((stream << 1) | previous);
    }
    byte ^= stream;
  }
}
} // namespace

std::vector<u8> read_file(const std::filesystem::path &path, u64 limit) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    throw std::runtime_error("Cannot open input file: " + path.string());
  auto end = input.tellg();
  if (end < 0 || static_cast<u64>(end) > limit)
    throw std::runtime_error("Input file exceeds size limit: " + path.string());
  std::vector<u8> data(static_cast<std::size_t>(end));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size())))
    throw std::runtime_error("Cannot read input file: " + path.string());
  return data;
}

Bios load_bios(const std::filesystem::path &path) {
  Bios result;
  result.decoded = read_file(path, 2 * 1024 * 1024);
  if (result.decoded.size() != 2 * 1024 * 1024)
    throw std::runtime_error("GameCube firmware must be exactly 2097152 bytes");
  result.crc32 = crc(result.decoded);
  switch (result.crc32) {
  case 0x6dac1f2a:
    result.revision = "NTSC 1.0";
    break;
  case 0xd5e6feea:
    result.revision = "NTSC 1.1";
    break;
  case 0xd235e3f9:
  case 0x86573808:
    result.revision = "NTSC 1.2";
    break;
  case 0x667d0b64:
    result.revision = "MPAL 1.1";
    break;
  case 0x4f319f43:
    result.revision = "PAL 1.0";
    result.pal = true;
    break;
  case 0xad1b7f16:
    result.revision = "PAL 1.2";
    result.pal = true;
    break;
  default:
    throw std::runtime_error("GameCube firmware checksum is not a recognized retail dump");
  }
  decode_rom(std::span<u8>(result.decoded).subspan(0x100, 0x1afe00));
  return result;
}

BootInfo load_dol(Memory &memory, std::span<const u8> file) {
  if (file.size() < 0x100)
    throw std::runtime_error("DOL header is shorter than 256 bytes");

  struct Section {
    u32 offset, address, length;
  };

  std::vector<Section> sections;
  bool executable_entry = false;
  const u32 entry = word(file, 0xe0);
  if (entry & 3)
    throw std::runtime_error("DOL entry point is unaligned");
  for (unsigned i = 0; i < 18; ++i) {
    u32 length = word(file, 0x90 + i * 4);
    if (!length)
      continue;
    u32 offset = word(file, i * 4), address = word(file, 0x48 + i * 4);
    const u64 padded = (u64(length) + 31) & ~u64{31};
    if (offset < 0x100 || padded > file.size() || offset > file.size() - padded)
      throw std::runtime_error("DOL section extends outside its file");
    if (u64(address) + padded > u64{0x100000000})
      throw std::runtime_error("DOL section address wraps");
    memory.range(Memory::boot_physical(address), static_cast<std::size_t>(padded));
    for (const auto &section : sections)
      if (u64(Memory::boot_physical(address)) < u64(section.address) + section.length &&
          u64(section.address) < u64(Memory::boot_physical(address)) + padded)
        throw std::runtime_error("DOL load sections overlap");
    if (i < 7 && entry >= address && u64(entry) < u64(address) + length)
      executable_entry = true;
    sections.push_back({offset, Memory::boot_physical(address), static_cast<u32>(padded)});
  }
  if (!executable_entry)
    throw std::runtime_error("DOL entry point is outside its text sections");
  const u32 bss = word(file, 0xd8), bss_size = word(file, 0xdc);
  if (bss_size) {
    if (u64(bss) + bss_size > u64{0x100000000})
      throw std::runtime_error("DOL BSS address wraps");
    memory.range(Memory::boot_physical(bss), bss_size);
  }
  BootInfo result{entry, 0, static_cast<unsigned>(sections.size()), "executable-loaded"};
  for (const auto &section : sections) {
    memory.copy_in(section.address, file.subspan(section.offset, section.length));
    result.loaded_bytes += section.length;
  }
  return result;
}

void initialize_boot_memory(Memory &memory, Console console, bool pal) {
  const bool wii = console == Console::Wii;
  // These are bootstrap values; the guest startup code replaces its own globals.
  const std::array<std::pair<u32, u32>, 9> common{{{0x20, 0x0d15ea5e},
                                                   {0x28, Memory::mem1_size},
                                                   {0x2c, wii ? 0x23u : 0x10000006u},
                                                   {0xcc, pal ? 1u : 0u},
                                                   {0xd0, 0x01000000},
                                                   {0xf8, wii ? 243000000u : 162000000u},
                                                   {0xfc, wii ? 729000000u : 486000000u},
                                                   {0x30d8, 0},
                                                   {0x30dc, 0}}};
  for (auto [address, value] : common)
    memory.write32(address, value);
  for (u32 vector : {0x300u, 0x800u, 0xc00u})
    memory.write32(vector, 0x4c000064);
  if (wii) {
    memory.write32(0x24, 1);
    memory.write32(0x34, 0x817fec60);
    memory.write32(0xe4, 0x8008f7b8);
    memory.write32(0xf0, Memory::mem1_size);
    memory.write32(0xf4, 0x8179b500);
    memory.write32(0x30d8, 0xffffffff);
    memory.write16(0x30e6, 0x8201);
    memory.write8(0x315c, 0x80);
    memory.write16(0x315e, 0x0113);
    memory.write32(0x3184, 0x80000000);
  }
}

void boot_disc(Memory &memory, cpu::Interpreter &cpu, disc::Image &image,
               const std::function<void()> &step, BootInfo &info) {
  const bool wii = image.header().console == Console::Wii;
  const bool pal = image.header().id.size() >= 4 &&
                   std::string("PDFISXHY").find(image.header().id[3]) != std::string::npos;
  info.stage = "boot-memory";
  cpu.configure_boot();
  initialize_boot_memory(memory, image.header().console, pal);
  std::array<u8, 32> disc_id{};
  image.read(0, disc_id);
  memory.copy_in(0, disc_id);
  if (wii) {
    std::array<u8, 4> game_id{};
    image.read_game(0, game_id);
    memory.copy_in(0x3180, game_id);
  }
  auto &state = cpu.state();
  state.gpr[1] = wii ? 0x816ffff0 : (pal ? 0x815edca8 : 0x81566550);
  if (!wii) {
    state.gpr[2] = pal ? 0x814b5b20 : 0x81465cc0;
    state.gpr[13] = pal ? 0x814b4fc0 : 0x81465320;
  }
  std::array<u8, 32> header{};
  image.read_game(0x2440, header);
  const u32 entry = word(header, 0x10), size = word(header, 0x14), trailer = word(header, 0x18);
  const u64 length = u64(size) + trailer;
  if (!length || length > 0x00100000 || entry < 0x81200000 || u64(entry) >= 0x81200000 + length ||
      (entry & 3))
    throw std::runtime_error("Invalid apploader entry point or length");
  std::vector<u8> loader(static_cast<std::size_t>(length));
  image.read_game(0x2460, loader);
  memory.copy_in(0x01200000, loader);
  info.loaded_bytes = length;
  info.stage = "apploader-entry";
  auto call = [&](u32 address) {
    const auto physical = Memory::boot_physical(address);
    memory.range(physical, 4);
    if (address & 3)
      throw std::runtime_error("Unaligned apploader function");
    state.pc = address;
    state.spr[cpu::spr::lr] = 0;
    while (state.pc != 0)
      step();
  };
  const u32 functions = wii ? 0x4000 : 0x3100;
  state.gpr[3] = 0x80000000 + functions;
  state.gpr[4] = state.gpr[3] + 4;
  state.gpr[5] = state.gpr[3] + 8;
  call(entry);
  const u32 init = memory.read32(functions), main = memory.read32(functions + 4),
            close = memory.read32(functions + 8);
  // Apploader reporting callback returns through the guest instruction stream.
  memory.write32(0x01300000, 0x4e800020);
  info.stage = "apploader-init";
  state.gpr[3] = 0x81300000;
  call(init);
  info.stage = "apploader-load";
  unsigned requests = 0;
  for (;;) {
    state.gpr[3] = 0x81300004;
    state.gpr[4] = 0x81300008;
    state.gpr[5] = 0x8130000c;
    call(main);
    if (!state.gpr[3])
      break;
    if (++requests > 4096)
      throw std::runtime_error("Apploader produced too many disc requests");
    u32 destination = memory.read32(0x01300004), amount = memory.read32(0x01300008);
    u64 offset = u64(memory.read32(0x0130000c)) << (wii ? 2 : 0);
    if (!amount || amount > Memory::mem1_size + Memory::mem2_size)
      throw std::runtime_error("Invalid apploader transfer length");
    const u32 physical = Memory::boot_physical(destination);
    memory.range(physical, amount);
    std::vector<u8> data(amount);
    image.read_game(offset, data);
    memory.copy_in(physical, data);
    info.loaded_bytes += amount;
    ++info.loaded_sections;
  }
  info.stage = "apploader-close";
  call(close);
  info.entry = state.gpr[3];
  memory.range(Memory::boot_physical(info.entry), 4);
  if (!info.entry || (info.entry & 3))
    throw std::runtime_error("Apploader returned an invalid game entry");
  state.pc = info.entry;
  info.stage = "game-entry";
}

std::vector<u8> self_test_dol() {
  std::vector<u8> file(0x120);
  put(file, 0, 0x100);
  put(file, 0x48, 0x80004000);
  put(file, 0x90, 32);
  put(file, 0xe0, 0x80004000);
  const std::array<u32, 8> code{
      0x38600007, // li r3,7
      0x38800009, // li r4,9
      0x7ca32214, // add r5,r3,r4
      0x3cc08000, // lis r6,0x8000
      0x90a65000, // stw r5,0x5000(r6)
      0x80e65000, // lwz r7,0x5000(r6)
      0x60000000, // nop
      0x48000000  // bounded stop loop
  };
  for (unsigned i = 0; i < code.size(); ++i)
    put(file, 0x100 + i * 4, code[i]);
  return file;
}
} // namespace cupid::runtime
