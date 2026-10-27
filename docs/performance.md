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

## Bulk EFB buffer reads

Bulk EFB encoders, copy-trigger clear and software fragments use CPU buffer
reads after their owning path synchronizes queued GPU work. Public EFB peeks
retain their synchronization. This removes repeated empty synchronization and
statistics construction from each pixel read.

Matched strict MSVC 19.43 x64 `/O2 /Ob2 /GL /MT /fp:strict` drivers compare
frozen earlier and current libraries on NVIDIA RTX 2000 Ada. Six processes
alternate in AB/BA/AB order. Each process first checks four GPU-to-CPU blend
transitions and 144 copy/clear cases, then runs three samples of 32 copies per
mode. Every output byte matches a direct encoder, and hashes match between
variants. Each full frame is 640 by 528 pixels.

| Source and copy size | Earlier median seconds | Current median seconds |
| --- | ---: | ---: |
| RGBA6, full | 0.281109 | 0.202601 |
| RGBA6, half | 0.177889 | 0.087043 |
| Z24, full | 0.262060 | 0.165293 |
| Z24, half | 0.145576 | 0.060160 |

The measured ratios range from 1.39 to 2.42. Builds, tests and game runs share
the host; these are component measurements rather than gameplay FPS. Earlier
unmatched-driver and overlapping-stride trials remain recorded separately.
Exact drivers, frozen headers/libraries, commands and all matched samples
remain in `.cache/efb-bulk-copy-20261023/`.

## Indexed scalar grouping

The resident scalar path also admits indexed RAM transfers. A fixed authored
matrix executes 16 million guest instructions per child over indexed integer,
indexed floating, immediate RAM and register-only loops. The initial decoder
passes 128 timed children per compiler with matching complete CPU/RAM hashes.
Its indexed paired median elapsed ratios are 0.326 to 0.386 on GCC and 0.296
to 0.365 on MSVC. The MSVC immediate controls include a 6.15 percent median
slowdown, which remains recorded.

The shipping variant shares the decoded transfer class between admission and
retirement. One further fixed MSVC comparison retains 48 children in ordered
baseline/initial/shared-decoder triplets. All state and RAM hashes match.
Four GameCube settings compare the final variant with the frozen baseline:

| Loop and backend | Final/baseline median paired elapsed ratio |
| --- | ---: |
| Indexed integer, interpreter | 0.344879 |
| Indexed floating, native integer | 0.300460 |
| Immediate RAM, interpreter | 1.028100 |
| Immediate RAM, native integer | 0.990243 |

The immediate interpreter control retains a 2.81 percent median slowdown and
a single 1.2275 ratio. No sample is removed or repeated based on its result.
These shared-host component results support the indexed path while retaining
its measured costs; they do not establish a commercial gameplay gain.
Source pairs, plans, commands and every sample remain under
`.cache/continuation-20261022/indexed-scalar-20261023/`.

## GPU tiled EFB encoding

GPU texture copies encode compact tile rows directly from EFB storage instead
of reading back the full color/depth buffers for CPU encoding. A fixed native
MSVC comparison uses matched strict `/GL` drivers and frozen source pairs.
Six processes run in AB/BA/AB order, with nine samples of 32 full-frame copies
per mode and variant. Every output byte and hash matches.

| Source and copy size | CPU bulk median seconds | GPU median seconds |
| --- | ---: | ---: |
| RGBA6, full | 0.291518 | 0.019829 |
| RGBA6, half | 0.142869 | 0.012780 |
| Z24, full | 0.237687 | 0.019339 |
| Z24, half | 0.088563 | 0.012766 |

These warm-copy ratios range from 6.94 to 14.70. Initialization and shader
compilation occur before the measured copy loop. Inclusive process costs
remain recorded, including the slower first two processes. Other builds,
tests and game runs share the host. The comparison uses the frozen initial
indexed CPU base and does not measure gameplay FPS or establish a whole-game
gain. Exact sources, drivers, samples and process costs remain under
`.cache/gpu-texture-copy-20261023/`.

## Synchronization callers

A fixed 8,000-field Sunshine census at `a26beb8` uses the interpreter, OpenGL
rendering and disabled host audio. It records 23 GL entry points, six SDL_GL
calls and the callers of CPU/GPU synchronization. All 60 final state
fields, raw state/stdout/frame bytes and stored files match the preceding
reference run. The 135 checkpoint boundaries, synchronization counts, readback
bytes and final hardware fragment total reconcile independently.

