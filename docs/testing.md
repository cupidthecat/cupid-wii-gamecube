# Tests

Public tests run without console firmware, commercial discs, save files or keys:

```sh
ctest --test-dir build/release --output-on-failure
```

CTest registers separate CPU, memory/core, disc, device, remote, remote-speaker, host mixer, extension-cipher, service discovery, DSP, handheld, graphics-command, vertex, texture, fragment, rasterizer, framebuffer-copy, IOS, network, system-configuration, executable-loader, font-loader and runtime suites, plus CLI help, version and synthetic runtime checks. Each suite has a timeout. Assertions stay active in Release builds because the test support code performs explicit checks.

The native integer suite requires actual emitted execution for supported
immediate and rotate operations and compares complete CPU state with the
interpreter. It also checks fetched instruction visibility, fallback, cache
bounds, interrupts and reset. The separate native CLI self-test selects that
backend and requires a nonzero emitted-operation count. Both skip on hosts
without x86-64 support; ordinary native game selection reports an error there.

The authored guest video suite executes its display, graphics, controller and input-dependent redraw operations through PowerPC instructions on both console modes. See [guest video](guest-video.md) for the fixture and checked output. It uses no private console assets.

The hardware renderer suite compares exact EFB pixels, depth and counters with the CPU path. The separate `guest_video_hardware` suite drives GPU draws, EFB texture copies and clears, texture sampling, XFB output and controller-driven redraw through actual guest instructions on both consoles. These suites skip an unavailable context by default; `CUPID_REQUIRE_HARDWARE_TESTS=ON` makes that condition fail. Shader and backend failures always fail. See [hardware rendering](hardware-rendering.md) for coverage, fallback and driver scope.

The input replay suite checks script parsing, field and instruction ordering, input ranges, and controller changes observed by authored guest SI polling instructions on both console modes.

The service discovery transport suite checks IPC scheduling, USB endpoints, L2CAP channel negotiation and SDP continuation through ACL packets. A queued HCI event keeps bulk input pending until the event is consumed; the test checks this ordering through the public interfaces.

The framebuffer access suite clears EFB data through the graphics FIFO and reads it through authored guest CPU loads on both consoles. Guest stores also poke color and depth, including ordered binary64 stores. It checks address aliases, narrow stores, alpha read modes, depth, format reinterpretation, out-of-range pixels, independent write masks and locked-cache DMA.

The same suite executes a cached RAM display list through guest stores to a FIFO page alias and checks its visible framebuffer clear. Core tests check gathering across the whole port page, byte order, adjacent unmapped addresses, and the memory-error callback. Runtime tests check that callback's PI cause, masking, acknowledgement, and unchanged error-detail registers on both consoles.

The system configuration suite checks typed SYSCONF parsing, default settings, controller registration, preservation of unrelated values, malformed inputs and serializer bounds. IOS fixtures check initialization before guest file reads, retained file ownership and settings, backups, and unsafe-path failures. See [system configuration](system-config.md) for the independent full-file comparison and its defined boundary choice.

The Remote speaker suite checks PCM8 and Yamaha ADPCM decoding, predictor state, clipping, sample-rate divisors, gain, suppression and invalid-input state preservation. It checks decoder samples and packet metadata.

The extension cipher suite checks authored key records against fixed results from the compiled behavioral target, encryption and decryption, all eight address phases, chunking, address wrap, and retained key material.

The output suite uses SDL's dummy video and audio drivers to create a renderer, upload guest framebuffer data, queue stereo samples at all four supported rates, and poll input. The input suite attaches virtual controllers and checks four-port hotplug, SI buttons and axes, analog triggers, Remote/Nunchuk mappings, and guest rumble through SDL's virtual driver callback. The host audio suite checks real SDL callbacks, DMA channel order, four speaker ports, gains and preserved DMA telemetry. Its Machine fixture sends speaker reports through IPC, USB and ACL while ordinary guest instructions advance the devices. These checks open no host window and play no sound. Headless builds report all three backend suites as skipped. They do not measure driver performance or verify a physical controller.

