// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/machine.hpp"
#include "cupid/build_info.hpp"
#include "cupid/runtime/elf.hpp"
#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
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
u64 cpu_state_hash(const cpu::State &state) {
  u64 hash = 14695981039346656037ull;
  const auto append = [&](u64 value, unsigned width) {
    for (unsigned n = 0; n < width; ++n)
      hash = (hash ^ static_cast<u8>(value >> ((width - n - 1) * 8))) * 1099511628211ull;
  };
  for (u32 value : state.gpr)
    append(value, 4);
  for (const auto &reg : state.fpr)
    for (u64 lane : reg.bits)
      append(lane, 8);
  for (u32 value : state.spr)
    append(value, 4);
  for (u32 value : state.sr)
    append(value, 4);
  for (u32 value : {state.pc, state.msr, state.cr, state.fpscr, state.last_pc, state.last_opcode})
    append(value, 4);
  for (u64 value : {state.cycles, state.instructions, state.timebase})
    append(value, 8);
  return hash;
}

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
      graphics_.write_efb(a, v);
    else
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
  devices_.disc_audio_samples = [this](std::span<const s16> samples, u32 divisor, u8 left,
                                       u8 right) {
    if (output_)
      output_->disc_audio(samples, divisor, left, right);
  };
  devices_.disc_audio_control = [this](u32 divisor, u8 left, u8 right) {
    if (output_)
      output_->disc_audio_control(divisor, left, right);
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
  auto replay = std::make_unique<platform::InputReplay>(path);
  input_replay_ = std::move(replay);
  input_replay_follower_.reset();
  input_follow_diagnostic_ = {};
}

void Machine::set_input_replay_follow(const std::filesystem::path &path,
                                      std::function<void(const std::string &)> diagnostic) {
  auto follower = std::make_unique<platform::InputReplayFollower>(path);
  auto replay = follower->refresh();
  if (!replay)
    throw std::runtime_error("Cannot initialize input replay follow mode: " + follower->error());
  input_replay_ = std::move(replay);
  input_replay_follower_ = std::move(follower);
  input_follow_diagnostic_ = std::move(diagnostic);
  last_input_follow_poll_ = std::chrono::steady_clock::now();
  if (input_follow_diagnostic_)
    input_follow_diagnostic_("Following input replay: accepted version 1");
}

void Machine::refresh_input_replay() {
  const auto rejected = input_replay_follower_->rejected_updates();
  const auto status = input_replay_follower_->status();
  if (auto replay = input_replay_follower_->refresh()) {
    input_replay_ = std::move(replay);
    if (input_follow_diagnostic_)
      input_follow_diagnostic_("Following input replay: accepted version " +
                               std::to_string(input_replay_follower_->accepted_updates()));
  } else if (input_follow_diagnostic_) {
    if (input_replay_follower_->rejected_updates() != rejected)
      input_follow_diagnostic_("Input replay replacement rejected: " +
                               input_replay_follower_->error());
    else if (status == platform::InputReplayFollowStatus::Rejected &&
             input_replay_follower_->status() != platform::InputReplayFollowStatus::Rejected)
      input_follow_diagnostic_("Input replay source recovered; accepted replay retained");
  }
}

