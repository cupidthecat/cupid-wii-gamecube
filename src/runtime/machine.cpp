// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/machine.hpp"
#include "cupid/build_info.hpp"
#include "cupid/runtime/elf.hpp"
#include <array>
#include <iomanip>
#include <random>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cupid::runtime {
namespace {
void save_persistent(const std::filesystem::path &path, std::span<const u8> bytes) {
  if (std::filesystem::is_symlink(path))
    throw std::runtime_error("Persistent data path is a symbolic link");
  std::random_device random;
  auto temporary = path;
  temporary += ".tmp-" + std::to_string(random()) + "-" + std::to_string(random());
  if (std::filesystem::exists(temporary))
    throw std::runtime_error("Temporary data path already exists");
  try {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.flush();
    if (!file)
      throw std::runtime_error("Cannot write persistent data");
    file.close();
    if (!file)
      throw std::runtime_error("Cannot close persistent data file");
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                              "Cannot replace persistent data");
#else
    std::filesystem::rename(temporary, path);
#endif
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}
} // namespace

Machine::Machine(Console console)
    : console_(console), memory_(console), cpu_(memory_, console), devices_(console),
      graphics_(memory_, devices_, console), dsp_(memory_, console),
      ios_(memory_, devices_, console) {
  memory_.mmio_handles = [this](u32 a) {
    return (a >= 0x08000000 && a < 0x0c000000) || devices_.handles(a);
  };
  memory_.mmio_read = [this](u32 a, unsigned w) {
    if (a >= 0x08000000 && a < 0x0c000000)
      return graphics_.read_efb(a);
    return devices_.read(a, w);
  };
  memory_.mmio_write = [this](u32 a, u32 v, unsigned w) {
    if (a >= 0x08000000 && a < 0x0c000000)
      throw BusFault(a, true, "CPU EFB writes are not implemented", true);
    devices_.write(a, v, w);
  };
  memory_.fifo_write = [this](u8 value) { graphics_.gather(value); };
  memory_.memory_error = [this] { devices_.set_interrupt(1, true); };
  devices_.graphics_read = [this](u32 a, unsigned w) { return graphics_.read(a, w); };
  devices_.graphics_write = [this](u32 a, u32 v, unsigned w) { graphics_.write(a, v, w); };
  devices_.fifo_reset = [this] { graphics_.reset_fifo(); };
  devices_.dsp_read = [this](u32 a) { return dsp_.read16(a); };
  devices_.dsp_write = [this](u32 a, u16 v) { dsp_.write16(a, v); };
  dsp_.interrupt = [this] { devices_.dsp_interrupt(); };
  dsp_.aram_read = [this](u32 address, std::span<u8> bytes) { devices_.read_aram(address, bytes); };
  devices_.ipc_read = [this](u32 a) { return ios_.read(a); };
  devices_.ipc_write = [this](u32 a, u32 v) { ios_.write(a, v); };
  ios_.set_shutdown_handler([this] { shutdown_requested_ = true; });
  ios_.set_remote_speaker_handler([this](unsigned port, const audio::RemoteSpeakerPacket &packet) {
    if (output_)
      output_->remote_speaker(port, packet);
  });
  devices_.dma_write = [this](u32 a, std::span<const u8> bytes) { memory_.copy_in(a, bytes); };
  devices_.dma_read = [this](u32 a, std::span<u8> bytes) { memory_.copy_out(a, bytes); };
  devices_.disc_read = [this](u64 offset, std::span<u8> bytes) {
    if (!image_)
      throw std::runtime_error("Disc command issued without an inserted disc");
    image_->read(offset, bytes);
  };
  ios_.disc_read = devices_.disc_read;
  ios_.partition_read = [this](u64 offset, std::span<u8> bytes) {
    if (!image_)
      throw std::runtime_error("Partition command issued without an inserted disc");
    image_->read_game(offset, bytes);
  };
  devices_.xfb_present = [this](const hw::Xfb &xfb) {
    if (output_)
      output_->present(memory_, xfb);
  };
  devices_.audio_samples = [this](std::span<const s16> samples, u32 rate) {
    if (output_)
      output_->audio(samples, rate);
  };
  devices_.rumble = [this](unsigned port, bool enabled) {
    if (output_)
      output_->rumble(port, enabled);
  };
  ios_.set_remote_rumble_handler([this](unsigned port, bool enabled) {
    if (output_)
      output_->rumble(port, enabled);
  });
}

void Machine::set_output(platform::Output *output) {
  output_ = output;
  for (unsigned port = 0; port < 4; ++port)
    ios_.set_remote_speaker_processing(port, output && output->remote_speaker_processing());
}

void Machine::set_limits(const RunLimits &limits) {
  limits_ = limits;
  started_ = std::chrono::steady_clock::now();
}