The fixed field 6,000–8,000 window takes 447.097668 seconds. Projected texture
range rejection accounts for 34,449 of 35,390 draw declines and 26,241 of
27,182 dirty synchronizations caused by those declines. Its synchronization
calls take 89.638871 seconds within the 97.870984-second draw-decline total;
these values overlap. Another 1,000 dirty synchronizations serve CPU color
peeks. The 29 APIs together account for 121.469527 seconds.

These are instrumented host call durations on a shared host. They include
driver work, scheduling and waits for preceding work; they do not identify
GPU execution time or establish a speed gain. Raw streams, exact comparisons,
source pins and the independent reconciliation remain in
`.cache/gfx-sync-reason-run-v2-20261024/` and
`.cache/gfx-sync-reason-postterminal-review-20261024/`.

## Integer shader division

Both raster shaders determine rounded binary32 division from integer quotient
and remainder, replacing the binary64 estimate and midpoint correction search.
The helper normalizes the significands and expands the remainder in three
bounded chunks. It preserves signed zero, gradual underflow, ties to even,
overflow and canonical NaN results. Unit divisors and equal magnitudes retain
their exact fast paths. This division helper requires neither shader integer64
nor binary64 arithmetic.

A fixed native comparison uses six processes in AB/BA/AB order. Each process
runs six profiles, retaining the first one-draw sample and a warm 64-draw sample.
All full color/depth readbacks, expected values and paired records match.
Warm unequal-projection candidate/baseline wall-time medians are 0.802310 with
mip selection and 0.639091 with fixed LOD. Every sample remains recorded,
including the first unit-divisor warm sample at 1.176048 and the first unequal
mip cold draw at 4.078421 seconds versus 0.029350 seconds. The first inclusive
candidate process takes 7.022113 seconds versus 4.375379 seconds; subsequent
process pairs are close. Timer-query intervals include submission, readback
and scheduling effects and are not treated as pure GPU execution time.

Sixteen fixed startup runs cover both supplied games, interpreter/native CPU
execution and both comparison orders. All eight pairs match all 60 state
values, raw state/stdout/frame bytes and stored files. Timing is mixed; the
first Sunshine interpreter candidate takes 1.286247 times the baseline.
A separate instrumented Sunshine run reaches 8,000 fields with all 60 values
and raw outputs matching the stock synchronization census. Its 1,232.462020
seconds include raw capture overhead and overlap a CPU benchmark window.
These shared-host observations establish neither an isolated whole-game gain
nor full-speed gameplay. Sources, every cold/warm/process cost, fixed plans
and exact comparisons remain under `.cache/integer-gpu-quotient-clean-20261024/`,
`.cache/integer-gpu-quotient-clean-stages-20261024/` and
`.cache/projected-guard-clean-quotient-run-20261024/`.

## CPU dispatch and host-control comparison

A frozen seven-file CPU change combines recent-cache hint queries, ordered
scalar budget minima, raw floating-register dispatch and MSVC x64 boolean
classification. The baseline is `05175df7e0d6f777c7a4ac17b5df963be3a437b7`.
Both images retain the same graphics implementation.

Strict native MSVC Release/LTO builds use the same static dependencies; matched
GCC Release builds use the same system dependencies under WSL. Fresh full-core
compilation, compiler commands, link inputs, ABI/layout checks and source hashes
were reconciled before execution. Independent correctness checks include full
component phase boundaries and continuation, raw register/host-control matrices,
classification controls and subsequent instruction-cache fetches.

The fixed timing schedule contains 1,920 child processes: 624 component and 336
floating-point processes per host. Four alternating comparison rounds have
baseline/baseline controls before and after them. CPU affinity is fixed.
All cold, setup, warm, hot, inclusive and process costs remain recorded. Every
complete State/RAM result matches; native component counters also match.
Component hot phases execute six million instructions. Their preceding phases
add 69,632 retirements. Floating-point profiles execute three eight-million-word
hot blocks, plus 1,024 warmup and eight cold words.

| Workload | Native MSVC hot candidate/baseline ratios | GCC hot candidate/baseline ratios |
| --- | ---: | ---: |
| Raw scalar signs | 0.651-0.711 | 0.820-0.877 |
| Raw paired signs and merges | 0.645-0.680 | 0.845-0.936 |
| Mixed floating operations | 0.746-0.787 | 0.859-0.923 |
| Arithmetic control | 0.823-0.855 | 1.050-1.070 |

