# Tests

The 224-file CPU fallthrough and portable fog snapshot passes all 65 Linux GCC
Release/LTO tests with zero skips. Formatting and static analysis pass all 42
product translation units. The complete Windows MSVC Release/LTO retry passes
all 64 tests with zero skips in 294.11 seconds. Its corrected configuration uses
a 180-second CPU grouping bound. The first matrix's 63 passes and one timeout
at 60 seconds remain recorded; the unchanged CPU executable separately passes
all 1,210,222 state and ordered-bus comparisons in 80.31 seconds.

The retry reuses 59 byte-identical sealed executables after checking all 478
compile commands, 186 generated/dependency headers and normalized link commands.
Only the CMake bound and this document differ from the original source; no C++
source, header, fixture or compiler flag changes. The original source manifest
is `456efec2b1e1f10dfea0e8d9bb9915aea2de8250329e75fdf52727a310c03fbb`;
the corrected configuration manifest is
`d6fa440a27c40589b984e74587b0e415b32410cca0af6a275c07f2251538c787`.
The fresh complete Clang Debug sanitizer matrix remains pending. Its expanded
252-case runtime fixture passes in 1,230.15 seconds under the unchanged
1,800-second bound. These matrices precede the later ordered GPU batching
integration; complete checks on that combined source remain required.

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

