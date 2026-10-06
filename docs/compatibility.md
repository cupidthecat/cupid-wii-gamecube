# Compatibility

Complete playability and full-speed execution remain unverified. Current retained captures show Sunshine airstrip movement and jumping on Windows, plus file selection, opening sequences and nonzero guest PCM for both supplied games on Windows and Linux. The table records those captures. Earlier endpoint-audio observations below retain their original source scope and have not been recertified by these runs.

| Game | Platform | Strongest observed behavior | Still unverified |
| --- | --- | --- | --- |
| Super Mario Sunshine, USA (`GMSE01`) | Windows, MSVC Release, RTX 3060 | Completed field 10,000; rendered airstrip level with ordinary left/right movement, release, jumping and landing; nonzero guest PCM. | FLUDD use, level completion, gameplay save/load, host playback, full speed. |
| Super Mario Sunshine, USA (`GMSE01`) | Linux, GCC Release, Mesa | Title/file selection and opening sequence through field 4,620; nonzero guest PCM. | Controllable level gameplay, gameplay save/load, commercial host playback, physical GPU execution, full speed. |
| Super Mario Galaxy, USA (`RMGE01`) | Windows, MSVC Release, RTX 3060 | Pointer/slot selection, normal Play input and opening storybook through field 2,970; nonzero guest PCM. | Gameplay controls, gameplay save/load, host playback, full speed. |
| Super Mario Galaxy, USA (`RMGE01`) | Linux, GCC Release, Mesa | Title, pointer/slot selection and opening storybook through field 2,130; nonzero guest PCM. | Gameplay controls, gameplay save/load, commercial host playback, physical GPU execution, full speed. |

The current captures use OpenGL graphics, headless output, disabled host audio,
bounded execution and private copies of user storage. Linux uses Mesa llvmpipe,
which is a software driver. These conditions do not establish host playback
or physical GPU acceleration on Linux.
Sunshine uses structurally validated USA IPL font slots. Galaxy uses its real
partition metadata and required IOS33 profile. The separate Sunshine audio
probe uses headless OpenGL and SDL audio; [host audio](host-audio.md) records
the frozen source, exact waveform match and timing limits. Synthetic tests
retain their separate scope.

Exact older bounds, frame/audio hashes, controls and source snapshots are preserved in [the run history](compatibility-history.md). Menu-input checkpoints include `c140c115aa881c19338936ca565668884578698e` and the subsequent validated input-follow snapshots. That history distinguishes an executable's configured revision string from the source snapshot actually used to build it.

## Retained candidate and RAM runtime checks

The latest source manifest contains 222 files and has SHA256
`475439c5ecef2d20e3a0fb1bfc9e27c4f9a6767e6e212cda03b2c70d978490b4`.
Windows executables report `emulation-candidate-20261004`; the Linux build
reports `unknown`. Receipts identify the actual source and binary separately
from those configured labels. Windows passes all 64 registered tests and Linux
all 65, with actual private-disc boot checks and required graphics execution.
These tests establish their stated behavior, not commercial playability.

The stock Windows Sunshine CLI, checkpoint recorder and repeated single-step
control agree exactly at three billion attempted steps: all 60 reported state
fields, frame bytes and persistent files match. The run reaches 320 fields,
with 338,464 guest PCM values and 62,194 nonzero values. GPU startup checkpoints
at fields 60 through 300 also match the software frame bytes. Longer ordinary
input captures reach the bounds listed above with zero reported memory errors.
The Windows captures end with allocation errors during host commit exhaustion;
the Linux captures are interrupted by WSL restarts. Their partial state and
images remain preserved. Neither is a completed gameplay or speed result.

The following RAM runtime contains 223 inputs with manifest SHA256
`d3e3b6719bf0fc093e9adec0d184ac221d020bdf766c1cdfbbeb1aaf5eceeebb`.
The frozen product passes all 64 Windows and 65 Linux Release tests; the
Windows comparison harness uses its recorded portable adapter overlay.
Its Sunshine stock, recorded, repeated-step and native controls agree on
guest results, frames and storage at three billion attempts. The Galaxy
interpreter/OpenGL prefix agrees with its earlier control at 1,000,200,000
attempts. These startup comparisons do not recertify later compatibility
observations. [Testing](testing.md) records the precise source and test scope.

The supplied USA 2 MiB container fails complete retail-firmware checksum
validation. Its independently validated font blocks remain usable through
`--ipl-fonts`; these runs do not treat the container as a valid complete BIOS.
Commands, immutable input and storage copies, source/binary hashes, checkpoint
frames and final receipts remain under `.cache/goal-emulation-20261004/`.

