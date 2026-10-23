# Performance measurements

The interpreter and software renderer still need performance suitable for play. Startup throughput, device-call speed and gameplay frame rate are separate measurements.

An initial Linux GCC profile sampled 100 million Sunshine startup instructions with profiling instrumentation and headless output. No video fields, graphics commands or audio samples occurred in that span. `Devices::advance` accounted for 40.34 percent of 8.70 sampled CPU seconds. This identified per-instruction device clock arithmetic as an optimization candidate; the profile does not describe active rendering or gameplay.

The corrected period helper retains the previous counter and callback behavior. Independent comparisons checked 840 period cases and 48,336 complete device register/callback comparisons for each console. Public regressions also cover rate changes, split and bulk advances, and counter overflow.

Six integrated Linux runs then compared two executables built from the same objects and libraries, with only the device implementation replaced by its preserved earlier version. Both used GCC 13 Release with `-O3`, strict floating-point behavior and system dependencies, and ran 100 million Sunshine startup instructions. Output was headless, host audio was disabled, and each run used fresh persistent storage. The order was earlier, corrected, corrected, earlier, earlier, corrected.

| Device implementation | Three elapsed times, seconds | Median, seconds |
| --- | --- | --- |
| Earlier | 18.95, 19.11, 21.00 | 19.11 |
| Corrected | 15.51, 16.97, 20.41 | 16.97 |

Every final JSON field matched across all six runs, including guest instructions, cycles, registers, exception state and output counters. The median ratio was 1.126. Builds and source checks were active on the same Intel Core i9-10900F host, so the elapsed-time spread limits the precision of that ratio. The measured implementation was a development snapshot after `5d8d36f8287873c05d3146c321bd549009bf39de`; source and executable hashes and exact build arguments were retained locally.

A later device deadline path avoids full timer arithmetic for calls strictly before the earliest pending event. It still updates the cycle counter and timed level interrupts on every instruction. MMIO access materializes earlier elapsed time, writes invalidate the deadline, and event boundaries retain the original callback order. A callback failure applies the preceding event-free interval before the current call, preserving partial state. The device comparison checked 263,146 advances and 1,112 complete state snapshots across both consoles, including reentrant callbacks, thrown callbacks, DMA data, card timers, rate changes and interrupt clearing.

For this change, two complete frozen headless runtimes used the source inputs of `c4b1ed598f6a338e5b94bae5de5a77b71a3a4ec4`; only the device header and implementation differed. Both used GCC 13.3 Release with `-O3`, strict floating-point behavior, system dependencies and no profiler instrumentation. Six 100-million-instruction Sunshine startup runs alternated earlier/deadline three times, with identical seed storage copied into separate run directories. The executable revision label was `device-deadline-private-validation` in both builds.

| Device scheduling | Elapsed times in run order, seconds | Median, seconds |
| --- | --- | --- |
| Earlier | 9.631800990, 9.185441248, 8.751572640 | 9.185441248 |
| Deadline | 6.415660196, 6.427195830, 6.275850010 | 6.415660196 |

All six runs exited normally and every final JSON field matched, including instructions, cycles, CPU registers, exception state and output counters. No VI fields, GX commands or audio samples occurred in this startup span. The median ratio was 1.432, measured while builds and longer game probes shared the host. This establishes a speedup for that bounded startup workload; it does not measure gameplay or active rendering. Exact commands, complete states, source manifests and executable hashes are preserved in the ignored `local-artifacts/device-deadline-prototype` directory. The earlier executable SHA-256 is `16b825c4b1361eb4d848c9e64d113df263e478c49f8df4ff8743895bca1601a6`; the deadline executable is `8217e9ddd896b1543b4b0a4821e1b0da0a36d87c7dbcea6c258fee0fd2e41370`.

Both frozen complete runtimes also passed the device, IOS, runtime, graphics, SDP transport and EFB access suites. Separate strict native and GCC comparisons and Clang 18 ASan/UBSan checks passed the device state/callback fixtures. A separately instrumented pair from the same source inputs retained complete state equality; `Devices::advance` self samples fell from 17.00 to 5.13 percent, and `update_interrupts` from 9.62 to 0.27 percent. Those profiler samples describe that instrumented startup pair only.

Future gameplay measurements should record guest video/audio rates, compiler and flags, output driver, host workload, run bounds and accepted controls. Keep profiles and commercial-game traces local. A throughput improvement without rendered, controllable gameplay does not establish playability.

## Retained native continuation timing

Live Windows process handles and immutable field snapshots supply a separate October 4 measurement of the older software-rendering continuations. Both use MSVC Release, headless output, disabled host audio and copied private storage. Source and executable provenance are recorded in [compatibility](compatibility.md).

| Run | Retained field interval | Wall seconds | Accumulated VI fields per second |
| --- | --- | ---: | ---: |
| Galaxy, saved-slot menu | 300 to 1,470 | 2,415.710 | 0.484330 |
| Sunshine, startup/title | 300 to 2,070 | 1,985.711 | 0.891368 |

These rates use actual process start times and capture timestamps. Both continuations, compilation, tests and separate CPU/GPU checks shared the host. They measure accumulated guest fields in those retained intervals, with no isolated-host or gameplay claim. They also predate the integrated GPU and CPU changes. Exact capture timestamps, verified executable paths and competing-process inventory remain in `local-artifacts/native-game-retained-timing.json`.

## Replay scheduling

The retained three-billion-step Sunshine startup profile attributed 4.57 percent of self samples to input-replay processing. The runtime now checks the next instruction and field deadlines before entering that path. It still installs initial and replaced input, preserves source-line ordering across the counters, and supports reset and events at `UINT64_MAX`.

