// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/command_processor.hpp"
#include "cupid/gfx/efb_copy.hpp"
#include "cupid/gfx/xfb_copy.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <sstream>

namespace cupid::gfx {
namespace {
constexpr unsigned width = 640, height = 528;

u32 word(std::span<const u8> bytes, std::size_t offset) {
  return (u32(bytes[offset]) << 24) | (u32(bytes[offset + 1]) << 16) |
         (u32(bytes[offset + 2]) << 8) | bytes[offset + 3];
}

[[noreturn]] void unavailable(u32 address, bool writing, const std::string &text) {
  throw BusFault(address, writing, text, true);
}

} // namespace

CommandProcessor::CommandProcessor(Memory &memory, hw::Devices &devices, Console console)
    : memory_(memory), devices_(devices), console_(console), colors_(width * height),
      depths_(width * height), texture_memory_(1024 * 1024),
      shader_(memory, bp_state_, tev_colors_, konst_colors_, texture_memory_),
      rasterizer_(bp_state_, xf_state_) {
  bp_state_[0xfe] = 0xffffff;
  xf_state_[0x500 + 0x3d * 4] = std::bit_cast<u32>(1.0f);
  xf_state_[0x500 + 0x3e * 4 + 1] = std::bit_cast<u32>(1.0f);
  xf_state_[0x500 + 0x3f * 4 + 2] = std::bit_cast<u32>(1.0f);
  rasterizer_.fragment = [this](unsigned x, unsigned y, u32 z, const FragmentInputs &inputs) {
    fragment(x, y, z, inputs);
  };
}

u32 CommandProcessor::fifo(unsigned offset) const {
  return registers_[offset / 2] | (u32(registers_[offset / 2 + 1]) << 16);
}

void CommandProcessor::fifo(unsigned offset, u32 value) {
  registers_[offset / 2] = static_cast<u16>(value);
  registers_[offset / 2 + 1] = static_cast<u16>(value >> 16);
}

void CommandProcessor::reset_fifo() {
  pipe_size_ = 0;
  pending_.clear();
  registers_[1] = 0x10;
  registers_[3] = registers_[5] = 0;
  fifo(0x2c, 0);
  fifo(0x28, console_ == Console::Wii ? 0x1fffffe0 : 0x03ffffe0);
  update_interrupts();
}

void CommandProcessor::gather(u8 byte) {
  pipe_[pipe_size_++] = byte;
  if (pipe_size_ != pipe_.size())
    return;
  pipe_size_ = 0;
  const u32 base = devices_.read(0x0c00300c, 4), end = devices_.read(0x0c003010, 4);
  const u32 pointer = devices_.read(0x0c003014, 4);
  if (pointer < base || pointer > end)
    unavailable(0x0c008000, true, "Write-gather FIFO pointer is outside its configured ring");
  memory_.copy_in(pointer, pipe_);
  const u32 next = pointer == end ? base : pointer + 32;
  devices_.write(0x0c003014, next, 4);
  if (registers_[1] & 16) {
    const u32 next_cp = fifo(0x34) == fifo(0x24) ? fifo(0x20) : fifo(0x34) + 32;
    fifo(0x34, next_cp);
    fifo(0x30, fifo(0x30) + 32);
    if (registers_[1] & 1) {
      devices_.write(0x0c00300c, fifo(0x20), 4);
      devices_.write(0x0c003010, fifo(0x24), 4);
      devices_.write(0x0c003014, next_cp, 4);
    }
  }
  update_interrupts();
  consume_fifo();
}

void CommandProcessor::consume_fifo() {
  while ((registers_[1] & 1) && fifo(0x30) >= 32) {
    const u32 pointer = fifo(0x38);
    if ((registers_[1] & 2) && pointer == fifo(0x3c))
      break;
    if (pointer < fifo(0x20) || pointer > fifo(0x24))
      unavailable(0x0c000038, false, "Graphics FIFO read pointer is outside its ring");
    const auto data = memory_.range(pointer, 32);
    submit(data);
    fifo(0x38, pointer == fifo(0x24) ? fifo(0x20) : pointer + 32);
    fifo(0x30, fifo(0x30) - 32);
  }
  update_interrupts();
}

void CommandProcessor::submit(std::span<const u8> data) {
  if (data.size() > 32 * 1024 * 1024 || pending_.size() > 32 * 1024 * 1024 - data.size())
    unavailable(0x0c008000, true, "Graphics command buffer exceeds its limit");
  pending_.insert(pending_.end(), data.begin(), data.end());
  std::size_t consumed = 0;
  while (consumed < pending_.size()) {
    auto amount = decode(std::span<const u8>(pending_).subspan(consumed));
    if (!amount)
      break;
    consumed += amount;
    ++command_count_;
  }
  pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(consumed));
}

