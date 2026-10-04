// SPDX-License-Identifier: GPL-2.0-or-later
#include "Common/Swap.h"
#include "Core/HW/StreamADPCM.h"
#include "cupid/hw/devices.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <span>
#include <vector>

namespace DVD {
#define CUPID_DISC_REFERENCE_ENUMS
#include "disc_stream_reference_functions.inc"
#undef CUPID_DISC_REFERENCE_ENUMS
} // namespace DVD

namespace DiscIO {
constexpr unsigned PARTITION_NONE = 0;
}

namespace AudioInterface {
enum class SampleRate { AI32KHz, AI48KHz };
}

class Mixer {
public:
  static constexpr u32 FIXED_SAMPLE_RATE_DIVIDEND = 108000000;

  void PushStreamingSamples(const s16 *samples, u32 frames) {
    if (!frames)
      return;
    auto &packet = packets.emplace_back(samples, samples + frames * 2);
    for (auto &sample : packet)
      sample = static_cast<s16>(Common::swap16(sample));
  }

  std::vector<std::vector<s16>> packets;
};

class SoundStream {
public:
  Mixer *GetMixer() {
    return &mixer;
  }

  Mixer mixer;
};

namespace {
void encoded_samples(u64 offset, std::span<u8> bytes) {
  for (std::size_t n = 0; n < bytes.size(); ++n)
    bytes[n] = static_cast<u8>((offset + n) * 71 + ((offset + n) >> 7));
}

struct ReferenceHost {
  struct Audio {
    u32 GetAISSampleRateDivisor() const {
      return divisor;
    }

    AudioInterface::SampleRate GetAISSampleRate() const {
      return divisor >= 3000 ? AudioInterface::SampleRate::AI32KHz
                             : AudioInterface::SampleRate::AI48KHz;
    }

    bool IsPlaying() const {
      return playing;
    }

    u32 divisor = 2248;
    bool playing = false;
  } audio;

  struct Timers {
    u64 GetTicksPerSecond() const {
      return clock;
    }

    u64 clock = 486000000;
  } timers;

  struct Reads {
    void StartRead(u64 offset, u32 length, unsigned, DVD::ReplyType, s64 ticks) {
      requests.emplace_back(offset, length);
      host.data.resize(length);
      encoded_samples(offset, host.data);
      host.failed = fail;
      host.remaining = static_cast<u64>(ticks);
    }

    ReferenceHost &host;
    bool fail = false;
    std::vector<std::pair<u64, u32>> requests;
  } reads{*this, false, {}};

  struct Timing {
    void ScheduleEvent(s64 ticks, unsigned, u64) {
      host.data.clear();
      host.failed = false;
      host.remaining = static_cast<u64>(ticks);
    }

    ReferenceHost &host;
  } timing{*this};

  Audio &GetAudioInterface() {
    return audio;
  }

  Timers &GetSystemTimers() {
    return timers;
  }

  Reads &GetDVDThread() {
    return reads;
  }

  Timing &GetCoreTiming() {
    return timing;
  }

  SoundStream *GetSoundStream() {
    return &sound;
  }

  bool IsTriforce() const {
    return false;
  }

  SoundStream sound;
  u64 remaining = 0;
  bool failed = false;
  std::vector<u8> data;
};
} // namespace

namespace DVD {
// The harness supplies only the surrounding host seams and observable state.
// Definitions below this class come from the unchanged external handlers.
class DVDInterface {
public:
  explicit DVDInterface(ReferenceHost &host) : m_system(host) {
  }

  size_t ProcessDTKSamples(s16 *, size_t, std::span<const u8>);
  u32 AdvanceDTK(u32, u32 *);
  void DTKStreamingCallback(DIInterruptType, std::span<const u8>, s64);
  void ResetDrive(bool);
  bool CheckReadPreconditions();
  void AudioBufferConfig(bool, u8);
  void ExecuteCommand(u32, u32 = 0, u32 = 0);

  bool IsDiscInside() const {
    return inserted;
  }

  void SetDriveState(DriveState state) {
    m_drive_state = state;
  }

  void SetDriveError(DriveError error) {
    m_error_code = error;
  }

  void advance(u64 cycles) {
    while (cycles >= m_system.remaining) {
      cycles -= m_system.remaining;
      const auto data = m_system.data;
      const auto interrupt = m_system.failed ? DIInterruptType::DEINT : DIInterruptType::TCINT;
      if (m_system.failed)
        SetDriveError(DriveError::ReadError);
      DTKStreamingCallback(interrupt, data, 0);
    }
    m_system.remaining -= cycles;
  }