These ranges span per-console/backend medians, not every individual sample.
The GCC arithmetic regression
remains part of the result. Integer controls are near parity; shorter cold and
inclusive samples include regressions. In particular, the Wii interpreter
polling profile has a 1.375 inclusive ratio despite a shorter hot interval.
Its compressed polling interval is below one millisecond, so startup and final
work dominate that inclusive sample. No uniform improvement is claimed.

Thirty-two native Windows commercial startup prefixes cover both games and CPU
backends, with AB/BA order and baseline/baseline controls before and after.
Sunshine reaches 320 VI fields within three billion attempts; Galaxy reaches 76
within one billion. All 60 reported values, final frames and stored files match.
Sunshine candidate/baseline wall ratios are 0.793-0.827 and Galaxy ratios are
0.736-0.792. A separate fixed 32-run GCC/WSL comparison uses software graphics
with the same bounds and comparison orders. All 60 values, final frames and
stored files match. Its Sunshine wall ratios are 0.912-0.990 and Galaxy ratios
are 0.934-0.980; every baseline control remains recorded.

These bounds describe startup prefixes, not sustained gameplay,
completed levels, endpoint audio or physical Linux performance.

A further fixed Sunshine replay reaches 8,000 fields in both images. All sixty
reported values, final raw frame bytes and stored files match. An identical
private Output-only observer records the existing hash of every presentation;
all 8,000 recorded 64-bit frame hashes match, with contiguous counts and terminal
hashes checked. This does not compare raw intermediate image bytes. These runs
share the host with correctness work and have no isolated speed interpretation.

The receipts, fixed schedules and all control costs remain in
`.cache/cpu-bundle-05175df-20261024/` and
`.cache/integrated-progress-20261024/cpu-bundle-*/`.

## Packed sequence storage and discrete projection

The native sequence cache retains exact opcode-content keys until its byte
budget fills. Dispatch collisions no longer retire compiled bodies, and code
writes change protection only on touched host pages. A frozen MSVC Release/LTO
comparison against `2d5bcea` runs 324 component children across both consoles,
short/mixed/heavy sequences, unchanged leaf controls and 1 to 8,192 keys.
Every paired complete State and guard image matches. At 257 keys, warm sequences
reuse their compiled bodies instead of recompiling on each sweep.

A further 192 children use fixed logical-processor affinity and eight million
hot calls each for 1, 64 and 256 keys. Results retain costs: mixed 256-key median
paired candidate/baseline ratios are about 1.165 on both consoles, while heavy
sequence controls improve. Other controls vary substantially. All cold, hot,
process and state records remain under
`.cache/native-blocks-20261025/timing-v1/` and `timing-controls-v2/`.
Neither component schedule establishes a uniform gain or commercial-game FPS.

The sampled projection guard can retain small draws whose continuous coordinate
range crosses a pole between actual raster samples. Six matched MSVC processes
run in AB/BA/AB order on NVIDIA RTX 2000 Ada. Each profile retains its first
draw and three warm 128-iteration samples. Every full color/depth buffer matches
ordinary software command processing, and all paired frame hashes and guest
fragment counts match.

| Command profile | Earlier warm median seconds | Sampled guard warm median seconds |
| --- | ---: | ---: |
| Crossing only | 0.0392574 | 0.0030610 |
| Safe/crossing alternation | 0.1976680 | 0.0053421 |
| Coarse-safe control | 0.0042604 | 0.0048605 |
| Quad-cap decline control | 0.1865590 | 0.2009820 |
| Extreme staged decline control | 0.2022980 | 0.1898370 |

The final safe/crossing sample records 385 full EFB readbacks earlier and four
with sampled admission. The coarse-safe and quad-cap controls retain their
slower medians. Complete process costs include cold initialization and full
buffer comparisons outside the timed submission interval. Builds, sanitizer
checks and longer game runs share the host; call durations include driver work
and waits. Exact commands, frozen libraries, all samples and terminal receipts
remain under `.cache/projection-20261025/timing-v1/`. These component results
do not establish sustained gameplay speed.

## Conditional branches and code-cache working sets

Native register spans can include conditional branch exits. Their emitted
retirement counts preserve ordinary timing cuts, including taken branches to
the following word. Arithmetic and control bodies share a 128 MiB runtime code
budget, independent of the initial hash-map reservation. Windows commits the
arena when the first sequence compiles. Standalone legacy constructors retain
their smaller budgets, and the runtime's individual-instruction cache remains
separate. This change trades a larger code allocation for retained working sets.