std::size_t CommandProcessor::decode(std::span<const u8> bytes) {
  const u8 op = bytes[0];
  if (op <= 7 || op == 0x3f || op == 0x44 || op == 0x48) {
    command_cycles_ += 6;
    return 1;
  }
  if (op == 8) {
    if (bytes.size() < 6)
      return 0;
    unsigned reg = bytes[1];
    u32 value = word(bytes, 2);
    if (reg < 0x30) {
      command_cycles_ += 12;
      return 6;
    }
    if (reg < 0x70)
      reg &= 0xf0;
    else if (reg < 0xa0)
      reg = (reg & 0xf0) | (reg & 7);
    if ((reg & 0xf0) == 0xa0)
      value &= console_ == Console::Wii ? 0x1fffffff : 0x03ffffff;
    if ((reg & 0xf0) == 0xb0)
      value &= 0xff;
    cp_state_[reg] = value;
    command_cycles_ += 12;
    return 6;
  }
  if (op == 0x10) {
    if (bytes.size() < 5)
      return 0;
    const u32 header = word(bytes, 1);
    unsigned count = ((header >> 16) & 15) + 1, address = header & 0xffff;
    if (bytes.size() < 5 + count * 4)
      return 0;
    if (address + count > xf_state_.size())
      unavailable(0x0c008000, true, "XF register upload extends beyond transform memory");
    for (unsigned i = 0; i < count; ++i)
      xf_state_[address + i] = word(bytes, 5 + i * 4);
    command_cycles_ += 18 + count * 6;
    return 5 + count * 4;
  }
  if (op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38) {
    if (bytes.size() < 5)
      return 0;
    const u32 header = word(bytes, 1), address = header & 0xfff, count = ((header >> 12) & 15) + 1,
              array = op / 8 + 8;
    const u64 source = u64(cp_state_[0xa0 + array]) + u64(header >> 16) * cp_state_[0xb0 + array];
    if (source > 0xffffffff || address + count > xf_state_.size())
      unavailable(0x0c008000, true, "Indexed XF upload address is invalid");
    const auto data = memory_.range(static_cast<u32>(source), count * 4);
    for (unsigned i = 0; i < count; ++i)
      xf_state_[address + i] = word(data, i * 4);
    command_cycles_ += 6;
    return 5;
  }
  if (op == 0x40) {
    if (bytes.size() < 9)
      return 0;
    if (display_list_)
      unavailable(0x0c008000, true, "Recursive display lists are unsupported");
    const u32 address = word(bytes, 1) & ~u32{31}, size = word(bytes, 5) & ~u32{31};
    auto list = memory_.range(address, size);
    display_list_ = true;
    std::size_t cursor = 0;
    try {
      while (cursor < list.size()) {
        const auto length = decode(list.subspan(cursor));
        if (!length)
          break;
        cursor += length;
        ++command_count_;
      }
    } catch (...) {
      display_list_ = false;
      throw;
    }
    display_list_ = false;
    command_cycles_ += 6;
    return 9;
  }
  if (op == 0x61) {
    if (bytes.size() < 5)
      return 0;
    command_cycles_ += 12;
    write_bp(bytes[1], word(bytes, 1) & 0xffffff);
    return 5;
  }
  if (op >= 0x80 && op <= 0xbf) {
    if (bytes.size() < 3)
      return 0;
    try {
      const VertexDecoder decoder(memory_, cp_state_, xf_state_, op & 7);
      const unsigned count = (unsigned(bytes[1]) << 8) | bytes[2];
      const std::size_t size = 3 + count * decoder.vertex_size();
      if (bytes.size() < size)
        return 0;
      draw(op, count, bytes.subspan(3, size - 3), decoder);
      command_cycles_ += 6 + u64(count) * 12;
      return size;
    } catch (const VertexError &error) {
      unavailable(0x0c008000, true, error.what());
    }
  }
  std::ostringstream error;
  error << "Unsupported GX command 0x" << std::hex << unsigned(op);
  unavailable(0x0c008000, true, error.str());
}