  ReferenceHost &m_system;
  bool inserted = true, m_stream = false, m_stop_at_track_end = false, m_enable_dtk = false;
  u8 m_dtk_buffer_length = 0;
  u64 m_audio_position = 0, m_next_start = 0, m_current_start = 0;
  u64 m_read_buffer_start_offset = 0, m_read_buffer_end_offset = 0;
  u64 m_read_buffer_start_time = 0, m_read_buffer_end_time = 0;
  u32 m_next_length = 0, m_current_length = 0, m_pending_blocks = 0, m_DIIMMBUF = 0;
  std::array<u32, 3> m_DICMDBUF{};
  DriveState m_drive_state = DriveState::ReadyNoReadsMade;
  DriveError m_error_code = DriveError::None;
  DIInterruptType m_last_interrupt = DIInterruptType::TCINT;
  StreamADPCM::ADPCMDecoder m_adpcm_decoder;
  unsigned m_finish_executing_command = 0;
};

#define DEBUG_LOG_FMT(...)                                                                         \
  do {                                                                                             \
  } while (false)
#define INFO_LOG_FMT(...)                                                                          \
  do {                                                                                             \
  } while (false)
#define ERROR_LOG_FMT(...)                                                                         \
  do {                                                                                             \
  } while (false)
#define ASSERT(value) CHECK(value)
#include "disc_stream_reference_functions.inc"
#undef ASSERT
#undef ERROR_LOG_FMT
#undef INFO_LOG_FMT
#undef DEBUG_LOG_FMT
} // namespace DVD

void check_disc_stream_schedule() {
  using namespace cupid;
  u64 callbacks = 0, commands = 0;
  for (Console console : {Console::GameCube, Console::Wii}) {
    for (unsigned scenario = 0; scenario < 16; ++scenario) {
      ReferenceHost host;
      host.audio.divisor = console == Console::Wii ? 2250 : 2248;
      host.timers.clock = console == Console::Wii ? 729000000 : 486000000;
      DVD::DVDInterface reference(host);
      reference.ResetDrive(true);
      reference.SetDriveState(DVD::DriveState::ReadyNoReadsMade);
      reference.DTKStreamingCallback(DVD::DIInterruptType::TCINT, {}, 0);
      host.sound.mixer.packets.clear(); // Initialization queues zero frames.
      hw::Devices observed(console);
      std::vector<std::pair<u64, u32>> reads;
      std::vector<std::vector<s16>> packets;
      observed.disc_read = [&](u64 offset, std::span<u8> bytes) {
        reads.emplace_back(offset, static_cast<u32>(bytes.size()));
        if (host.reads.fail)
          throw std::out_of_range("Authored differential stream failure");
        encoded_samples(offset, bytes);
      };
      observed.disc_audio_samples = [&](std::span<const s16> samples, u32 divisor, u8, u8) {
        CHECK(divisor == host.audio.divisor);
        packets.emplace_back(samples.begin(), samples.end());
      };
      observed.prepare_disc_boot(false, 10);
      auto command = [&](u32 word, u32 offset = 0, u32 length = 0) {
        reference.ExecuteCommand(word, offset, length);
        const auto result = observed.disc_immediate(word, offset, length);
        CHECK(result.failed == (reference.m_last_interrupt == DVD::DIInterruptType::DEINT));
        CHECK(result.immediate == reference.m_DIIMMBUF);
        const auto state = static_cast<u32>(reference.m_drive_state);
        CHECK(result.error ==
              (static_cast<u32>(reference.m_error_code) | ((state ? state - 1 : 0) << 24)));
        ++commands;
      };
      command(0xe2000000);
      command(0xe401000a);
      command(0xe1050000);
      command(0xe401000a);
      command(0xe0000000);
      command(0xe1000000, 0x2000 + scenario * 64, 32 * (scenario + 1));
      for (unsigned event = 0; event < 400; ++event) {
        const u32 divisor =
            (console == Console::Wii ? 2250u : 2248u) * (event % 7 < 3 ? 3u : 2u) / 2;
        host.audio.divisor = divisor;
        host.audio.playing = event % 13 != 0;
        observed.write(0x0c006c00, (host.audio.playing ? 1u : 0u) | (divisor < 3000 ? 2u : 0u), 4);
        host.reads.fail = event % 31 == 17;
        if (event % 37 == 1)
          command(0xe1000000, 0x4000 + event * 32, 32 * (event % 17 + 1));
        if (event % 37 == 19)
          command(0xe1000000);
        if (event % 43 == 11)
          command(0xe1010000);
        const u64 amount = host.remaining;
        reference.advance(amount);
        observed.advance(amount);
        CHECK(reads == host.reads.requests);
        CHECK(packets == host.sound.mixer.packets);
        for (u32 status = 0; status < 4; ++status)
          command(0xe2000000 | (status << 16));
        command(0xe0000000);
        ++callbacks;
      }
      // Resetting drive state leaves an already scheduled failed completion live.
      reference.ResetDrive(true);
      reference.SetDriveState(DVD::DriveState::ReadyNoReadsMade);
      observed.reset_disc(true);
      observed.prepare_disc_boot(false, 10);
      command(0xe401000a);
      command(0xe1000000, 0x8000, 1920);
      host.audio.playing = true;
      host.reads.fail = true;
      observed.write(0x0c006c00, 1 | (host.audio.divisor < 3000 ? 2 : 0), 4);
      const u64 scheduling = host.remaining;
      reference.advance(scheduling);
      observed.advance(scheduling);
      reference.ResetDrive(true);
      observed.reset_disc(true);
      const u64 completion = host.remaining;
      reference.advance(completion);
      observed.advance(completion);
      command(0xe0000000);
      CHECK(reads == host.reads.requests);
      CHECK(packets == host.sound.mixer.packets);
      callbacks += 2;
    }
  }
  std::cout << callbacks << " drive callback intervals, " << commands << " exact command results\n";
}