## Sunshine airstrip controls

The RAM runtime combined with the separately validated empty-coverage renderer
completes 10,000 fields normally on Windows. Its 223-file source manifest is
`35e438ee6d88483576f67adc2eeaae079b7a8eba17065b67a4edbf1ffe7bfdc7`;
the interpreter/OpenGL checkpoint executable has SHA256
`5d214b28997b0d8258a002d35495c5591ad040708ea1ec3ddc20e3bba681eaea`.
All 86 shared checkpoints through field 5,160 match the earlier native capture
on guest state and raw frame bytes. Build labels, native counters, three GPU
execution counters and accepted replay-update counts are recorded separately.
The renderer's existing dispatch cap can repartition later work between the
GPU and checked CPU path after empty dispatches are removed.

Execution passes the earlier field-6,717 rendering failure. Field 7,140 shows
Mario, Peach and the aircraft in the airstrip level. Ordinary stick holds at
fields 7,590–7,650 and 7,770–7,830 move Mario in both directions; subsequent
released-input frames show his displaced position. A presses at 7,600 and
7,780 produce airborne frames followed by landing. Two replay versions are
accepted with zero rejections, preserving the consumed input prefix.

The final state records 80,178,884,970 retired instructions, 10,000 presented
fields and 10,682,464 guest PCM values, including 10,320,440 nonzero values.
Both memory-error counters, DAR and DSISR remain zero. The bounded run takes
2,596.62 seconds with checkpoint output and shared-host work; this is not a
full-speed result. Host audio is disabled. Changed persisted card bytes alone
do not establish a gameplay save and reload. FLUDD use, level completion,
host playback and gameplay save/load remain unverified. The source, final
state, raw frames, replay versions and memory telemetry are audited in
`.cache/goal-emulation-20261004/sunshine-airstrip-controls-receipt.json`.

## Earlier continuation records

The following observations describe their original snapshots. The older
`local-artifacts` directories named below are absent from the current workspace;
their documented observations have not been recertified by the latest runs.

The October 4, 2026 Sunshine native continuation uses a sealed working snapshot after `563d1a8853a351184d35f0f0e35d6b4f0a13392a`, including aligned cache reads, carry corrections and queued card-DMA completion. Its separate Linux build passes the public headless suites. Both plain CLIs and forwarding observers produce identical complete reported state at the 100-million-attempt bound on both platforms. Retained commercial prefixes at fields 30, 120, 300 and 450 also match the earlier Linux observer apart from configured revision metadata. This verifies those prefixes, not the unfinished run's future outcome.

The fresh native Galaxy continuation uses the archived `563d1a8853a351184d35f0f0e35d6b4f0a13392a` source and a separately copied guest-created save directory. Its executable retains configured `5632c6a1df64` metadata; source and binary hashes are recorded together. The guest's read reply at instruction 7,427,126,331 returns all 48,640 game-data bytes, and the later close succeeds. The retained reply bytes equal the copied seed exactly; the seed is unchanged. Field 750 visibly shows the full title logo and nonzero PCM. Field 1,350 shows the restored `4 Mario` slot with zero stars and star bits, plus Play This File, Copy, Icon and Erase after ordinary slot input. Its state has 16,337,093,317 instructions, 1,445,504 PCM values, 706,533 nonzero values and zero DAR/DSISR. This establishes guest file I/O, restored slot selection and menu rendering. Controllable gameplay and later saves still need evidence.

Both continuations use ordinary replay input through the public input-follow path. Private traces, images, save bytes and exact commands remain local. A process being live or reaching a bound is not a compatibility result; report its captured behavior separately.

The native Galaxy continuation's field 1,770 shows the opening storybook after the normal Play press at field 1,530. Its state has 21,313,875,794 instructions, 1,893,952 PCM values, 1,093,482 nonzero values and zero DAR/DSISR. The current native Sunshine continuation shows the aircraft at field 2,820 and the opening airstrip at 2,940. Retained guest SI reads establish movement, jumping, physical Start and the later A confirmation through the normal pad path. Its SRAM and card bytes still match the copied seed. These are opening-sequence observations; controllable level gameplay remains unverified.

The same native Galaxy run responds to later storybook input. Field 3,150 shows the star-festival page with its A prompt, field 3,180 shows that page fading, and field 3,240 shows the next comet page. The retained transport span contains 26 actual A-bearing HID reports. Field 3,210 has 38,301,964,653 instructions, 3,431,488 PCM values, 2,622,650 nonzero values and zero DAR/DSISR. This verifies an opening-story response through ordinary input; level controls and gameplay are still unverified.