The pure host mixer suite checks sample queues, filtering, saturation, rate changes and overflow without SDL. The handheld suite checks authentication calculations, checked memory requests, an authored PowerPC command path, malformed buffers and unsupported variants.

The disc-stream suite checks ADPCM decoding, drive command state, callback
timing and actual guest DI/AI writes on both consoles. An optional compiled
comparison covers every decoder header pair and retained predictor history,
plus unchanged drive command handlers and streaming callbacks extracted into
the build directory. The scheduling driver supplies clocks, encoded reads and
mixer capture; it preserves the compared command, planner and reset functions.
SDL audio checks exercise the separate stream queue, channel order and AI
gains. These fixtures use authored bytes. See [disc audio](disc-audio.md) for
the exact comparison scope and source mapping.

Graphics scheduling tests check retained CP/PE interrupt levels after PI
acknowledgement, split event boundaries and register-driven FIFO resumption.
The separately frozen component/state comparison is recorded in
[graphics scheduling](graphics-scheduling.md).

Renderer readback checks standard, widescreen and square pixel views at the initial window size and after a resize. Mouse checks place infrared points inside the displayed image and exclude the surrounding black bars.

The headless audio statistics suite checks sample counts, nonzero values, the full signed peak range and a fixed hash of canonical big-endian PCM bytes. Invalid rates and incomplete stereo pairs leave these statistics unchanged. The SDL output suite checks the same statistics while queueing audio.

The synthetic runtime check executes a redistributable program assembled within the project. Passing that check shows the tested execution path works; it does not establish commercial-game compatibility. See [compatibility](compatibility.md) for asset-dependent results and remaining runtime limits.

## Integrated snapshot checks

A sealed October 4 integrated scheduling/cache/disc-audio snapshot passes all
41 public checks and 17 optional compiled comparisons on native MSVC and GCC
13. Both runs require successful OpenGL tests; Windows uses the RTX 3060 and
Linux uses Mesa under Xvfb. The private asset-dependent opt-in test skips on
both. Extracted Windows and Linux packages pass help, exact version metadata
and guest execution checks. The frozen 209-file manifest has SHA-256
`ef66567bb5946a7fe410ad1a59bf95476c46efe21cb20e3392677a2ea2b448e4`;
receipts remain in `local-artifacts/integrated-continuation-20261004`.
This snapshot excludes later native integer, GPU framebuffer-copy and
on-demand disc-encryption work. Those changes require their own checks.

The next sealed 215-file snapshot includes those three additions. Native MSVC
Release and GCC 13 Release, both with LTO and required graphics contexts, pass
43 public checks and all 17 compiled comparisons. The one private opt-in test
skips. Both extracted packages also pass the native guest self-test. The source
manifest SHA-256 is
`7a1f6e68b4c942c8802888ef100ce820b2db53c3ebcb69c3277165a1ea457858`.
The format gate first rejects mixed line endings in `src/hw/devices.cpp`;
normalizing that file preserves every decoded text line and all 142 checked
files then pass. Original failure, repaired bytes, hashes and complete logs
remain in `local-artifacts/integrated-native-xfb-20261004`.
Clang 18 static analysis also passes all 42 product translation units with no
project diagnostics. The 256 generated audio coefficients pass their exact
regeneration check.

The same executables run Sunshine for three billion attempts and Galaxy for
one billion attempts with both CPU backends on each platform. All 44 guest
state fields, final PPM bytes and persistent files match between interpreter
and native execution. Native execution actually handles 149,319,791 Sunshine
operations and 81,038,847 Galaxy operations; the bounded cache holds 1,024
entries. These runs use OpenGL, disabled host audio and copied ordinary input.

| Platform | Game | Interpreter seconds | Native seconds | VI fields |
| --- | --- | ---: | ---: | ---: |
| Windows | Sunshine | 145.598339 | 141.971633 | 320 |
| Windows | Galaxy | 68.769293 | 67.836642 | 76 |
| Linux | Sunshine | 139.636891 | 142.837500 | 320 |
| Linux | Galaxy | 72.558600 | 71.245818 | 76 |