void CommandProcessor::write_bp(unsigned index, u32 value) {
  const u32 mask = bp_state_[0xfe];
  bp_state_[index] = (bp_state_[index] & ~mask) | (value & mask);
  if (index != 0xfe)
    bp_state_[0xfe] = 0xffffff;
  value = bp_state_[index];
  if (index == 0x47 || index == 0x48) {
    token_pending_ = static_cast<u16>(value);
    token_irq_pending_ |= index == 0x48;
    if (!event_remaining_)
      event_remaining_ = std::max<u64>(500, command_cycles_);
  } else if (index == 0x45 && (value & 255) == 2) {
    finish_pending_ = true;
    if (!event_remaining_)
      event_remaining_ = std::max<u64>(500, command_cycles_);
  } else if (index == 0x52)
    copy_efb(value);
  else if (index == 0x55 || index == 0x56) {
    const unsigned offset = index == 0x55 ? 0x10 : 0x14;
    pixel_registers_[offset / 2] = static_cast<u16>(value & 1023);
    pixel_registers_[offset / 2 + 1] = static_cast<u16>((value >> 10) & 1023);
  } else if (index == 0x65 || index == 0x63)
    load_texture_memory(index, value);
  else if (index >= 0xe0 && index <= 0xe7) {
    auto &colors = (value & 0x800000) ? konst_colors_ : tev_colors_;
    auto &color = colors[(index >> 1) & 3];
    const auto component = [](u32 bits) { return static_cast<s32>((bits & 2047) ^ 1024) - 1024; };
    color[(index & 1) ? 2 : 0] = component(value);
    color[(index & 1) ? 1 : 3] = component(value >> 12);
  }
}

void CommandProcessor::load_texture_memory(unsigned index, u32 value) {
  if (index == 0x65) {
    u32 source = bp_state_[0x64] << 5;
    if (console_ == Console::GameCube)
      source &= 0x01ffffff;
    const std::size_t destination = (value & 1023) * 512;
    const std::size_t size = ((value >> 10) & 2047) * 32;
    const auto bytes = memory_.range(source, size);
    std::copy(bytes.begin(), bytes.end(), texture_memory_.begin() + destination);
    return;
  }
  if (!value)
    return;
  const u32 source = bp_state_[0x60] << 5;
  const u64 even = u64(bp_state_[0x61]) * 32, odd = u64(bp_state_[0x62]) * 32;
  const std::size_t count = value & 32767;
  if (((value >> 15) & 3) != 3) {
    if (even >= texture_memory_.size())
      return;
    const std::size_t size = std::min(count * 32, texture_memory_.size() - std::size_t(even));
    const auto bytes = memory_.range(source, size);
    std::copy(bytes.begin(), bytes.end(), texture_memory_.begin() + std::size_t(even));
    return;
  }
  if (even >= texture_memory_.size() || odd >= texture_memory_.size())
    return;
  const std::size_t tiles = std::min({count, (texture_memory_.size() - std::size_t(even)) / 32,
                                      (texture_memory_.size() - std::size_t(odd)) / 32});
  const auto bytes = memory_.range(source, tiles * 64);
  for (std::size_t i = 0; i < tiles; ++i) {
    std::copy_n(bytes.begin() + i * 64, 32, texture_memory_.begin() + std::size_t(even) + i * 32);
    std::copy_n(bytes.begin() + i * 64 + 32, 32,
                texture_memory_.begin() + std::size_t(odd) + i * 32);
  }
}

void CommandProcessor::draw(u8 opcode, unsigned count, std::span<const u8> bytes,
                            const VertexDecoder &decoder) {
  shader_.validate();
  if ((bp_state_[0x43] & 7) > 3)
    throw VertexError("Unsupported EFB pixel encoding");
  std::vector<std::optional<TransformedVertex>> vertices;
  vertices.reserve(count);
  for (unsigned i = 0; i < count; ++i) {
    const auto decoded =
        decoder.decode(bytes.subspan(i * decoder.vertex_size(), decoder.vertex_size()));
    vertices.emplace_back(
        decoded.skip ? std::nullopt : std::optional<TransformedVertex>(decoder.transform(decoded)));
  }
  rasterizer_.draw(opcode, vertices);
}

