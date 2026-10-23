# Compatibility

Complete playability and full-speed execution remain unverified. Current
Windows Sunshine execution shows controlled FLUDD use and save completion.
A separate saved-card restart reaches a controllable Airport, but does not
show the preceding FLUDD or coin progress restored. Endpoint audio fails its
independent delivery checks. Other captures retain their own source scopes.

## Current Sunshine route and restart

The `d8a88fc` Windows interpreter/OpenGL runtime completes the 24,000-field
Sunshine route twice on NVIDIA 597.30, using identical ordinary inputs,
validated USA font slots and isolated storage copies. The plain CLI and a
passive cached-frame observer match all 60 final JSON fields without
exclusions, the raw final frame and every stored file. All 400 passive frames
also match the earlier frozen route below, including controlled spray,
M-mark removal, coin collection and the game's save-complete prompt.

The resulting card A has SHA256
`242cbceb084ea3e2a6e227758ab9d6f72c22faf0371feb2f2656707cbda71b86`.
A separate current-runtime restart from a copy recognizes slot A, enters
Airport and responds to ordinary stick movement. It ends normally at 12,000
fields, with all four persisted files unchanged and both cards structurally
valid. The loaded scene shows zero coins and no FLUDD; this establishes slot
recognition and controllable world entry, without proving restoration of the
prior gameplay changes.

That restart also uses actual WASAPI playback. A bounded 120-second capture
retains the real callback output and a separate endpoint loopback. Recorder
and packet-continuity checks pass, but the product reports 16 cumulative
queue drops and the complete callback span has no exact endpoint match at
any integer-frame offset. Their cause and timing remain unresolved; see
[host audio](host-audio.md). Shared-host captures establish neither full
speed nor isolated performance. First Shine, level completion and clean
gameplay audio remain unverified.

A later private rejection observer completes another 12,000-field restart
with zero product drops and zero events across all six source queues. Its
single 120-second callback window is valid, but no endpoint waveform is
recorded. This diagnostic does not reproduce or explain the earlier sixteen
drops, and does not change the failed audio qualification.

A separate frozen-d8 native continuation exits normally at 32,000 fields.
All 400 raw frames through 24,000 match the interpreter route above; this
does not compare complete CPU state at that earlier boundary. Ordinary input
closes the save prompt, navigates around the tank, clears nearby goop and
triggers the large goop creature's appearance. It remains active at the final
bound, without a first Shine. Nine replay updates are accepted; one
mistaken digital-trigger value is rejected before application and retained
in the input history. The final card bytes match the earlier 24,000-field save.
Host audio remains disabled, and no speed or saved-level restoration is claimed.

A later frozen-d8 interpreter continuation matches all 533 native checkpoint
frames through field 31,980, then continues with ordinary movement and aiming.
It stops at field 44,158 with a renderer X/Y depth-origin range error at
`0x802edf08`, returning code 4. All nineteen replay versions are accepted;
735 checkpoint frames and the final state/frame remain sealed. The final card
bytes match the earlier save. No first Shine or clean completion is established.

An independent passive diagnostic replay reproduces the error, all 735 checkpoint
frames, the final frame and every stored file. Across all 60 final JSON fields,
only the replay-update count differs because this run uses a collapsed static
replay. It records the culled triangle and identifies a scaled depth-origin
intermediate beyond signed 32-bit range. A bounded conversion fix passes that
triangle and independent public rasterizer regressions under three compiler
profiles. A new game run must still pass the original failure point; no first
Shine, saved-level restoration or speed result is established by these checks.
Detailed failure evidence is recorded in [testing](testing.md).

## Frozen Sunshine FLUDD and save capture

The frozen `df978ec` Windows interpreter/OpenGL observer completes 24,000 fields
normally in 4,306.704763 seconds on NVIDIA 597.30, using validated USA IPL font
slots. Ordinary input acquires FLUDD, sprays water at field 20,580, clears the
tank's M mark by 21,660, and collects its coin by 22,080. The game displays
"Save complete" at 23,760 and remains on its continue prompt at the frame bound.

