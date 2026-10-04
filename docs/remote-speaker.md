# Wii Remote speaker decoding

The speaker decoder accepts signed PCM8 and four-bit Yamaha ADPCM packets. It returns up to forty host-endian mono samples, a sample-rate divisor and separate left/right volume values. It keeps the ADPCM predictor and adaptive step between packets; PCM packets leave that state unchanged. Reset restores a zero predictor and step 127.

The caller supplies the format, little-endian rate divisor and volume from the speaker's register bank. Muting a speaker-data HID report prevents that report from reaching the decoder. Raw I2C writes to sample address zero bypass HID mute. The host speaker-processing setting is separate from the speaker-status bit set by HID report `0x14`. Its behavioral default is disabled. This module does not mix remote samples into the main audio stream or implement the host speaker queues and resampler.

Disabled processing, a zero divisor or an empty packet produces no samples and leaves the decoder state unchanged. Zero volume still decodes samples and advances ADPCM state. Active packets with an unknown format, more than twenty bytes or a NaN pan fail explicitly before changing decoder state. Pan is clamped to the range -1 through 1; ADPCM volumes above 127 use the supplied volume as their divisor.

The behavioral reference logs and discards unknown sample formats. This runtime reports those formats as explicit errors rather than treating them as decoded audio.

Rate metadata preserves the behavioral calculation's two integer divisions: first `12,000,000 / divisor` for PCM or `6,000,000 / divisor` for ADPCM, then `108,000,000 / (rate * 2)`. Host routing must retain that final divisor rather than replacing it with a rounded frequency. ADPCM consumes each byte's high nibble before its low nibble and clips its predictor and adaptive step after each sample.

The behavioral reference is pinned at `562875dd60fd0ff50b65dbbb611a2096802cd1b9`. Relevant relative paths and functions are:

| Behavior | Files and functions |
| --- | --- |
| Sample decoding, gain, rate and state | `Source/Core/Core/HW/WiimoteEmu/Speaker.{h,cpp}`, `SpeakerData`, `Reset`, `SetSpeakerEnabled`, `BusWrite` |
| HID sample length and mute | `Source/Core/Core/HW/WiimoteEmu/EmuSubroutines.cpp`, `HandleSpeakerData`, `HandleSpeakerMute`, `HandleSpeakerEnable`; `Source/Core/Core/HW/WiimoteCommon/WiimoteReport.h`, `OutputReportSpeakerData` |
| Host processing setting | `Source/Core/Core/HW/WiimoteEmu/WiimoteEmu.cpp`, `RefreshConfig`; `Source/Core/Core/Config/MainSettings.cpp`, `MAIN_WIIMOTE_ENABLE_SPEAKER` |
| Mono delivery and rate metadata | `Source/Core/AudioCommon/Mixer.{h,cpp}`, `PushWiimoteSpeakerSamples`, `SetWiimoteSpeakerVolume` and `FIXED_SAMPLE_RATE_DIVIDEND` |

Public tests cover known PCM and ADPCM values, split-packet continuity, predictor and step clipping, zero-volume state changes, suppressed processing, pan and rate boundaries, reset and malformed packets. MSVC with warnings treated as errors, strict GCC and Clang 18 with address and undefined-behavior sanitizers passed the focused suite.

A separate local executable compiled the full unchanged `Speaker.cpp`. Controlled system, sound-stream and setting adapters captured its mono samples, rate divisor and volumes; decoding and state updates were not replaced. Save-state serialization and the host mixer/resampler were excluded. The deterministic seed was `0x53504b52`. Both formats ran every byte-sized volume, seven rate divisors and seven pan positions, followed by disabled, zero-divisor, empty and reset cases. All 26,112 checkpoints matched, including 397,658 samples and decoder state. SHA-1 over 1,186,996 serialized sample/state/metadata bytes was `dc9f141de6a91923767307d755f82075ba576040`. These are software comparisons; no hardware speaker result is claimed.
