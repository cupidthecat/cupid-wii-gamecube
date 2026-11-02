# Disc audio streaming

GameCube disc audio uses DI commands `0xe1`, `0xe2` and `0xe4`, separate from
DSP audio DMA. Both console modes expose the register path. The runtime reads
the disc header's streaming flag during GameCube bootstrap. A zero header
buffer size selects ten. Wii bootstrap leaves streaming disabled until it is
configured through the drive interface.

`0xe4` changes the enable flag and four-bit buffer setting after the first
disc-ID read and before another read or stream command. A drive reset clears
the stream configuration and requires another disc-ID read. Commands preserve
the defined missing-disc, changed-disc, stopped-motor, missing-ID, disabled-buffer,
late-configuration and invalid-subcommand errors. DI completion still uses the
existing 300 microsecond command delay; a physical seek/read timing model remains
unfinished. Seek `0xab` completes without moving the read buffer, following the
pinned handler's defined behavior. An enabled `0xe1` command consumes the permitted
configuration period before its subcommand is checked, including invalid
subcommands. Disabled stream commands leave that period open.

Starting a stream supplies its disc-byte offset and length. A later start
queues the next track; a zero offset and length requests a stop at the track
end. That pending stop suppresses later track changes until stop completion or
an explicit stop. Status queries return streaming state, the current position
rounded down to 32 KiB in word units, current track start in word units, or
track length in bytes.

Each 32-byte ADPCM block produces 28 stereo frames. The decoder retains two
unclipped predictor histories per channel, clamps prediction before adding the
signed nibble, and clips only the emitted sample. Four predictor coefficient
pairs and all sixteen shift settings are implemented. Reserved predictor
selectors use zero prediction, matching the target's default branch. Filter
history resets when a new track is selected.

The stream clock uses the exact 108 MHz dividend and AI sample-rate divisor.
Callbacks schedule six blocks at the 48 kHz setting or four at the 32 kHz
setting. A queued chunk completes before another is requested. Pausing AI
delivers the pending chunk once, then queues silence without advancing disc
position. Read failures skip that completion and one read request; the next
successful callback resumes scheduling. The failed completion sets the drive's
read-error status without changing normal DI transfer interrupts. A drive reset
clears stream state while an already queued failed completion remains scheduled.
This silence and retry sequence is
defined stream behavior, separate from unsupported device operations.

Track planning follows the pinned callback order: it changes track position
and resets history while scheduling the next contiguous disc read. A batch
crossing a noncontiguous track boundary still reads contiguously from its old
start; the next request uses the updated position. Tests retain this behavior.
It is a source compatibility decision, not a physical-hardware observation.

Decoded pairs retain the guest right/left convention. Host output converts
them to left/right and queues them independently of DSP DMA and Remote
speakers. AI volume changes apply to the separate stream queue, including
already queued samples. The SDL callback mixes DMA, disc audio and speakers
in that order. Disabled host audio still records deterministic stream sample
counts, nonzero values, peak and PCM hash. The four `disc_audio_*` state fields
describe those delivered samples before volume, host resampling and mixing;
they include scheduled silence. Existing `audio_*` fields continue to describe
DSP DMA alone.

## Validation and behavior mapping

The behavioral checkout is pinned at
`562875dd60fd0ff50b65dbbb611a2096802cd1b9`. Paths below are relative to
`Source/Core/`.

| Product code | Behavioral implementation and callers | Scope |
| --- | --- | --- |
| `audio::DiscAdpcm` | `Core/HW/StreamADPCM.cpp`, `ADPDecodeSample`, `ADPCMDecoder::DecodeBlock`, `ResetFilter`; `Core/HW/DVD/DVDInterface.cpp`, `ProcessDTKSamples` | Header interpretation, nibble order, history, rounding and saturation. |
| `audio::DiscStream` | `Core/HW/DVD/DVDInterface.cpp`, `AdvanceDTK`, `DTKStreamingCallback`, `ResetDrive` | Track queue, filter resets, pending blocks, read errors, AI pause and event periods. |
| `hw::Devices` DI controls | `Core/HW/DVD/DVDInterface.cpp`, `ExecuteCommand`, `CheckReadPreconditions`, `AudioBufferConfig`, `FinishExecutingCommand`; `Core/HW/ProcessorInterface.cpp`, reset-code writer | Command state, status, errors and reset. |
| `runtime::Machine::load_disc` | `Core/Boot/Boot.cpp`, `DVDReadDiscID`; `Core/Boot/Boot_BS2Emu.cpp`, GameCube bootstrap | ID-read state, header flag and default buffer size. |
| Separate output queue | `Core/HW/AudioInterface.cpp`, control and volume writers; `AudioCommon/Mixer.cpp`, `PushStreamingSamples`, `SetStreamingVolume`, `MixerFifo::Mix` | Exact divisors, guest channel order, volume and additive mixing. |

`disc_stream` uses authored blocks and commands to check decoder samples,
callback boundaries, both clocks, all four divisors, pauses, track changes,
read errors, drive resets, command errors and split versus bulk advances.
Its PowerPC fixture writes DI and AI registers through the actual CPU/memory
path on both consoles and checks nonzero stream telemetry without DSP DMA.
`host_audio` checks SDL callback channel order and gains; `audio_stats` checks
separate canonical PCM hashes and invalid-input state preservation.

The optional `disc_stream_reference_checks` compiles the unchanged pinned
decoder. Its October 4, 2026 strict GCC and MSVC runs compare all 65,536 header
pairs
with sixteen consecutive randomized blocks each: 1,048,576 blocks and
58,720,256 exact sample values, aggregate FNV64 `2016091799604778107`.
The same optional test extracts selected drive functions and command cases from
that checkout into the build directory. It executes the unchanged stream
callback, track planner, reset, read preconditions and commands against authored
clock, read and mixer seams. Both platforms compare 12,864 callback
intervals and 65,312 command results, with exact read offsets, PCM packets and
status/error values on both clocks. It covers rate changes, pauses, short and
noncontiguous tracks, stops, queued read failures and resets. Python 3 is required
only when these optional comparisons are enabled.

Both stream suites also pass GCC AddressSanitizer, UndefinedBehaviorSanitizer
and leak checks. These runs use frozen source copies so simultaneous changes
cannot leave headers and object files out of sync.

The host seams complete scheduled reads deterministically; they do not execute
the external drive thread or its full timing system. No physical console or
commercial disc-audio playback result is claimed. Bounded October 4 local runs
of both supplied RVZ images reached the game entry at ten million CPU attempts
on Windows and Linux. Sunshine delivered 2,688 scheduled sample values and Galaxy delivered
3,024, all zero. Those runs verify the integrated silence path and do not verify
audible game audio or gameplay.
