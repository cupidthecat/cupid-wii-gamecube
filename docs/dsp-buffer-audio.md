# Buffer-oriented DSP audio tasks

The task backend recognizes nine legacy buffer-mixing profiles and seven
accelerator-backed profiles. Guest program uploads select them through the normal
DSP mailbox. They share task switching and pending-mail update interrupts, but
retain their separate command protocols and sample-processing rules.

Legacy profiles:
`8d69a19b`, `cc2fd441`, `a81582e2`, `dbbeeb61`, `bad876ef`, `fa9c576f`,
`614dd145`, `5dbf8bf1` and `373a950e`.

Accelerator-backed profiles:
`008366af`, `078066ab`, `5ad4d933`, `84c680a9`, `a02a6131`, `2e5e4100` and `002e5e41`.

## Legacy buffers

The legacy task reads a 64-byte parameter block, mixes 1,024 stereo sample pairs
into retained output and uploads the output when requested. It can clear output,
download existing output, or add another voice. Source reads use 32-byte blocks
from guest main memory, including the profile's unaligned reload behavior.

Formats include signed mono/stereo PCM8 and PCM16, unsigned PCM8, and little-endian
PCM16 where the profile supports them. Flags select pause, looping and the input
address step. The mixer preserves frequency counters, delayed starts, alternate
buffers, per-channel volumes, historical channel ordering and the extra input
word visible to an unaligned stereo read.

Output accumulation saturates to signed sixteen-bit samples. Scaled voice samples
instead truncate to sixteen bits, matching this task family. The delayed-start
path also preserves the selected source behavior: it drops the frame remainder
when the delay expires, and a fully delayed frame decrements the count 1,023 times.
It is not silently repaired to a different audio policy.

## Accelerator-backed buffers

These tasks mix 96 stereo sample pairs using the shared DSP sample accelerator.
GameCube reads connected ARAM; Wii reads the selected MEM1 or MEM2 backing. The
parameter block includes its intentionally unaligned frequency field, fractional
counter, decoder state, delay, flags and channel volumes. Mixing preserves input
channel ordering by profile, unsigned formats, saturation, loop-exception resume
and the finished flag. Output remains retained until the guest requests its upload.

## Mail and compatibility behavior

Initialization queues mail without an immediate interrupt. The regular task
update requests an interrupt while mail remains pending. Mix and upload commands
send synchronization mail with an interrupt request. Temporary task uploads retain
parameter, input and output state when returning to the suspended task.

The legacy protocol checks the low command word unless the task-control prefix
is present. Its diagnostic ROM-word request returns zero, and its status-test
request returns `22640000`, as defined by the selected task implementation. The
oldest legacy profile responds to termination with synchronization rather than
completion. The accelerator-backed protocol requires its full command prefix.
Unknown commands retain the family's distinct mail/no-mail behavior.

The accelerator-backed source deliberately acknowledges termination even for
firmware versions whose termination comparison is broken. It also processes the
ordinary format paths for profile `5ad4d933`, despite that firmware's defective
mono/unsigned variants. These are compatibility choices from the selected source,
not claims of instruction-level reproduction of those firmware bugs. The separate
instruction backend remains available for executing supplied DSP programs.

## Qualification and limits

Review and executable comparisons use pin
`2c805fee8f232d090c454381bb1b8422242345e1`, including
`Source/Core/Core/HW/DSPHLE/UCodes/ASnd.{cpp,h}`, `AESnd.{cpp,h}`, the common task
upload and factory code, mailbox handling and the shared sample accelerator.
The private harness links the actual task classes and memory/DSP managers.

Initial passes compare 6,912 legacy and 5,376 accelerated frames. The public
fixture pass compares another 18,432 frames: sixteen profiles, 128 authored cases,
three consecutive frames and GameCube/Wii primary/Wii secondary memory variants.
Every comparison checks all 64 KiB of fixture RAM and returned mail words. Cases
cover paused and running voices, format and rate combinations, channel ordering,
delay boundaries, unaligned input, alternate buffers, repeated mixing and output
uploads. The full-RAM results form the public regression digests. The actual source timing
manager also verifies pending-mail update interrupts for all sixteen profiles.
Public runtime tests boot and suspend/resume each profile on both console modes,
including a temporary switch through the command-list audio family.

No firmware, copyrighted game data or captured audio is part of these fixtures.
They establish component behavior, not audible playback, application compatibility,
full-system timing or full-speed operation. Unsupported task data-memory transfers
and invalid host backing ranges still stop with diagnostics. DSP save-state
serialization is not implemented by this module.

Local qualification includes twelve focused Release/LTO runtime, DSP, event-budget
and CLI tests, plus the buffer suite under GCC and Clang ASan/UBSan with fatal
findings enabled. Local leak checking is unavailable under the host tracing
restriction. Bounded Sunshine and Galaxy startup pairs each execute one billion
guest instructions and match the prior `90131b1` binary in final state except build
revision, raw frame and persisted files. These games do not qualify the new
buffer-task families by that observation.