void CommandProcessor::fragment(unsigned x, unsigned y, u32 z, const FragmentInputs &inputs) {
  const std::size_t offset = std::size_t(y) * width + x;
  const u32 z_mode = bp_state_[0x40], blend = bp_state_[0x41];
  const bool early = (bp_state_[0x43] & 64) != 0;
  const auto depth_test = [&] {
    if (!(z_mode & 1))
      return true;
    if (!compare((z_mode >> 1) & 7, z, depths_[offset]))
      return false;
    if (z_mode & 16)
      depths_[offset] = z & 0xffffff;
    return true;
  };
  if (early && !depth_test())
    return;
  auto located_inputs = inputs;
  located_inputs.x = x;
  located_inputs.viewport_width = std::bit_cast<float>(xf_state_[0x101a]);
  auto color = shader_.shade(located_inputs, &z);
  if (!color || (!early && !depth_test()))
    return;
  ++fragment_count_;
  pixel_registers_[8] = std::min(pixel_registers_[8], static_cast<u16>(x & ~1u));
  pixel_registers_[9] = std::max(pixel_registers_[9], static_cast<u16>(x | 1));
  pixel_registers_[10] = std::min(pixel_registers_[10], static_cast<u16>(y & ~1u));
  pixel_registers_[11] = std::max(pixel_registers_[11], static_cast<u16>(y | 1));
  const u32 previous = this->color(x, y);
  Color destination{};
  for (unsigned i = 0; i < 4; ++i)
    destination[i] = static_cast<u8>(previous >> (24 - i * 8));
  if ((bp_state_[0x43] & 7) != 1)
    destination[3] = 255;
  if (blend & 1) {
    if (blend & 2048) {
      for (unsigned i = 0; i < 4; ++i)
        (*color)[i] = static_cast<u8>(std::max(0, int(destination[i]) - int((*color)[i])));
    } else {
      const auto factor = [&](unsigned mode, unsigned channel, bool source) -> unsigned {
        switch (mode) {
        case 0:
          return 0;
        case 1:
          return 255;
        case 2:
        case 3: {
          const unsigned component = source ? destination[channel] : (*color)[channel];
          return mode == 2 ? component : 255 - component;
        }
        case 4:
        case 5:
          return mode == 4 ? (*color)[3] : 255 - (*color)[3];
        default:
          return mode == 6 ? destination[3] : 255 - destination[3];
        }
      };
      Color result{};
      for (unsigned i = 0; i < 4; ++i) {
        unsigned sf = factor((blend >> 8) & 7, i, true), df = factor((blend >> 5) & 7, i, false);
        sf += sf >> 7;
        df += df >> 7;
        result[i] = static_cast<u8>(std::min(255u, ((*color)[i] * sf + destination[i] * df) >> 8));
      }
      color = result;
    }
  } else if (blend & 2) {
    const unsigned operation = (blend >> 12) & 15;
    for (unsigned i = 0; i < 4; ++i) {
      const unsigned s = (*color)[i], d = destination[i];
      const std::array<unsigned, 16> results{0,     s & d,  s & ~d,   s,        ~s & d, d,
                                             s ^ d, s | d,  ~(s | d), ~(s ^ d), ~d,     s | ~d,
                                             ~s,    ~s | d, ~(s & d), 255};
      (*color)[i] = static_cast<u8>(results[operation]);
    }
  }
  if (bp_state_[0x42] & 256)
    (*color)[3] = static_cast<u8>(bp_state_[0x42]);
  if ((blend & 4) && (bp_state_[0x43] & 7) == 1) {
    constexpr unsigned dither[2][2] = {{0, 2}, {3, 1}};
    for (unsigned i = 0; i < 3; ++i)
      (*color)[i] =
          static_cast<u8>(((*color)[i] - ((*color)[i] >> 6) + dither[y & 1][x & 1]) & 252);
  }
  if (!(blend & 8))
    for (unsigned i = 0; i < 3; ++i)
      (*color)[i] = destination[i];
  if (!(blend & 16))
    (*color)[3] = destination[3];
  u32 packed = 0;
  for (u8 component : *color)
    packed = (packed << 8) | component;
  write_color(x, y, packed);
}