A new native Sunshine continuation uses an independently sealed working snapshot based on `949784e21ac66fe3ed6be50f34292724098ef7f7`, built with MSVC Release and LTO. Its 200-file source manifest has SHA256 `2bb8f640f5ed1b91f439a712cf3bb8aadf4ddfc95533766ad6317a5b09e01a30`. Software graphics, headless output and disabled host audio remain the run conditions. The plain CLI and forwarding observer produce identical complete state at the 100-million-attempt bound, including the full CPU and physical-memory hashes, when each receives a byte-identical copy of the same runtime-created blank-card/SRAM seed. Separate newly initialized cards initially produced different memory hashes; those observations are retained. The long run uses ordinary Start, stick and A inputs, the validated USA fonts, and bounds of 100 billion attempts and 14,400 seconds. Its current source snapshot does not include subsequent device or graphics edits. An unfinished continuation establishes no later compatibility milestone.

The same sealed source also builds with GCC13 Release and LTO on Linux. The Linux plain CLI and observer match each other and the native complete state at 100 million attempts. Its three-billion-attempt prefix reaches 320 fields, generates 338,464 PCM values with 62,194 nonzero values, and has zero DAR/DSISR. All ten retained checkpoints at fields 30 through 300 have identical complete state and raw frame bytes to the native continuation. This is a software-rendered startup comparison; later execution and gameplay remain outside that comparison.

The same native Galaxy continuation reaches the outdoor plaza by field 4,230.
Future ordinary Nunchuk input holds stick-y at 220 from fields 4,410 to 4,470,
then returns it to 128. Retained frames show Mario moving toward the flowerbed
and remaining closer after release; ongoing A pulses show jumping and landing.
Actual mode-`0x37` HID reads contain 200 changed extension reports during the
hold, followed by 100 reports with the preceding neutral payload. The replay
has six accepted versions and no rejected replacement. Field 4,500 records
53,704,777,192 instructions, 4,808,864 PCM values, 3,995,027 nonzero values and
zero DAR/DSISR. Frame, state, replay and transport proofs remain in
`local-artifacts/galaxy-startup-dsp-worker/galaxy-play-di-native-20261004`.
Stick-x at 220 from fields 4,530 to 4,590 then moves Mario visibly around the
right side of the flowerbed; the next captured frame retains that position
after release. These actions use the normal Remote/Nunchuk transport.
This establishes movement and jumping in the opening plaza on that frozen
software runtime. Later objectives, gameplay save/load and full speed remain
outside the observation.

The run ends normally at its four-hour limit with 5,192 fields and
61,975,211,874 successful instructions. Left input from fields 4,770 to 4,830
and later forward input produce changed transport payloads and visible
movement. The right and left holds each have 200 changed mode-`0x37` reports,
followed by 100 neutral reports. All seven replay versions are accepted, with
no rejected replacements. Final PCM contains 5,547,808 values, 4,733,364
nonzero values and peak 29,746; DAR, DSISR and both memory-error counters are
zero. Every original seed file remains byte-identical. The retained file trace
contains an unchanged SYSCONF write and no game-file write, so this run adds no
gameplay save/reload result. Final hashes and input windows are recorded in
`final-gameplay-movement-audit.json` beside the earlier proofs.

The separate Sunshine GPU continuation built from the earlier graphics snapshot
ends with an explicit raster-coordinate error at field 6,717 and instruction
54,146,047,965, during the opening transition. Its last frame is black; DAR,
DSISR and both memory-error counters remain zero. Original source and binary
hashes, input, final state and RAM are preserved in
`local-artifacts/gameplay-hardware-fixed-native-20261004`. This observed
rendering failure prevents a level-playability claim.

The exact failing draw was captured and reduced from a 13-vertex strip to four
vertices. Its culled triangle needs a depth-plane origin at projected X
4,552,191; the coverage-coordinate guard rejected that origin before the
primitive could be culled. Separate interpolation-origin conversion repairs
the draw without clamping coordinates or dropping a primitive. Both the full
and reduced draws match all seven final depth-plane words from the unchanged
compiled arithmetic, including under arithmetic sanitizers. A fresh continuation
uses the isolated repair, identical frozen storage and ordinary replay. Its
early checkpoints match the failed run; execution beyond field 6,717 and level
playability remain unverified. [Rasterizer checks](rasterizer-reference.md)
record the captured input and regression scope.

A new Galaxy GPU continuation uses the sealed integrated source described in
[testing](testing.md), the default interpreter and copied restored-slot data.
The stock CLI and rebuilt forwarding observer match all 52 reported state
fields, final frame bytes and persistent files at one billion attempts, with
actual GPU fragment execution. These observations cover that bounded prefix;
later comparisons retain their own source and capture scope.

