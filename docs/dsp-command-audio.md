# Command-list DSP audio

The task backend recognizes six GameCube command-audio program hashes:
`3ad3b7ac`, `3daf59b9`, `4e8a8b21`, `07f88145`, `e2136399` and `3389a79e`.
Guest boot uploads select these tasks through the normal DSP mailbox. The runtime
does not replace guest code, intercept game functions or launch an external core.

## Implemented behavior

Each command list processes five milliseconds of audio in nine 160-sample buses.
Commands initialize buffers, download and mix channels, apply per-bus volume,
upload auxiliary channels, construct stereo output, apply compressor ramps and
continue another command list. Unknown command IDs end processing. Short command
uploads retain the previous tail; an oversized copy leaves the buffer unchanged.

Guest-resident voice blocks supply ADPCM, signed PCM8 and signed PCM16 data,
loop and streaming histories, sample-rate conversion, envelope and channel-volume
ramps, low-pass filtering, last mixed sample values and per-millisecond parameter
updates. Both voice-block layouts and the older mixer-control mapping are handled.
The task retains initial-time-delay parameters without applying a phase shift,
matching the selected source path. This is not a claim of a hardware-accurate delay
implementation.

Nearest and linear conversion work without external data. Pass `--dsp-coeff PATH`
alone to supply a 4,096-byte big-endian coefficient table for four-bank polyphase
conversion. Without a table, polyphase requests use the source's linear fallback.
Adding `--dsp-rom PATH` selects the separate [instruction backend](dsp-instructions.md)
instead. No ROM or coefficient data is included in the repository.

Completion mail requests an interrupt 2,500 CPU cycles later when the mailbox was
empty. If mail was already queued, consuming its front entry delivers the deferred
interrupt immediately. Reading completion mail or switching tasks does not cancel
an already scheduled interrupt. The CPU event budget includes these deadlines.
The 2,500-cycle delay is source compatibility behavior, not a measured hardware
latency. Task upload and resume preserve audio state across a temporary task.

## Validation and limits

Implementation review used pin `2c805fee8f232d090c454381bb1b8422242345e1`, including
`Source/Core/Core/HW/DSPHLE/UCodes/AX.cpp`, `AXVoice.h`, `AXStructs.h`, the common
upload helper and mailbox/event callers. Private comparisons link the actual
source libraries, including its task, memory, DSP and timing managers. They compare
mail words and the complete 128 KiB synthetic RAM region, not only PCM output.
No game, BIOS or DSP ROM data is needed for these fixtures.

The initial comparison passes cover 768 command lists, 3,072 voice frames without
coefficients and 3,072 frames with an authored coefficient table across all six
profiles. A further 4,608-frame pass crosses all three source formats and all
three conversion modes. Another 1,728 byte-for-byte comparisons establish the
complete-RAM digests used by the public deterministic voice regression fixtures,
with and without an authored coefficient table. Deliberately changing a voice
envelope in the comparison causes the expected RAM mismatch. The actual source
timing manager also confirms the delayed-completion, mailbox-consumption and
reset-survival cases. Public tests also cover mailbox behavior, saturation, bus volume,
compressor attack/release, voice loops and stops, malformed chains and runtime
interrupt deadlines. These observations do not establish audible playback,
full-game compatibility or complete DSP parity.

Cyclic
voice lists, unbounded command chains, invalid backing ranges, out-of-range
coefficient banks and unsupported task data-memory transfers stop with diagnostics.
The old profile's unsupported compressor command and the undefined `abs(INT_MIN)`
compressor input are rejected rather than assigned invented behavior. The
instruction backend remains a separate way to execute supplied DSP programs.

A bounded Sunshine startup check also compares the candidate with `90131b1` for
both the existing task backend and the instruction backend. Each pair uses the
same initial user files and stops after 120 video fields. All final state fields
except build revision, the raw frame and all persisted user-file bytes match.
This checks that connecting the new family did not change those existing startup
paths. Sunshine does not select this command-audio family in that observation,
so the check is not game-level validation of the newly added profiles.

## Wii command-audio profiles

Profiles `fa450138`, `7699af32`, `d9c4bf34`, `adbc06bd`, `347112ba` and
`4cc52064` process three-millisecond frames. They add twelve regular mixing buses,
eight Remote buses, persistent main/auxiliary volume ramps, synchronization mail,
three voice-block layouts and unsigned envelope volume. The older profiles apply
per-millisecond updates when requested. Newer profiles include the biquad and
Remote filter paths. Main-memory sample reads use the selected MEM1 or MEM2
backing, including the accelerator's format-dependent addressing units.

Remote mixing resamples each 96-sample frame to eighteen samples, or each older
32-sample subframe to six. It writes four output buffers in guest memory. This
is distinct from the Remote speaker packet decoder and host speaker output; it
does not establish physical-controller playback.

The implementation follows the same pinned source's `AXWii.cpp`, `AXWii.h`,
`AXStructs.h` and Wii-selected paths in `AXVoice.h`, including callers and task
selection. The middle voice-block layout preserves the source's gap before the
Remote fields. Old high-pass parameters and initial-time-delay parameters remain
retained without an added effect. DPL2 output uploads the additional auxiliary
channel defined by the source; it does not invent the unmodeled phase effects.

Actual source comparisons pass 768 command lists, 2,304 repeated command lists
and 3,456 complete voice frames
across all six profiles, both sample-memory banks and coefficient-table modes.
The latter covers format and conversion combinations, loop/stop/stream behavior,
voice updates, unsigned envelopes, filters and Remote mixing. Those complete-RAM
results supply the public fixture digests. Repeating each voice fixture for three
frames also passes 10,368 comparisons with retained decoder and volume state.
Each comparison checks mail words and all 128 KiB of authored main RAM. Public
integration tests boot every profile through normal DSP mailboxes and verify
coefficient byte order, synchronization interrupts and RAM results.

These are synthetic component and runtime-integration observations. No Wii title
using this task family, audible output or physical Remote has been qualified by
these tests. Full-system timing and complete DSP parity remain separate goals.

After the Wii additions, bounded Sunshine and Galaxy startup pairs each execute
one billion guest instructions on the prior `90131b1` binary and the candidate,
using identical initial user files. All final state fields except build revision,
raw frames and persisted file bytes match within each pair. These existing game
paths do not establish use of the new command-audio profiles.

The source factory also names demo hashes `2ea36ce6` and `5ef56da3`, but its
Wii voice-block read/write switch supplies no layout for either. They remain
unsupported here rather than using uninitialized voice data or guessing a layout.