Across platforms all 52 reported fields, including backend diagnostics, match
for each game/backend pair, as do frames and final storage. Galaxy's copied
seeds differ only by a SRAM file created when the older long run ended between
copies; that difference and the resulting identical final SRAM are recorded.
The first Linux comparison helper failed before launching a runtime because it
tried to hash a whole image in memory. Streaming hashes resolve that failure;
both attempts remain preserved. Concurrent builds, captures and continuations
share the host, so these timings establish no isolated backend speedup or
full-speed gameplay.

## Sanitizers

On Linux with GCC or Clang:

```sh
cmake --preset sanitizers
cmake --build --preset sanitizers --parallel
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --preset sanitizers
```

AddressSanitizer checks invalid host memory access and leaks. UndefinedBehaviorSanitizer checks invalid operations in the compiled C++ code. These checks supplement the assertions; they do not test console hardware accuracy.

## Private disc integration

The default CTest run marks `local_disc_integration` as skipped and prints the reason. To opt in with a legally obtained local dump:

```sh
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCUPID_ENABLE_LOCAL_INTEGRATION=ON \
  -DCUPID_LOCAL_GAME="/absolute/path/to/game.rvz"
cmake --build build/local --parallel
ctest --test-dir build/local -R local_disc_integration --output-on-failure
```

The integration command is bounded to 10,000 instructions. Unsupported boot paths or instructions fail with diagnostics. Record the revision, console mode, backend, limits and observed stopping point before treating a local result as a compatibility entry. A disc parsing result, boot trace or frame is not evidence of playable gameplay.

Keep private assets and traces containing private data outside tracked files. Public CI runs synthetic fixtures and reports the private test skip. Hardware observations and independently compared execution traces need separate provenance; running a test program here does not establish a real-hardware result.

## Optional behavioral comparisons

Set `CUPID_BEHAVIOR_REFERENCE_ROOT` to the pinned external behavioral checkout to compile `cpu_reference_checks`. This test compares reciprocal and reciprocal-square-root estimates across exponent ranges, table boundaries, signs and deterministic random inputs. It links the external helper directly into the test executable. The product runtime uses its own implementation.

The same option enables these C++23 comparisons and downloads pinned fmt 9.1.0:

- `cpu_fused_reference_checks` compares single-precision fused arithmetic under four host rounding modes.
- `cpu_ni_reference_checks` compares scalar guest arithmetic under all rounding modes with NI enabled and disabled, including tiny results at the normal-value boundary. It uses the actual SIMD rounding and flush controls on x86_64 and reports an architecture skip elsewhere.
- `cpu_memory_reference_checks` compares page-crossing integer, binary64 and quantized loads, RAM stores, BAT updates and cached TLB attributes against the unchanged MMU, MMIO and interpreter implementations. It checks destination and update registers, both floating lanes, faults, DAR, DSISR, page-table words, write transactions and memory-error requests. See [CPU mappings](cpu-reference.md) for its selected attribute combinations and exclusions.
- `physical_memory_reference_checks` compares physical memory aliases, ranges, locked-cache transfers, page-table access and instruction-cache behavior against the unchanged memory, MMU, cache and MMIO implementations. It requires x86_64.
- `cpu_step_reference_checks` compares zero-opcode dispatch, exception state and performance-monitor behavior through the unchanged stepping implementation. Scheduler timing and nonzero dispatch are excluded. It requires x86_64.
- `host_audio_reference_checks` compares the complete unchanged host mixer with independently implemented queues, including exact samples, gain/rate changes, overflow, buffering, initial speaker rates, early callbacks and four-speaker mixing. See [host audio](host-audio.md) for its 17,653 checkpoints and excluded streams.
- `vertex_reference_checks` compares position, normal, lighting and texture-coordinate transforms.
- `rasterizer_reference_checks` compares clipping, coverage and interpolated samples for triangle, line and point commands.
- `texture_reference_checks` compares exact texel components across all texture encodings, palettes, texture units, RAM mip levels and manual texture memory. It also compares nearest and bilinear sampling with clamp, repeat and mirror modes, negative coordinates, fractional boundaries and split-bank RGBA8 fetches. The sampling comparisons use manual texture memory at mip level zero; they do not establish LOD selection accuracy.
- `fragment_reference_checks` compares regular and comparison arithmetic, signed registers, konst selectors, swaps, retained texture inputs, alpha-test decisions, indirect matrices and coordinates, depth textures, and fog. Controlled 4×4 RGBA8 textures make coordinate changes observable while keeping sampling outside the target's application subsystems. Fog cases cover every nonzero mode, both projections and defined range-table interpolation indices.
- `sdp_reference_checks` compares complete HID and identity service records, transaction fields and all captured continuation fragments.
- `efb_copy_reference_checks` compares encoded texture bytes across framebuffer formats, copy formats, scale, dimensions and source positions.
- `xfb_copy_reference_checks` compares filtered YUV output across framebuffer formats, dimensions, scale, gamma, source positions and destination stride.
- `pointer_reference_checks` compares projected infrared coordinates and sizes against the unchanged camera and matrix implementations across cursor positions and sensor-bar placements.