Two frozen GCC Release runtimes based on the preserved `563d1a8853a351184d35f0f0e35d6b4f0a13392a` source differ only in replay scheduling and its Machine caller. Six runs alternate earlier/deadline/deadline/earlier/earlier/deadline, using the same copied storage seed, supplied Sunshine RVZ, USA fonts, ordinary input replay, software graphics, headless output and disabled host audio. Each reaches 100 million step attempts. Every final JSON field matches. No graphics commands, video fields or audio samples occur in this startup span.

| Replay processing | Elapsed times, seconds | Median, seconds |
| --- | --- | --- |
| Earlier | 7.556359, 10.930437, 7.899015 | 7.899015 |
| Deadline | 7.226713, 9.521131, 7.262314 | 7.262314 |

The median ratio is 1.088. Other builds and game probes shared the i9-10900F host, and the spread limits that timing estimate. Exact arguments, final states, executable hashes and frozen source pairs are retained in `local-artifacts/input-replay-deadline-20261004`. This is startup evidence, with no gameplay speed claim. The replay, follower and authored USB/HID guest suites passed on the frozen deadline runtime; the deadline fixture also covers empty and exhausted streams, counter jumps, reset and largest-counter events.

The same initial profile attributed 7.70 percent of sampled CPU time to `DSP::Engine::advance`. Its period accumulator now adds short instruction intervals directly and uses modulo only when crossing an update boundary. Total cycle overflow checks, update phase, mailbox interrupts and audio updates retain their prior behavior. The full existing DSP suite passed against both the preserved and corrected implementations under strict GCC and Clang 18 ASan/UBSan.

A separate component measurement ran twenty million varying advances for ROM and CARD tasks on both consoles, three times each. Earlier runs took 0.215 through 0.234 seconds; corrected runs took 0.030 through 0.033 seconds. Program hashes, mailbox values, control registers and interrupt counts matched in all twelve pairs. This measures the scheduling call, with no audio rendering or guest CPU execution.

Six integrated 100-million-instruction startup runs replaced only the DSP object against the same preserved runtime checkpoint `6f33d49f6e73248093e8a9533719e37e37b6b5d1`. Every final JSON field matched. Median elapsed times were 8.322 seconds before and 8.634 seconds after. Package builds and two longer game probes were active and host load changed during the measurements; these results establish no integrated speedup. They are retained separately from the component result.

## Current CPU and DSP paths

The October 4 instruction-cache change preserves replacement state while accelerating aligned and repeated same-line reads. A frozen three-billion-step Sunshine pair takes 211.680 seconds before and 185.991 seconds after, with every emitted final JSON field equal. A separate authored comparison matches 8,192 complete CPU states, ordered bus records and final RAM images byte for byte. [CPU behavior mappings](cpu-reference.md) record the exact source isolation, repeated startup measurements and their limits.

The DSP countdown now returns inline before the next update boundary. Three interleaved 200-million-call repetitions per mode and console show 30.0 to 41.3 percent less elapsed time in the component workload. Original and current full DSP traces match across 2,912 checkpoints and 119,768,032 bytes, including audio, RAM and callbacks. [DSP mappings](dsp-reference.md) retain the modes, compiler, input hashes and narrower caller comparison. The component ratio does not establish a whole-game improvement.

## Optional Release link-time optimization

`CUPID_ENABLE_LTO=ON` requests supported Release link-time optimization. It retains strict floating-point evaluation and stays off by default. The experiment uses the same sealed October 4 source as the integrated hardware checks, with explicit `integrated-runtime-private-20261004` metadata, copied storage, Sunshine RVZ and fonts, ordinary replay, headless software graphics and disabled host audio.

| Compiler and workload | Baseline seconds | LTO seconds | Measurement |
| --- | ---: | ---: | --- |
| GCC 13.3, 100M startup | 5.82850 | 4.12804 | Median of three pairs; all three LTO runs faster. |
| GCC 13.3, 3B rendered phase | 175.1940 | 141.2915 | Median of three pairs; two LTO runs faster. |
| MSVC, 100M startup | 5.80338 | 5.41383 | Median of three pairs; all three LTO runs faster. |
| MSVC, 3B rendered phase | 222.89857 | 204.05083 | One pair, LTO first. Process CPU times are 222.50000 and 203.67188 seconds. |

The three GCC phase pairs are baseline/LTO 175.1940/202.9613, 177.9627/124.0056 and 166.0546/141.2915 seconds; the slower LTO result remains recorded. Other builds, tests and native continuation runs shared the host. WSL reported some child CPU counters above elapsed time, so those counters do not establish precise scheduler share. The median reductions are 19.4 percent for the GCC rendered phase and 8.46 percent for the one native phase. Neither measures full-speed gameplay.

Every reported state field matches within every pair, including CPU/RAM, frame and audio hashes. The rendered phases each reach 320 fields, 65,917 GX commands, 18,956,440 fragments and 338,464 audio values. All native persisted files match too. Separate GCC baseline/LTO authored traces match 8,192 complete CPU states, ordered bus operations and final RAM images byte for byte. GCC passes all 40 public cases, including required Mesa shader tests, and seven pinned CPU/memory comparisons; native MSVC passes all 40 public cases and sixteen direct comparisons. Exact flags, source and binary hashes, workload inventories, failures and all measurements remain in `local-artifacts/release-lto-20261004/` and `local-artifacts/integrated-runtime-20261004/native-ipo-measurement/`.


## Additional scheduling and texture-cache measurements