A completed software control uses the same integrated core, seed and immutable
replay through field 1,050. All 35 captured checkpoints agree on every reported
non-graphics guest state field. Frame and memory hashes first differ at field
690, where 17 PPM bytes differ. At field 1,050, full CPU state and all 24 MiB of
main RAM match; 28 bytes in secondary RAM and 16 PPM bytes differ. The retained
comparison is `local-artifacts/galaxy-software-control-20261004/completed-comparison.json`.
The original discrepancy remains recorded with that source snapshot.

The repaired projection and graphics-state cache complete a fresh GPU run
through the same field 1,050 bound. All 35 checkpoint frames and reported
non-backend state fields match the software control. Final full CPU state, all
main and secondary RAM bytes, PCM and persisted files match exactly. The run
executes 12,747,664,586 instructions, 268,063 GPU dispatches and 1,487,715,052 GPU
fragments, with 406 fallback rectangles and seven CPU fragments. This closes
the captured mismatch for that frozen core. The later shader-coordinate reuse
has separate public pixel and captured-draw checks; broader gameplay graphics
still need evidence. [Hardware rendering](hardware-rendering.md) records the
source isolation and retained receipts.

The separate nine-billion-attempt Galaxy audio run uses ordinary replay input
to advance past wrist-strap instructions. Its actual Windows endpoint samples
match the rendered callback prefix, including nonzero music; the final 864
callback frames are cancelled at shutdown and are excluded from that exact
prefix claim. An input-free run remains at the wrist-strap screen with silent
guest PCM. The Linux Sunshine endpoint attempt instead fails: guest execution
and PCM match the stock CLI, but the Pulse transport stops callbacks and hangs
during shutdown. [Host audio](host-audio.md) preserves both successful and failed
capture scopes, final-buffer limits and attribution controls.

Two current Linux Output replays deliver the entire captured Sunshine DMA
waveform exactly through an owned Pulse sink with zero guest queue drops.
Pausing that owned server reproduces stopped callbacks and queue drops while
the producer continues; close still completes while the server is paused.
The current shared WSLg probe instead delivers partial signal before its server
refuses connections. Reliable physical Linux playback remains unverified.
These endpoint results do not establish gameplay or full speed.

A separate sealed intermediate Windows core completes Sunshine through field
10,000 using the interpreter and physical RTX 3060 OpenGL. Ordinary replay
inputs produce rendered movement, jumping, release and C-stick camera changes
on the airstrip. The run takes 3,445.33 seconds on the shared host, executes
80,179,453,444 instructions and generates 10,320,343 nonzero guest PCM values.
Host playback is disabled. All five replay versions are accepted, with zero
rejected replacements or instruction-memory errors. The two initially
preformatted raw cards contain no game entries. The guest creates a valid
seven-block Sunshine file on card A; its header, both directories and both
allocation tables pass checksum checks. Card B and every original seed file
remain unchanged. This verifies guest file creation, while saved level
progress, restart/load, FLUDD use and level completion remain unverified.
The completed capture and corrected card audit remain in
`.cache/continuation-runtime-20261005/sunshine-gameplay/verification.json`.

The final Linux GCC core matches that Windows capture at all 118 shared
checkpoints through field 7,080, including every reported value except the
configured revision and every raw frame byte. Later replay replacements have
separate version records. Host playback is disabled on both platforms, and
the final Linux gameplay continuation has its own bounded execution receipt.

## Firmware and performance

The three local IPL candidates have the expected 2 MiB size but fail recognized full-ROM checksums. Their boot-ROM bytes are not accepted as firmware. The USA container's decompressed font slots pass independent structure checks and supply bounded EXI font reads. It does not provide Wii system data. See [font validation](fonts.md) and [boot requirements](boot.md).

The tested host has an Intel Core i9-10900F and NVIDIA RTX 3060. [Hardware-rendering tests](hardware-rendering.md) execute real compute work and pass exact pixel comparisons on the RTX 3060 and Mesa. The [performance page](performance.md) records startup and component workloads separately. None establishes full-speed commercial gameplay.

## Recording a result

Record the source revision and worktree status, platform/compiler, graphics and audio backend, console, input, storage seed, run bounds, final state and visible behavior. Preserve exact source and executable hashes when configured revision metadata predates compilation.

Use separate milestones: apploader completed, title reached, gameplay reached, and playable. Playable requires rendered gameplay, usable controls, audio and demonstrated save/load. Unit tests, a title frame and a successful bounded exit establish only their named observations.