Graphics comparisons link fmt. The product still builds as C++20 and does not link it. Test-only hooks stop with errors if unused application or image-decoding paths are entered.

External graphics and memory comparison sources retain visible compiler warnings on MSVC and use `-O1` on GCC. Project source and the test drivers retain the configured warning-as-error policy and strict floating-point flags.

Unused parameters in the unmodified framebuffer-copy and memory comparison sources remain visible warnings on GCC.

The stepping target's unmodified CPU metadata source also retains its enum conditional warnings on GCC.

The default public run marks all sixteen comparison tests as skipped because the external checkout is not a dependency of the product build. Keep the comparison checkout's revision with the test results. These comparisons cover the named helpers, selected CPU dispatch, memory transactions, graphics stages, pointer projection, service records and mixer paths; they do not compare complete CPU execution or prove hardware accuracy.

## Package checks

`tools/package-verify.cmake` executes the binary extracted from the package, including the synthetic runtime check. This catches missing runtime libraries and packaging paths that ordinary build-tree tests miss. Package checks run in Windows and Linux CI.

## Checked snapshot: 2026-10-04

The development snapshot after `e6c92355e39e` included the Remote speaker decoder and IPC binding, CPU external-control repairs, and graphics-originated memory-fault regressions. Project builds used strict floating-point flags and treated compiler warnings as errors.

| Configuration | Public tests passed | Direct comparisons passed | Declared skips |
| --- | ---: | ---: | ---: |
| Windows MSVC Release | 31 | 10 | 1 |
| Linux GCC Release | 31 | 10 | 1 |
| Linux Clang Release | 31 | 0 | 11 |
| Linux GCC ASan + UBSan, headless | 29 | 0 | 13 |
| Linux Clang ASan + UBSan, headless | 29 | 0 | 13 |

The private disc test was skipped in every configuration. The three configurations without the optional behavioral checkout skipped its ten comparisons; headless builds also skipped the two SDL backend suites.

Clang-format 18 passed, and Clang 18 static analysis checked all 33 product translation units with no project diagnostics. SHA256 hashes for all 113 captured source, header, test and build inputs remained unchanged through the five builds, test runs and source checks.

## Checked snapshot after `6f33d49f6e73`

This snapshot added CPU split-load repairs, the extension cipher and Remote binding, the DSP scheduling fast path, and the direct memory-load comparison. Builds retained strict floating-point flags and treated project warnings as errors.

| Configuration | Public tests passed | Direct comparisons passed | Declared skips |
| --- | ---: | ---: | ---: |
| Windows MSVC Release | 32 | 11 | 1 |
| Linux GCC Release | 32 | 11 | 1 |
| Linux Clang Release | 32 | 0 | 12 |
| Linux GCC ASan + UBSan, headless | 30 | 0 | 14 |
| Linux Clang ASan + UBSan, headless | 30 | 0 | 14 |

Both Windows and GCC passed all 14,336 direct memory-load comparisons. The private disc test remained skipped. Configurations without the behavioral checkout skipped all eleven comparisons; headless builds also skipped the two SDL suites.

Clang-format 18 checked the C++ files and test adapter includes. Clang 18 static analysis checked all 34 product translation units with no project diagnostics. All 118 captured source, header, test and build inputs retained their SHA256 hashes through the final builds, tests and source checks.

