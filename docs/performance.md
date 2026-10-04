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

The same initial profile attributed 7.70 percent of sampled CPU time to `DSP::Engine::advance`. Its period accumulator now adds short instruction intervals directly and uses modulo only when crossing an update boundary. Total cycle overflow checks, update phase, mailbox interrupts and audio updates retain their prior behavior. The full existing DSP suite passed against both the preserved and corrected implementations under strict GCC and Clang 18 ASan/UBSan.

A separate component measurement ran twenty million varying advances for ROM and CARD tasks on both consoles, three times each. Earlier runs took 0.215 through 0.234 seconds; corrected runs took 0.030 through 0.033 seconds. Program hashes, mailbox values, control registers and interrupt counts matched in all twelve pairs. This measures the scheduling call, with no audio rendering or guest CPU execution.

Six integrated 100-million-instruction startup runs replaced only the DSP object against the same preserved runtime checkpoint `6f33d49f6e73248093e8a9533719e37e37b6b5d1`. Every final JSON field matched. Median elapsed times were 8.322 seconds before and 8.634 seconds after. Package builds and two longer game probes were active and host load changed during the measurements; these results establish no integrated speedup. They are retained separately from the component result.