On Linux, `host_audio_shutdown` uses an owned Pulse server and null sink to check
Output teardown while that server is stopped. It verifies worker completion and
callback lifetime before resuming the server for cleanup. The optional fixture
skips when the server executable is absent. See [host audio](host-audio.md#pulse-shutdown)
for the unchanged-SDK failure and staged-SDK pass.

After SDL shutdown, the fixture allows up to 100 milliseconds for Linux to
remove joined threads from `/proc/self/task`, while still requiring exactly
one remaining task. A joined Pulse mainloop thread can briefly retain its
entry; direct pinned-SDL loops reproduce this race. The parent still requires
the worker to close within one second while the server is stopped. A separate
live-thread negative control must fail the task-count predicate, and joined
cleanup must pass. Failures print remaining task status for diagnosis.

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

Hosted CI at `62db09e` passes its Linux Release configurations but rejects the
single 20,261-byte shader literal on its older Windows compiler. Splitting it
into adjacent 9,569-byte and 10,692-byte literals preserves every concatenated
GLSL byte while meeting the documented [compiler limit](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-errors-1/compiler-error-c2026?view=msvc-170).
The same CI finds a Clang sanitizer failure at a generated-code call and a
60-second disc-test timeout. Original logs remain in
`local-artifacts/ci-62db09e-20261004`; fixes need separate verification.

The generated-code failure is reproduced locally under Clang 18: its function
sanitizer probes eight bytes before a compiler-generated function entry, where
the emitted leaf code has no type metadata. Both cold and cached calls now use
one helper that excludes only this intentional call from the function-prefix
check. Address and other undefined-behavior checks remain enabled, and an
ordinary incorrectly typed C++ function-pointer control still fails. The full
2,752,512-case suite passes Debug sanitizers in 24.86 seconds with the original
60-second test budget.

Expanded multi-codec disc tests pass Clang Debug sanitizers in 152.83 seconds
when run directly without a timeout. Their sanitizer-only CTest budget is now
300 seconds; Release and every other test retain their existing budgets. The
direct measurement uses installed compression libraries, while hosted CI builds
the pinned dependencies. The original timeout and complete passing execution
are retained separately.

The next integrated 215-file snapshot has manifest SHA256
`202aa48bb55d0713dbb0b77d5cb8056c7029ef9d4ab7c200133d52d925be1964`.
It includes the sanitizer helper, shader literal split, one-pass disc metadata
collector and disabled-speaker callback guard. MSVC and GCC Release with LTO
each pass 43 public checks and 17 compiled comparisons, with the private opt-in
test skipped; both extracted packages pass both guest execution backends.
GCC and Clang Debug sanitizers each pass all 38 available public checks with
23 explicit skips. Under concurrent builds and continuations, their disc tests
take 287.62 and 296.25 seconds, and native tests take 42.11 and 40.81 seconds.
The complete sanitizer runs take 557.29 and 547.59 seconds respectively.

The first format gate catches helper whitespace; the repair preserves all 317
C++ tokens, and all 142 checked files pass. Clang 18 static analysis passes all
42 product translation units with no project diagnostics. Original frozen
bytes, the format failure, repaired header, complete checks and hashes remain
in `local-artifacts/integrated-portability-collector-20261004`. Later
compatibility and audio documentation updates have separately recorded hashes.

Hosted CI at `734e26e` passes the compiler and sanitizer fixes. Its Windows
build succeeds, but both graphics suites fail with
`OpenGL function unavailable: glCreateShader` on the hosted driver. The original
logs remain in `local-artifacts/ci-734e26e-20261004`. Windows CI now deploys the
[Mesa 26.2.3 software driver](https://github.com/pal1000/mesa-dist-win/releases/tag/26.2.3)
beside its test executables after verifying archive SHA256
`3f3613adb43cfd0f2e665ce2400b130c275f0b3317cb3a05566320a3a67589ed`.
The job requires a graphics context and selects llvmpipe; unavailable functions,
shader errors and pixel differences still fail the tests. This setup needs its
own hosted verification. Both graphics suites pass locally with this exact
archive and the previous integrated executables, reporting llvmpipe with LLVM
23.1.2. The renderer suite takes 29.56 seconds and the guest suite 4.45 seconds;
their full logs and executable hashes remain beside the original CI failures.

The sealed AES/Windows-driver snapshot has 215 files and manifest SHA256
`4be9a5a5753de8f9596a7b3ef70ec93e1cd0561dcd5c3447b918667a08b3fb64`.
MSVC and GCC Release with LTO each pass all 43 public checks and 17 compiled
comparisons, with one private opt-in skip. Both packages pass interpreter and
native guest execution checks. GCC and Clang Debug sanitizers each pass 38
available public checks, with 23 explicit skips; their disc tests take 167.05
and 177.97 seconds. All 142 format checks and Clang 18 static analysis of 42
product translation units pass. Complete logs and frozen/current input hashes
remain in `local-artifacts/integrated-aes-mesa-20261004`; later documentation
updates have separately recorded hashes. Hosted Windows verification is pending.

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

## Event-bounded candidate after `f8dc5ee`

The sealed candidate contains 222 source, header, build, test and documentation
inputs. Its SHA256 manifest is
`475439c5ecef2d20e3a0fb1bfc9e27c4f9a6767e6e212cda03b2c70d978490b4`;
the configured executable label is `emulation-candidate-20261004`. This label is
development metadata, separate from the source receipt. Later working-tree
changes are outside that validation snapshot.

Windows MSVC Release with LTO, strict floating-point behavior and project
warnings as errors passes all 64 registered tests: 46 public suites, 17 direct
comparisons and the enabled private Sunshine boot test. No test is skipped.
The required OpenGL suites execute on the RTX 3060, including both authored
CPU-to-VI guests. The actual-disc test covers its bounded boot assertions,
not gameplay. Logs and exact build commands remain in
`.cache/goal-emulation-20261004/`.

The same sealed source builds with strict GCC 13.3 Release and LTO on Linux.
All 65 registered tests pass: the same 64 tests plus the owned PulseAudio
shutdown fixture. Both required OpenGL suites run through Mesa llvmpipe under
Xvfb; the private Sunshine boot test is enabled. There are no failures or skips.
The full formatting check reports six files in this snapshot, including older
headers; its original log remains recorded. Formatting corrections belong to
the following snapshot. The test pass does not imply that every developer
check passed.

Clang 18 static analysis passes all 42 product translation units, including
SDL/OpenGL branches. Its configured target suppresses 175 diagnostics from
non-project code; the complete output is retained. The frozen source and staged
Linux copy still match all 222 original file hashes after the checks.

The initial Windows baseline exposed two comparison-harness failures. A scalar
subtraction expectation was optimized across an intended host rounding change;
placing the arithmetic behind a non-inlined call restores that boundary.
The unchanged product arithmetic then passes all 2,435,072 results and lanes
under four rounding modes with NI enabled and disabled. The memory comparison
overflowed the original 1 MiB host stack because large fixtures and a nested
BAT-update temporary overlapped. Heap ownership reduces its fixture frame from
927,744 to 368 bytes while preserving constructor/destructor order, all 53
checks and all 62 loops. Its unchanged 782,464 cases pass on Windows and GCC,
with the original stack limit. Neither repair relaxes an expected value.

A Windows Sunshine control compares the stock grouped runtime with repeated
public `Machine::step()` at three billion attempts. All 60 final JSON fields,
PPM bytes and persistent files match exactly. A separately compiled checkpoint
recorder also matches the frozen baseline stock CLI at that bound. These runs
reach 320 VI fields and produce 338,464 guest PCM values, including 62,194
nonzero values. They cover deterministic startup execution with headless output
and host audio disabled; they establish no gameplay or full-speed result.

## RAM runtime after `f8dc5ee`

The following sealed runtime contains 223 inputs with SHA256 manifest
`d3e3b6719bf0fc093e9adec0d184ac221d020bdf766c1cdfbbeb1aaf5eceeebb`.
Its executable label is `ram-runtime-20261004`. Resident scalar RAM groups,
cross-line polling and the 4,096-cycle quiet ceiling are included. The two
renderer files remain the earlier candidate versions; the later empty-coverage
change has separate required-device validation.

Strict GCC 13.3 Release/LTO passes all 65 registered tests in 197.66 seconds,
including the private actual-disc boot, required Mesa rendering and owned
PulseAudio shutdown scopes. Formatting passes, and Clang 18 static analysis
passes all 42 product translation units. Its 175 suppressed diagnostics are
from non-project code. The initial build was interrupted by a WSL restart;
its logs remain separate from the completed persistent-directory rerun.

Windows MSVC strict Release/LTO passes all 64 tests in 207.74 seconds with
the same frozen product. Five comparison fixtures use the recorded portable
adapter overlay. The overlay adds missing unused virtual-function definitions
and retains explicit failure for unsupported calls; expected values and case
loops are unchanged. An earlier direct-termination destructor triggered a
warning under `/WX`; indirect termination preserves the failure behavior and
passes that configuration. Original allocation, build and incomplete-test
failures remain recorded. The corrected Linux portable-adapter run also passes
all 65 tests in 177.42 seconds, with unchanged product commands and core bytes.
The first overlay changed the comparison object's link order and failed one
ordered-copy check; restoring the original object order closes that failure.
Its original log remains preserved.

The first complete Clang 18 Debug ASan/UBSan run executes all 65 scopes with
zero skips: 58 pass and seven fail. Six fail at their original time bounds:
event budget, CPU memory, physical memory, fragment, XFB copy and host-audio
comparisons. The EFB copy comparison reports a misaligned word load in the
separately pinned encoder, whose pixels have a three-byte stride. That original
failure is retained. All seven scopes pass their focused corrected checks,
preserving the original cases. CPU memory takes 905.06 seconds with only
external comparison translation units at `-O1`; product and fixture code
remain Debug `-O0`. Fragment, XFB and host-audio diagnostics pass in 125.18,
547.20 and 127.00 seconds. Their measured allowances are recorded separately
from the original timeouts. No complete corrected sanitizer pass is claimed.

The optional sanitizer encoder comparison generates a private build input
with exactly 18 defined byte-copy word loads. A pinned normalized checksum,
load-site count and byte-identical reverse transform guard the recipe; the
external checkout remains unchanged. Original and defined loads both match
all 267,659,520 output bytes across 180,480 cases under strict GCC/LTO. The
defined-load Clang Debug sanitizer comparison passes the identical scope in
97.67 seconds with the original encoder optimization. No sanitizer category,
comparison case or expected byte is removed. The corrected full matrix uses
the public preparation recipe and measured test bounds, retaining Debug
product/fixtures and all instrumentation.

The new Sunshine stock CLI, ordinary checkpoint recorder and repeated public
stepping agree at three billion attempts on all 60 final state fields, raw
frame bytes and persistent storage. All five shared checkpoints agree too.
The native backend matches the interpreter on the same guest results, with
only the explicitly selected CPU backend and native execution counters
excluded. The new Galaxy interpreter/OpenGL prefix at 1,000,200,000 attempts
also matches its earlier control on all guest state, frame bytes and storage.
Configured build labels are compared separately. These checks cover bounded
startup execution with host audio disabled; they establish no gameplay or
full-speed result. Commands, source selections, binary hashes, original
failures and completed audits remain in `.cache/goal-emulation-20261004/`.

A separately sealed RAM/empty-coverage combination completes Sunshine through
field 10,000 with ordinary left/right movement, release, jumping and landing
in the rendered airstrip level. All 86 shared checkpoints through field 5,160
match the earlier capture on guest state and raw frames, with explicitly
recorded backend and replay metadata differences. [Compatibility](compatibility.md)
records its separate manifest, input windows and remaining playability limits.

## Cache-flush loops and runtime quiet intervals

Fresh strict MSVC CPU and native suites pass the cache-flush extension. The
grouped fixture executes 1,217,464 complete state/RAM/ordered-bus comparisons
and 58,455,150 grouped retirements. Clang 18 `-O1` address/undefined-behavior
checks with assertions pass those same CPU suites. CPU source and object
hashes remain unchanged across those executions; receipts are under
`.cache/cpu-production-20261005/{msvc,clang-san}/verification.json`.

The Windows runtime fixture passes all 634 cases. It retains the earlier 252
cases, adds 298 cache-flush cases with twelve independently warmed hidden DEC
phases, and adds 84 longer varying-address RAM cases at instruction, device,
frame and decrementer boundaries. Comparisons include complete CPU and RAM,
reported state, ordered callbacks with CPU snapshots and sample bytes, MMIO
and trace bytes.

A separate complete 41-translation-unit headless Clang 18 `-O1` sanitizer
build passes runtime grouping in 215.28 seconds and event budgets in 16.73
seconds. This is an optimized private instrumented snapshot, with assertions
and system compression dependencies. It is separate from the public Debug
`-O0` matrix. Later texture edits are outside that frozen snapshot; its CPU,
runtime, peripheral and fixture hashes still match their checked inputs.
The exact source, library and binary hashes remain in
`.cache/cpu-production-20261005/runtime-clang-san/verification.json`.

Eight ordinary stock-CLI software-rendered captures compare the supplied
games at one billion attempts. All reported state fields other than the
configured revision, raw frames and copied persistent files match. Their
startup-only scope and shared-host timing are recorded in
[performance](performance.md). Complete current Release and public Debug
sanitizer matrices have their own source and execution records.

The same sealed core passes stock-versus-checkpoint-recorder OpenGL comparisons
at one billion attempts on both supplied games. All reported state fields,
including GPU counters, raw frame bytes and persistent storage match. The
captures reach 72 Sunshine fields and 76 Galaxy fields on the RTX 3060.
Host playback is disabled, and the scope remains startup. Receipts are retained
in `.cache/continuation-runtime-20261005/hardware-observer-parity.json`.

An intermediate prepared-texture Windows matrix executes all 64 scopes: 63
pass, and the authored hardware guest fails its earlier expectation of at
least twelve physical raster calls. All preceding pixels, frame hashes,
controller redraw and fragment checks pass. A deterministic repeat preserves
the failure. An independently compiled diagnostic against the same sealed
headers and core measures exactly three batches, 768 fragments and zero
fallbacks on each console. The corrected fixture asserts those two counters
separately and retains every pixel/state check. The original complete failure
and the focused diagnostic remain under
`.cache/continuation-integrated-20261005/`.

The runtime fixture now also includes 122 register/compare/conditional-loop
boundary cases, for 756 total. These cover instruction and VI cuts, decrementer
interrupts, DMA, DSP, IOS, replay replacement, host polls, trace bytes and
external interrupts. Strict MSVC validation against the sealed earlier core
passes with every shared CPU/RAM/callback/state observation equal. That result
validates the fixture independently of the new CPU admission path; the final
combined runtime has separate execution receipts.

The sealed 227-file combined source completes all 64 Windows Release/LTO
scopes with zero failures or skips in 654.30 seconds. The runtime fixture
takes 178.32 seconds and the CPU group fixture takes 179.95 seconds, both
within their original 180-second bounds. All sealed executables remain
unchanged. The authored manifest is
`db84f05925ccd4af0f1e0c22b26007de79515f8e3549081e289a9d023b3c8df6`;
the receipt is
`.cache/continuation-integrated-20261005/windows-final-matrix-receipt.json`.

A subsequent test-configuration change gives the two enlarged Release
fixtures 300 seconds and the public Debug sanitizer runtime fixture 5,400
seconds. The latter now runs 756 cases, three times its earlier 252-case
scope. These allowances retain every case, assertion and sanitizer category.
Reconfiguration audits compare compile/link commands, executables, generated
headers and compiled inputs byte for byte. Only CTest metadata changes;
the configuration manifest is
`f4244c67f36a5fa861be327b1ee0d72009bb9039332f8954dc7bb7dcc3e90204`.

The current CPU packet passes 1,755,652 full CPU/RAM/ordered-bus comparisons
and 59,371,574 grouped retirements under strict MSVC and Clang 18 address and
undefined-behavior checks. A separate 42-translation-unit current core passes
all 756 runtime cases and event-budget checks with Clang 18 `-O1`, assertions
and the same sanitizer categories. That private headless build uses system
compression dependencies. Its source and dependency hashes match before and
after execution; it remains separate from the public SDL-enabled Debug
`-O0` matrix. Receipts are in
`.cache/cpu-register-branch-20261005/runtime-clang-san-final/verification.json`.

The earlier 224-file fallthrough/fog/runtime snapshot completes all 65 public
Debug sanitizer scopes with zero failures or skips in 4,844.22 seconds.
Product and authored fixture translation units remain at `-O0`; `-O1`
overrides occur only in separately compiled external comparisons. That
manifest is
`d6fa440a27c40589b984e74587b0e415b32410cca0af6a275c07f2251538c787`.
The full log, JUnit results, scope audit and source manifest remain under
`.cache/goal-emulation-20261004/linux-fallthrough-fog-runtime-bounds-clang-sanitizers/`.
This complete pass belongs to that later source, and does not replace the
earlier 223-file matrix's recorded failures. The 227-file public Debug matrix
had not finished when that receipt was written.

The sealed current GCC core also passes stock-versus-recorder comparisons at
one billion attempts on both supplied games through the physical Linux
D3D12/NVIDIA path. Every reported field, including GPU counters, raw frame
bytes and persisted files matches. The bounds reach 72 Sunshine fields and
76 Galaxy fields. Host playback is disabled, so these startup comparisons
establish no gameplay audio or frame-rate result. Source, dependency, binary
and capture receipts remain in
`.cache/continuation-runtime-20261005/linux-final-hardware-parity/verification.json`.

The sealed current GCC Release/LTO matrix completes all 65 scopes with zero
failures or skips in 530.03 seconds. The 756-case runtime fixture takes 189.43
seconds under the approved 300-second allowance, and the CPU group fixture
takes 90.23 seconds. All 60 sealed executables remain unchanged, and all 43
product translation units retain strict warning, floating-point and LTO
flags. LLVM 18 formatting and static analysis also pass. Receipts and full
logs remain under `.cache/continuation-integrated-20261005/`.

Both required Linux physical D3D12/NVIDIA graphics scopes pass on that same
sealed source. Guest video completes in 16.86 seconds. The unchanged full
renderer executable first reaches its 120-second watchdog, then completes
all checks in about 199 seconds under an explicit 600-second watchdog.
These include 1,129 XFB profiles, 919 rejected staging boundaries and
5,536,481,280 compared RAM bytes. The original timeout and unchanged binary
hash remain recorded in `linux-final-physical-receipt.json`. Three concurrent
gameplay processes share the GPU, so this execution does not isolate the
cause of the longer physical-driver time.

A separate Sunshine comparison checks 118 shared checkpoints through
field 7,080. Every reported value except the configured revision, including
graphics and replay counters, and every raw frame byte matches between the
sealed intermediate Windows core and the final Linux GCC core. Host audio
is disabled. These observations cover those completed checkpoints, before
later replay replacement; they do not compare full RAM or establish gameplay
audio, save/load or speed. Exact binaries, invocations and checkpoint hashes
remain in
`.cache/continuation-runtime-20261005/live-sunshine-cross-platform-prefix.json`.

## 2026-10-22 runtime checkpoint

This checkpoint shares conditional-branch execution between resident groups and
the interpreter, retains unchanged GPU texture ranges, corrects projected
division on Intel Iris Xe, implements controller origin calibration, and accepts
byte-packed Yay0 font masks. Public fixtures cover each behavior. The optional
compiled comparisons retain their assertions and fail-fast unused services while
matching the checked APIs at `2c805fee8f232d090c454381bb1b8422242345e1`.

| Build or check | Observed result |
| --- | --- |
| Windows MSVC strict Release/LTO | All 63 runnable public and differential scopes pass in 251.25 seconds. The disabled private boot scope is recorded as a skip in that matrix. |
| Windows opt-in private boot | The same executable passes the separate 10,000-attempt Sunshine boot scope in 0.07 seconds after enabling its local asset configuration. This is an early boot check. |
| Linux GCC strict Release, pinned dependencies | All 47 public scopes pass, zero skips, in 170.66 seconds. Includes required graphics and the owned Pulse shutdown regression. |
| Compiled behavioral comparisons | All seventeen pass on both platforms. Linux uses installed compression and SDL libraries for these separate comparisons. |
| Clang 18 Debug with explicit `-O1`, ASan/UBSan | CPU groups, devices, fonts and all 756 runtime boundary cases pass, zero skips, in 172.54 seconds. Leak and undefined-behavior checks remain enabled. This is separate from the hosted default Debug configuration. |
| Sanitized OpenGL | Required texture, projected-division and hardware guest-video scopes pass with leak checking on llvmpipe. |
| Source checks | LLVM 18 formatting, static analysis of all 43 product translation units, and generated audio coefficients pass. |

The original Windows matrix's Intel projection failure remains recorded. A
runtime-fed compute probe reproduces the one-ULP error and confirms the corrected
fixed-point coordinate. The repaired full Intel hardware suite passes with no
projection fallback. The first Linux owned-server regression also remains a
failure against the unmodified system SDL library. Rebuilding the default pinned,
patched backend closes that shutdown test; installing a Pulse server alone does
not fix the system library.

Eight stock startup captures run each supplied game on Windows and Linux with
software and OpenGL rendering, one billion attempts per capture. Every reported
non-backend guest-state field, raw frame byte and persisted file matches within
each game. They reach 72 Sunshine fields and 76 Galaxy fields. These captures
precede the Intel projection correction and establish startup equivalence for
that source scope, with host audio disabled. A fresh stock/forwarding-observer
Sunshine pair on the corrected renderer also matches every final JSON value,
frame byte and persisted file, including replay-follow counters.

The current Windows host uses an i7-13700H and Intel Iris Xe OpenGL. Linux OpenGL
uses llvmpipe, not physical GPU acceleration. Commands, source and binary hashes,
original failures, JUnit files and raw captures remain under the ignored
`.cache/continuation-20261022/` directory and matching `/root/wii-20261022-*` logs.
Commercial gameplay, saved level progress, host gameplay audio and full speed
still require their own evidence.

The subsequent FP RAM-group and exact GPU-identity packet passes all 64 native
Windows scopes, including seventeen compiled behavioral comparisons and the
enabled private boot check, with zero skips in 299.48 seconds. All 47 pinned
Linux public scopes pass with zero skips in 189.20 seconds. Clang 18 ASan/UBSan
CPU groups, devices and runtime boundaries pass with leak checks in 233.98
seconds. The exhaustive GPU identity sweep passes on Intel and llvmpipe,
including the sanitizer build; LLVM 18 formatting and all 43 static-analysis
translation units also pass.

Sixteen stock runs compare this packet with the preceding packaged core:
Windows/Linux, software/OpenGL, both games and both source versions. Every
reported JSON value, raw frame byte and persisted file matches within each
pair. Sunshine reaches 320 fields at three billion attempts; Galaxy reaches
76 fields at one billion. Host audio is disabled. The ignored receipts are
`{windows,linux}-fp-identity-stock-pairs/verification.json` under the continuation
cache. These fixed startup bounds establish equivalence, with no gameplay
speed claim.

The preceding hosted Windows run times out in the full hardware test at its
old 120-second limit. A private cold-cache reproduction with the exact pinned
Windows software graphics driver passes every case in 185.519 seconds. The
test now permits 600 seconds of bounded CPU headroom and retains every case.
That local reproduction is separate from hosted CI qualification.

The following XFB staging-span packet passes its public copy regression, full
hardware renderer, hardware guest-video and compiled XFB comparison on native
Windows and Linux. The compiled check matches 43,255,200 bytes over 5,487 defined
cases and rejects 4,513 undefined cases before RAM changes. All four scopes also
pass under Clang 18 ASan/UBSan with leak checking. LLVM 18 formatting and targeted
static analysis of the changed product translation unit pass. A static-analysis
warning about an opaque planner width is closed with an explicit nonzero-row
invariant check; row layout and rejection rules retain their previous behavior.

Eight additional stock software captures compare this packet with the preceding
core across both games and both platforms. Every JSON value, frame byte and
persisted file matches within each pair, at the same 320-field Sunshine and
76-field Galaxy startup bounds. Raw receipts remain in
`{windows,linux}-xfb-spans-stock-pairs/verification.json`. Final-source component
timings, callback/rounding checks and source hashes remain beside the
`xfb-staging-*` logs. The broad matrix above and these narrower checks retain
their separate source scopes.

The scalar-group assembly packet's final source passes the CPU-group suite and
both CLI execution selftests on Windows, Linux and Clang 18 ASan/UBSan with leak
checking. Each full CPU-group run verifies 1,824,604 complete states and ordered
bus observations, retiring 59,419,142 grouped instructions. The new single-line
store/ICBI and missing-continuation controls add 512 comparisons. LLVM 18
formatting and the repository's configured static checks pass for the changed
translation unit.

The earlier applied form also passes runtime-group/event-budget checks on both
platforms, the Windows compiled CPU memory/step/dispatch comparisons, and the
sanitized runtime/event scopes. Eight stock software captures of that form
match every reported value, raw frame byte and persisted file against the
preceding core, at the same 320-field Sunshine and 76-field Galaxy bounds. These
checks precede deletion of an unread unsigned mask update; final CPU-group and
CLI checks follow that cleanup. Receipts and exact source hashes remain under
`scalar-packet-applied/`, `scalar-packet-applied-final/` and
`{windows,linux}-scalar-packet-stock-pairs/` in the continuation cache. They
establish the recorded source slices without a gameplay speed claim.

The subsequent `df978ec` checkpoint passes all seven jobs in its
[hosted matrix](https://github.com/cupidthecat/cupid-wii-gamecube/actions/runs/39874113702):
Windows, three Linux Release configurations, GCC and Clang sanitizers, and
source checks. Its local Windows and Linux packages also pass executable
verification. After the host's NVIDIA driver changes from 597.16 to 597.30,
the unchanged shipping renderer and hardware guest-video suites pass again,
along with all 17,179,869,184 signed division identity cases. The device-change
receipt is `.cache/continuation-20261022/driver-epoch-59730/receipt.json`.

A fresh check of the shared WSLg display also selects Mesa 25.2.8 llvmpipe,
with acceleration reported as unavailable. Single-process D3D12 and Intel
adapter requests retain that software renderer and report no DRI3 support on
the screen. An exposed GPU bridge device alone does not qualify acceleration.
The probes change no shared configuration; their logs remain under
`.cache/continuation-20261022/linux-graphics-probe/`.

The ordinary Remote acceleration correction passes rebuilt `remote`,
`remote_speaker`, `extension_cipher`, `ios`, `input_replay` and
`input_replay_follow_guest` shipping targets on Windows MSVC and pinned Linux
GCC. All six tests pass on each platform. Clang 18 formatting and targeted
static analysis also pass. The 16,384 authored wire cases and sanitizer checks
are described in [input validation](input.md); exact commands, source hashes
and the intentionally failing earlier-packer control remain under
`.cache/continuation-20261022/remote-accel-z-audit/`.

The subsequent FIFO environment packet `b566e7d` passes shipping Windows/Linux
graphics, vertex, rasterizer, runtime and runtime-group scopes, plus full
hardware-renderer and guest-video checks. Clang 18 ASan/UBSan and targeted
format/static checks pass. Eight software comparison pairs across both games,
platforms and CPU backends retain every reported state value, frame byte and
stored file. The authored counterexample and original failing source remain
under `raster-cpu-rounding-audit/`; `fifo-environment-public/shipping-receipt.json`
records the applied source and shipping checks.

The lazy fragment-program cache packet `d8a88fc` passes full shipping renderer,
guest-video and FIFO suites on both platforms and under Clang 18 ASan/UBSan.
A separate cache-only sanitizer run enables leak detection and passes. Eight
stock OpenGL runs across both games/platforms match full state, frames and
storage at 320/76 fields. Exact-head packages pass executable verification and
retain all eleven legal files byte for byte. The first integration attempt's
direct-GL test linker errors remain preserved beside the corrected SDL-loaded
test and passing logs in `indirect-zero-study/proposal/`. These local checks
do not establish full-speed gameplay. The complete hosted matrices at
[`b566e7d`](https://github.com/cupidthecat/cupid-wii-gamecube/actions/runs/39885566703),
[`d8a88fc`](https://github.com/cupidthecat/cupid-wii-gamecube/actions/runs/39886432599)
and documentation checkpoint
[`ae17dd2`](https://github.com/cupidthecat/cupid-wii-gamecube/actions/runs/39887546716)
each pass all seven jobs. These results cover their recorded source heads.

The separately frozen Sunshine FLUDD/save capture uses producer `df978ec`,
USA font slots and NVIDIA 597.30. It ends normally at field 24,000 with 400 raw
checkpoint pairs, 30 accepted replay histories, zero replay rejections, and
142 unchanged driver samples. Controlled spray, M removal, coin collection
and save completion are visible. Only card A changes: 54 container bytes,
including 29 logical save-payload bytes; card structures remain valid.
The immutable 1,060-file manifest SHA256 is
`bad0669944c55d1985bd2094dd2a7a35f72c16aad8075382b420759935adbbcf`.
Artifacts remain under `frame-observer-df978ec/` in the continuation cache;
`sunshine-long-card-structure-audit.json` records the card checks. Shared-host
overlaps, original mistaken controls and observer counter differences remain
recorded. The subsequent current-core route and restart checks below retain
their own scopes; [compatibility](compatibility.md) states the gameplay limits.

The frozen `d8a88fc` plain/passive Sunshine pair completes 24,000 fields in
both runs. All 60 final JSON fields match without exclusions, as do the raw
final frame and complete storage. All 400 passive cached frames match the
earlier route. Its only cross-source final differences are revision, accepted
replay-update count and three GPU diagnostics; guest CPU, RAM, audio and frame
fields match. The passive observer adds no guest-state reads or GPU diagnostic
synchronization. The immutable 596-file manifest SHA256 is
`fe39ddf4514d2ff7a6344040fd34d92ca9c89ea0eccd798270696fd4544d9dea`.
Independent checks rehash every entry and repeat the full-state, frame and
storage comparisons. Receipts remain under
`passive-frame-observer/sunshine24000-20261023T021637796414Z/` in the continuation
cache. Shared CPU/GPU/audio work and raw wall times remain recorded separately;
this is a route-correctness result, without a full-speed claim.

A separate frozen-d8 saved-card restart ends normally at 12,000 fields. Slot
recognition, Airport entry and ordinary movement are visible. Original seed
and final storage match all four files, and both card structures remain valid.
The prior FLUDD/coin progress is not shown restored. Actual WASAPI output
supplies 5,760,000 stereo frames over one bounded 120-second callback window,
without recorder overflow or control errors. Loopback packet continuity and
metadata checks pass. Two independent qualification gates fail: 16 cumulative
product queue drops and no exact complete-span endpoint match. The queue-drop
counter covers the entire run; no window-specific timing or cause is inferred.
The qualification manifest SHA256 is
`65a9e492621e1de1becf384d83a61109ef0333ee7d46b3f32da05a66f04927ab`.
All source, commands, PCM, failed comparisons and card audits remain under
`windows-gameplay-audio/saved-restart-20261023T030024865931Z/` in the continuation
cache. These observations do not qualify clean audio or saved level progress.

The frozen-d8 Windows Galaxy navigation capture observes ordinary movement
after Nunchuk Y=220 at fields 6,420–6,480, a jump after A at 6,570–6,580,
and landing at 6,660. Independent visual review confirms the displacement,
airborne Mario and subsequent landing outside the cinematic. The raw frame
SHA256 values are `9bd0a86db1b78daa64c26ea8555654b930494e3f72075d0e17f774649c8d0508`
at 6,480, `b420e250929f581f325e4b136a35efe1285e879ec52dce02f16627fce1f07e95`
at 6,600 and `971cb9bd9bd8c51ec65354ad9da168e57a60dc8fc88fdf1d6e254d899d2a1091`
at 6,660. Ordinary walking later reaches the road crest at 7,680. Input
histories, raw frames, PNG reviews and milestone hashes remain under
`passive-frame-observer/galaxy-navigation-20261023T061218485138Z/` in the
continuation cache. This longer route uses the previously checked cached-frame
observer, interpreter, OpenGL and disabled host audio; its complete plain-CLI
comparison is subsequently completed below. These individual observations do
not establish level completion, saved gameplay restoration, audio delivery
or full speed.

That Galaxy run subsequently exits normally at its original host limit:
7,203.773225 seconds, 13,056 fields and 156,058,378,433 retired instructions.
It retains 217 raw checkpoint frames, nineteen accepted replay updates, zero
rejections and 238 unchanged driver samples. The Star Bit counter reaches one
and ordinary plaza entry triggers the airship attack cinematic; its later
automatic motion is not counted as player control. Only SRAM changes, with
all Wii save files unchanged. The source profile, local seed, observer, game
and fonts remain unchanged. The immutable 462-file manifest has SHA256
`322c30efaac79168faac7ea5b26852b2876f9a2ad487f24ba6f11d8eab713527`;
independent verification rehashes every entry. The final image remains in
the cinematic, without a completed objective, level save or continuation
prompt. The recorded terminal timestamp is a process-polling observation.

The separate frozen-d8 native Sunshine continuation ends normally at exactly
32,000 fields, retaining 533 raw checkpoint frames. Its first 400 raw frames
match the current interpreter route through 24,000; no complete CPU-state
comparison is made at that intermediate endpoint. Ordinary inputs close the
save prompt, navigate to the central goop and trigger the raised creature's
appearance. It remains active at termination, with no first Shine observed.
The final card is byte-identical to the previously validated current-route save.
All producer inputs remain unchanged. Nine replay updates are accepted;
one invalid digital-R value is rejected without application and remains in
the history. The audit distinguishes historical LF-text hashes from actual
CRLF file-byte hashes without rewriting old receipts. The immutable 595-file
manifest has SHA256
`41cd7f4900768dfa5222695f7fdc10ad5e0df850c8dc63675f3b12f049f42768`.
All entries are independently rehashed; the capture remains under
`sunshine-native-progression-20261023T061605774402Z/` in the continuation cache.
Its shared-host wall time does not establish speed, and host audio is disabled.

The private LR/CTR native proposal passes strict MSVC, GCC and Clang 18
ASan/UBSan correctness checks, including the full 131,072-encoding classifier
matrix, 10,240 transfer comparisons, complete CPU state, ordered bus/RAM,
group quotas, cache saturation, partial prefixes and event boundaries.
An untimed four-binary harness gate retires 45,864 instructions with exact
state, ordered bus and full 16 MiB RAM comparisons. Sixteen startup parity
children also pass: interpreter JSON matches without exclusions, while native
comparisons permit only fifteen individually named execution diagnostics.
Guest state, raw frames and persisted files match within every required pair.

The later fixed experiment retains 384 component pairs and 32 game pairs,
or 832 unique measured children. Every child returns zero without timeout;
all required semantic comparisons pass. The first driver's interpreter-cache
assertion failure, corrected offline recovery and separate continuation remain
distinct. Independent verification rehashes all 4,034 sealed files and
832 receipts, then recomputes every component and startup paired median/range.
The assessment SHA256 is
`407aa4a3fd9c0542332a35df6614ec368d92ab7cc6f0bcdd6dd9f87e936d00c7`;
the final seal SHA256 is
`0b5e135a1b81ce7cb578cd9aa64ad9914e5cd44c5224cf0e9abfc6eed13b2031`.
All raw costs and unfavorable controls remain in the continuation cache under
`native-lrctr-timing/continuation-v3/`. The candidate stays private because
the [performance results](performance.md#rejected-lrctr-native-extension)
show material control regressions. These passing checks do not qualify
commercial gameplay performance or full CPU accuracy.

The exact-endpoint plain Galaxy comparison subsequently completes normally
at 156,059,049,984 attempts, 156,058,378,433 retired instructions and 13,056
fields. All sixty JSON keys are compared against the frozen navigation run;
only outcome (`time-limit` versus `instruction-limit`) and accepted replay
updates (nineteen versus one) differ. All other keys, the raw frame and the
complete stored-file tree match. The 43-file immutable manifest SHA256 is
`ae4aa1059649cb5e7bc12109cecd0c37cf1a88ab66213b84b6c7f380c230c4ac`.
Independent review rehashes every entry and repeats the whole comparison.
The original handle returns zero; no restart or extension occurs. Its
6,602.176527-second shared-host wall time is not a source-causal speed result.
Evidence remains under
`passive-frame-observer/galaxy-full-route-plan-20261023T082042962921Z/`
in the continuation cache.

A later queue-rejection diagnostic completes one further 12,000-field
Sunshine saved-card restart with zero product drops, zero observed rejection
events and zero counts for all six sources. All trace lifetime and callback
recorder gates pass; one bounded callback contains 5,758,976 stereo frames.
Independent review rehashes the complete 383-file raw manifest and recomputes
all 11,517,952 sample values and 5,624 packet bounds. This run has no endpoint
waveform and does not reproduce the earlier sixteen drops. The earlier audio
qualification remains failed; [host audio](host-audio.md#later-queue-rejection-diagnostic)
records the exact scope and retained external metadata changes.

A private ordered-tile host packer passes strict MSVC, GCC and Clang 18
ASan/UBSan checks. Authored independent oracles check every payload word,
ordered record index, invalid-input transaction and fixed-capacity bound.
A constructive maximum reaches 2,176 unique tiles and references, requiring
24,967 words. These checks exercise only host packing, without GPU execution
or a speed claim.

A separate neutral plain/capture pair then completes naturally at 2,040
fields on NVIDIA 597.30. Complete final JSON matches with zero exclusions,
as do raw frames and every stored file, including the immutable historical
plain case. The capture contains one complete natural synchronization
interval: 171 consecutive draws, including the fixed 64-record target and
all prefix/suffix draws. Independent parsing verifies all 10,396 events,
231 blobs, original scalar/batch membership, all 254 payload words, keys,
capacities and the exact endpoint. Every byte of shader input buffers 1, 4
and 6 is known at each of the forty fragment dispatches. Initial write-only
pixel latches remain explicitly unknown; only captured meaningful fields
have actual selected-value oracles.

The packet SHA256 is
`c1757a13e641f6e9f17726b0f038ec540b94f6fd890b546bc0795b0038cb1eb4`.
Source stability checks rehash 23 private source files, 323 external pins
and twenty build outputs. The protected evidence/build root occupies about
93.2 MiB. Its 512 MiB logical envelope excludes separately measured guest
scratch and is not an atomic filesystem quota. Tracked requested logger
memory is not an RSS guarantee. Both original children return zero without
a bound firing, retry or substitution. The packet and independent audits
remain under `tile-list-feasibility/` and `tile-capture-independent-review/`
in the continuation cache. Capture validity alone does not establish GPU
correctness or performance; the later stock replay is recorded below.

A later frozen-d8 Sunshine interpreter continuation ends abnormally at field
44,158, after 350,648,336,438 retired instructions and 350,648,712,764 attempts.
The actual child returns 4 with a renderer depth-origin range error at
`0x802edf08`. The JSON outcome is `unsupported-instruction`, but the diagnostic
originates in rasterization; it does not demonstrate missing CPU opcode support.
All nineteen replay versions are accepted without rejection. The capture retains
735 committed checkpoints and the final state/frame; all 533 images through
31,980 match the earlier native route. The final card remains byte-identical to
the earlier save, with no first Shine or new saved-level progress established.
Independent review rehashes all 948 sealed files. Its manifest SHA256 is
`61dcbccee4588eb3961f4f646caf3ea98e4882091bad584e58afeb41b48be9bb`.
The terminal time is a polling observation. No restart or extension occurs.
Evidence remains under
`passive-frame-observer/sunshine-first-shine-plan-20261023T083849061078Z/`
in the continuation cache. Actual failing vertex/projection values are absent,
so source arithmetic studies remain separate from a reproduced game fix.

A private retired-instruction census passes eighteen C++ gates and six CPU
layout checks across strict MSVC, GCC and Clang 18 ASan/UBSan builds. Actual
compiler/link commands and coherent source/header/library bindings are checked;
external static dependencies remain uninstrumented. Collector lifetime,
overflow, reset/thread misuse, retirement and exception controls pass alongside
the existing integer-run, native-integer, runtime-group and event-budget suites.
One valid and eight negative binary-schema fixtures and a separate export
verification also pass. All 53 command children have actual terminal and owned
process-cleanup receipts. Earlier compiler, fixture and insufficient-deadline
failures remain preserved. The successful gate receipt SHA256 is
`933aa369d0c0b5e42ece7757c8d34192d8222f87c967c5b419fe42eed0745cfd`.

Sixteen subsequent serial startup children compare stock and census builds
on Windows and GCC, using both CPU backends, software graphics and no host
audio. All eight pairs match complete JSON with zero exclusions, raw frames
and the full stored-file tree. Sunshine reaches 320 fields and Galaxy 76.
Every table sum equals its retired-instruction delta, including apploader
execution from the initial zero counter. Full path tables match across
platforms; path-summed raw bins match across backends. Independent review
rehashes all 387 sealed files and recomputes these comparisons and counts.
The parity receipt SHA256 is
`c5e08d073198ec2ce4f695e01646c3602ffe7d2e139e3a504e27c471dac144f4`.
Evidence remains under `retired-instruction-census/runner-v5/` in the continuation
cache. These are bootstrap/startup frequencies, not a host-time profile,
full-route neutrality result or gameplay FPS measurement. No census-based CPU
admission change is integrated.

A standalone stock-shader replay subsequently passes on Intel Iris Xe,
Linux llvmpipe and NVIDIA RTX 2000 Ada with driver 597.30. Each context executes
the complete 171-draw interval twice, with distinct deterministic fills for
unknown initial latch words. Full color, depth, all eleven counters, XFB bytes
and captured meaningful selected fields match their original oracles. All
three vendors also produce identical complete fixed-fill raw outputs and the
same explicit latch-validity mask. Independent decoding checks every latch
word: 1,167,360 written words agree between fills, while 1,536,000 untouched
words retain their respective fills. These fills do not recover unknown
historical input values.

Each vendor passes eighteen authored positive cases and rejects three deliberate
GPU mutations covering blend order, copy/clear order and retained texture state.
Separate Windows/Linux host-only gates each pass fourteen controls and eighteen
authored preflights without initializing SDL video or creating a context.
Actual strict Release/LTO commands, standalone SDL/system-runtime linkage,
all 98 build artifacts and source/dependency pins are independently checked.
All baseline replay children and watchers terminate normally without a bound
firing or a retry. The original compiler-warning and incomplete filesystem
observation attempts remain preserved. Root review rehashes all 33 vendor
output files and independently repeats the raw comparisons. Evidence remains
under `stock16-interval-replay-v3/` in the continuation cache. No candidate
tile shader, timer query, gameplay speed or physical Linux GPU qualification
is established by this baseline.

A separate CPU-only depth-anchor arithmetic study passes six positive checks
across strict MSVC, GCC and Clang 18 ASan/UBSan builds. Three stock controls
retain their exact depth-origin rejection and exit code 1; they are negative
controls, not passing stock tests. The proposal preserves the original valid
conversion branch and widens only the integral intermediate outside that range,
checking signed pixel-origin subtraction across the full EFB. Authored cases
exercise finite culled vertices, retained nonzero z-freeze gradients, boundary
origins and unchanged coverage/nonfinite rejections. The complete existing
rasterizer suite also passes for each proposal build. Independent review
rehashes 229 sealed outputs and 142 build-tree artifacts and checks all 27
terminal child/cleanup receipts. Evidence remains under
`depth-origin-wide-study/component-runner-v2/` in the continuation cache.
That initial study predates the actual Sunshine numeric record and establishes
only its authored component scope.

A later passive diagnostic replay reproduces the exact failure after
350,648,712,764 attempts. All 735 checkpoint frames, the final frame and four
stored files match the original byte for byte. All 60 final JSON keys are
compared; the sole difference is the replay-update count, 19 versus 1, from
using a collapsed static replay. The child returns 4 and its finite owner
records actual wait and process cleanup. The new 761-file manifest has SHA256
`1d2089c3ae16727f53f25d471efa6349cfbefedb0eef296bbb0db401f627c385`.
The raw numeric error record remains private under
`depth-diagnostic-route-v2-execution/reproduction/` in the continuation cache.

The finalized depth-origin patch preserves the original signed 32-bit branch
and widens only the integral scaled intermediate. It rejects final anchors
that would overflow signed EFB pixel subtraction. A separate component attempt
builds six targets per profile with strict MSVC Release/LTO, GCC 13 Release
and Clang 18 ASan/UBSan, including leak checks. Twelve positive tests and six
expected negative controls pass across 36 guarded command children. The
captured triangle rejects at the exact old guard on stock and retains the
expected finite nonconstant plane after the patch. Later public draw calls
verify its frozen plane and CPU fragment depths. The exact public rasterizer
implementation and full test suite pass in all three profiles; the public
additions use authored analytic inputs and boundary values.

Root review rehashes all 547 component files and verifies actual project
compiler flags, six link maps per profile, expected exits and owned cleanup.
The component handoff SHA256 is
`07045f2591987b0b29a00c8215d076aae72557eef1d9656f362c362c46454e10`.
Evidence remains under `depth-origin-wide-study/captured-component-v3/` and
`captured-component-v3-result/` in the continuation cache. These checks qualify
the recorded triangle and synthetic rasterizer behavior; a new game replay
must still pass the original failure point.

The corrected producer also passes a separate Windows interpreter/OpenGL
startup pair with host audio disabled. Plain and passive builds each reach
one billion attempts and 72 VI fields. All 60 final JSON fields, raw frame
bytes and four stored files match without exclusions; passive field 60 also
matches the historical frame. Root startup acceptance SHA256 is
`3f3abcd66e61cd70f0bc6dac49c4d33b04542e2953bb247d6bd2cd8a3d7bae70`.
This short pair does not pass the old 44,158-field failure or establish longer
gameplay, audio, save restoration or speed.

## 2026-10-23 graphics regression qualification

The full hardware-renderer suite now includes sixteen authored wide-origin
draws and twenty-four analytic unequal-division projection/fog draws. They
check complete color and depth buffers across scalar and batched versions of
the generic and zero raster programs. The wide-origin checks retain a plane
through an actual culled CPU draw and then submit visible z-freeze work.

The complete default suite passes on Windows NVIDIA with strict MSVC Release
and on WSLg llvmpipe with GCC 13 Release and Clang 18 ASan/UBSan, including
leak detection. Each run completes all 52 stage lines, sixteen first-use rows
and the existing division and compiler-failure checks. The tested public test
source has SHA256
`226a8bf801ec2d803a7891deb4300f8f42bb04db248e55da8246eb2b372b3d64`.
Independent acceptance is recorded as
`root-current-depth-three-profile-hardware-acceptance.json` in the continuation
cache, with SHA256
`6ab99faad2a176c7ce676d0a59b6e3f1edd9553a2d92bbe1723bf92669395fd0`.

The first Linux launch, which forced the SDL offscreen backend and software
rendering, crashed during backend initialization. That failed sequence remains
preserved, including its completed Windows suite. A separate single GDB run
localizes the observed SIGSEGV to Mesa's screen initialization, reached through
EGL and SDL offscreen window creation. The later renderer-string query has not
been reached; the internal driver failure remains unexplained. The later Linux
suites use the previously successful inherited WSLg environment. Their success
qualifies that separate recipe. These tests do not establish game progress or
physical Linux GPU support.

## 2026-10-23 graphics performance studies

Two private candidates remain outside the product. Each fixed study retains
158 complete captured intervals per device, two fresh contexts, twelve paired
comparison blocks and six same-stock control blocks. The devices are Intel
Iris Xe, Mesa 25.2.8 llvmpipe and NVIDIA RTX 2000 Ada on driver 597.30. Every
interval retains the original cap of sixteen primitives, all 41 dispatches and
barriers, complete raw-output checks and the repeated CPU serializer lifecycle.
Host intervals include replay validation and readbacks; they are not product
CPU costs or game frame rates. Other owned heavy work is held during each
window, but foreign cleanup and OS activity are not verified idle.

The tiled candidate's median paired candidate/stock host ratios are 0.973863,
1.065017 and 0.995369 in that device order. GPU totals are 0.987457, 1.062060
and 1.002226. Mesa regresses, NVIDIA has no useful gain, and Intel's host change
resembles its same-stock control. The candidate is not promoted. All raw trials,
controls and costs remain under `stock16-tiled-performance-measurement-review-v2/`
in the continuation cache. Independent acceptance has SHA256
`938edd20ceb95e5a0afefe5f2efe958089b4d99a9ae15a1497ec79cba442557a`.

The float-estimate candidate changes only the initial quotient estimate and
retains exact correction and fallback. Helper and full-fragment correctness
checks pass on all three devices before timing. Its paired host ratios are
0.984330, 0.999737 and 0.997344; GPU totals are 0.994560, 0.986274 and 0.996663.
On NVIDIA, scalar dispatches regress in every comparison block, with ratios
1.214046 through 1.254932 and median 1.227085. Batch work improves to 0.880433,
largely offsetting that regression in the total. This does not justify a general
change or a device-specific batch policy. Independent review checks all 474
real rows, 414 authored results, 9,348 query values, twelve metrics and their
controls, ranges and tails. The earlier ready-schema rejection ran no child or
context; its failed preflight remains preserved separately. Evidence remains
under `float-estimate-performance-measurement-review-v2/`; acceptance SHA256 is
`a58d40f07e6b7b0baee3b8e2cdf22f980760485673e911eeca503ae8b41ed2b4`.

For depth-fix commit `9245effc3c3e`, all seven jobs pass in the
[pull-request matrix](https://github.com/cupidthecat/cupid-wii-gamecube/actions/runs/39996463067).
The [push matrix](https://github.com/cupidthecat/cupid-wii-gamecube/actions/runs/39996454100)
retains a Windows renderer timeout at 600.05 seconds; its other six jobs pass.
The passing Windows renderer takes 370.99 seconds. The pull-request merge and
head have the same committed source tree, but binary identity and the timeout's
cause are not established. Renderer progress markers and retention of the two
public CTest logs now preserve evidence for subsequent runs without removing
cases or increasing the deadline.

For test commit `19049adb120e`, all seven jobs pass in the
[push matrix](https://github.com/cupidthecat/cupid-wii-gamecube/actions/runs/40011427236).
The [pull-request matrix](https://github.com/cupidthecat/cupid-wii-gamecube/actions/runs/40011435144)
passes six jobs and retains a Windows renderer timeout at 600.04 seconds.
Both sanitizer jobs pass in each matrix. The passing Windows renderer takes
384.13 seconds. The failed output is an exact prefix of the passing output,
including all sixteen first-use rows and the completed wide-origin checks.
Its last unfinished stage is `xfb_clear_checks`, but the existing markers do
not record elapsed time. The buffered CI timestamps cannot establish which
stage consumed the cumulative deadline. The committed trees match; checkout
and generated SDL revision metadata differ, and binary equality is unproven.

Test commit `5781117681a0` adds `[renderer-time]` lines for integral milliseconds
from main entry and around each existing stage, including fixture construction
and backend setup. That diagnostic change preserves the original marker lines,
cases, order and 600-second deadline. Timing calls stay outside check functions
and their floating environment guards. `stage_ms` includes the begin timing
log, the function and its local destructors, and the original returned marker; it excludes the final
timing log. These diagnostics can perturb scheduling and do not explain a
previous timeout. An unfinished stage still lacks a completed duration.

The updated test source, SHA256
`85a4d744dff831ef3a7c405b626ce219c11603f8ee72d15ed32faa3129ee0a6a`,
passes one complete default suite per profile against the accepted core
archives: strict MSVC Release/LTO on Windows NVIDIA, GCC 13 Release on inherited
WSLg llvmpipe, and Clang 18 ASan/UBSan with leak detection on that WSLg recipe.
Each run preserves all 52 original stage markers and sixteen first-use rows,
and validates 52 paired, monotonic integer timing lines. All owned processes
terminate with complete pipe drains and cleanup. Root acceptance SHA256 is
`0fcb22092b3b414ae1a1ba94ca5a6ab3b789398d45b428df62c5052b73c48fb6`.
These runs share the host with the fixed Sunshine replay and qualify the test
change's correctness, without isolating timing or explaining the hosted timeout.

For test commit `5781117681a0`, the Windows renderer passes in 379.55 seconds in
the [push job](https://github.com/cupidthecat/cupid-wii-gamecube/actions/runs/40027321832/job/120666650787).
The [pull-request job](https://github.com/cupidthecat/cupid-wii-gamecube/actions/runs/40027328389/job/120666675046)
reaches the cumulative 600.05-second timeout. Its XFB clear check returns at
581,176 milliseconds after taking 5,041 milliseconds; the division-fragment
check then starts. The first-use environment check completes in both jobs,
taking 232,997 and 388,214 milliseconds respectively. All sixteen environment
rows complete. These timings locate the cost in these attempts; they do not
establish why the older job timed out or prove that a particular check hung.
All seven push jobs pass. The pull-request matrix passes six jobs and retains
this Windows timeout; both GCC and Clang sanitizer jobs pass in each matrix.

CTest runs the four program-cache checks through the existing
`--program-cache-only` route in `hardware_renderer_program_cache`. The
`hardware_renderer` entry uses `--skip-program-cache-checks` for the remaining
checks. Both entries require a device when configured to do so, retain a
600-second limit and preserve every assertion. Their combined maximum budget
is 1,200 seconds, with separate process and backend startup. The compiler checks
retain all 128 draws, 64 compiler failures and sixteen environment rows. Running
the executable without selectors still executes the complete combined suite.
Use `ctest -R hardware_renderer` to select both entries; an exact
`^hardware_renderer$` filter selects only the remaining checks. Fresh CMake
registrations pass with required hardware, optional hardware and headless
sanitizer configurations. Four local invocations pass: Windows runs the two
partitions separately; GCC and Clang ASan/UBSan run the combined suite. Hosted
validation of the changed registrations is pending.

The renderer reuses a state-buffer pixel read within one synchronization when
several latch slots select that pixel. Different sequence prefixes can select
the same pixel, so the lookup uses the decoded pixel address. The local cache
holds at most six entries and ends with the synchronization call. It preserves
the existing buffer binding and transfer for the first read of each pixel.

A regression checks coincident and distinct pixels, sequence prefixes, changed
palettes, clean synchronization, null-recipient draining and later recovery.
Both scalar and batch draws validate complete shader state, EFB color and
depth, counters and untouched sentinels. In the fixed traced fixture, each of
three compiler profiles reads 20 pixels instead of 60 with identical results.
The four full-suite runs use untraced private binaries and empty stderr. Root
acceptance SHA256 is
`abd11179fbd9062d1803fff9be5bf57faed63c072ab78868643c695ba8c43df5`.
Two Windows interpreter/OpenGL startup runs use the candidate renderer with
the accepted main objects and remaining core. Each reaches one billion
attempts and 72 VI fields. All 60 JSON fields and actual JSON/stdout bytes,
raw final frames and four stored files match the accepted baseline and each
other. The passive field-60 frame also matches. Root acceptance SHA256 is
`a299dd2cc765eae52a2b373496b53fc82c72cde249e1ffc162cef0b7976da04d`.
These links retain the accepted main objects' older commit metadata.

The public source adds formatting changes to those tested private inputs;
its exact compiled bytes still require hosted validation. These startup and
fixture checks do not establish a gameplay speedup or qualify the long route.

## 2026-10-23 scalar RAM admission study

A private candidate reuses the first scalar RAM admission only when the first
memory instruction recomputes the same effective address. Address changes,
later memory operations and branch checks retain their original guards. Thirty
semantic gates, six layout checks and eight startup parity pairs pass across
strict MSVC, GCC and Clang ASan/UBSan builds. The timing study uses a frozen
runtime snapshot with software graphics and no host audio.

The fixed component window contains 768 children and 384 pairs across twelve
guest loops, both consoles, both execution backends and MSVC/GCC. Separate
untimed full-state/RAM oracles pass before measurement. Every raw trial,
same-stock bracket, cold/setup/prime/warm cost, hot-loop cost and guardian cost
is retained. Windows same-address cases improve in this fixture, but changed
addresses and GCC cases include regressions. For example, the two Windows
GameCube interpreter changed-valid-address hot ratios are 1.0758 and 1.0769;
the GCC pure-integer equivalents are 1.0979 and 1.1073. Inclusive costs and
controls remain available rather than selecting only favorable hot loops.

The separately fixed startup window completes all 64 children and 32 pairs.
Sunshine uses three billion attempts and reaches 320 VI fields; Galaxy uses
one billion attempts and reaches 76. All complete JSON bytes and values, raw
frames and stored files match their frozen baselines without exclusions.
Both comparison orders and both same-stock brackets remain in every setting.
Ratios below one indicate less time. The following values use guardian costs,
including child launch, execution, waiting, serialization and storage flush:

| Compiler | Game | Backend | Candidate/stock median | Two candidate/stock ratios, range | Two same-stock ratios, range |
| --- | --- | --- | ---: | --- | --- |
| Windows | Sunshine | native | 0.984924 | 0.956484-1.013363 | 1.001917-1.005378 |
| Windows | Sunshine | interpreter | 0.990803 | 0.982358-0.999247 | 0.978654-0.996610 |
| Windows | Galaxy | native | 0.989300 | 0.985473-0.993128 | 0.987518-1.106057 |
| Windows | Galaxy | interpreter | 0.995942 | 0.995266-0.996619 | 0.954658-1.006206 |
| GCC | Sunshine | native | 0.999647 | 0.994739-1.004556 | 0.948615-1.002853 |
| GCC | Sunshine | interpreter | 1.073193 | 0.991565-1.154821 | 1.080008-1.110369 |
| GCC | Galaxy | native | 1.029882 | 1.017948-1.041816 | 0.957333-1.009539 |
| GCC | Galaxy | interpreter | 1.025346 | 1.020790-1.029903 | 0.975138-1.016367 |

Fresh seed-copy costs are also retained separately and added to guardian costs.
The combined medians preserve these results, including both GCC Galaxy
regressions. The large GCC Sunshine interpreter ratio remains in the data.
All four Windows comparison ranges overlap their observed control ranges.
Two comparison pairs per setting provide descriptive evidence only; no
confidence estimate or isolated-host claim is made. Other owned heavy work is
held during measurement, but foreign cleanup and OS activity remain unknown.
Windows logical CPU 0 and Linux vCPU 0 are recorded separately; their physical
mapping is unproven. Startup costs do not establish gameplay frame rates.

The candidate remains private. Root and independent peer reviews recompute all
64 raw results, 32 pair records, eight settings and three startup cost metrics.
The startup acceptance SHA256 is
`c9950126b6648fa19364e8d8d809d17e8fc0ab4b23eb5f29dc65ef59379abc5c`.
Evidence remains under `scalar-reuse-measurement-v3-components-handoff-v1/`
and `scalar-reuse-measurement-v3-games-handoff-v1/` in the continuation cache.
No adaptive rerun or product admission change follows these mixed results.

## 2026-10-23 DAC clock and host floating controls

Disabled main audio DMA now delivers sixteen zero samples on each eight-frame
DAC tick. It performs no RAM read and preserves the DMA descriptor, block
count and completion status. Public checks cover both consoles and rates,
exact boundaries, split advances, stop/restart, reload length changes and
absent callbacks. The stock implementation fails on the first missing silent
packet. Output sample counts and hashes intentionally include these zeros;
older captures retain their original PCM diagnostic scope.

On x86-64, incoming host FTZ/DAZ previously changed guest subnormal results.
The CPU scope now selects flushing from FPSCR.NI, preserves subnormal inputs,
masks host floating traps and restores the complete incoming MXCSR. Its
3,184 public cases cover both consoles, all host/guest rounding modes,
signed scalar and paired operations, NI, unmasked host traps, quantized
stores, guest program-fault delivery and rejected operations. Separate stock
negative controls expose the changed result and leaked host status.

Strict MSVC Release/LTO and GCC Release each pass thirteen selected CPU,
device, runtime, guest-video and reference checks, including 2,435,072 NI
arithmetic results and destination lanes. The rebuilt host-audio fixture and
compiled mixer comparison also pass on both profiles; GCC additionally passes
the owned Pulse shutdown check. These are fifteen distinct checks on Windows
and sixteen on GCC, assembled from the selected run and the corrected audio
fixture run. The fixture initially assumed no disabled-DMA samples, then used
the wrong reset rate; its final independent calendar checks all zero samples
and their hash at the Wii reset rate of 32 kHz, including output replacement.

Clang 18 ASan/UBSan with leak detection passes those thirteen checks and the
two rebuilt audio checks using system SDL. Its separately selected shutdown
check fails because that recipe does not include the pinned SDL shutdown
patch. The original failed assertion and earlier missing-target invocations
remain preserved. Fresh validation with pinned dependencies is pending;
none of these checks establishes clean commercial endpoint playback or speed.
Logs remain under `disabled-dac-integrated-20261023/` in the continuation cache.

The original fixed-depth Sunshine replay is stopped. Its 827 raw checkpoints
reach field 49,620, beyond the earlier field-44,158 renderer failure. All 735
historical frames through field 44,100 match byte-for-byte. The frozen
`9245eff` binary, source freeze and build receipt retain their recorded hashes.
The owner and child process IDs are absent; final state, exit status and
storage flush are missing. Its planned 50,000-field endpoint remains incomplete.
This evidence predates the latch readback and audio changes, and proves only
the image prefix and later checkpoint progress. Review remains under
`depth-route-partial-audit-20261023/` in the continuation cache.

The rebuilt `169cce8` Windows interpreter/OpenGL CLI also completes fresh
one-billion-attempt startup comparisons for both supplied games. Sunshine
reaches 72 VI fields and Galaxy reaches 76. Every one of the sixty JSON fields
is compared with a frozen `9245eff` run using the same fonts, replay and copied
storage seed. Fifty-seven fields match; the differences are revision metadata,
main sample count and main sample hash. Sunshine's count changes from 73,296
to 202,656 values; Galaxy's changes from 85,376 to 91,424. Nonzero counts and
peaks match. Final raw frames and every persisted file match byte-for-byte.
These checks do not reconcile complete PCM waveforms or qualify host playback,
gameplay or speed. Commands, source/binary/input hashes and all differences
remain under `runtime-controls-startup-20261023/` in the continuation cache.

Both seven-job CI matrices pass at `675ec57`. At `169cce8`, hosted Linux tests
expose a remaining streamed-audio fixture assumption that disabled main DMA
produces no samples. Its two 168-frame stream periods also span exactly 42
eight-frame main DAC ticks at the authored rate. The corrected assertion
checks all 672 zero values, zero peak/nonzero count and their literal FNV64
hash. The focused fixture passes MSVC Release/LTO, GCC Release and Clang
ASan/UBSan with leak detection. The original hosted failure remains recorded;
new-head CI must verify the corrected complete suite.

## Queued DAC timing and bulk EFB reads

The main DAC retains queued ticks across rate writes and keeps separate
200-cycle DMA-start interrupts. Thirty-six authored calendar cases cover both
consoles, split/bulk advances, initial and subsequent rates, stop/restart,
reset, enabled data and rate writes during sample callbacks. Strict GCC and
Clang 18 ASan/UBSan each match the extracted scheduling components over 20
cases and 158 snapshots. All six inspected external source hashes match the
current pinned checkout. This is a component comparison rather than complete
system event-order validation. The stream fixture now expects 656 silent
values because its first 32 kHz tick retains its queued deadline.

EFB regressions cover 144 queued GPU texture-copy/clear cases and four queued
GPU-to-CPU blend transitions. They compare every encoded destination byte,
padding and stride guards, source formats, half scaling and color/alpha/depth
clear masks. The full Windows hardware renderer suite also passes on NVIDIA
RTX 2000 Ada. These checks preserve GPU readback, retained fragment state and
copy-before-clear behavior while bulk operations use synchronized buffers.

Thirteen selected strict MSVC Release/LTO tests pass for the initial indexed
decoder, bulk EFB and queued audio changes: CPU, native integer, CPU groups,
device, disc stream, event budget, runtime groups, graphics, FIFO environment,
EFB copy/access, XFB copy and guest video. The final shared-decoder CPU group
fixture separately passes strict MSVC Release/LTO, strict GCC and Clang 18
ASan/UBSan with
1,868,844 complete CPU/RAM/ordered bus comparisons and 59,442,206 grouped
instructions. Sanitizers instrument the C++ runtime and fixture; emitted
machine code is checked through differential execution.

The initial Windows interpreter/OpenGL combination completes eight fixed-work
startup children in AB/BA order: Sunshine reaches three billion attempts and
320 fields, and Galaxy reaches one billion attempts and 76 fields. All sixty
JSON fields, raw frames and persistent files match within every pair without
exclusions. Elapsed times vary substantially with concurrent work and do not
establish gameplay speed. Frozen source/binary hashes and every comparison
remain under `current-optimization-startup-20261023/` in the continuation cache.

After decoder reuse, one further final-source pair per game retains all sixty
state fields, raw frames and stored files without exclusions at the same
bounds. Both final-source children are slower than their paired baselines:
Sunshine takes 17.807 versus 16.691 seconds, and Galaxy takes 38.785 versus
37.807 seconds. These shared-host startup samples do not establish an
integrated speedup. Their exact evidence remains under
`final-indexed-startup-20261023/` in the continuation cache. A separate normal
audio gameplay observation uses the frozen initial decoder and retains that
distinct source scope.

## GPU tiled copies and queued bounding-box reset

The direct GPU copy fixture covers 3,384 complete RAM/EFB/clear profiles across
all legal tile/source pairs, both scales, four intensity-flag combinations,
odd and edge footprints, zero/overlapping/long strides and clear masks. It
passes native MSVC, GCC/llvmpipe and Clang 18 ASan/UBSan. A separate Wii MEM2
run passes the same 3,384 profiles. Shader execution and host drivers remain
outside compiler sanitizer instrumentation. Two intentionally incorrect
half-scale and depth-channel shaders each fail the fixture.

The queued draw/copy/clear fixture expands to 752 cases. A separate review
matches 4,096 old/refactored CPU encoder cases, including callback order and
exception type/message. Additional combined sequences preserve traversal
latches across copies, masked clear, later scalar/batched GPU draws, copied
RAM replacing an active texture and retained-map CPU fallback.

The frozen GPU implementation completes sixteen fixed-work game children:
two AB/BA pairs per game on Windows and Linux. Sunshine reaches three billion
attempts/320 fields and Galaxy one billion/76 fields. Linux retains all sixty
reported fields, frames and stored files. Windows retains all fields except
revision metadata, plus exact frames and storage. This comparison precedes
the final shared CPU decoder and bounding-box correction; final combined
receipts retain their separate source scope. No gameplay speed is qualified.

A newly authored regression reproduces a queued bounding-box reset failure
on the earlier GPU core. The corrected path passes 32 native and llvmpipe
cases, including either axis, partial/full/zero BP masks, repeated writes,
earlier and later draws, the unaffected axis and accepted-fragment counts.
Each case requires a single final EFB readback. Linux state/latch checks and
the complete native hardware suite also pass. The fix resets only two GPU
accumulation words after earlier rectangles complete.

At `7688c15`, hosted normal GCC and Clang suites expose the DSP fixture's old
assumption that a rate write moves the already queued initial tick. The fixed
fixture checks one cycle before/at the retained 32 kHz boundary, one cycle
before/at the tenth fast-rate packet, and the complete 160-value PCM result.
It passes native MSVC and strict GCC against the queued-DAC core. The original
CI failures remain recorded. The frozen earlier full GCC run passes its other
65 tests and skips the private disc placeholder; its DSP failure is retained.

Implementation, negative controls, fixed windows and review receipts remain
in `.cache/gpu-texture-copy-20261023/`, `.cache/gpu-texture-copy-review-20261023/`
and `.cache/bbox-linux-review-20261023/`. Games and captures remain private.

The final combined MSVC Release/LTO tree passes nine selected tests: DSP,
graphics, CPU EFB copy, guest video, full hardware rendering, program-cache
checks, the 3,384-profile GPU fixture, hardware guest video and the compiled
EFB encoder comparison. A strict GCC overlay links the final three graphics
objects before the frozen core archive; the full hardware suite, GPU fixture,
CPU/reference encoders and DSP checks pass, with old graphics objects excluded.
The headless renderer stubs compile, and missing-device fixture controls return
77 when optional and fail when required.

One final native startup pair per supplied game compares this combined tree
with the preceding shared-decoder binary. All sixty JSON values, raw frames
and stored files match without exclusions. Sunshine takes 15.572/14.781 seconds
and Galaxy 35.835/34.914 seconds, preceding/combined, under shared host load.
These bounded prefixes do not qualify gameplay speed. Logs, complete source
pins and every comparison remain under `gpu-merged-*` in the continuation cache.

The combined `a26beb8` tree also completes one fixed Sunshine replay with
normal WASAPI audio at exactly 10,000 VI fields in 1,837.153 seconds. The
original owner waits for emulator exit zero; the runner also exits zero.
All 166 expected checkpoints through field 9,960 are present, with equal
CPU/device cycles and zero logged audio queue drops. All sixty saved-state
values are compared with both preceding runs. Fifty-four match; the retained
differences are revision metadata, two host audio counters and three hardware
work counters. CPU/RAM hashes, total GX commands/fragments, guest PCM/disc
hashes and counts, the raw final frame and every stored file match.

The observed 9,900-field span takes 1,823.643 seconds, or 5.428694 VI/s.
Field 9,960 is reached at 1,827.274 seconds, versus 2,248.311 seconds for the
preceding bulk-copy observation and 2,033.076 seconds for the earlier traced
run. These sequential shared-host observations do not isolate a GPU speedup.
Every complete fixed 600-field window is retained; the later windows from
6,000 through 9,600 range from 3.448006 to 5.412801 VI/s. Full-speed gameplay
remains unqualified.

A preplanned late profiler attempt returns zero but reports session creation
failure on stderr and creates no trace. Its three overlapping 60-field
intervals remain marked as command overhead, with no stack evidence claimed.
Separate passive process counters record 11.25 CPU seconds across a
30.003-second bracket; they do not identify a wait source. Saved state and
stdout retain their independently advancing host audio counters: saved state
has 1,024 additional mixed frames and 2,048 additional nonzero sample values.
The final image shows Mario, a nearby FLUDD device and an NPC at the Airport;
it does not establish device acquisition or level completion. Zero product
queue drops do not establish clean endpoint playback. The full source/input
pins, terminal review, every difference and window, and file seal remain in
`a26-gpu-texture-audio-telemetry-20261023` in the continuation cache.

Both hosted seven-job matrices pass at `a26beb8`: Windows, normal GCC and
Clang, GCC LTO, source checks, and GCC/Clang address/undefined-behavior checks.
Hosted software-rendering checks do not qualify a physical Linux GPU or
audio endpoint.

## Integer raster division

The hardware division fixture exercises the actual helper shared by both
raster shaders. Its 1,123,224 operand pairs include every encoded exponent
pair with four significand-boundary pairs and four sign combinations, all
23 subnormal leading bits in either operand, result underflow boundaries,
literal sticky/tie anchors, special values and a fixed random sequence.
An independent 64-bit integer oracle supplies expected bits; strict MSVC,
GCC and undefined-behavior-checked oracle runs agree on the corpus hash.
Three deliberately incorrect rounding variants fail their literal anchors.

GPU inputs contain operand bits and a global row tag. Expected values remain
on the host. Dispatches contain at most 32,768 cases, using at most 262,160
input bytes and 393,216 output bytes. Each output carries the quotient, its
complement and the row tag. Two distinct output poisons require 6,739,344
exact word checks. The integer helper has no forced estimate or estimator
fallback, so the fixture makes no estimate-path coverage claim.

The final private source passes twelve required-context gates: matched stock
and candidate hardware suites and 3,384-profile raster fixtures on native
Windows OpenGL, GCC with software OpenGL, and Clang address/undefined-behavior
checks with software OpenGL. The Linux checks link current graphics, main
and test objects before a frozen core archive. Sanitizer coverage applies to
those selected objects, not the full core. Full embedded shader literals,
compiler dependencies and native debug-source records are checked against
the pinned inputs. Headless renderer stubs compile as well.

A fresh baseline neutrality run, six component processes and sixteen startup
processes also exit zero under their original owners. The longer 8,000-field
candidate capture matches all final values and raw outputs, and its complete
capture counts reconcile with synchronization reasons at every checkpoint.
These checks cover arithmetic and the recorded game prefixes; they do not
establish completed levels, physical Linux GPU behavior or gameplay speed.
Gate receipts and independent reviews remain in the ignored
`integer-gpu-quotient-clean-*` and `projected-guard-clean-quotient-*` cache trees.

After integration, the actual repository source builds with MSVC Release/LTO
and GCC Release. Eleven selected graphics, reference-encoder and CLI checks
pass on each, including the full hardware suite, program cache, all division
cases, the 3,384-profile fixture and hardware guest video. Unlike the private
Linux overlay gates, this GCC build rebuilds the current project core and uses
installed dependencies. Formatting changes preserve every non-whitespace byte
and all literals/comments; both complete shader sources remain unchanged.
All sixteen complete shader blobs in the eight checked executables match.
Final JUnit reports, raw CTest logs, source/binary hashes and normalization
proofs remain in `.cache/quotient-public-integration-20261024/`.