The IPC deadline change matches 3,212 full IOS/SDP checkpoints and
109,068,024 serialized bytes. Three interleaved repetitions of 200 million
advances per console and workload reduce component medians by 57.1 to 68.3
percent. Six 100-million-attempt Sunshine runs retain all 44 reported fields
and all four persistent files; medians are 5.721769 seconds earlier and
5.553776 seconds current. The three-billion-attempt pair takes 269.199949 and
190.877508 seconds respectively, but concurrent work differs and prevents a
precise isolated-host speed claim. Exact states, flags, source and executable
hashes remain in `local-artifacts/ios-deadline-20261004`.
[Runtime mappings](runtime-reference.md) describe the event semantics.

The command-processor deadline path likewise improves its component workload
while preserving PI reassertion and FIFO/event boundaries. Its longer
Sunshine pair is slightly slower, with exact state, frame and storage matches.
[Graphics scheduling](graphics-scheduling.md) records both results.

The GPU texture cache avoids repeated decoding and broad texture-memory
checks when the reachable source bytes remain unchanged. On the RTX 3060,
1,024 repeated C8 draws take 1.59635 seconds earlier and 0.0267991 seconds
current; 64 filter-only updates take 0.325189 and 0.0044628 seconds. Exact EFB
comparisons pass. Commercial pairs retain all 44 reported fields, frame bytes
and persistent files: Sunshine at three billion attempts takes
367.960/371.777 seconds earlier/current, and Galaxy at one billion takes
201.853/203.136 seconds. Neither pair establishes a whole-game gain. The
[hardware rendering record](hardware-rendering.md) separates these frozen
cache measurements from later framebuffer-copy work.

## Live-register CPU BAT tables

Relocated instruction and data accesses use lazily built host BAT tables. Each
lookup first compares all active BAT register words with its snapshot, including
direct changes through the public CPU state. Register changes and four/eight-bank
switches rebuild the map before use. The fast lookup and snapshot checks are
inline; rebuilding runs separately. [CPU mappings](cpu-reference.md) describe
the pinned mapping rules and compatibility decisions.

Three alternating pairs per console and loop measured 65 million instructions
per sample under WSL with GCC 13.3, strict floating-point flags, no LTO and fixed
CPU affinity. Code uses the boot BAT mappings and a warmed instruction cache;
Wii data uses MEM2. All complete-state checksums match within each workload.

| Console and loop | Scalar median seconds | Table median seconds | Scalar/table ratio |
| --- | ---: | ---: | ---: |
| GameCube integer control | 2.286626 | 1.630244 | 1.403 |
| GameCube integer/memory/rotate mixture | 2.920403 | 2.521969 | 1.158 |
| GameCube rotate-heavy loop | 2.426957 | 2.031016 | 1.195 |
| Wii integer control | 2.363621 | 2.080386 | 1.136 |
| Wii integer/memory/rotate mixture | 2.857846 | 2.842658 | 1.005 |
| Wii rotate-heavy loop | 2.389862 | 1.833824 | 1.303 |

Other work shares the host and individual timings vary substantially. The Wii
mixed-loop difference does not establish a material gain. These authored CPU
workloads do not establish gameplay speed. An earlier direct SIMD BAT selector
and an out-of-line table lookup also passed correctness checks, but their full
CPU measurements included regressions; those variants are not retained.

The added public tests cover 131,072 mutable mappings, and unchanged pinned
IBAT/DBAT table generation supplies another 131,072 comparisons. Existing
memory, exception, step and dispatch comparisons also pass. Separate scalar
and retained traces match 16,384 complete CPU states, ordered bus records and
both final RAM images byte for byte, totaling 155,957,504 bytes. The trace
includes BAT/HID4 mutations, cache visibility, privileged writes, interrupts,
guest faults and timer changes. Frozen sources, exact commands, preliminary
failures, all timing samples and binary traces remain in
`.cache/cpu-bat-simd-20261004/`.

Three alternating pairs also run each supplied game for 100 million attempted
steps from identical copied user data and replay files. The frozen integrated
variants differ only in the CPU interpreter object and use the same private
class layout. Every reported state field and every persistent file matches
within each game's six runs. Sunshine medians are 7.394077 seconds scalar and
6.281991 seconds table; Galaxy medians are 9.517884 and 10.194303 seconds. The
Galaxy samples include a regression and establish no integrated gain. Both
prefixes produce zero VI fields. Galaxy executes 1,948 GX commands, 3,502,080
software fragments and 4,192 silent audio values in this span; Sunshine
produces no GX commands or CPU audio values. These are startup comparisons,
not gameplay or frame-rate measurements. `prefix-results.json` records all
states, persisted hashes, exact commands, seeds and executable hashes.

## Floating-point rounding guards

Guest scalar, paired-single and quantized operations avoid setting and restoring
host rounding controls when both the incoming C mode and x86-64 SIMD bits already
match. Differing controls still use the scoped set/restore path. This reduces
host environment writes in the usual round-to-nearest case.

Three alternating pairs measure 13 million instructions per sample under WSL
with GCC 13.3, strict floating-point flags, a warmed instruction cache, boot BAT
mapping, no LTO and fixed affinity. The frozen variants differ only in the
rounding guard. Every complete CPU-state checksum and final host rounding mode
matches within each workload.

| Loop and guest mode | Earlier median seconds | Guard median seconds | Earlier/guard ratio |
| --- | ---: | ---: | ---: |
| Scalar, nearest | 0.966406 | 0.891347 | 1.084 |
| Scalar, downward | 0.980700 | 0.931642 | 1.053 |
| Paired-single, nearest | 1.467147 | 1.248665 | 1.175 |
| Paired-single, downward | 1.341236 | 1.301233 | 1.031 |