A separate GCC/Ninja incremental check changed the embedded revision from its short form to the full revision and back. Each change rebuilt only the CLI entry point and state writer before relinking. The executed binaries reported the requested revision in both forms.

## Checked snapshot after `4ec8b8d68776`

This snapshot corrected cached PTE attributes and added filesystem usage queries, infrared and interleaved reports, and DSP FIR and command handling. The five configurations passed again: Windows and GCC each passed 32 public suites and eleven comparisons; Clang passed 32 public suites; both headless sanitizer builds passed 30 public suites. Declared skips remained 1, 1, 12, 14 and 14 respectively.

Windows and GCC each passed 14,336 memory-load comparisons and 30,848 store, BAT and TLB comparisons, for 45,184 cases. The memory comparison records error requests and RAM before writes; interrupt scheduling remains outside its scope.

Clang-format 18 passed, including test adapter includes. Clang 18 static analysis checked all 34 product translation units with no project diagnostics. All 118 captured source, header, test and build input hashes remained unchanged through the final checks.

## Checked snapshot after `f29f714d99c8`

This snapshot added the host mixer and SDL callback route, handheld authentication, BAT matching repairs, filesystem statistics, and queries through ordinary file descriptors. Builds retained strict floating-point flags and treated project warnings as errors.

| Configuration | Public tests passed | Direct comparisons passed | Declared skips |
| --- | ---: | ---: | ---: |
| Windows MSVC Release | 35 | 12 | 1 |
| Linux GCC Release | 35 | 12 | 1 |
| Linux Clang Release | 35 | 0 | 13 |
| Linux GCC ASan + UBSan, headless | 32 | 0 | 16 |
| Linux Clang ASan + UBSan, headless | 32 | 0 | 16 |

Windows and GCC each passed 651,392 memory comparisons and 17,653 complete-mixer checkpoints. A separate Clang ASan + UBSan build instrumented the mixer driver, authored mixer, unchanged comparison implementation and fmt source; it passed the same 17,653 checkpoints.

Verification caught an incorrect initial speaker rate, which is now 3 kHz. It also reproduced a target queue case in which an immediate burst was dropped entirely after empty callbacks. The direct comparison and pure test retain that case. The Machine fixture now supplies its unchanged 8 kHz stream at 20 samples per 2.5 ms; its three-second deadline and positive output checks remain. It passed twenty consecutive runs on each of MSVC and GCC.

Clang-format 18 passed, including test adapter includes. Clang 18 static analysis checked all 36 product translation units with no project diagnostics. All 128 captured source, header, test and build input hashes remained unchanged through the final checks. The coefficient generator reproduced the compiled window on both Windows and Linux.

## Checked snapshot after `c4b1ed598f6a`

This snapshot corrected CPU memory routing, physical aliases and instruction-cache identity, added device timing boundaries, and connected ROM-launched handheld tasks. The full matrix exposed an EFB byte-load regression: the new CPU callback returned a whole word. The repair narrows device results to the requested width. The original EFB assertions and new callback-width regressions passed on both consoles.

| Configuration | Public tests passed | Direct comparisons passed | Declared skips |
| --- | ---: | ---: | ---: |
| Windows MSVC Release | 35 | 13 | 1 |
| Linux GCC Release | 35 | 13 | 1 |
| Linux Clang Release | 35 | 0 | 14 |
| Linux GCC ASan + UBSan, headless | 32 | 0 | 17 |
| Linux Clang ASan + UBSan, headless | 32 | 0 | 17 |

Windows and GCC each passed the physical-memory comparison: 722 CPU RAM cases, 320 host-range cases, 32 DMA cases, 36 unresolved-memory error cases, padding page-table checks, and 526,736 mixed cache reads across 600,000 operations. The other twelve comparisons passed again.

Clang-format 18 passed, including test adapter includes. Clang 18 static analysis checked all 37 product translation units with no project diagnostics. All 131 captured source, header, test and build input hashes remained unchanged through the corrected checks. Windows and Linux reproduced the audio coefficients. Separate package-verifier fixtures on both hosts accepted matching revision metadata and rejected deliberately mismatched metadata.