u32 CommandProcessor::color(unsigned x, unsigned y) const {
  if (x >= width || y >= height)
    throw std::out_of_range("EFB color coordinates out of range");
  const u32 stored = colors_[y * width + x];
  const unsigned format = bp_state_[0x43] & 7;
  if (format == 1) {
    u32 value = 0;
    for (unsigned i = 0; i < 4; ++i) {
      const u32 component = (stored >> (18 - i * 6)) & 63;
      value = (value << 8) | (component << 2) | (component >> 4);
    }
    return value;
  }
  if (format > 3)
    unavailable(0x0c001000, false, "Unsupported EFB pixel encoding");
  return (stored << 8) | 255;
}

u32 CommandProcessor::depth(unsigned x, unsigned y) const {
  if (x >= width || y >= height)
    throw std::out_of_range("EFB depth coordinates out of range");
  return depths_[y * width + x];
}

u32 CommandProcessor::read_efb(u32 address) const {
  if (address < 0x08000000 || address >= 0x0c000000)
    throw BusFault(address, false, "Address is outside the CPU EFB window");
  if (address & 0x00800000)
    unavailable(address, false, "Combined color/depth EFB reads are not implemented");
  const unsigned x = (address & 0xfff) >> 2, y = (address >> 12) & 1023;
  if (x >= width || y >= height)
    return 0;
  if (address & 0x00400000)
    return depth(x, y);
  const auto rgba = color(x, y);
  const auto argb = (rgba >> 8) | ((rgba & 255) << 24);
  switch (pixel_registers_[4] & 3) {
  case 0:
    return argb & 0xffffff;
  case 1:
    return argb | 0xff000000;
  case 2:
    return argb;
  default:
    unavailable(address, false, "Invalid EFB alpha-read mode");
  }
}

void CommandProcessor::write_color(unsigned x, unsigned y, u32 value) {
  if ((bp_state_[0x43] & 7) == 1) {
    u32 packed = 0;
    for (unsigned channel = 0; channel < 4; ++channel)
      packed = (packed << 6) | ((value >> (26 - channel * 8)) & 63);
    value = packed;
  } else {
    if ((bp_state_[0x43] & 7) > 3)
      unavailable(0x0c001000, true, "Unsupported EFB pixel encoding");
    value >>= 8;
  }
  colors_[y * width + x] = value;
}

void CommandProcessor::copy_efb(u32 control) {
  unsigned x = bp_state_[0x49] & 1023, y = (bp_state_[0x49] >> 10) & 1023;
  unsigned w = (bp_state_[0x4a] & 1023) + 1, h = ((bp_state_[0x4a] >> 10) & 1023) + 1;
  if (x >= width || y >= height)
    return;
  w = std::min(w, width - x);
  h = std::min(h, height - y);
  const u32 stride = bp_state_[0x4d] << 5;
  if (stride) {
    try {
      if (control & (1 << 14))
        copy_efb_xfb(memory_, bp_state_,
                     [this](unsigned sx, unsigned sy) { return color(sx, sy); });
      else
        copy_efb_texture(memory_, bp_state_,
                         {[this](unsigned sx, unsigned sy) { return color(sx, sy); },
                          [this](unsigned sx, unsigned sy) { return depth(sx, sy); }});
    } catch (const std::exception &error) {
      unavailable(0x0c008000, true, error.what());
    }
  }
  if (control & (1 << 11)) {
    const u32 value = ((bp_state_[0x4f] & 255) << 24) | ((bp_state_[0x50] & 0xffff) << 8) |
                      ((bp_state_[0x4f] >> 8) & 255);
    for (unsigned row = y; row < y + h; ++row)
      for (unsigned column = x; column < x + w; ++column) {
        const u32 previous = color(column, row);
        const u32 color_mask =
            ((bp_state_[0x41] & 8) ? 0xffffff00 : 0) | ((bp_state_[0x41] & 16) ? 255 : 0);
        write_color(column, row, (value & color_mask) | (previous & ~color_mask));
        if (bp_state_[0x40] & 16)
          depths_[row * width + column] = bp_state_[0x51];
      }
  }
}