A matched Galaxy startup comparison of the same branch-capable core with
512 KiB and 128 MiB budgets runs on MSVC and GCC. At one billion attempts, the
smaller arena compiles 7,571 bodies and retires 5,850 entries; the runtime budget
retains all 4,963 compiled bodies with zero retirements. Guest reports, final
raw frames and stored files match. The constructor, mapping, protection and
cold allocation checks remain under `.cache/native-runtime-budget-20261025/`.

Eight further Windows startup children compare the combined branch/cache work
against `a717752` in AB/BA order. Sunshine compiles 2,815 sequence bodies and
executes 10,469,355 sequence calls; Galaxy compiles 4,963 bodies and executes
41,842,975 calls. Neither run retires a cached body. CPU/RAM hashes, final raw
frames and stored files agree with the earlier core. The runs share the host
with correctness checks. Their elapsed times do not establish a runtime gain
or sustained gameplay FPS. The complete comparison remains under
`.cache/native-blocks-20261025/native-integration-prefix-v2/`.

Control entries also have 4,096 PC-indexed dispatch hints. The last-entry check
retains priority. Each PC hint requires exact fetched contents and count; a
collision preserves the compiled body in the content map. This avoids hashing
on a matching hint without using guest RAM as a substitute for fetched words.

A private MSVC component schedule runs 24 AB/BA children. All six State/RAM
phase images and all native counters match in every pair. Median paired hot
candidate/baseline ratios are 0.856, 0.768 and 0.864 for taken-branch working
sets of 64, 257 and 512 keys. The one-key control retains a 1.069 regression;
untaken and arithmetic controls are near parity. Samples last 40-90 ms on a
shared host and do not establish a whole-game gain. All cold, process and
control records remain under `.cache/native-pc-dispatch-20261025-v1/timing-v1/`.

Eight matched startup children compare the integrated PC hints against the
same branch-capable core. CPU/RAM hashes, raw frames, stored files and every
native counter match. The same Sunshine and Galaxy startup bounds apply.
The longer Sunshine route runs separately against the preceding integrated
core; its outcome is not evidence for the PC-hint change.

## Ordered medium rectangles

Rectangles of 257 through 4,096 pixels can share the existing ordered GPU
batch. The sixteen-packet limit, 4,096-pixel union bound, occupancy check and all
state/texture/copy boundaries remain in force. A 64-square quad now uses one
dispatch instead of two while preserving the complete CPU reference image.
This reduces submission calls in that fixture; it does not establish a
commercial-game speedup.

Required-device tests pass on Windows/NVIDIA and GCC/Mesa. The eight matched
startup children preserve guest state, frame bytes, stored files and native
counters. A further Windows pair reaches 2,400 Galaxy fields with exact CPU/RAM
hashes, every native counter, raw frame bytes and stored files. Hardware
dispatches fall from 1,759,712 to 1,629,306. Hardware fragments remain
3,340,681,497 and fallback rectangles remain 895. The pair takes 404.07 and
391.36 seconds on a shared host; it is not an isolated FPS measurement or a
completed-level result. Exact commands, source snapshots and retained controls
remain under `.cache/gpu-mid-batch-20261025-v1/`.

## Ordered fragment rasterization

The OpenGL backend can submit sparse and large rectangles through instanced
bounding quads and fragment interlock. The shared shader still performs the
guest coverage, depth, TEV, blend and retained-state operations. Native batches
hold at most 256 rectangles and 1,048,576 summed bounding pixels.

A frozen native CLI reaches 2,400 Galaxy fields with CPU/RAM hashes, every native
CPU counter, fragments, fallbacks, raw frame bytes and persisted files matching
the preceding compute snapshot. Fragment submissions fall from 1,629,306 to
74,840; other copy and synchronization compute programs remain in use.
Its 351.94-second run and the earlier 391.36-second capture have different
concurrent workloads. Their difference does not isolate this change's cost.

A later four-child A/B/B/A comparison uses the corrected automatic-selection
runtime, 1,200 Galaxy fields, fresh copies of the same seed and ordinary replay,
and disabled host audio. All four guest-state, native-counter, frame and storage
comparisons pass. Times in execution order are 97.4213 seconds for compute,
100.0329 and 93.1655 for ordered rasterization, then 104.8860 for compute.
The candidate/baseline median ratio is 0.955. The first pair is 2.68 percent
slower, and the reverse pair is faster. Other CPU work and the separate gameplay
observer remain active; these are shared-host menu samples, not sustained
gameplay FPS or an isolated speedup.