Other builds, tests and game continuations share the host. Individual samples
vary, including a slower guarded scalar/downward sample. The downward mode
still changes host controls, so its small median difference does not establish
a benefit from skipping writes. These authored loops do not establish a game
frame rate. Exact sources, samples, executable hashes and a matching 16,384-step
full CPU/host-environment trace remain in `.cache/cpu-rounding-20261004/`.

A separate cycle-metadata lookup prototype also preserves its measured loop
states, but has mixed timings with GameCube mixed-loop and Wii rotate-loop
regressions. It is retained only as a private experiment in
`.cache/cpu-cycle-tables-20261004/`.

## Event-bounded runtime execution

The runtime now admits resident register-integer groups between device events.
Command processing, DSP mailboxes, IOS requests and the hardware devices each
report how many cycles can pass before their next visible update. An event at
cycle N permits at most N - 1 grouped cycles. PI level causes and overdue
requests force ordinary stepping. Hardware-device advances defer work inside
that interval, then materialize elapsed state before a register access or event.

Device comparisons cover three million cycles on each console, including the
ordered DMA, audio, disc and framebuffer callbacks, interrupt levels, registers
and final RAM, ARAM, SRAM and card bytes. Separate complete-core tests exercise
CP token/finish interrupts, DSP mailbox phases, IOS acknowledgement/reply
deadlines, CPU-generated MMIO and overflow boundaries. Five alternating pairs
of 150 million device advances reduce isolated medians by 31 to 43 percent.
These device-only loops do not establish commercial gameplay throughput.

Validated CPU RAM transactions also use fixed-width unaligned host copies and
byte reversal after the existing range and device checks. Five alternating
pairs of 50 million transactions preserve every checksum. Four-byte reads
change from 0.288950 to 0.214183 seconds, eight-byte reads from 0.385666 to
0.216563 seconds, and four-byte writes from 0.277871 to 0.227904 seconds.
Two-byte writes regress from 0.227412 to 0.245495 seconds. These isolated WSL
measurements retain all samples and establish no whole-game gain. Public tests
cover unaligned access, boundary bytes, aliases and all supported widths.

The integer-group details and admission guards are recorded in
[native CPU execution](native-cpu.md). Frozen sources, build receipts, complete
runtime comparisons and supplied-game prefix results are retained in the ignored
`local-artifacts/integrated-fast-paths-20261004/` and
`.cache/continuation-scheduling-20261004/` directories. Commercial performance
claims require those executable comparisons to finish.

## RAM clears, resident cache-flush groups and VI calculations

Five alternating pairs compare ten million 32-byte clears per sample. The
direct transfer and eight scalar stores produce identical full-memory hashes.
Strict GCC measurements under WSL give these medians:

| CPU RAM region | Eight stores, seconds | Direct clear, seconds |
| --- | ---: | ---: |
| MEM1 | 0.431671 | 0.027446 |
| MEM1 allocation padding | 0.273609 | 0.020057 |
| Wii MEM2 | 0.389914 | 0.039152 |
| Locked cache | 0.346860 | 0.024335 |

These measure the RAM transfer alone. A separate four-pair CPU fixture measures
the exact resident `dcbf`/integer/conditional-branch group. Stepped/grouped
medians are 0.613679/0.339549 seconds for GameCube interpreter,
0.649758/0.444294 for GameCube native, 0.625700/0.352676 for Wii interpreter
and 0.623561/0.360315 for Wii native. Every complete CPU-state checksum matches.
Retaining repeatedly overwritten native destinations improves a dependent
sequence fixture by 16.9 percent; independent stores remain within timing noise.
Complete grouped native loops improve by only 1.0 to 2.5 percent in that series.

Seven alternating device pairs show 21.2 to 24.6 percent less time in scheduled
VI updates after caching unchanged geometry. Single-cycle samples vary within
two percent. Aligned 64 KiB ARAM transfers take 74.8 to 80.8 percent less time,
with identical bytes, callbacks and ending registers. Sources, exact commands,
all samples and focused strict/sanitizer logs remain in
`.cache/goal-emulation-20261004/`, `.cache/cpu-sequence-register-20261004/` and
`.cache/device-video-timing-20261004/`.

Five draw-admission pairs measure 200,000 unchanged begins per sample. RTX 3060
medians for one, four and sixteen stages change from
0.022288/0.026028/0.048997 to 0.010778/0.010577/0.010814 seconds. Mesa medians
change from 0.021608/0.024053/0.040102 to 0.012421/0.012051/0.012666.
An isolated Galaxy one-billion-attempt pair differs only in the renderer
object. All 60 JSON fields, frame bytes and eight persistent files match at
76 VI fields, 764 dispatches and 104,532,960 GPU fragments, with no fallbacks.
Elapsed times are 70.916 and 69.061 seconds. The shared host limits timing
precision; these component and startup results establish no gameplay frame rate.
The renderer receipts remain in `.cache/graphics-group-20261004/`.

## Guarded RAM polling and quiet MMIO reads

A fresh Galaxy instruction trace records 193,080 fetched instructions, all
repetitions of a word load, immediate comparison and taken conditional branch.
The branch crosses an instruction-cache line. The new CPU fixture retains that
placement and uses ordinary Memory backing, with four alternating pairs of
100 million retired instructions under strict GCC and fixed CPU affinity.
Every final architectural checksum matches the earlier CPU.