## Checked snapshot after `25500396aba6`

This snapshot repaired instruction-cache fills when backing memory is unavailable and added a counter for rejected instruction reads. Windows and GCC each passed 57,856 additional defined fetch and cache comparisons with no divergences or device callbacks. The earlier physical-memory cases and all twelve other direct comparisons passed again.

The five configurations retained their previous counts: Windows and GCC each passed 35 public suites and thirteen comparisons; Clang passed 35 public suites; both headless sanitizer builds passed 32 public suites. Declared skips remained 1, 1, 14, 17 and 17 respectively.

Clang-format 18 passed, including test adapter includes. Clang 18 static analysis checked all 37 product translation units with no project diagnostics. All 131 captured source, header, test and build input hashes remained unchanged through the final checks. These comparisons cover defined instruction-read and cache behavior; they do not prove hardware accuracy.

## Checked snapshot after `bee4094c74bb`

This snapshot added bounds for attempted CPU steps, corrected the default pointer projection, and expanded DSP command handling. Public regressions cover zero-cycle and fault bounds, polling after 4,096 attempted steps, reset accounting, and pointer mapping in all three display modes.

| Configuration | Public tests passed | Direct comparisons passed | Declared skips |
| --- | ---: | ---: | ---: |
| Windows MSVC Release | 36 | 15 | 1 |
| Linux GCC Release | 36 | 15 | 1 |
| Linux Clang Release | 36 | 0 | 16 |
| Linux GCC ASan + UBSan, headless | 33 | 0 | 19 |
| Linux Clang ASan + UBSan, headless | 33 | 0 | 19 |

Windows and GCC each passed 48 zero-dispatch comparisons and 232,098 exact pointer projections, with all thirteen earlier comparisons passing again. The stepping comparison excludes scheduler timing and nonzero dispatch. Pointer comparisons cover coordinates and sizes for the tested cursor positions and sensor-bar placements.

Clang-format 18 passed, including test adapter includes. Clang 18 static analysis checked all 38 product translation units with no project diagnostics. All 136 captured source, header, test and build input hashes remained unchanged through the final checks.

## Checked snapshot after `c140c115aa88`

This snapshot corrected translated instruction-fault timing, added the fetched-opcode trace flag, and connected complete input-replay replacements to SI and USB/HID input. Public tests cover accepted replacements, rejected edits, recovery, retained controls and full CPU-state preservation during follow callbacks.

| Configuration | Public tests passed | Direct comparisons passed | Declared skips |
| --- | ---: | ---: | ---: |
| Windows MSVC Release | 38 | 15 | 1 |
| Linux GCC Release | 38 | 15 | 1 |
| Linux Clang Release | 38 | 0 | 16 |
| Linux GCC ASan + UBSan, headless | 35 | 0 | 19 |
| Linux Clang ASan + UBSan, headless | 35 | 0 | 19 |

Windows and GCC each passed 48 zero-dispatch and 288 translated instruction-fault comparisons. These exclude scheduler timing and nonzero dispatch. The native comparison fixture initially exceeded the default stack: its service objects now use ordinary heap lifetimes, and actual BAT reset callers initialize their lookup arrays. Both original failures remain recorded. The trace regression failed with the previous Machine implementation and passed with the updated implementation using the same current CPU and frozen inputs.

The first sanitizer runs exceeded the guest follower test's inherited 60-second budget. The unchanged binaries completed every assertion in 136.53 seconds on GCC and 109.86 seconds on Clang. Taking full CPU snapshots only at the declared 4,096-attempt poll boundaries reduced separate instrumented runs to 98.97 and 81.45 seconds. The fixture also checks callback cadence. Its 28 guest instructions, device-cycle bounds, three-second update waits, report checks and full-state assertions remain unchanged. With a scoped 180-second budget, the corrected full sanitizer runs passed this test in 140.65 and 112.22 seconds.

Separate authored CLI checks passed on Windows and GCC in both console modes: valid follow input ran exactly 16 CPU steps, and invalid initial input was rejected before boot. Clang-format 18 passed, including test adapter includes. Clang 18 static analysis checked all 39 product translation units with no project diagnostics. All 140 captured source, header, test and build input hashes remained unchanged through the corrected checks.

