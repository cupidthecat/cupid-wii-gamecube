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
in the continuation cache. No offline GPU replay, tile shader or timer query
has yet been run for this packet, so capture validity is not a GPU correctness
or performance result.