| Backend and quiet budget | Earlier median seconds | RAM-group median seconds |
| --- | ---: | ---: |
| Interpreter, 3 cycles | 4.75662 | 4.38096 |
| Interpreter, 8 cycles | 4.77517 | 2.40585 |
| Interpreter, 4,096 cycles | 4.63353 | 0.003884 |
| Native, 3 cycles | 4.48952 | 3.95896 |
| Native, 8 cycles | 4.30614 | 2.01487 |
| Native, 4,096 cycles | 4.74452 | 0.004065 |

The large-budget result measures a stable wait loop whose repeated loads have
the same value throughout an event-free interval. It does not measure complete
game execution, rendering, device work or a gameplay frame rate. Exact trace,
source, compile commands and all samples remain in
`.cache/cpu-scalar-run-20261004/` and `.cache/goal-emulation-20261004/`.

Seven alternating pairs of valid 20-million-instruction MMIO polling loops
measure the separate quiet device-materialization path. GCC GameCube medians
change from 1.75217 to 1.51129 seconds and Wii from 1.87640 to 1.58541.
MSVC strict/LTO medians change from 2.38163 to 2.01359 and from 2.27244 to
2.00042. All 112 samples match complete state and report zero faults. An earlier
benchmark accidentally fell through its branch and measured fault retries;
that failed experiment remains recorded and is excluded from these results.
Receipts remain in `.cache/runtime-scheduling-20261004/`.

## Empty hardware rectangles

A private Galaxy diagnostic through field 1,100 records 721,242 raster
rectangles. Of these, 628,209 contain at most 64 pixels, and 75,088 have no
covered samples. The counts describe that captured workload, not every game.
The bounded coverage rejection avoids an upload and dispatch for admitted
empty rectangles while preserving visible neighbors and retained texture state.

Five alternating RTX pairs replay 20,000 captured empty rectangles per sample.
The earlier path issues 20,000 dispatches; the corrected path issues none.
Median component time changes from 0.125024 to 0.0043678 seconds with exact
EFB results. Positive warm-up work still proves actual GPU execution. This
measures empty work admission, not rendering an entire game or its frame rate.
Required-device checks and all samples remain in
`.cache/graphics-gameplay-20261004/empty-fix/`.

## Conditional-branch fallthrough and portable fog cost

Three alternating or reversed Galaxy startup pairs compare the earlier RAM
groups with the conditional-branch fallthrough extension at one billion
attempts. All final guest state, raw frames, checkpoints and storage match.
Earlier/extended elapsed seconds are 61.38/53.37, 61.38/51.94 and 76.29/48.65.
The median paired ratio is 1.182. The third earlier sample rises substantially
under shared-host work, so all rows remain recorded. A single Sunshine pair
is neutral at 12.87/13.10 seconds. These strict GCC, no-LTO startup captures
use llvmpipe and establish no full-speed gameplay result. Sources and all
samples remain in `.cache/cpu-fallthrough-20261004/`.

The exact portable fog repair has a measured cost. Four alternating RTX pairs
use 20 warmed samples per backend and mode, with 512 captured rectangles in
each sample. Fog-enabled GPU medians rise from 9.360896 to 11.223552 ms,
or 19.90 percent; wall time rises 15.45 percent. Fog-disabled GPU medians are
7.197696 and 7.127040 ms, within sample variation. Every complete EFB/depth
and accepted-fragment count matches. The arithmetic repair closes an exact
Linux-driver pixel error; these component times establish no commercial
frame rate. Sources, all samples and shared-host conditions remain in
`.cache/graphics-gameplay-20261004/fog-d3d12/cost-native-v2/`.

## Ordered small-rectangle batching

Three alternating RTX component pairs compare the original renderer with the
bounded ordered batch. Median original/batch ratios are 2.037 for overlapping
work, 10.872 for spread work, 1.587 for textured work and 1.875 for blending.
Complete EFB, depth and retained state match. The unchanged large path's ratio
is 0.969, within the recorded variation. CPU wall-time outliers remain in the
receipts; they establish no CPU throughput improvement.

The current 224-file Galaxy pair reaches 1,050 fields with identical captured
guest and graphics state. Raster dispatches fall from 240,351 to 112,118.
Elapsed seconds are 205.072 and 200.067 on the shared host. This one sequential
pair verifies dispatch reduction; replicated whole-game speed remains to be
measured. Sources, all samples and original failures remain in
`.cache/gpu-ordered-batch-20261005/`.

## Runtime bookkeeping across resident groups

Five alternating strict MSVC Release/LTO pairs execute a varying-address RAM
copy loop for 20 million retirements. Both variants use the same current core;
only the runtime object differs. Every reported state value matches, including
CPU and memory hashes, device clocks, native counters and fault counters.
The interpreter median changes from 0.546792 to 0.450402 seconds, a ratio of
1.214. Native execution changes from 0.577855 to 0.462358 seconds, a ratio of
1.250. The shared host also runs other work, and all twenty samples remain
recorded.

This measures reduced dispatcher and peripheral bookkeeping while successive
CPU groups share an event-free interval. It does not measure a commercial
frame rate. The 634-case Windows runtime comparison separately checks complete
CPU/RAM state, ordered callbacks and boundary behavior. Source objects,
commands, binaries and raw samples remain in
`.cache/continuation-runtime-20261005/measurements.json`. An earlier compiler
attempt failed with an out-of-heap diagnostic under low host commit headroom.
The retry overwrote that command's log; the original failure remains in the
execution transcript.

Two alternating stock-CLI pairs per supplied game compare the earlier
fallthrough runtime with the cache-flush and quiet-interval extensions at one
billion attempted instructions. All reported guest state, raw frames and
persisted files match after excluding the configured revision string.

