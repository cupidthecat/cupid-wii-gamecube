# Tests

Public tests run without console firmware, commercial discs, save files or keys:

```sh
ctest --test-dir build/release --output-on-failure
```

CTest registers separate CPU, memory/core, disc, device, remote, service discovery, DSP, graphics-command, vertex, texture, fragment, rasterizer, framebuffer-copy, IOS, network, system-configuration, executable-loader, font-loader and runtime suites, plus CLI help, version and synthetic runtime checks. Each suite has a timeout. Assertions stay active in Release builds because the test support code performs explicit checks.

The authored guest video suite executes its display, graphics, controller and input-dependent redraw operations through PowerPC instructions on both console modes. See [guest video](guest-video.md) for the fixture and checked output. It uses no private console assets.

The input replay suite checks script parsing, field and instruction ordering, input ranges, and controller changes observed by authored guest SI polling instructions on both console modes.

The service discovery transport suite checks IPC scheduling, USB endpoints, L2CAP channel negotiation and SDP continuation through ACL packets. A queued HCI event keeps bulk input pending until the event is consumed; the test checks this ordering through the public interfaces.

The framebuffer access suite clears EFB data through the graphics FIFO and reads it through authored guest CPU loads on both consoles. It checks address aliases, alpha read modes, depth, format reinterpretation and locked-cache DMA.

The same suite executes a cached RAM display list through guest stores to a FIFO page alias and checks its visible framebuffer clear. Core tests check gathering across the whole port page, byte order, adjacent unmapped addresses, and the memory-error callback. Runtime tests check that callback's PI cause, masking, acknowledgement, and unchanged error-detail registers on both consoles.

The system configuration suite checks typed SYSCONF parsing, default settings, controller registration, preservation of unrelated values, malformed inputs and serializer bounds. IOS fixtures check initialization before guest file reads, retained file ownership and settings, backups, and unsafe-path failures. See [system configuration](system-config.md) for the independent full-file comparison and its defined boundary choice.

The output suite uses SDL's dummy video and audio drivers to create a renderer, upload guest framebuffer data, queue stereo samples at all four supported rates, and poll input. The input suite attaches virtual controllers and checks four-port hotplug, SI buttons and axes, analog triggers, Remote/Nunchuk mappings, and guest rumble through SDL's virtual driver callback. These checks open no host window and play no sound. Headless builds report both suites as skipped. They do not measure driver performance or verify a physical controller.

Renderer readback checks standard, widescreen and square pixel views at the initial window size and after a resize. Mouse checks place infrared points inside the displayed image and exclude the surrounding black bars.

The headless audio statistics suite checks sample counts, nonzero values, the full signed peak range and a fixed hash of canonical big-endian PCM bytes. Invalid rates and incomplete stereo pairs leave these statistics unchanged. The SDL output suite checks the same statistics while queueing audio.

The synthetic runtime check executes a redistributable program assembled within the project. Passing that check shows the tested execution path works; it does not establish commercial-game compatibility. See [compatibility](compatibility.md) for asset-dependent results and remaining runtime limits.

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
- `vertex_reference_checks` compares position, normal, lighting and texture-coordinate transforms.
- `rasterizer_reference_checks` compares clipping, coverage and interpolated samples for triangle, line and point commands.
- `texture_reference_checks` compares exact texel components across all texture encodings, palettes, texture units, RAM mip levels and manual texture memory. It also compares nearest and bilinear sampling with clamp, repeat and mirror modes, negative coordinates, fractional boundaries and split-bank RGBA8 fetches. The sampling comparisons use manual texture memory at mip level zero; they do not establish LOD selection accuracy.
- `fragment_reference_checks` compares regular and comparison arithmetic, signed registers, konst selectors, swaps, retained texture inputs, alpha-test decisions, indirect matrices and coordinates, depth textures, and fog. Controlled 4×4 RGBA8 textures make coordinate changes observable while keeping sampling outside the target's application subsystems. Fog cases cover every nonzero mode, both projections and defined range-table interpolation indices.
- `sdp_reference_checks` compares complete HID and identity service records, transaction fields and all captured continuation fragments.
- `efb_copy_reference_checks` compares encoded texture bytes across framebuffer formats, copy formats, scale, dimensions and source positions.
- `xfb_copy_reference_checks` compares filtered YUV output across framebuffer formats, dimensions, scale, gamma, source positions and destination stride.

Graphics comparisons link fmt. The product still builds as C++20 and does not link it. Test-only hooks stop with errors if unused application or image-decoding paths are entered.

External graphics comparison sources retain visible narrowing warnings on MSVC and use `-O1` on GCC to avoid an optimizer warning in their fmt calls. Project source and the test drivers retain the configured warning-as-error policy and strict floating-point flags.

Unused parameters in the unmodified framebuffer-copy comparison sources remain visible warnings on GCC.

The default public run marks all ten comparison tests as skipped because the external checkout is not a dependency of the product build. Keep the comparison checkout's revision with the test results. These comparisons cover the named helpers, graphics stages and service records; they do not compare complete CPU execution or prove hardware accuracy.

## Package checks

`tools/package-verify.cmake` executes the binary extracted from the package, including the synthetic runtime check. This catches missing runtime libraries and packaging paths that ordinary build-tree tests miss. Package checks run in Windows and Linux CI.
