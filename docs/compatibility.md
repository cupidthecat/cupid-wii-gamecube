# Compatibility

Complete playability and full-speed execution remain unverified. Both supplied games reach file selection and generate nonzero guest PCM on Windows and Linux. Sunshine accepts menu movement, jumping and slot input. Galaxy accepts pointer and slot input, reopens guest-created data, and reaches an outdoor plaza where ordinary stick and A input produce movement and jumping. These observations use private local dumps and normal guest input.

| Game | Platform | Strongest observed behavior | Still unverified |
| --- | --- | --- | --- |
| Super Mario Sunshine, USA (`GMSE01`) | Windows, MSVC Release | Accepted Start, menu movement, jumping and slot A; rendered opening movie. A separate GPU run's actual SDL samples match Windows endpoint capture. | Controllable level gameplay, guest save creation and reopening, full speed. |
| Super Mario Sunshine, USA (`GMSE01`) | Linux, GCC Release | Accepted menu controls and slot A; the 48-billion-step run progresses through the opening airstrip/movie scenes. | Controllable level gameplay, save/load, commercial host playback, full speed. |
| Super Mario Galaxy, USA (`RMGE01`) | Windows, MSVC Release | Guest-created files reopen exactly, the restored slot launches, and opening-story input reaches the outdoor plaza. Ordinary Nunchuk forward, right and left input and A produce visible movement and jumping. A separate replay run's nonzero SDL samples match Windows endpoint capture. | Broader gameplay controls, complete gameplay save/load behavior, full speed. |
| Super Mario Galaxy, USA (`RMGE01`) | Linux, GCC Release | Title, pointer, file-slot hover and slot selection accepted; file-creation confirmation reached. | Accepted live controls through gameplay, complete save creation/reload, commercial host playback, full speed. |

The menu and gameplay observations use software graphics, headless output,
disabled host audio, bounded execution and private copies of user storage.
Sunshine uses structurally validated USA IPL font slots. Galaxy uses its real
partition metadata and required IOS33 profile. The separate Sunshine audio
probe uses headless OpenGL and SDL audio; [host audio](host-audio.md) records
the frozen source, exact waveform match and timing limits. Synthetic tests
retain their separate scope.

Exact older bounds, frame/audio hashes, controls and source snapshots are preserved in [the run history](compatibility-history.md). Menu-input checkpoints include `c140c115aa881c19338936ca565668884578698e` and the subsequent validated input-follow snapshots. That history distinguishes an executable's configured revision string from the source snapshot actually used to build it.

## Current continuation runs

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
rendering failure is under diagnosis and prevents a level-playability claim.

A new Galaxy GPU continuation uses the sealed integrated source described in
[testing](testing.md), the default interpreter and copied restored-slot data.
The stock CLI and rebuilt forwarding observer match all 52 reported state
fields, final frame bytes and persistent files at one billion attempts, with
actual GPU fragment execution. The long continuation is unfinished; its capture
hooks and normal input establish no outcome beyond observed checkpoints.

The separate nine-billion-attempt Galaxy audio run uses ordinary replay input
to advance past wrist-strap instructions. Its actual Windows endpoint samples
match the rendered callback prefix, including nonzero music; the final 864
callback frames are cancelled at shutdown and are excluded from that exact
prefix claim. An input-free run remains at the wrist-strap screen with silent
guest PCM. The Linux Sunshine endpoint attempt instead fails: guest execution
and PCM match the stock CLI, but the Pulse transport stops callbacks and hangs
during shutdown. [Host audio](host-audio.md) preserves both successful and failed
capture scopes, final-buffer limits and attribution controls.

## Firmware and performance

The three local IPL candidates have the expected 2 MiB size but fail recognized full-ROM checksums. Their boot-ROM bytes are not accepted as firmware. The USA container's decompressed font slots pass independent structure checks and supply bounded EXI font reads. It does not provide Wii system data. See [font validation](fonts.md) and [boot requirements](boot.md).

The tested host has an Intel Core i9-10900F and NVIDIA RTX 3060. [Hardware-rendering tests](hardware-rendering.md) execute real compute work and pass exact pixel comparisons on the RTX 3060 and Mesa. The [performance page](performance.md) records startup and component workloads separately. None establishes full-speed commercial gameplay.

## Recording a result

Record the source revision and worktree status, platform/compiler, graphics and audio backend, console, input, storage seed, run bounds, final state and visible behavior. Preserve exact source and executable hashes when configured revision metadata predates compilation.

Use separate milestones: apploader completed, title reached, gameplay reached, and playable. Playable requires rendered gameplay, usable controls, audio and demonstrated save/load. Unit tests, a title frame and a successful bounded exit establish only their named observations.
