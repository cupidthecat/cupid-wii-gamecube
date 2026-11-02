// SPDX-License-Identifier: GPL-2.0-or-later
#include "AudioCommon/Enums.h"
#include "AudioCommon/Mixer.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "cupid/audio/host_mixer.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
bool running = true;
u64 config_version = 0, checkpoints = 0, bytes_compared = 0, overflow_warnings = 0;
u64 serialized_hash = 14695981039346656037ull;
std::map<std::size_t, Config::ConfigChangedCallback> callbacks;
std::size_t next_callback = 0;

[[noreturn]] void unused() {
  throw std::logic_error("Unexercised mixer service");
}
} // namespace

// Ordinary constructed setting and platform objects provide only fixture inputs.
namespace Config {
const Info<AudioCommon::DPL2Quality> MAIN_DPL2_QUALITY{{}, AudioCommon::DPL2Quality::Low};
const Info<int> MAIN_AUDIO_BUFFER_SIZE{{}, 80};
const Info<bool> MAIN_AUDIO_FILL_GAPS{{}, true};
const Info<bool> MAIN_AUDIO_PRESERVE_PITCH{{}, false};
const Info<float> MAIN_EMULATION_SPEED{{}, 1.f};
const Info<bool> MAIN_WIIMOTE_AUDIO_ROUTING_ENABLED{{}, false};
const std::array<Info<bool>, 4> MAIN_WIIMOTE_AUDIO_OUTPUT_ENABLED{
    Info<bool>{{}, false}, Info<bool>{{}, false}, Info<bool>{{}, false}, Info<bool>{{}, false}};
const Info<bool> MAIN_GBA_AUDIO_ROUTING_ENABLED{{}, false};
const std::array<Info<bool>, 4> MAIN_GBA_AUDIO_OUTPUT_ENABLED{
    Info<bool>{{}, false}, Info<bool>{{}, false}, Info<bool>{{}, false}, Info<bool>{{}, false}};

u64 GetConfigVersion() {
  return config_version;
}

std::optional<std::string> GetAsString(const Location &) {
  unused();
}

ConfigChangedCallbackID AddConfigChangedCallback(ConfigChangedCallback callback) {
  const auto id = next_callback++;
  callbacks.emplace(id, std::move(callback));
  return {id};
}

void RemoveConfigChangedCallback(ConfigChangedCallbackID id) {
  if (callbacks.erase(id.id) != 1)
    throw std::logic_error("Invalid mixer setting callback");
}
} // namespace Config

// Cached, explicitly supplied values exclude parsing and file logging.
bool TryParse(const std::string &, bool *) {
  unused();
}

std::string ReplaceAll(std::string, std::string_view, std::string_view) {
  unused();
}

namespace Core {
struct System::Impl {};

System::System() : m_impl(std::make_unique<Impl>()) {
}

System::~System() = default;

State GetState(System &) {
  return running ? State::Running : State::Paused;
}
} // namespace Core

class DPL2FSDecoder {};

namespace AudioCommon {
SurroundDecoder::SurroundDecoder(u32 rate, u32 block)
    : m_sample_rate(rate), m_frame_block_size(block) {
}

SurroundDecoder::~SurroundDecoder() = default;

size_t SurroundDecoder::QueryFramesNeededForSurroundOutput(size_t) const {
  unused();
}

void SurroundDecoder::PutFrames(const short *, size_t) {
  unused();
}

void SurroundDecoder::ReceiveFrames(float *, size_t) {
  unused();
}
} // namespace AudioCommon

namespace File {
IOFile::IOFile() : m_file(nullptr), m_good(true) {
}

IOFile::~IOFile() {
  if (m_file)
    std::terminate();
}
} // namespace File

WaveFileWriter::WaveFileWriter() : m_current_sample_rate_divisor(0) {
}

WaveFileWriter::~WaveFileWriter() = default;

bool WaveFileWriter::Start(const std::string &, u32) {
  unused();
}

void WaveFileWriter::Stop() {
  unused();
}

void WaveFileWriter::AddStereoSamplesBE(const short *, u32, u32, int, int) {
  unused();
}

namespace Common::Log {
void GenericLogFmtImpl(LogLevel level, LogType, const char *, int, fmt::string_view,
                       const fmt::format_args &) {
  overflow_warnings += level == LogLevel::LWARNING;
}
} // namespace Common::Log