Only `memory-card-a.raw` changes from the immutable startup seed. Its header,
both directory copies and allocation checks remain valid. The existing seven-block
`GMSE01` save retains its identity and chain; 29 bytes of its 57,344-byte logical
payload change. This establishes an observed guest save and persisted changes,
without decoding the game-specific save fields or proving restart/load.

The capture shares CPU/GPU resources with recorded correctness work. Its
checkpoint observer can repartition three GPU diagnostics; the later plain/
passive comparison above independently verifies the current route. First
Shine, level completion, clean gameplay host audio and full speed remain
unverified. The 1,060-file immutable manifest is recorded with the detailed
evidence in [testing](testing.md).

## Current Galaxy controls

The frozen `d8a88fc` Windows interpreter/OpenGL runtime reaches normal outdoor
festival gameplay on NVIDIA 597.30. Ordinary Nunchuk input at fields
6,420–6,480 moves Mario across the plaza. A separate A press at 6,570–6,580
produces a visible jump at 6,600 and landing at 6,660. Later ordinary walking
reaches the road crest overlooking the town and castle at 7,680. These frames
show player control outside the opening cinematic.

The Star Bit counter increases from zero to one by field 8,280. Ordinary
walking into the plaza at 9,420–9,600 triggers the subsequent airship attack
cinematic. Controls remain neutral afterward. The run exits normally at its
original two-hour host limit, at field 13,056, while that cinematic continues.
All nineteen replay updates are accepted without rejections. Only SRAM changes;
the Wii save files remain unchanged. No completed objective or level save is
observed.

The passive observer saves already cached frames. A fresh plain-CLI run with
the exact final input reaches the same 156,059,049,984 step attempts and
13,056 fields. All sixty JSON keys are compared; the only differences are
the expected host-limit versus instruction-limit outcome and nineteen
versus one accepted replay updates. Every other field, the raw final frame
and every persisted file match. Both runs use disabled host audio. These
observations establish movement, jumping and route progress; they do not
establish a completed objective, gameplay save restoration, clean host audio
or full speed.

## Earlier platform captures

| Game | Platform | Strongest observed behavior | Still unverified |
| --- | --- | --- | --- |
| Super Mario Sunshine, USA (`GMSE01`) | Windows, MSVC Release, RTX 3060 | Completed field 10,000; rendered airstrip level with ordinary left/right movement, release, jumping and landing; nonzero guest PCM. | FLUDD use, level completion, gameplay save/load, host playback, full speed. |
| Super Mario Sunshine, USA (`GMSE01`) | Linux, GCC Release, Mesa | Title/file selection and opening sequence through field 4,620; nonzero guest PCM. | Controllable level gameplay, gameplay save/load, commercial host playback, physical GPU execution, full speed. |
| Super Mario Galaxy, USA (`RMGE01`) | Windows, MSVC Release, RTX 3060 | Pointer/slot selection, normal Play input and opening storybook through field 2,970; nonzero guest PCM. | Gameplay controls, gameplay save/load, host playback, full speed. |
| Super Mario Galaxy, USA (`RMGE01`) | Linux, GCC Release, Mesa | Title, pointer/slot selection and opening storybook through field 2,130; nonzero guest PCM. | Gameplay controls, gameplay save/load, commercial host playback, physical GPU execution, full speed. |

Those earlier captures use OpenGL graphics, headless output, disabled host audio,
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

A separate frozen `747d092` native Sunshine continuation recognizes its existing
startup file after restart. It runs to its requested 3,600-second host limit,
exiting normally after 3,600.615 seconds at field 9,051. Ordinary replay controls
dismiss dialogue, move Mario, rotate the camera, interact with a Toad and produce
swimming. All sixteen replay versions are accepted, with zero rejections or
instruction-backing errors. The 150 complete checkpoint pairs, final image,
control history and source/binary receipts remain in
`.cache/continuation-20261022/frame-observer/sunshine-restart-20261022T222307516193Z/`.