| Game | Pair | Earlier seconds | Extended seconds | Fields at the bound |
| --- | --- | --- | --- | --- |
| Sunshine | 1 | 32.570 | 10.026 | 72 |
| Sunshine | 2, reversed | 25.330 | 9.204 | 72 |
| Galaxy | 1 | 77.957 | 77.757 | 76 |
| Galaxy | 2, reversed | 87.624 | 82.651 | 76 |

These Windows captures use software graphics, disabled host audio, copied
storage and ordinary replay input. Sunshine produces 40,784 nonzero guest PCM
values; Galaxy's earlier opening bound remains silent. Compilation and memory
pressure vary during the captures, so the rows establish exact startup
equivalence and their observed timing, without a gameplay speed claim.
Commands, source and binary hashes, storage inventories and all eight runs
remain in `.cache/continuation-runtime-20261005/startup-1b/verification.json`.

## Current register groups and prepared textures

Two alternating or reversed hardware-rendered pairs per supplied game compare
the sealed cache-flush/quiet-interval core with the newer register-group and
prepared-texture combination. Sunshine uses three billion attempts and
Galaxy uses one billion. All reported fields except the configured revision,
raw frame bytes and persisted files match in all four pairs.

| Game | Pair | Earlier wall seconds | Current wall seconds | Earlier CPU seconds | Current CPU seconds | Fields |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Sunshine | 1 | 58.290 | 76.429 | 37.859 | 44.719 | 320 |
| Sunshine | 2, reversed | 30.732 | 35.678 | 21.016 | 23.250 | 320 |
| Galaxy | 1 | 71.035 | 64.206 | 59.250 | 55.031 | 76 |
| Galaxy | 2, reversed | 58.229 | 63.737 | 53.703 | 55.750 | 76 |

The newer combination is slower in both Sunshine pairs; Galaxy is mixed.
These Windows OpenGL interpreter captures use the RTX 3060, one fixed CPU
affinity, copied storage, ordinary replay and disabled host audio. Compilation,
tests and longer gameplay captures share the host. The observations establish
guest equivalence and preserve the measured regression; they do not establish
an overall speed improvement. Source, executable hashes, complete commands,
all samples and state/storage comparisons remain in
`.cache/continuation-runtime-20261005/register-texture-hardware-prefixes/verification.json`.

Prepared decoding separately measures ratios of 1.41 to 2.36 across eleven
formats in CPU cache-fill comparisons with 555,456 exact texel checks.
Demand-sampling controls range from 0.758 to 1.192 on the shared host.
The scalar sampler remains in its original translation unit. This component
result does not explain or erase the combined runtime timings above; see
[hardware rendering](hardware-rendering.md) for the isolated source and scope.

## Conditional branch dispatch

A shared conditional-branch handler retains its taken decision, avoiding a
second BO/BI test and the generic instruction decoder in resident groups. The
native path also avoids looking up a branch in its integer operation cache.
Taken fallthroughs still end a group, and CTR, LR, cache visibility and event
budgets retain their existing behavior.

A pinned WSL GCC `-O3` ABBA comparison executes 100 million guest instructions
per backend per sample, with 50 million two-instruction groups. All samples end
with the same register value and retirement count.

| Backend | Earlier seconds | Shared-handler seconds |
| --- | --- | --- |
| Interpreter | 2.41731, 2.42263 | 2.35108, 2.14140 |
| Native | 2.39583, 2.48087 | 2.14371, 2.09924 |

These are branch-loop measurements under concurrent host work. They do not
establish a whole-game speed improvement. The strict CPU suite separately
passes 1,762,052 complete state and ordered-bus comparisons, with 59,377,974
grouped retirements. Commands, harness and all samples remain in the local lab
receipt `cpu-branch-20261022/result.txt`.

The partial texture-upload fixture separately measures host-to-device byte
counts. With three enabled 64-texel units, changing one unit uploads 608 bytes
instead of rebuilding all 1,120 bytes. Exact CPU/GPU pixels and queued draw order
are checked. This reduces upload traffic in that fixture by 45.7 percent; it is
not a frame-rate measurement.

## Floating-point RAM groups and exact GPU identities

A resident FP-transfer loop compares the frozen preceding core with the grouped
load/store path. Seven alternating samples per backend execute eight million
instructions each. Complete CPU and RAM hashes and ordered bus observations
match in every sample; native executed/fallback accounting also matches the
retirement count.

| Host | Backend | Earlier median seconds | Grouped median seconds |
| --- | --- | ---: | ---: |
| Windows | Interpreter | 0.357349 | 0.146711 |
| Windows | Native integer | 0.416085 | 0.171376 |
| Linux | Interpreter | 0.331128 | 0.170209 |
| Linux | Native integer | 0.322524 | 0.185680 |

GPU division by a signed unit value, or by an equal nonzero finite magnitude,
now returns the exact result bits after the existing special-value guards.
Fifteen counterbalanced native GPU timestamp samples use 262,144 points per
case. Varying W with unit Q changes from 1.1822 to 0.5911 milliseconds; the
general-division control changes from 1.1896 to 1.1862 milliseconds. Complete
raw outputs match. The flat unit case also improves, with substantial clock
variation retained in its receipt.

These are component measurements on a shared host, with no gameplay frame-rate
claim. CPU samples and source receipts remain in the local
`floating-ram-benchmark-{windows,linux}.json` files; GPU commands and samples
remain in `projection-identity-benchmark.txt`, all under
`.cache/continuation-20261022/`. An inline instruction-cache probe was rejected:
its alternating component samples were slower than the existing out-of-line
path. That experiment remains private and is absent from the product changes.

## XFB staging spans

