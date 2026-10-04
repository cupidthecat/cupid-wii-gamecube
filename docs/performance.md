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

Future gameplay measurements should record guest video/audio rates, compiler and flags, output driver, host workload, run bounds and accepted controls. Keep profiles and commercial-game traces local. A throughput improvement without rendered, controllable gameplay does not establish playability.