void Machine::set_trace(const std::filesystem::path &path, u64 count, bool after_boot, u64 start) {
  auto parent = path.parent_path();
  if (!parent.empty())
    std::filesystem::create_directories(parent);
  trace_.open(path, std::ios::out | std::ios::trunc);
  if (!trace_)
    throw std::runtime_error("Cannot open execution trace");
  trace_count_ = count;
  trace_after_boot_ = after_boot;
  trace_emitted_ = 0;
  trace_start_ = start;
}

void Machine::set_user_directory(const std::filesystem::path &path) {
  user_directory_ = path;
  std::filesystem::create_directories(path);
  ios_.set_user_directory(path);
  const auto sram = path / "gc-sram.bin";
  if (std::filesystem::exists(sram)) {
    auto bytes = read_file(sram, 0x44);
    if (bytes.size() != 0x44)
      throw std::runtime_error("Persistent SRAM has an invalid length");
    devices_.set_sram(bytes);
  }
  if (console_ == Console::GameCube) {
    for (unsigned slot = 0; slot < 2; ++slot) {
      const auto card = path / (slot == 0 ? "memory-card-a.raw" : "memory-card-b.raw");
      if (std::filesystem::exists(card))
        devices_.set_memory_card(slot, read_file(card, 32 * 1024 * 1024));
      else {
        std::array<u8, 12> flash{};
        std::random_device random;
        for (auto &byte : flash)
          byte = static_cast<u8>(random());
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
        const u64 format_time = static_cast<u64>(seconds - 946684800) * 40500000;
        auto bytes = hw::Devices::format_memory_card(16, format_time, flash);
        save_persistent(card, bytes);
        devices_.set_memory_card(slot, bytes);
      }
    }
  }
}

void Machine::install_bios(const Bios &bios) {
  devices_.set_ipl(bios.decoded);
}

void Machine::install_fonts(const IplFonts &fonts) {
  devices_.set_ipl_fonts(fonts.shift_jis.bytes, fonts.western.bytes);
}

void Machine::set_input_replay(const std::filesystem::path &path) {
  input_replay_ = std::make_unique<platform::InputReplay>(path);
}

void Machine::load_executable(std::span<const u8> file, bool elf) {
  shutdown_requested_ = false;
  if (input_replay_)
    input_replay_->reset();
  memory_.clear();
  cpu_.reset();
  cpu_.configure_boot();
  initialize_boot_memory(memory_, console_);
  boot_ = elf ? load_elf(memory_, file) : load_dol(memory_, file);
  cpu_.state().pc = boot_.entry;
  cpu_.state().gpr[1] = 0x816ffff0;
}

void Machine::load_disc(const std::filesystem::path &path) {
  shutdown_requested_ = false;
  if (input_replay_)
    input_replay_->reset();
  image_ = std::make_unique<disc::Image>(path);
  if (image_->header().console != console_)
    throw std::runtime_error("Disc console differs from the selected console mode");
  memory_.clear();
  cpu_.reset();
  if (console_ == Console::Wii) {
    const auto version = image_->required_ios();
    if (!version || !ios::initialize_ios_memory(memory_, *version))
      throw std::runtime_error("Disc requests an unsupported IOS memory profile");
    ios_.set_version(*version);
    ios_.set_partition_open(true);
    memory_.write32(0x3194, 0);
    memory_.write32(0x3198, static_cast<u32>(image_->game_partition_offset() >> 2));
  }
  boot_disc(memory_, cpu_, *image_, [this] { step(); }, boot_);
  if (console_ == Console::Wii) {
    const auto metadata = image_->wii_metadata();
    if (!metadata ||
        !ios_.set_title_context(metadata->ticket, metadata->tmd, metadata->partition_offset))
      throw std::runtime_error("Disc title metadata cannot establish an IOS context");
  }
}