Its final four persisted user files are byte-identical to the immutable restart
seed and earlier startup-file capture. Both card headers, directory copies and
allocation-table copies pass validation; card A retains its valid seven-block
Sunshine file, and card B stays empty. This verifies initial file recognition
and preserved persistence. FLUDD, saved level progress and gameplay save/load
remain unverified. Host audio is disabled, and this run does not establish full
speed. The card audit is `memory-card-restart-audit.json` beside that run.

A separate `14fab0f` Galaxy OpenGL prefix matches all 34 checkpoint frames and
reported values through field 2,040 against the earlier frozen core, apart from
revision, outcome and replay bookkeeping. It reaches the six-slot file menu;
the earlier core also responds to ordinary pointer input. The newer prefix is
slower in the shared-host timing pair, retained in [performance](performance.md).
Its old checkpoint lacks persisted-storage output, so storage equality is not
claimed for that comparison. These observations remain distinct from current
head gameplay qualification.

The earlier frozen-core Galaxy continuation subsequently exits normally at
its requested host limit: 3,600.892 seconds, field 5,197 and 62,278,102,686
instructions. Ordinary pointer and button input creates and selects a file,
starts it and advances the opening story. The final queued field-5,220 press
falls beyond termination and is not applied. Fourteen replay replacements
are accepted with no rejections. Final storage adds `GameData.bin`,
`banner.bin` and a system-configuration backup, changes SRAM and filesystem
metadata, and removes no files. The immutable seed, source user directory and
observer executable remain unchanged. Controllable Mario movement and level
progress are not reached. The terminal audit and 259-file manifest remain in
`.cache/continuation-20261022/frame-observer/galaxy-controller-20261022T224649363173Z/`.

A frozen `df978ec` Sunshine run reaches field 11,940 with ordinary movement,
camera, dialogue and swimming controls, but does not acquire FLUDD. It exits
abnormally with code `0x6e76003b` before its requested 12,000-field bound. No
final state or frame is available. All eighteen replay versions are accepted;
the last complete checkpoint records zero instruction-backing errors. The
610-file artifact manifest and unchanged-input checks remain in
`.cache/continuation-20261022/frame-observer-df978ec/sunshine-fludd-20261022T235220329254Z/`.

The terminal polling receipt at `2026-10-23T00:30:37.137660Z` falls inside the
system log's NVIDIA display-device restart interval, from `00:30:36.632Z` to
`00:30:38.284Z`, during installation of driver 597.30. This strongly supports
the driver transition as a cause, without an exact OS exit timestamp or a
definitive crash diagnosis. A fresh context identifies 597.30; the shipping
hardware renderer, hardware guest-video tests and all 17,179,869,184 signed
division identity cases pass on that driver. Earlier 597.16 measurements keep
their original scope. Logs and source/binary hashes remain in
`.cache/continuation-20261022/driver-epoch-59730/receipt.json`. This interrupted
run does not establish clean persistence or any additional gameplay milestone.

## Firmware and performance

The three local IPL candidates have the expected 2 MiB size but fail recognized full-ROM checksums. Their boot-ROM bytes are not accepted as firmware. The USA container's decompressed font slots pass independent structure checks and supply bounded EXI font reads. It does not provide Wii system data. See [font validation](fonts.md) and [boot requirements](boot.md).

The earlier captures used an Intel Core i9-10900F and NVIDIA RTX 3060. The newer
native checks use an i7-13700H with Intel Iris Xe and NVIDIA RTX 2000 Ada;
Linux graphics use llvmpipe.
[Hardware-rendering tests](hardware-rendering.md) execute real compute work and
pass exact pixel comparisons on their recorded devices. The
[performance page](performance.md) separates startup and component workloads.
None establishes full-speed commercial gameplay.

## Recording a result

Record the source revision and worktree status, platform/compiler, graphics and audio backend, console, input, storage seed, run bounds, final state and visible behavior. Preserve exact source and executable hashes when configured revision metadata predates compilation.

Use separate milestones: apploader completed, title reached, gameplay reached, and playable. Playable requires rendered gameplay, usable controls, audio and demonstrated save/load. Unit tests, a title frame and a successful bounded exit establish only their named observations.