void CommandProcessor::advance(u64 cycles) {
  if (event_remaining_) {
    if (cycles >= event_remaining_) {
      event_remaining_ = 0;
      pixel_registers_[7] = token_pending_;
      token_irq_ |= token_irq_pending_;
      finish_irq_ |= finish_pending_;
      token_irq_pending_ = finish_pending_ = false;
      update_interrupts();
    } else
      event_remaining_ -= cycles;
  }
  command_cycles_ = 0;
  consume_fifo();
}

void CommandProcessor::update_interrupts() {
  const u32 count = fifo(0x30);
  const u16 control = registers_[1];
  const bool bp = (control & 2) && fifo(0x38) == fifo(0x3c);
  devices_.set_interrupt(0x800, (control & 1) && (((control & 4) && count > fifo(0x28)) ||
                                                  ((control & 8) && count < fifo(0x2c)) ||
                                                  ((control & 32) && bp)));
  devices_.set_interrupt(0x200, token_irq_ && (pixel_registers_[5] & 1));
  devices_.set_interrupt(0x400, finish_irq_ && (pixel_registers_[5] & 2));
}

u16 CommandProcessor::read16(u32 address) {
  if (address < 0x0c001000) {
    const unsigned offset = address & 0xfff;
    if (offset == 0) {
      const u32 count = fifo(0x30);
      const bool bp = (registers_[1] & 2) && fifo(0x38) == fifo(0x3c);
      return static_cast<u16>((count > fifo(0x28) ? 1 : 0) | (count < fifo(0x2c) ? 2 : 0) |
                              (!count || fifo(0x38) == fifo(0x34) ? 4 : 0) |
                              (!count || bp || !(registers_[1] & 1) ? 8 : 0) | (bp ? 16 : 0));
    }
    if (offset == 4)
      return 0;
    if (offset == 0x64)
      return 4;
    if (offset >= 0x40 && offset <= 0x5a)
      return 0;
    if (offset == 2 || offset == 6 || offset == 0xa || (offset >= 0x20 && offset <= 0x3e))
      return registers_[offset / 2];
  } else {
    const unsigned offset = address & 0xfff;
    if (offset <= 0xa || offset == 0xe || (offset >= 0x10 && offset <= 0x2e))
      return pixel_registers_[offset / 2];
  }
  unavailable(address, false, "Unmapped graphics register read");
}

void CommandProcessor::write16(u32 address, u16 value) {
  if (address < 0x0c001000) {
    const unsigned offset = address & 0xfff;
    if (offset == 2) {
      registers_[1] = value & 63;
      consume_fifo();
      return;
    }
    if (offset == 4) {
      return;
    }
    if (offset == 6) {
      registers_[3] = value & 7;
      return;
    }
    if (offset == 0xa) {
      registers_[5] = value & 255;
      return;
    }
    if (offset >= 0x20 && offset <= 0x3e) {
      registers_[offset / 2] =
          value & ((offset & 2) ? (console_ == Console::Wii ? 0x1fff : 0x3ff) : 0xffe0);
      if (offset == 0x32 || offset == 0x3a)
        consume_fifo();
      else
        update_interrupts();
      return;
    }
  } else {
    const unsigned offset = address & 0xfff;
    if (offset <= 8) {
      pixel_registers_[offset / 2] = value;
      return;
    }
    if (offset == 0xa) {
      if (value & 4)
        token_irq_ = false;
      if (value & 8)
        finish_irq_ = false;
      pixel_registers_[5] = value & 3;
      update_interrupts();
      return;
    }
  }
  unavailable(address, true, "Unmapped graphics register write");
}

u32 CommandProcessor::read(u32 address, unsigned bytes) {
  if ((address & 1) || bytes == 1 || (bytes != 2 && bytes != 4))
    throw BusFault(address, false, "Invalid graphics register read width");
  if (bytes == 2)
    return read16(address);
  return (u32(read16(address)) << 16) | read16(address + 2);
}

void CommandProcessor::write(u32 address, u32 value, unsigned bytes) {
  if ((address & 1) || bytes == 1 || (bytes != 2 && bytes != 4))
    throw BusFault(address, true, "Invalid graphics register write width");
  if (bytes == 2) {
    write16(address, static_cast<u16>(value));
    return;
  }
  write16(address, static_cast<u16>(value >> 16));
  write16(address + 2, static_cast<u16>(value));
}
} // namespace cupid::gfx