void Machine::step() {
  if (shutdown_requested_)
    throw LimitReached("guest-shutdown");
  const auto &state = cpu_.state();
  if (limits_.instructions && state.instructions >= limits_.instructions)
    throw LimitReached("instruction-limit");
  if (limits_.frames && output_ && output_->presented_frames() >= limits_.frames)
    throw LimitReached("frame-limit");
  if (input_replay_ && input_replay_->update(state.instructions, devices_.field_count())) {
    const auto &input = input_replay_->state();
    for (unsigned port = 0; port < input.pads.size(); ++port) {
      devices_.set_pad(port, input.pads[port]);
      if (console_ == Console::Wii)
        ios_.set_remote(port, input.remotes[port]);
    }
  }
  if (state.instructions - last_poll_ >= 4096 || state.instructions == 0) {
    last_poll_ = state.instructions;
    if (limits_.seconds > 0 &&
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count() >=
            limits_.seconds)
      throw LimitReached("time-limit");
    if (output_) {
      if (!output_->poll(devices_, !input_replay_))
        throw LimitReached("window-closed");
      if (console_ == Console::Wii && !input_replay_)
        ios_.set_remote(0, output_->remote_state());
    }
  }
  const u32 before = state.pc;
  cpu_.set_external_interrupt(devices_.external_interrupt_pending());
  const u32 cycles = cpu_.step();
  graphics_.advance(cycles);
  dsp_.advance(cycles);
  ios_.advance(cycles);
  devices_.advance(cycles);
  if (trace_ && state.instructions >= trace_start_ && trace_emitted_ < trace_count_ &&
      (!trace_after_boot_ || boot_.stage == "game-entry")) {
    ++trace_emitted_;
    trace_ << "{\"step\":" << state.instructions << ",\"pc_before\":" << before
           << ",\"pc_after\":" << state.pc << ",\"opcode\":" << state.last_opcode
           << ",\"cr\":" << state.cr << ",\"msr\":" << state.msr
           << ",\"lr\":" << state.spr[cpu::spr::lr] << ",\"ctr\":" << state.spr[cpu::spr::ctr]
           << ",\"dar\":" << state.spr[cpu::spr::dar] << ",\"dsisr\":" << state.spr[cpu::spr::dsisr]
           << ",\"srr0\":" << state.spr[cpu::spr::srr0] << ",\"gpr\":[";
    for (unsigned i = 0; i < state.gpr.size(); ++i)
      trace_ << (i ? "," : "") << state.gpr[i];
    trace_ << "]}\n";
    if (!trace_)
      throw std::runtime_error("Cannot write execution trace");
  }
}

void Machine::run() {
  for (;;)
    step();
}

void Machine::persist() {
  if (user_directory_.empty())
    return;
  const auto bytes = devices_.sram();
  save_persistent(user_directory_ / "gc-sram.bin", bytes);
  for (unsigned slot = 0; slot < 2; ++slot) {
    if (!devices_.memory_card_dirty(slot))
      continue;
    save_persistent(user_directory_ / (slot == 0 ? "memory-card-a.raw" : "memory-card-b.raw"),
                    devices_.card_image(slot));
    devices_.mark_memory_card_saved(slot);
  }
}

void Machine::write_state(std::ostream &out, const std::string &outcome) const {
  const auto &state = cpu_.state();
  const auto host_audio = output_ ? output_->host_audio_stats() : platform::HostAudioStats{};
  out << "{\"version\":\"" << build::version << "\",\"commit\":\"" << build::commit
      << "\",\"console\":\"" << (console_ == Console::Wii ? "wii" : "gamecube")
      << "\",\"outcome\":\"" << outcome << "\",\"stage\":\"" << boot_.stage
      << "\",\"pc\":" << state.pc << ",\"entry\":" << boot_.entry
      << ",\"instructions\":" << state.instructions << ",\"cycles\":" << state.cycles
      << ",\"vi_fields\":" << devices_.field_count()
      << ",\"presented_frames\":" << (output_ ? output_->presented_frames() : 0)
      << ",\"audio_samples\":" << (output_ ? output_->audio_samples() : 0)
      << ",\"audio_nonzero_samples\":" << (output_ ? output_->audio_nonzero_samples() : 0)
      << ",\"audio_peak\":" << (output_ ? output_->audio_peak() : 0)
      << ",\"audio_hash\":" << (output_ ? output_->audio_hash() : 0)
      << ",\"host_audio_frames\":" << host_audio.mixed_frames
      << ",\"host_audio_nonzero_samples\":" << host_audio.nonzero_samples
      << ",\"host_audio_dropped_granules\":" << host_audio.dropped_granules
      << ",\"gx_commands\":" << graphics_.command_count()
      << ",\"gx_fragments\":" << graphics_.fragment_count()
      << ",\"loaded_sections\":" << boot_.loaded_sections
      << ",\"loaded_bytes\":" << boot_.loaded_bytes
      << ",\"frame_hash\":" << (output_ ? output_->last_frame_hash() : 0) << ",\"cr\":" << state.cr
      << ",\"msr\":" << state.msr << ",\"srr0\":" << state.spr[cpu::spr::srr0]
      << ",\"srr1\":" << state.spr[cpu::spr::srr1] << ",\"dar\":" << state.spr[cpu::spr::dar]
      << ",\"dsisr\":" << state.spr[cpu::spr::dsisr] << ",\"gpr\":[";
  for (unsigned i = 0; i < state.gpr.size(); ++i)
    out << (i ? "," : "") << state.gpr[i];
  out << "]}\n";
}
} // namespace cupid::runtime