## Checked snapshot after `5632c6a1df64`

This snapshot adds byte-granular instruction reads and corrects the tested cache and odd-address dispatch behavior. The previous runtime trace fixture failed because it assumed an odd instruction address caused an alignment fault before fetching. The revised fixture checks odd-address fetches separately from a decoded data-alignment fault, an interrupt without a fetch, and a translated instruction fault without a fetch. The original failure is retained.

| Configuration | Public tests passed | Direct comparisons passed | Declared skips |
| --- | ---: | ---: | ---: |
| Windows MSVC Release | 38 | 16 | 1 |
| Linux GCC Release | 38 | 16 | 1 |
| Linux Clang Release | 38 | 0 | 17 |
| Linux GCC ASan + UBSan, headless | 35 | 0 | 20 |
| Linux Clang ASan + UBSan, headless | 35 | 0 | 20 |

Windows and GCC each passed 5,632 additional cases using the unchanged instruction tables and selected integer and branch dispatch. These include rejected cache fills, warm and cold byte reads, strict backing-memory reads, and aligned and odd instruction addresses. GCC also compares the ordered backing-memory calls through forwarding linker wrappers. Windows checks the authored transfer order and all value, CPU-state and cache-state cases; it does not claim interception of the comparison implementation's calls. The new dispatch comparison excludes scheduler timing, device behavior and other instruction families.

Both complete sanitizer runs passed the unchanged guest follower assertions within their 180-second budget: 148.51 seconds on GCC and 121.88 seconds on Clang. Clang-format 18 passed, including test adapter includes. Clang 18 static analysis checked all 39 product translation units with no project diagnostics. All 141 captured source, header, test and build input hashes remained unchanged through the final checks.

## Integrated hardware snapshot after `563d1a8853a3`

The October 4 sealed working snapshot adds OpenGL fragment rendering, CPU EFB pokes, carry corrections, cache acceleration, queued card-DMA completion and replay/DSP deadline paths. Its executables use the explicit label `integrated-runtime-private-20261004`; that label identifies a development build, not a Git commit. The retained receipt records all 203 source, header, test, build and documentation inputs with SHA256 manifest `15474d0e1a8f2cc4f414f902ccc9762aac653e3a927fa96a2fff357b99b84852`.

| Configuration | Public tests passed | Direct comparisons passed | Declared skips |
| --- | ---: | ---: | ---: |
| Windows MSVC Release, RTX 3060 | 40 | 16 | 1 |
| Linux GCC Release, Mesa/Xvfb | 40 | 16 | 1 |
| Linux Clang 18 Release, Mesa/Xvfb | 40 | 0 | 17 |
| Linux GCC ASan + UBSan, headless | 35 | 0 | 22 |
| Linux Clang 18 ASan + UBSan, headless | 35 | 0 | 22 |

All listed configurations use strict floating-point flags and treat project warnings as errors. Both OpenGL suites actually run in every Release row. Each pixel suite checks 1,799 dispatches and 2,789,052 fragments; the guest hardware fixture checks two frames and 768 GPU fragments per console. Missing hardware is configured to fail. The private disc CTest remains skipped; commercial comparisons run separately with private copies of storage.

Clang-format 18, the 256 generated audio coefficients and Clang 18 static analysis of all 40 product translation units pass. Static analysis includes the SDL/OpenGL branches. The sealed inputs retain their hashes through the Clang and GCC sanitizer checks. The guest follower passes in 174.36 seconds on GCC and 118.19 seconds on Clang within its unchanged 180-second budget. Extracted Windows and Linux GCC packages pass help, version and synthetic execution checks.

Separate matched native software/OpenGL commercial runs reach three billion Sunshine and one billion Galaxy step attempts. Every reported state field except the three hardware counters matches, including CPU/RAM, frame and PCM hashes; final PPM bytes and persistent storage bytes match too. See [hardware rendering](hardware-rendering.md) for counters, image scope, source provenance and shared-host timing limits. These runs do not establish gameplay or full speed. Build logs, binary hashes, test reports and source receipts remain in the ignored local artifact directories.