The software XFB encoder now selects a nearest source row once per contiguous
compact-row span instead of once per byte. Seven counterbalanced samples per
profile perform eight complete copies each. The private before/current driver
also checks 1,054 defined layouts across all four host rounding modes, matching
23,154,752 bytes including gaps and guards, source callback order/count and
callback-failure atomicity. It rejects 994 undefined layouts through the shared
unchanged planner.

| Profile | Windows earlier/current milliseconds | Linux earlier/current milliseconds |
| --- | ---: | ---: |
| 640 by 480 | 22.320 / 15.186 | 13.731 / 10.713 |
| 320 by 128, downscale | 2.212 / 1.804 | 1.791 / 1.440 |
| 320 by 128, upscale | 3.637 / 2.004 | 1.647 / 1.218 |
| 640 by 480, short stride | 18.216 / 13.759 | 10.929 / 10.012 |

These strict MSVC/LTO and GCC component measurements share the host with game
captures and builds. They do not establish commercial gameplay speed. Exact
commands, frozen libraries, driver source and all samples remain under
`.cache/continuation-20261022/xfb-runs-*` and `xfb-native/`.
The final samples use a symbol-renamed, otherwise identical shipping copy
function, including its nonzero compact-row invariant check. A normalized source
hash and the earlier prototype samples remain beside the final measurements.

## Scalar-group assembly

Mask-based prefix selection and direct single-line cache spans reduce repeated
scalar-group assembly. A strict MSVC 19.43 x64 `/O2 /Ob2 /fp:strict /MT` build
with `/GL` and `/LTCG` compares the frozen preceding CPU with the final applied
source. Nine alternating samples per backend execute 80 million instructions
each. Every pair matches complete CPU/RAM hashes, ordered bus observations and
native statistics.

| Loop | Backend | Earlier median seconds | Applied median seconds |
| --- | --- | ---: | ---: |
| FP RAM transfers | Interpreter | 1.332720 | 1.253740 |
| FP RAM transfers | Native integer | 1.372090 | 1.298680 |
| Integer RAM transfers | Interpreter | 1.171560 | 1.100330 |
| Integer RAM transfers | Native integer | 1.186890 | 1.162180 |

The earlier eight-million-instruction MSVC samples were noisy: their FP
ratio-of-medians was 1.4 to 3.0 percent slower, while individual paired ratios
ranged from 0.89 to 1.27. Integer transfers improved 4.8 to 5.9 percent; the
unchanged register-loop control was 0.5 to 1.9 percent slower. Those samples
remain recorded beside the longer batch. Earlier prototype Clang/Windows and
GCC/Linux runs also retain their own source/flag scopes; GCC scalar-memory
reductions range from 20 to 24 percent, with a 1.1 percent slower interpreter
branch control.

All measurements share the host with other work. The longer component samples
measure a 2.1 to 6.1 percent reduction, without establishing commercial gameplay
speed. Exact final-source, project settings, compiler logs, binaries and every
sample remain under `.cache/continuation-20261022/scalar-packet-msvc/`; earlier
prototype evidence remains in `scalar-packet-23b/`.

## Recorded Galaxy menu prefix

Two frozen Windows observer binaries compare the earlier core `747d092` with
`14fab0f` through field 2,040. Both use Intel Iris Xe OpenGL, disabled host audio,
the same immutable storage seed and the same accepted ordinary controller
events. The newer observer first passes a separate stock-CLI parity check.
Every reported value and raw frame at all 34 shared checkpoints matches after
excluding only revision, outcome and replay-version bookkeeping. CPU/RAM/PCM
hashes, graphics counters and final frame bytes match within that reported
scope. The old observer's embedded revision predates its product sources;
its frozen source/library receipt identifies the actual `747d092` core.

| Bound | Earlier elapsed seconds | Newer elapsed seconds |
| --- | ---: | ---: |
| Field 2,040 checkpoint | 575.943 | 738.098 |

The newer run terminates normally at its field limit in 738.563 seconds,
including final persistence. The newer combination is slower in this one
shared-host pair. Other observers, builds, tests and GPU probes run concurrently;
the timing does not isolate a source change or establish representative speed.
This observation does not erase the component measurements or qualify an
integrated gain. The earlier field-2,040 storage snapshot is unavailable, so
no storage equality is claimed at that checkpoint.

The immutable 83-file comparison manifest, per-checkpoint timing records,
source/binary hashes and the two raw frame sets remain under
`.cache/continuation-20261022/frame-observer-xfb/galaxy-prefix-2040-20261022T230821380789Z/`.

## Windows adapter request checks

The Windows graphics backend exports the standard high-performance GPU
requests. On the current hybrid laptop, the unchanged binary selects Iris Xe;
the rebuilt binary selects NVIDIA RTX 2000 Ada. Both retain `59edc60` build
metadata, with executable hashes and parsed export values in
`.cache/continuation-20261022/gpu-hint-final/`.

Four stock OpenGL runs verify equal reported state, raw frames and persisted
files within each game pair, with host audio disabled:

| Game and bound | Intel elapsed seconds | NVIDIA elapsed seconds |
| --- | ---: | ---: |
| Sunshine, three billion attempts / 320 fields | 14.401 | 47.918 |
| Galaxy, one billion attempts / 76 fields | 33.168 | 27.474 |

These are single sequential pairs on a shared host. Shader compilation,
driver caches and concurrent workloads are uncontrolled. Sunshine is slower
with the request in this pair; Galaxy is faster. The observations verify the
recorded outputs across adapters and do not establish representative gameplay
speed. Commands, device strings, timings and hashes remain in
`.cache/continuation-20261022/windows-gpu-hint-stock-pairs/`.