namespace Common {
bool MsgAlertFmtImpl(bool, MsgType, Log::LogType, const char *, int, fmt::string_view,
                     const fmt::format_args &) {
  unused();
}
} // namespace Common

namespace {
using cupid::audio::MixerOptions;
using cupid::audio::SampleFifo;

void configure(const MixerOptions &options) {
  ++config_version;
  Config::MAIN_AUDIO_BUFFER_SIZE.TryToSetCachedValue({int(options.buffer_ms), config_version});
  Config::MAIN_AUDIO_FILL_GAPS.TryToSetCachedValue({options.fill_gaps, config_version});
  Config::MAIN_AUDIO_PRESERVE_PITCH.TryToSetCachedValue({options.preserve_pitch, config_version});
  Config::MAIN_EMULATION_SPEED.TryToSetCachedValue({options.speed, config_version});
  Config::MAIN_DPL2_QUALITY.TryToSetCachedValue({AudioCommon::DPL2Quality::Low, config_version});
  Config::MAIN_WIIMOTE_AUDIO_ROUTING_ENABLED.TryToSetCachedValue({false, config_version});
  Config::MAIN_GBA_AUDIO_ROUTING_ENABLED.TryToSetCachedValue({false, config_version});
  for (const auto &value : Config::MAIN_WIIMOTE_AUDIO_OUTPUT_ENABLED)
    value.TryToSetCachedValue({false, config_version});
  for (const auto &value : Config::MAIN_GBA_AUDIO_OUTPUT_ENABLED)
    value.TryToSetCachedValue({false, config_version});
  running = options.running;
  for (const auto &[id, callback] : callbacks) {
    static_cast<void>(id);
    callback();
  }
}

void compare(const std::vector<s16> &actual, const std::vector<s16> &expected) {
  if (actual != expected) {
    const auto at =
        std::mismatch(actual.begin(), actual.end(), expected.begin()).first - actual.begin();
    throw std::runtime_error("Mixer mismatch at checkpoint " + std::to_string(checkpoints) +
                             " sample " + std::to_string(at) + " actual " +
                             std::to_string(actual[std::size_t(at)]) + " expected " +
                             std::to_string(expected[std::size_t(at)]));
  }
  ++checkpoints;
  bytes_compared += actual.size() * 2;
  for (s16 sample : actual) {
    const auto bits = static_cast<u16>(sample);
    serialized_hash = (serialized_hash ^ (bits >> 8)) * 1099511628211ull;
    serialized_hash = (serialized_hash ^ (bits & 255)) * 1099511628211ull;
  }
}

u64 sample_hash(std::span<const s16> samples) {
  u64 hash = 14695981039346656037ull;
  for (s16 sample : samples) {
    const u16 value = static_cast<u16>(sample);
    hash = (hash ^ (value >> 8)) * 1099511628211ull;
    hash = (hash ^ (value & 255)) * 1099511628211ull;
  }
  return hash;
}

void fixed_cases() {
  for (unsigned scenario = 0; scenario < 3; ++scenario) {
    MixerOptions options;
    options.fill_gaps = scenario != 2;
    configure(options);
    auto reference = std::make_unique<Mixer>(options.output_rate);
    const u32 divisor = scenario == 1 ? 36000 : 3375;
    SampleFifo authored(divisor);
    const u8 left = scenario == 2 ? 0 : 137, right = 233;
    authored.set_volume(left, right);
    reference->SetWiimoteSpeakerVolume(0, left, right);
    const unsigned count = scenario == 2 ? 40000 : 1024;
    std::vector<s16> input(count);
    for (unsigned n = 0; n < count; ++n)
      input[n] = std::bit_cast<s16>(u16(n * 12345 + 677));
    reference->PushWiimoteSpeakerSamples(0, input.data(), input.size(), divisor);
    authored.push_mono(input);
    std::vector<s16> a(8192), b(8192);
    reference->MixWiimoteSpeaker(0, b.data(), b.size() / 2);
    authored.mix(a, options);
    compare(a, b);
    std::cout << "FIXED " << scenario << " hash " << sample_hash(b) << " dropped "
              << authored.dropped_granules() << '\n';
    if (scenario == 2) {
      for (unsigned n = 0; n < 12; ++n) {
        std::fill(a.begin(), a.end(), s16(0));
        std::fill(b.begin(), b.end(), s16(0));
        reference->MixWiimoteSpeaker(0, b.data(), b.size() / 2);
        authored.mix(a, options);
        compare(a, b);
      }
      std::cout << "DRAIN hash " << sample_hash(b) << " nonzero "
                << std::count_if(b.begin(), b.end(), [](s16 value) { return value != 0; }) << '\n';
    }
  }
}

void startup_cases() {
  MixerOptions options;
  configure(options);
  for (unsigned port = 0; port < 4; ++port)
    for (unsigned empty_frames : {0u, 1024u, 2048u, 8192u})
      for (u32 divisor : {3375u, 13500u, 36000u}) {
        auto reference = std::make_unique<Mixer>(options.output_rate);
        SampleFifo authored(36000);
        const auto mix = [&](unsigned frames) {
          std::vector<s16> a(frames * 2), b(frames * 2);
          reference->MixWiimoteSpeaker(port, b.data(), frames);
          authored.mix(a, options);
          compare(a, b);
        };
        // The output callback can run at the constructor's rate before any packet arrives.
        mix(empty_frames);
        authored.set_rate_divisor(divisor);
        authored.set_volume(137, 233);
        reference->SetWiimoteSpeakerVolume(port, 137, 233);
        std::array<s16, 20> input;
        input.fill(16384);
        for (unsigned packet = 0; packet < 80; ++packet) {
          const auto before = overflow_warnings;
          const auto dropped = authored.dropped_granules();
          reference->PushWiimoteSpeakerSamples(port, input.data(), input.size(), divisor);
          authored.push_mono(input);
          if (overflow_warnings - before != authored.dropped_granules() - dropped)
            throw std::runtime_error("Startup mixer overflow count differs");
          // A 20-sample packet consumes this many output frames at its declared rate.
          mix(unsigned(std::lround(20.0 * options.output_rate * divisor / 108000000.0)));
        }
        mix(8192);
      }

  // This exact empty-read/burst sequence drops every granule in the target queue.
  auto reference = std::make_unique<Mixer>(options.output_rate);
  SampleFifo authored(36000);
  authored.set_rate_divisor(13500);
  reference->PushWiimoteSpeakerSamples(0, nullptr, 0, 13500);
  std::vector<s16> a(2048), b(2048);
  reference->MixWiimoteSpeaker(0, b.data(), 1024);
  authored.mix(a, options);
  compare(a, b);
  const auto before = overflow_warnings;
  std::vector<s16> input(1600, s16{16384});
  reference->PushWiimoteSpeakerSamples(0, input.data(), input.size(), 13500);
  authored.push_mono(input);
  if (overflow_warnings - before != 12 || authored.dropped_granules() != 12)
    throw std::runtime_error("Empty-read burst did not preserve the target overflow behavior");
  for (unsigned remaining = 144000; remaining;) {
    const unsigned frames = std::min(remaining, 1024u);
    a.assign(frames * 2, s16{0});
    b.assign(frames * 2, s16{0});
    reference->MixWiimoteSpeaker(0, b.data(), frames);
    authored.mix(a, options);
    compare(a, b);
    if (std::any_of(b.begin(), b.end(), [](s16 value) { return value != 0; }))
      throw std::runtime_error("Dropped startup burst unexpectedly reached the target output");
    remaining -= frames;
  }
}

void mono_cases() {
  std::mt19937 random(0x4d495845);
  for (u32 rate : {32000u, 32028u, 44100u, 48000u, 48042u})
    for (u32 divisor : {2250u, 3375u, 4500u, 9000u, 36000u})
      for (bool fill : {false, true})
        for (bool active : {false, true})
          for (u32 buffering : {0u, 20u, 80u, 1000u}) {
            MixerOptions options;
            options.output_rate = rate;
            options.fill_gaps = fill;
            options.running = active;
            options.buffer_ms = buffering;
            configure(options);
            auto reference = std::make_unique<Mixer>(rate);
            SampleFifo authored(divisor);
            for (unsigned phase = 0; phase < 20; ++phase) {
              const u8 left = u8(random()), right = u8(random());
              authored.set_volume(left, right);
              reference->SetWiimoteSpeakerVolume(0, left, right);
              const unsigned count = phase == 0 ? 0 : phase == 1 ? 40000 : random() % 1024;
              std::vector<s16> input(count);
              for (auto &sample : input)
                sample = static_cast<s16>(random());
              const auto before = overflow_warnings;
              const auto dropped = authored.dropped_granules();
              reference->PushWiimoteSpeakerSamples(0, input.data(), input.size(), divisor);
              authored.push_mono(input);
              if (overflow_warnings - before != authored.dropped_granules() - dropped)
                throw std::runtime_error("Mixer overflow count differs");
              const unsigned frames = phase == 19 ? 8000 : random() % 2048;
              std::vector<s16> a(frames * 2), b(frames * 2);
              reference->MixWiimoteSpeaker(0, b.data(), frames);
              authored.mix(a, options);
              compare(a, b);
            }
          }
}

void speed_cases() {
  std::mt19937 random(0x53454544);
  for (float speed : {0.f, 0.5f, 1.f, 1.5f, 2.f})
    for (bool preserve : {false, true})
      for (unsigned gain = 0; gain < 256; ++gain) {
        MixerOptions options;
        options.speed = speed;
        options.preserve_pitch = preserve;
        configure(options);
        auto reference = std::make_unique<Mixer>(48000);
        SampleFifo authored(36000);
        authored.set_volume(u8(gain), u8(255 - gain));
        reference->SetWiimoteSpeakerVolume(0, gain, 255 - gain);
        std::vector<s16> input(2048);
        for (auto &sample : input)
          sample = static_cast<s16>(random());
        reference->PushWiimoteSpeakerSamples(0, input.data(), input.size(), 36000);
        authored.push_mono(input);
        std::vector<s16> a(8192), b(8192);
        reference->MixWiimoteSpeaker(0, b.data(), b.size() / 2);
        authored.mix(a, options);
        compare(a, b);
      }
}

void combined_cases() {
  std::mt19937 random(0x53544552);
  for (unsigned case_number = 0; case_number < 100; ++case_number) {
    MixerOptions options;
    configure(options);
    auto reference = std::make_unique<Mixer>(options.output_rate);
    std::array<std::unique_ptr<SampleFifo>, 6> authored;
    for (auto &fifo : authored)
      fifo = std::make_unique<SampleFifo>();
    for (unsigned phase = 0; phase < 30; ++phase) {
      constexpr std::array<u32, 5> rates{32000, 32028, 44100, 48000, 48042};
      options.output_rate = rates[random() % rates.size()];
      options.buffer_ms = random() % 256;
      options.speed = float(random() % 5) * 0.5f;
      options.preserve_pitch = (random() & 1) != 0;
      options.fill_gaps = (random() & 1) != 0;
      options.running = (random() & 1) != 0;
      configure(options);
      reference->SetSampleRate(options.output_rate);
      for (unsigned channel = 0; channel < 6; ++channel) {
        constexpr std::array<u32, 5> divisors{2250, 3375, 4500, 9000, 36000};
        const u32 divisor = divisors[random() % divisors.size()];
        const u8 left = u8(random()), right = u8(random());
        const unsigned frames = random() % 2048;
        std::vector<s16> input(frames * (channel < 2 ? 2 : 1));
        for (auto &sample : input)
          sample = static_cast<s16>(random());
        authored[channel]->set_rate_divisor(divisor);
        if (channel)
          authored[channel]->set_volume(left, right);
        if (channel < 2) {
          std::vector<s16> encoded(input.size());
          for (std::size_t n = 0; n < input.size(); n += 2) {
            encoded[n] = std::bit_cast<s16>(std::byteswap(static_cast<u16>(input[n + 1])));
            encoded[n + 1] = std::bit_cast<s16>(std::byteswap(static_cast<u16>(input[n])));
          }
          authored[channel]->push_stereo(input);
          if (!channel) {
            reference->SetDMAInputSampleRateDivisor(divisor);
            reference->PushSamples(encoded.data(), frames);
          } else {
            reference->SetStreamInputSampleRateDivisor(divisor);
            reference->SetStreamingVolume(left, right);
            reference->PushStreamingSamples(encoded.data(), frames);
          }
        } else {
          reference->SetWiimoteSpeakerVolume(channel - 2, left, right);
          reference->PushWiimoteSpeakerSamples(channel - 2, input.data(), frames, divisor);
          authored[channel]->push_mono(input);
        }
      }
      const unsigned frames = random() % 2048;
      std::vector<s16> a(frames * 2), b(frames * 2);
      reference->Mix(b.data(), frames);
      for (auto &fifo : authored)
        fifo->mix(a, options);
      compare(a, b);
    }
  }
}
} // namespace

int main() {
  try {
    mono_cases();
    speed_cases();
    combined_cases();
    fixed_cases();
    startup_cases();
    std::cout << "PASS " << checkpoints << " checkpoints " << bytes_compared << " bytes hash "
              << serialized_hash << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
