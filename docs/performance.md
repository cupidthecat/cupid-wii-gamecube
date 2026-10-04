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