That timing snapshot precedes the initialization error-classification guard;
its raster arithmetic and submission policy are unchanged by the guard.

Raw samples and their exact source/binary identities remain in
`.cache/gpu-ordered-auto-20261025-v2/balanced-v1/`; the earlier 2,400-field
comparison is in `.cache/gpu-native-raster-galaxy2400-20261025-v1/`.

## Native word-transfer packets

A frozen MSVC eight-word component comparison records candidate/baseline hot
ratios of 0.523-0.530 for one key and 0.682-0.714 for 257 keys, across relocation
disabled and enabled. Complete State and RAM match. The translated unaligned
decline control retains a 1.058 ratio. Earlier two- and four-word prototypes
retain regressions of roughly 1.10-1.26; runtime admission therefore requires
eight words. These shared-host samples do not establish whole-game speed.

Eight ordinary Windows CLI startup children compare the frozen CPU candidate
against `cf383f2` in A/B/B/A order for each game. Sunshine reaches 320 fields at
three billion attempts and Galaxy reaches 76 fields at one billion. Guest-state
diagnostics, hardware counters, raw frames and stored files match. Native CPU
diagnostics report the newly emitted transfers separately. Source, controls and
terminal comparisons remain under
`.cache/native-mixed-ram-20261025-v1/qualification-v4/` and
`commercial-gate-v1/`. These startup bounds precede integration with the ordered
fragment renderer and do not qualify gameplay FPS.

## Protected floating-point operation costs

A 32-child balanced component comparison measures normal scoped work and the
protected callback. All results match. Shared-host candidate/baseline ratios are
1.0096 under MSVC and 0.9866 under GCC. These near-parity samples measure wrapper
overhead rather than a gameplay gain. Samples and emitted-code comparisons,
including the recorded non-LTO extra template call, remain under
`.cache/hfe-exception-boundary-20261025-v1/`.

## XFB row copies

Little-endian hosts copy each completed XFB staging row into guest RAM with
`memcpy`. A 96-child MSVC Release/LTO comparison covers six valid layouts and
two 64 MiB cycling-buffer cases. Median paired candidate/baseline elapsed
ratios range from 0.044 to 0.502; all 24 balanced quartets favor the candidate.
The 640-by-528 row set records 0.122 with reused buffers and 0.464 with cycling
buffers. Setup, allocation and hashing are outside the timer; shared dispatch
and bookkeeping costs remain included. No samples are removed.

These measurements cover CPU row scattering after readback, not readback waits,
GPU execution or whole-emulator speed. The logical big-endian fallback retains
the original byte loop; no big-endian hardware result is established. Qualified
loop, source, executable and raw samples remain under
`.cache/xfb-row-memcpy-20261026-v1/` and
`.cache/xfb-row-scatter-results-20261026-v1/`.

## Native byte and halfword blocks

An authored CPU workload compares baseline `3b74213e72d3` with the native
`lbz`, `stb`, `lhz`, `lha`, and `sth` extension. Each sample executes ten million
eight-instruction resident blocks, with strict floating-point flags, GCC 14.2,
Release optimization, no LTO, no graphics/audio and no disc. Three A/B/B/A
quartets run per workload and console configuration on the shared Linux host.
Both builds select the native backend explicitly; the default backend is unchanged.
Every serialized CPU field and all 65,536 RAM bytes match across each workload's
twelve samples. Native counters differ as expected.

| Configuration | Baseline median seconds | Candidate median seconds |
| --- | ---: | ---: |
| GameCube, mixed byte/halfword operations | 0.956808 | 0.556368 |
| Wii, mixed byte/halfword operations | 0.959272 | 0.546389 |
| GameCube, existing word-only operations | 0.576315 | 0.539287 |
| Wii, existing word-only operations | 0.561403 | 0.513376 |

The first implementation improved narrow transfers but made the word-only
component medians 2.7–8.8 percent slower across two measurement passes. The
retained implementation caches native packet eligibility with resident
instruction words, eliminating repeated packet scans; the table shows this
final pass. Earlier results are retained rather than discarded.

These are synthetic CPU-path measurements, not game profiles or gameplay FPS.
They do not establish sustained full speed on either console. Source, all timing
samples and binary hashes are retained in `.cache/native-widths-20261027/`.