## Rejected native single-operation replacement experiment

A private two-hit FIFO prototype keeps the 1,024-entry limit, replacing the
permanent single-operation cache with fixed executable slots. It passes
416 timed component children and 32 fixed-work game children. CPU state and
RAM checks pass throughout; game pairs also match frames and storage. All
raw samples, cold costs, maintenance counts and negative controls remain
recorded. The production cache is unchanged.

Four fixed AB/BA startup pairs per game/platform give median elapsed-time
ratios of 0.9856/1.0064 for Windows Sunshine/Galaxy and 0.9817/0.9911 for
Linux. These small or mixed startup changes do not justify the required
control regressions. In the GameCube component fixtures, the 1,024-operation
resident loop is 15.6% slower on
Windows and 17.0% slower on Linux; the unsupported-integer control is
5.0%/9.6% slower. The phased hot-set fixture's inclusive time increases
91.7%/29.7%, and the 1,025-operation cycle is 71.0%/20.2% slower. The
prototype is rejected for integration.

The experiment uses fixed software-rendered startup work with host audio
disabled. Shared captures remain active on other logical processors; the
affinity reservation does not establish host isolation. No samples are
discarded or repeated based on their result. The complete sealed evidence
remains under `.cache/continuation-20261022/native-two-hit-fifo-timing/`.
These results do not establish representative gameplay speed.

## Rejected split-cap GPU experiment

A private experiment splits each owned batched dispatch at eight rectangles,
preserving the original draw order, state, global keys and program bytes.
It uses 404 complete snapshots: 57 change and 347 remain literal controls.
It does not merge snapshots or invent their initial framebuffer state.
Correctness checks pass on Intel Iris Xe, llvmpipe and NVIDIA RTX 2000 Ada,
including authored ordering controls and intentionally failing variants.

One fixed window retains eleven pairs per snapshot and driver, or 26,664
elapsed GPU queries. Summed per-snapshot medians for the 57 changed cases
increase on every driver:

| Driver | Original cap 16, ms | Split cap 8, ms | Increase |
| --- | ---: | ---: | ---: |
| Intel Iris Xe | 90.641408 | 144.776580 | 59.72% |
| Mesa llvmpipe | 32.729378 | 58.128308 | 77.60% |
| NVIDIA RTX 2000 Ada, 597.30 | 6.267904 | 10.168320 | 62.23% |

The unchanged controls increase 0.97%, 1.23% and 0.03%, respectively. All
negative deltas, ties, raw ranges and construction costs remain recorded;
no query is discarded or repeated. The timer covers prepared-buffer binds,
dispatches and their barriers, excluding uploads, restores and validation.
These are conditional snapshot sums under shared CPU/GPU load, not a scene
weighting or gameplay FPS result. The additional dispatches outweigh the
reduced rectangle scans in this experiment. The candidate is rejected and
production keeps cap 16. The sealed source, correctness checks and single
query window remain under
`.cache/continuation-20261022/batch-cap-study/cap8-timing/`.

## Rejected LR/CTR native extension

A private extension admits canonical LR/CTR register transfers to the native
backend. One-cycle reads may join resident groups; two-cycle writes remain
group boundaries. It retains the original 1,024-operation cache and compact
pages. The candidate passes the authored correctness suites and all 832
measured children: 768 component children and 64 fixed-work game children.
It is rejected for integration because targeted gains accompany material
control regressions and small, mixed startup changes.

Four fixed AB/BA pairs cover each of 96 component settings across both console
modes, both CPU backends and Windows/Linux. Affected native hot-loop elapsed
ratios range from 0.365 to 0.723 on Windows and 0.443 to 0.705 on Linux.
Several Windows native controls are consistently slower:

| Component | GameCube elapsed increase | Wii elapsed increase |
| --- | ---: | ---: |
| Resident 64-operation loop | 11.39% | 8.67% |
| Other SPR, XER | 7.04% | 7.09% |
| Noncanonical LR encoding | 10.05% | 9.45% |
| LR after cache saturation | 6.62% | 4.53% |

These percentages use medians of the four paired ratios. Both resident-loop
controls are slower in all four pairs. Cold LR round-trip median costs also
increase: 62.2 to 77.6 microseconds on Windows GameCube, and 50.0205 to
79.6075 microseconds on Linux GameCube. All setup, cold, warm, hot and inclusive
costs, counter differences, maintenance counts and remaining controls are
retained; no cause is assigned to the regressions.

Native startup paired median ratios are 0.9894/1.0060 for Windows
Sunshine/Galaxy and 0.9982/0.9829 for Linux. Interpreter startup ratios are
1.0081/1.0064 and 1.0239/1.0647; the last setting spans 0.8715 to 1.1979.
All required guest-state, frame and storage comparisons pass. Software graphics
and disabled host audio limit these results to startup work.

The first component interval fails after 40.112599 seconds on a driver assertion
that incorrectly requires native cache occupancy from the interpreter. Its 138
children all return zero. Corrected offline validation recovers all 69 pairs;
only the remaining 315 pairs run in a separate 288.992642-second interval.
The original failure and 632.290067-second gap remain recorded. The subsequent
64-child game stage completes once, without timeout or repeat. Shared gameplay,
audio load changes and the observed WSL clock offset remain explicit; affinity
does not establish isolation. No row is filtered or rerun based on its result.

The complete assessment and 4,034-file seal remain under
`.cache/continuation-20261022/native-lrctr-timing/continuation-v3/`.
These mechanism and startup results do not qualify gameplay speed or justify
changing production CPU admission.