void Machine::load_executable(std::span<const u8> file, bool elf) {
  shutdown_requested_ = false;
  if (input_replay_)
    input_replay_->reset();
  memory_.clear();
  cpu_.reset();
  step_attempts_ = last_poll_ = 0;
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
  std::array<u8, 32> disc_id{};
  image_->read(0, disc_id);
  devices_.prepare_disc_boot(console_ == Console::GameCube && disc_id[8] != 0, disc_id[9]);
  memory_.clear();
  cpu_.reset();
  step_attempts_ = last_poll_ = 0;
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

template <bool Grouped> void Machine::step_internal() {
  if (shutdown_requested_)
    throw LimitReached("guest-shutdown");
  const auto &state = cpu_.state();
  if (limits_.instructions && step_attempts_ >= limits_.instructions)
    throw LimitReached("instruction-limit");
  if (limits_.frames && output_ && output_->presented_frames() >= limits_.frames)
    throw LimitReached("frame-limit");
  if (step_attempts_ - last_poll_ >= 4096 || step_attempts_ == 0) {
    last_poll_ = step_attempts_;
    const auto now = std::chrono::steady_clock::now();
    if (limits_.seconds > 0 &&
        std::chrono::duration<double>(now - started_).count() >= limits_.seconds)
      throw LimitReached("time-limit");
    if (input_replay_follower_ && now - last_input_follow_poll_ >= std::chrono::milliseconds(100)) {
      last_input_follow_poll_ = now;
      refresh_input_replay();
    }
    if (output_) {
      if (!output_->poll(devices_, !input_replay_))
        throw LimitReached("window-closed");
      if (console_ == Console::Wii && !input_replay_)
        ios_.set_remote(0, output_->remote_state());
    }
  }
  if (input_replay_ && input_replay_->needs_update(state.instructions, devices_.field_count()) &&
      input_replay_->update(state.instructions, devices_.field_count())) {
    const auto &input = input_replay_->state();
    for (unsigned port = 0; port < input.pads.size(); ++port) {
      devices_.set_pad(port, input.pads[port]);
      if (console_ == Console::Wii)
        ios_.set_remote(port, input.remotes[port]);
    }
  }
  if constexpr (Grouped) {
    if (!trace_.is_open() && cpu_.integer_run_possible()) {
      constexpr u64 maximum = std::numeric_limits<u64>::max();
      u64 instructions =
          std::min({u64{4096}, maximum - state.instructions, maximum - step_attempts_,
                    u64{4096} - (step_attempts_ - last_poll_)});
      if (limits_.instructions)
        instructions = std::min(instructions, limits_.instructions - step_attempts_);
      if (input_replay_)
        instructions = std::min(instructions, input_replay_->event_free_instructions(
                                                  state.instructions, devices_.field_count()));
      const u64 cycles =
          std::min({u64{4096}, graphics_.event_free_cycles(), dsp_.event_free_cycles(),
                    ios_.event_free_cycles(), devices_.event_free_cycles()});
      if (instructions >= 2 && cycles >= 2) {
        cpu_.set_external_interrupt(devices_.external_interrupt_pending());
        const auto grouped =
            cpu_.try_integer_run(static_cast<u32>(instructions), static_cast<u32>(cycles));
        if (grouped.instructions) {
          step_attempts_ += grouped.instructions;
          instructions -= grouped.instructions;
          u64 remaining_cycles = cycles - grouped.cycles;
          u32 elapsed = grouped.cycles;
          // Entry callbacks can reach a limit or the next host poll. Keep the
          // existing single admission when that boundary is already due.
          const bool single_group =
              shutdown_requested_ ||
              (limits_.instructions && step_attempts_ >= limits_.instructions) ||
              (limits_.frames && output_ && output_->presented_frames() >= limits_.frames) ||
              step_attempts_ - last_poll_ >= 4096;
          bool declined = false;
          const auto flush = [&] {
            graphics_.advance(elapsed);
            dsp_.advance(elapsed);
            ios_.advance(elapsed);
            devices_.advance(elapsed);
          };
          try {
            while (!single_group && instructions >= 2 && remaining_cycles >= 2) {
              if (!cpu_.integer_run_possible()) {
                declined = true;
                break;
              }
              const auto next = cpu_.try_integer_run(static_cast<u32>(instructions),
                                                     static_cast<u32>(remaining_cycles));
              if (!next.instructions) {
                declined = true;
                break;
              }
              step_attempts_ += next.instructions;
              instructions -= next.instructions;
              remaining_cycles -= next.cycles;
              elapsed += next.cycles;
            }
          } catch (...) {
            // Make completed groups visible before a failed admission unwinds.
            flush();
            throw;
          }
          flush();
          if (!declined)
            return;
          // The next instruction can access MMIO or deliver an interrupt.
          // Flush the quiet interval before executing it, without retrying
          // the rejected group.
        }
      }
    }
  }
  const u32 before = state.pc;
  cpu_.set_external_interrupt(devices_.external_interrupt_pending());
  ++step_attempts_;
  const u32 cycles = cpu_.step();
  graphics_.advance(cycles);
  dsp_.advance(cycles);
  ios_.advance(cycles);
  devices_.advance(cycles);
  if (trace_ && state.instructions >= trace_start_ && trace_emitted_ < trace_count_ &&
      (!trace_after_boot_ || boot_.stage == "game-entry")) {
    ++trace_emitted_;
    trace_ << "{\"step\":" << state.instructions << ",\"attempt\":" << step_attempts_
           << ",\"pc_before\":" << before << ",\"pc_after\":" << state.pc
           << ",\"opcode\":" << state.last_opcode
           << ",\"opcode_fetched\":" << (cpu_.last_step_fetched_instruction() ? "true" : "false")
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

void Machine::step() {
  step_internal<false>();
}

void Machine::run() {
  for (;;)
    step_internal<true>();
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
  const auto disc_audio = output_ ? output_->disc_audio_stats() : platform::DiscAudioStats{};
  const auto hardware = graphics_.hardware_stats();
  const auto native = cpu_.native_statistics();
  out << "{\"version\":\"" << build::version << "\",\"commit\":\"" << build::commit
      << "\",\"console\":\"" << (console_ == Console::Wii ? "wii" : "gamecube")
      << "\",\"outcome\":\"" << outcome << "\",\"stage\":\"" << boot_.stage
      << "\",\"pc\":" << state.pc << ",\"entry\":" << boot_.entry
      << ",\"instructions\":" << state.instructions << ",\"step_attempts\":" << step_attempts_
      << ",\"cycles\":" << state.cycles << ",\"vi_fields\":" << devices_.field_count()
      << ",\"cpu_backend\":\""
      << (cpu_.execution_backend() == cpu::ExecutionBackend::NativeInteger ? "native"
                                                                           : "interpreter")
      << "\",\"native_executed\":" << native.executed << ",\"native_fallback\":" << native.fallback
      << ",\"native_compilations\":" << native.compiled_operations
      << ",\"native_cache_hits\":" << native.cache_hits
      << ",\"native_capacity_fallbacks\":" << native.capacity_fallbacks
      << ",\"native_cache_entries\":" << native.cache_entries
      << ",\"native_executable_bytes\":" << native.executable_bytes
      << ",\"native_block_runs\":" << native.block_runs
      << ",\"native_block_executed\":" << native.block_executed
      << ",\"native_block_compilations\":" << native.block_compilations
      << ",\"native_block_cache_hits\":" << native.block_cache_hits
      << ",\"native_block_replacements\":" << native.block_replacements
      << ",\"native_block_cache_entries\":" << native.block_cache_entries
      << ",\"native_block_executable_bytes\":" << native.block_executable_bytes
      << ",\"native_block_compilation_failures\":" << native.block_compilation_failures
      << ",\"cpu_state_hash\":" << cpu_state_hash(state)
      << ",\"memory_state_hash\":" << memory_.state_hash() << ",\"input_replay_updates\":"
      << (input_replay_follower_ ? input_replay_follower_->accepted_updates() : 0)
      << ",\"input_replay_rejections\":"
      << (input_replay_follower_ ? input_replay_follower_->rejected_updates() : 0)
      << ",\"presented_frames\":" << (output_ ? output_->presented_frames() : 0)
      << ",\"audio_samples\":" << (output_ ? output_->audio_samples() : 0)
      << ",\"audio_nonzero_samples\":" << (output_ ? output_->audio_nonzero_samples() : 0)
      << ",\"audio_peak\":" << (output_ ? output_->audio_peak() : 0)
      << ",\"audio_hash\":" << (output_ ? output_->audio_hash() : 0)
      << ",\"disc_audio_samples\":" << disc_audio.samples
      << ",\"disc_audio_nonzero_samples\":" << disc_audio.nonzero_samples
      << ",\"disc_audio_peak\":" << disc_audio.peak << ",\"disc_audio_hash\":" << disc_audio.hash
      << ",\"host_audio_frames\":" << host_audio.mixed_frames
      << ",\"host_audio_nonzero_samples\":" << host_audio.nonzero_samples
      << ",\"host_audio_dropped_granules\":" << host_audio.dropped_granules
      << ",\"page_table_write_errors\":" << memory_.page_table_write_errors()
      << ",\"instruction_backing_read_errors\":" << memory_.instruction_backing_read_errors()
      << ",\"gx_commands\":" << graphics_.command_count()
      << ",\"gx_fragments\":" << graphics_.fragment_count()
      << ",\"hardware_dispatches\":" << hardware.dispatches
      << ",\"hardware_fragments\":" << hardware.fragments
      << ",\"hardware_fallback_rectangles\":" << hardware.fallback_rectangles
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
