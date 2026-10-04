// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/memory.hpp"
#include "cupid/gfx/hardware_renderer.hpp"
#include "cupid/gfx/rasterizer.hpp"
#include "cupid/hw/devices.hpp"
#include <array>
#include <span>
#include <vector>

namespace cupid::gfx {
class CommandProcessor {
public:
  CommandProcessor(Memory &memory, hw::Devices &devices, Console console);
  void gather(u8 byte);

  void advance(u64 cycles) {
    if (event_remaining_ || ((registers_[1] & 1) && fifo(0x30) >= 32)) {
      advance_pending(cycles);
      return;
    }
    command_cycles_ = 0;
    devices_.set_interrupts(0xe00, interrupt_causes_);
  }

  void reset_fifo();
  u32 read(u32 address, unsigned width);
  u32 read_efb(u32 address) const;
  void write_efb(u32 address, u32 value);
  void set_hardware_rendering(bool enabled);
  std::string hardware_device() const;
  HardwareStats hardware_stats() const;
  std::string hardware_fallback_reason() const;
  void write(u32 address, u32 value, unsigned width);
  // Feeds already captured command bytes; useful for display lists and regression tests.
  void submit(std::span<const u8> data);

  u32 cp_state(unsigned index) const {
    return cp_state_.at(index);
  }

  u32 bp_state(unsigned index) const {
    return bp_state_.at(index);
  }

  u32 xf_state(unsigned index) const {
    return xf_state_.at(index);
  }

  u32 color(unsigned x, unsigned y) const;
  u32 depth(unsigned x, unsigned y) const;

  u64 command_count() const {
    return command_count_;
  }

  u64 fragment_count() const {
    synchronize_hardware();
    return fragment_count_;
  }

private:
  Memory &memory_;
  hw::Devices &devices_;
  Console console_;
  std::array<u16, 0x33> registers_{};
  mutable std::array<u16, 0x18> pixel_registers_{};
  std::array<u32, 256> cp_state_{}, bp_state_{};
  std::array<u32, 0x1058> xf_state_{};
  mutable std::vector<u32> colors_, depths_;
  std::unique_ptr<HardwareRenderer> hardware_;
  bool hardware_upload_pending_ = true;
  TevRegisters tev_colors_{}, konst_colors_{};
  std::vector<u8> texture_memory_;
  mutable FragmentShader shader_;
  Rasterizer rasterizer_;
  std::array<u8, 32> pipe_{};
  unsigned pipe_size_ = 0;
  std::vector<u8> pending_;
  u64 command_count_ = 0, command_cycles_ = 0;
  mutable u64 fragment_count_ = 0;
  mutable u64 hardware_fragment_count_ = 0;
  u64 event_remaining_ = 0;
  u32 interrupt_causes_ = 0;
  u16 token_pending_ = 0;
  bool token_irq_pending_ = false, finish_pending_ = false, token_irq_ = false, finish_irq_ = false;
  bool display_list_ = false;
  u32 fifo(unsigned low_offset) const;
  void fifo(unsigned low_offset, u32 value);
  u16 read16(u32 address);
  void write16(u32 address, u16 value);
  void consume_fifo();
  void advance_pending(u64 cycles);
  std::size_t decode(std::span<const u8> bytes);
  void write_bp(unsigned index, u32 value);
  void load_texture_memory(unsigned index, u32 value);
  void draw(u8 opcode, unsigned count, std::span<const u8> bytes, const VertexDecoder &decoder);
  void fragment(unsigned x, unsigned y, u32 depth, const FragmentInputs &inputs);
  void copy_efb(u32 control);
  void update_interrupts();
  void write_color(unsigned x, unsigned y, u32 color);
  void synchronize_hardware() const;
  void upload_hardware();
};
} // namespace cupid::gfx
