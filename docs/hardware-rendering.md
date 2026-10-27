# Hardware rendering

`--graphics opengl` selects the OpenGL 4.3 renderer. The default
`--graphics software` uses the validated CPU renderer. Both choices render the
guest's GX commands into the same 640 by 528 EFB. The hardware backend uses a
hidden SDL OpenGL context, so it also works with `--headless` when the host has a
video driver that can create that context. A `CUPID_HEADLESS_ONLY` build excludes
SDL and reports hardware rendering as unavailable.

On Windows, SDL-enabled executables export the standard NVIDIA and AMD
high-performance adapter requests. Hybrid graphics drivers can use these hints
when choosing the OpenGL device. The active renderer's device string records
the selected adapter.

CPU code decodes vertices, transforms and lights them, assembles primitives,
clips them, applies culling, and prepares interpolation planes. GPU programs
then evaluate fixed-point triangle coverage and fragment inputs, run TEV and
alpha/depth tests, blend the result, and store raw color/depth EFB bits. Triangle
dispatches have storage barriers to preserve GX command order. This backend
does not yet move vertex transforms or primitive setup onto the GPU.

On devices with fragment interlock and sufficient shader-storage capacity,
automatic mode attempts instanced bounding rectangles and ordered fragment
programs. Ordinary initialization declines retain compute rendering. Coverage, TEV,
depth, blending and retained-state arithmetic use the shared batch shader.
The owned framebuffer is single-sample; each pixel's EFB and retained writes
remain inside its ordered critical section. State, texture, copy, clear,
bounding-box and synchronization boundaries retain their existing ordering.

A native submission holds at most 256 rectangles and 1,048,576 summed bounding
pixels. The global 4,094-rectangle ordinal bound remains unchanged. Devices
without the required capabilities use compute dispatch, including its
sixteen-packet, union-area and occupancy limits. Both native shader families
are validated before guest work can queue.

For backend comparisons, `CUPID_GX_ORDERED_RASTER=0` requires compute and
`CUPID_GX_ORDERED_RASTER=1` requires ordered rasterization. The default value
is `auto`; unsupported required mode and invalid values report an error.
This selection applies within `--graphics opengl`; the CLI's software-rendering
default is unchanged.

## Coverage and fallback

The shader handles triangles, strips, fans, quads, expanded lines and points;
scissor rectangles; frozen depth planes; up to 16 TEV stages; signed TEV
registers, konst selections, swaps, comparison and arithmetic operations;
up to four indirect texture maps, all indirect formats and biases, normal and
S/T-scaled matrices, matrix exponents, wraps, bump-alpha raster inputs, and
coordinate accumulation after the first TEV stage;
alpha tests; all depth comparisons and early/late depth writes; depth textures;
orthographic and perspective linear, exponential, squared exponential and
backward fog, including interpolated range adjustment;
all blend factors, subtraction and logic operations; color/alpha masks;
constant alpha; and RGBA6 dithering.

Textures use the existing checked tile decoder for I4, I8, IA4, IA8, RGB565,
RGB5A3, RGBA8, C4, C8, C14X2 and CMPR. Decoded pixels and mip offsets are uploaded
to shader storage. GPU sampling applies the console's fixed-point coordinates,
wrap rules, seven-bit bilinear weights, derivative-based LOD, point mip
selection and trilinear blending. Base-level manual TMEM textures and split-bank
RGBA8 preloads use the same path. Cache hits require exact decoding-state and
source-byte equality. Filter, wrap and LOD-bias changes update GPU sampling
state without decoding or uploading unchanged texels. A changed reachable mip
count still invalidates the decode. Cache revisions control GPU uploads.

When enabled units retain their decoded sizes, a changed texture updates only
its packed buffer range and the mip-offset table. Changing a unit's size or
enabling or disabling a unit rebuilds the packed buffer. Pending rectangles
finish before either upload so they retain their original texture contents.
`texture_upload_bytes` counts host-to-device texture bytes, including the table.
The required texture suite checks per-unit changes, queued draws, layout changes
and equal-size decodes with different mip offsets against exact CPU pixels.

Draw-state uploads compare all 256 BP words and all 32 TEV/konst words with
the last uploaded array. Equal arrays reuse the existing GPU register buffer.
An identical admitted state also reuses its eligibility result and packed
registers. Each draw still checks all reachable texture bytes. A
rejected state leaves the last GPU register array intact. Framebuffer copy and clear
commands use a separate parameter buffer and restore the fragment program.

The BP indirect-stage count controls indirect texture prefetches. A zero count
does not disable the TEV stages' coordinate wrapping or add-previous operations.
The renderer keeps two fragment-program families: generic and zero-prefetch.
The latter replaces only the prefetch count with a compile-time zero; both
retain the same stage math, texture latches and draw-admission checks. Each
family creates its scalar and batched programs on first use, with at most four
fragment programs per context. Pending draws finish before a state or family
switch, and each queued batch retains its original family.

Program pairs become available only after both compile and link successfully.
A failed pair deletes its partial programs and propagates the backend error.
Compilation and each program's first real dispatch use the default host
floating-point environment and restore the caller's controls and exception
flags. This covers drivers that defer compilation until dispatch. Cached
draws avoid saving the environment. Copy commands restore only an already-used
scalar program, or program zero until the next draw binds its program.

Dirty EFB synchronization also uses the default host floating-point environment,
including when the caller supplies no fragment-shader recipient. The scope covers
context selection, pending draws, driver queries and readback, then restores the
caller's controls and sticky flags. A clean EFB returns before entering the scope.
The hardware tests check cold null-recipient synchronization across all sixteen
rounding and denormal profiles, alongside the existing preflushed controls.

Projected texture coordinates keep the CPU path's float rounding at each step:
the reciprocal of interpolated W, the Q product, the projected quotient, and
the S/T products before conversion to signed 17.7 coordinates. Both divisions
check the estimated quotient against exact double midpoint products and select
the nearest binary32 value with ties to even. The same helper handles fog
divisions in both raster shaders. The shader's `precise` qualifier preserves
operation order but does not require a driver to round division exactly.

The Intel Iris Xe driver reproduced a one-ULP projected-quotient error even
with double intermediates. It changed the fixed-point coordinate from 49,492
to 49,491 and selected a different filtered texel. The midpoint correction
retains GPU execution and restores the exact CPU value. The projection fixture
checks that boundary in individual and batched dispatches, plus 512 profiles
with zero and nonzero Q.

Within each fragment, stages reuse the most recently selected coordinate's
projected S/T values. The first sample also prepares and retains its 2 by 2
derivatives. Selecting another coordinate invalidates those values. Each
texture unit still applies its own filtering, wrap and LOD state to the
retained arithmetic inputs.

Source snapshots cover the checked RAM mip extent, the selected manual TMEM
bank or both RGBA8 banks, and the selected palette. C4, C8 and C14X2 compare
32, 512 and 32,768 palette bytes respectively. Unrelated TMEM writes do not
invalidate those snapshots. Every relevant byte is compared directly; cache
validity does not depend on a hash or a sample of the source data.

Retained texture operands before an enabled sample use the CPU path. So do
first-stage indirect add-previous
coordinates, which depend on the previous fragment, and indirect operations
that need a map retained from a previous draw. Reserved indirect wraps and
matrix selectors retain CPU diagnostics. Other fallbacks include manual mip
chains, mip ranges beyond the checked
ten-level limit, interpolation poles, non-finite planes, and triangles whose
edge equations exceed the shader's checked 32-bit range. Prefetching an unmapped
texture falls back to demand sampling, which preserves the CPU diagnostic if a
fragment actually accesses those bytes. Existing unsupported software states,
including multisample coverage, still fail explicitly.

Depth-texture operations without an enabled sample also fall back, since they
can read the raw texture value retained from an earlier draw. Invalid fog
parameters and potentially undefined zero-divided-by-zero fog calculations
retain CPU diagnostics.

Fog range adjustment uses the XF viewport width, the BP center offset and all
ten packed coefficients. Each pixel selects and interpolates its two adjacent
coefficients, with the center sample clamped to the last table entry. Zero
coefficients, a zero or non-finite viewport width, and overflowing squared
offsets retain the checked CPU path. Fog divisions and square roots use double
intermediates rounded back to float; ordinary GPU float approximations changed
one quantized color channel in the native range regression.

The four exponential curves use immutable tables of quantization boundaries,
uploaded once per context. Each boundary is the first representable float
whose CPU fog factor reaches the corresponding integer bucket. The shader
searches the selected curve's 256 boundaries and applies the same integer color
interpolation as the CPU. This avoids differences in driver exponential
approximations near a bucket boundary. The tables share the CPU's fog-factor
function, which remains covered by independent fragment comparisons.

The final JSON includes hardware dispatch, fragment and fallback-rectangle
counts. These counters identify work executed through OpenGL;
an OpenGL software driver can produce them too. Check the reported renderer
before treating them as evidence of physical GPU acceleration.

## EFB visibility

CPU reads, CPU pokes, bounding-box reads and software fallback restore pending
EFB writes to the CPU buffers. GPU texture and XFB copies finish earlier draws
before encoding directly from GPU storage. Software writes
mark the GPU copy stale, and the next hardware rectangle uploads the current
raw EFB arrays. Format changes reinterpret the same stored color bits.

Enabled texture sampling also writes a retained TEV texture-state record.
Separate GPU counters select the last texture sample, indirect coordinates and
each enabled indirect map in the CPU rasterizer's traversal order. Disabled
stages keep earlier samples even when several draws remain pending. Alpha and
late depth rejection still update those latches; early depth rejection leaves
them intact. Synchronization restores the complete sampled state to the CPU
shader before a continuation that needs retained values.
When several of the six restoration slots select the same pixel, synchronization
reads that pixel once and reuses its eight state words. Sequence prefixes may
differ while selecting the same pixel. The local lookup ends with each call,
so later dirty synchronization reads the updated GPU state.

Color and depth pokes obey the CPU framebuffer address selectors and update
their selected EFB pixels independently of GX blend/depth write masks.
Out-of-range pixels are ignored. Combined color/depth accesses remain
unsupported. Texture copies use the shared checked layout and GPU tile encoder.

XFB copies use a compute shader to filter and encode the GPU's raw EFB into
packed YUYV output. Only the copied row bytes return to the CPU before the
command writes guest RAM; stride padding remains unchanged. Source bounds,
scaling, staging extents and mapped destination bytes are checked before
dispatch or RAM mutation. The same checked copy plan drives both encoders.
Zero-stride suppression and unsupported source or staging edges retain the
CPU behavior and diagnostics.

Vertical filtering uses the seven BP coefficients grouped into three integer
weights. Gamma uses a table of all 256 quantized channel values for each
selector, computed with the CPU encoder's existing float operations. The CPU
encoder still computes its own gamma values, and the GPU checks compare its
complete output bytes against that encoder. Double intermediates preserve
nearest-source-row selection after vertical scaling. Copy-triggered RGB,
alpha and depth clears execute as a later GPU dispatch with their independent
write masks. Subsequent GPU draws see the clear, while a later CPU EFB read
synchronizes the resulting color, depth, counters and retained texture state.

## Validation

Run the public differential suite on a host with OpenGL 4.3:

```sh
build/release/cupid_hardware_renderer_tests --require-device
build/release/cupid_hardware_renderer_tests --require-device --benchmark
```

On Linux without an active display, `xvfb-run -a` can provide an X server for
Mesa. CTest skips only a missing required video/context capability. Shader
compilation, linking and unexpected backend failures fail the test. The
`--require-device` option also makes a missing context a test failure.

The suite compares exact EFB colors, depths, bounding-box values and fragment
counts against the CPU renderer. Cases cover clipping and primitive assembly,
interpolation, 16 logic operations, 64 source/destination blend combinations,
96 generated multistage TEV/alpha/depth states, 1,000 generated linear-fog
profiles, every implemented texture
encoding with several filters and wrap pairs, changed texture RAM, manual
split-bank sampling, retained texture state, depth-texture operations,
CPU pokes, XFB copy/clear ordering, and CPU fallback followed by hardware work.
Another 1,024 generated profiles exercise one to four indirect maps and up to
16 TEV stages, all formats, biases, bump selections, matrix kinds and exponents,
wraps, subsequent-stage accumulation, filtering and signed-coordinate overflow.
A separate fixture compares complete texture, raw-texture, map and coordinate
latches through queued disabled stages, alpha rejection, early and late depth
rejection, and rendered CPU continuations. Reserved selectors must still fail.
Another 2,048 generated fog profiles cover all enabled selectors, both
projections, signed parameters, paired-infinity parameters, all range-table
coefficients and the center endpoint. They require hardware dispatches and
exact color, depth, fragment and bounding-box equality. A separate fixture
checks 9,198 adjacent-depth pixels around the four exponential curves'
quantization boundaries without fallback. Invalid range parameters preserve
CPU diagnostics, and a valid CPU continuation followed by hardware work must
preserve the EFB. Use `--fog-only` to run these focused fog checks.

`--projection-only` checks one authored I8 gradient pixel at a projected
division boundary and 256 generated reciprocal profiles with zero Q. The
first case requires fixed coordinate 49,492 and RGB value `0x272727`. Each
profile compares the GPU's sampled color with the CPU fragment shader and
requires hardware execution without fallback. These cases test both divisions
whose rounded results feed texture coordinates.
Another 512 profiles vary W, Q and S/T planes across 4 by 4 rectangles. They
compare all 8,192 sampled colors with the CPU fragment shader, including six
I8 mip levels, point and trilinear mip selection, both derivative modes,
signed LOD bias, point and linear filters, and all wrap modes. Every profile
requires one hardware dispatch and zero fallbacks.
Adding `--benchmark` to `--projection-only` times 64 draws of a 128 by 128
rectangle with 16 stages selecting one projected coordinate. The timed work
executes 1,048,576 GPU fragments, requires zero fallbacks, and checks every EFB
pixel against the authored gradient result after readback.

`--state-only` checks nine queued draws after repeated identical begins,
changed BP/TEV/konst words, and a rejected state followed by the preceding
valid state. It compares the final pixels exactly with the CPU shader.
Adding `--benchmark` measures 200,000 unchanged begins after those checks.

`--program-cache-only` checks lazy creation and reuse of all four fragment
programs, zero allocation IDs, partial-pair cleanup after a real compilation
failure, and successful retry. It compares 200 mixed zero-to-one-through-four
indirect-stage draws with the CPU renderer, including wrapping, accumulation,
partial workgroups, rejection, retained maps and rejected-state continuation.
Another fixture checks 128 first-use and cached draws plus 64 compilation
failures across all four host rounding modes, mixed x87/SSE controls and
FTZ/DAZ settings. It checks caller flags at the protected dispatch boundary;
existing readback-side flag changes are outside that scope. Batch-first use,
XFB copy and subsequent scalar use exercise the program-restore ordering.
These checks are also part of the full renderer suite.

The shipping MSVC and GCC builds pass the full renderer, guest-video and FIFO
environment suites. Clang 18 ASan/UBSan passes those same scopes; a separate
cache-only run also passes with leak detection enabled. Eight stock OpenGL
runs across both games and Windows/Linux preserve every reported state field,
raw frame byte and stored file at the recorded 320/76-field startup bounds.
The separate frozen cache-only 2,040-field comparison preserves those outputs
against its plain stock runtime. These checks do not establish full-speed
gameplay.

A separate 256-profile fixture queues 512 draws across workgroup-edge widths
and heights, sparse triangle coverage, all depth comparisons, early and late
depth writes, alpha rejection and zero through four indirect maps. It matches
the complete EFB, fragment counts and retained texture/indirect latches after
28,549 accepted fragments. The full required-device suite and both authored
CPU-to-VI guests pass on the RTX 3060 and Mesa. Counter-aggregation prototypes
also passed pixel checks but were slower and were removed; shader bytes remain
unchanged by the draw-admission cache.

Before the indirect extension, the suite passed on October 4, 2026, under WSL with Mesa
`llvmpipe (LLVM 20.1.2, 256 bits)`: 1,799 dispatches and 2,789,052 shader fragments.
The optional benchmark verified all pixels after 64 full EFB quads. Its CPU
rendering time was 2.413 seconds and its OpenGL time was 2.969 seconds. This
software-driver result validates execution and pixel equivalence; physical GPU
performance needs a separate driver run. Related graphics, fragment,
rasterizer and CPU EFB guest tests also passed in that Linux build.

These synthetic checks do not establish commercial-game rendering, full-speed
gameplay, audio, input or saving.

That suite also passed on Windows with
`NVIDIA GeForce RTX 3060/PCIe/SSE2`: 1,799 dispatches and 2,789,052 shader fragments.
The baseline 64-quad benchmark took 3.060 seconds on the CPU path and 0.0826 seconds
through that GPU, a 37.1-fold rendering-time difference for this synthetic
single-stage workload. The benchmark includes completion/readback and checks
every final EFB pixel and depth. It does not measure whole-game speed.
The initial native compiler rejected dynamically indexed local register
arrays. The shader now uses explicit register fields, and both drivers pass
the same assertions. Compilation and linking failures remain test failures.

`cupid_guest_video_tests --hardware --require-device` also passed under WSL
with Mesa and on Windows with the RTX 3060 on both console modes. Each guest executed its CPU FIFO stores,
hardware draws, texture-copy round trip, XFB copy, VI presentation and
controller-dependent redraw. Each console produced two checked frames and
768 hardware shader fragments without fallback rectangles. The frame hashes
remain the authored guest fixture's existing exact expectations.

The indirect extension passed exact comparisons on both of those drivers. Its
main fixture executed 3,855 dispatches and 6,614,716 hardware fragments, and its
separate latch fixture executed six dispatches and 267 hardware fragments.
The native extension checks reused a Release core built with experimental IPO
enabled. The prototype inputs, link libraries, patch and driver receipts were
frozen locally before the extension was integrated. These synthetic checks
do not establish that a commercial game's indirect effects are correct.

The indirect implementation follows the pinned software TEV behavior used by
the validated CPU path. Its S/T matrix multiplication divides signed products
by 256 with truncation toward zero; the pinned generated-shader path uses an
arithmetic shift instead. The CPU path also clears bump alpha when its selector
is disabled, while the generated path can retain it within a fragment. These
are source-level differences between the behavioral paths, not observations
from a physical console. Unsampled maps, first-stage accumulation and reserved
wraps retain the CPU result or diagnostic rather than guessing at hardware
behavior. No external-runtime binary differential was run for this extension.

The fog extension passed on October 4, 2026, in fresh MSVC 19.51 Release and GCC
13.3 Release builds, on the RTX 3060 and WSL llvmpipe drivers described above.
Its fog fixture executed 2,049 dispatches and 3,516,084 hardware fragments, and
its bucket fixture checked 9,198 pixels without fallback. The complete hardware
suite, both console-mode hardware guests, graphics and fragment unit tests,
81,357 independently compared rasterized samples, and 500,000 independent
fragment comparisons passed on both platforms. The native hardware suite took
31.62 seconds and the Linux suite took 15.44 seconds under concurrent local
work; these are test durations, not game-performance measurements. No new
commercial-game or real-console fog validation is established by these checks.

## Texture-cache validation

`--texture-cache-only` runs focused cache checks. RAM writes, the last entry of
each palette format, palette-format changes, independent RGBA8 bank writes,
bank-base changes and uneven mip layouts must preserve exact CPU fragment
colors and depths. Filter/wrap updates and bytes immediately outside the
selected banks and palettes must reuse the decode. Cache counters check these
invalidation boundaries, and every submitted pixel is compared to the CPU
shader. The complete required-device suite and both console-mode hardware
guests passed with these checks on October 4, 2026, in fresh MSVC 19.51 and
strict GCC 13.3 Release builds. The independent fragment, texture and
rasterizer comparisons also passed on both platforms.

The optional benchmark submits 1,024 one-pixel draws from a fixed 256 by 256 C8
texture, then 64 draws with alternating filtering. Each interval includes
completion/readback and checks the entire EFB. Three repetitions produced
these median elapsed times:

| Driver and workload | Earlier cache | Direct byte comparisons |
| --- | ---: | ---: |
| RTX 3060, 1,024 steady draws | 1.59635 s | 0.0267991 s |
| RTX 3060, 64 filter updates | 0.325189 s | 0.0044628 s |
| WSL llvmpipe, 1,024 steady draws | 1.74295 s | 0.154231 s |
| WSL llvmpipe, 64 filter updates | 0.253102 s | 0.0102404 s |

The cache keeps byte-exact invalidation while avoiding a serial hash pass over
RAM and all 1 MiB of TMEM on each indexed draw. The earlier filter-update
workload also decoded and uploaded the entire texture every time. These are
synthetic cache measurements. Other builds and game runs shared the host, and
the ratios do not establish whole-game speed. Frozen source pairs, binaries,
full logs and timing receipts remain under the ignored local directory
`local-artifacts/graphics-cache-20261004/`.

Frozen MSVC runtimes also compared the two caches through the supplied
commercial images on the RTX 3060. Both used OpenGL, headless output, disabled
host audio, identical copied storage and fixed input replays. Sunshine loaded
the same validated USA IPL fonts. A private current-runtime diagnostic printed
cache counters after execution; guest state and emulation code otherwise
differed only in the cache implementation and its returned statistics.

| Game and step-attempt limit | Fields | GPU dispatches | GPU fragments | Earlier elapsed | Current elapsed |
| --- | ---: | ---: | ---: | ---: | ---: |
| Sunshine, 3 billion | 320 | 424 | 18,956,440 | 367.960 s | 371.777 s |
| Galaxy, 1 billion | 76 | 764 | 104,532,960 | 201.853 s | 203.136 s |

All four runs exited normally at their requested instruction limits with zero
fallback rectangles. Within each pair, all 44 final JSON fields, the complete
JSON bytes, final PPM bytes and every persisted user-file byte matched. Input
image, copied font and replay hashes captured during execution matched their
after-run hashes. These startup workloads establish cache equivalence, with
no whole-game speed improvement. Concurrent game runs and builds shared the
host. Sunshine decoded three textures, made 103 packed texture uploads and
submitted 4,482,656 bytes in exact comparison spans; Galaxy decoded 240
textures, made 382 uploads and submitted 704 comparison-span bytes. The
counter measures the compared spans, which can exit at an earlier mismatch;
it does not measure actual host memory traffic. The cold-texture Galaxy
startup does not exercise the repeated-cache benchmark's workload. Neither
bounded pair establishes gameplay or full-speed execution.

## XFB-copy validation

The XFB compute path passed the complete required-device suite and both
console-mode hardware guests on October 4, 2026, in isolated MSVC 19.51 and
strict GCC 13.3 Release builds, using the RTX 3060 and WSL llvmpipe drivers
above. `--xfb-only` runs the focused copy checks. They compare all destination
bytes, including sentinel-filled padding, for 1,129 defined profiles across
all four supported EFB formats, gamma selectors, random filters, clamping,
vertical scales and MEM1/MEM2 destinations. Another 919 profiles must reject
undefined staging or source edges before mutation. Together the checks compare
5,536,481,280 RAM bytes. Thirty-two copy/clear states check independent RGB,
alpha and depth masks; eight queued copy/clear commands require one subsequent
EFB readback and exact CPU state. A copy between retained indirect draws must
preserve texture, map and coordinate latches.

The independently compiled behavioral comparisons also passed on both
platforms: 267,659,520 EFB-copy bytes across 180,480 generated cases, and
43,255,200 XFB bytes across 5,487 defined generated cases. Their other 4,513 XFB
cases reject undefined extents or staging/source edges. These checks preserve
the validated software encoder's chroma filter, which weights the preceding
pixel, first pixel and second pixel by one, two and one. The pinned generated
GPU encoder instead averages the pair's chroma. No physical-console comparison
establishes which path applies to those differing cases.

Before implementation, a private timer around `CommandProcessor::copy_efb`
measured 161 XFB copies, all with clears, in Sunshine's 3-billion-attempt
OpenGL startup run. They consumed 9.492 seconds of its 392.279-second elapsed
time. Its complete final JSON matched the earlier sealed startup state. This
identified recurring commercial copy work, including full EFB readback,
CPU encoding and CPU clear. A later private timer measured the same 161 commands
with the GPU path: 0.335 seconds inside `copy_efb`, with its final JSON identical
to the new current-runtime run below. Packed RAM output completes inside that
scope. GPU clear submissions remain ordered with subsequent work; completion
of the last queued clear occurs during later EFB observation. The two profile
runs shared the host with other work and do not isolate overall game speed.

The focused benchmark encodes 32 copies of a 640 by 224 RGBA6 EFB with vertical
filtering and all gamma selectors. Every GPU copy completes, writes RAM and
matches the CPU output before timing ends. Three repetitions gave these median
times; the CPU interval starts with host EFB colors already available.

| Driver | CPU encoding | GPU encoding, packed readback and RAM write |
| --- | ---: | ---: |
| RTX 3060 | 0.450219 s | 0.0177251 s |
| WSL llvmpipe | 0.353303 s | 0.0287211 s |

These component timings include comparison work in the GPU interval. Other
local runs shared the host, so they do not establish whole-game speed. Frozen
source pairs, copied inputs, binaries, arguments, output and comparison logs
remain under `local-artifacts/graphics-xfb-20261004/`.

Frozen native OpenGL runtimes compared the earlier CPU copy/clear path with the
GPU path through the same copied Sunshine and Galaxy seeds, replays and fonts.
Both identified the RTX 3060. The pair differed in copy implementation, returned
copy statistics and a private post-run diagnostic. An initial source snapshot
crossed an unrelated CPU interface update; both private snapshots restored the
same previously validated runtime interface before building. The live runtime
was unchanged by that repair.

| Game and attempt limit | Earlier elapsed | GPU-copy elapsed | GPU XFB copies | Packed readback bytes |
| --- | ---: | ---: | ---: | ---: |
| Sunshine, 3 billion | 214.494 s | 200.791 s | 161 | 92,323,840 |
| Galaxy, 1 billion | 105.434 s | 98.674 s | 70 | 40,857,600 |

Both pairs exited normally at their requested limits. All 44 final JSON fields,
complete JSON bytes, final PPM bytes and persisted user-file bytes matched:
four Sunshine files and eight Galaxy files. Sunshine retained 424 draw
dispatches and 18,956,440 shader fragments; Galaxy retained 764 dispatches and
104,532,960 fragments. Both had zero fallback rectangles. Each GPU-copy run
performed one full color/depth EFB readback, totaling 2,703,360 bytes, at final
observation. Image, font and replay hashes matched the earlier copied-input
receipts after execution. These concurrent startup timings support commercial
copy equivalence and record reduced copy work; they do not establish isolated
whole-game speed, gameplay or full-speed execution.

## Bounded commercial comparisons

The projective precision checks passed on October 4, 2026, in isolated strict
MSVC 19.51 and GCC 13.3 Release builds, using the RTX 3060 and WSL llvmpipe
drivers above. Both complete required-device hardware suites passed as well.
The regression's authored projected-division case failed on the native driver
before the fix. A generated zero-Q reciprocal case also failed before its
division was rounded. No comparison tolerances or software fallbacks were
added.

A private Galaxy capture located a native color difference in field 664,
GX command 647,885. Replaying the complete captured draw reproduced one raw
EFB pixel difference at `(387,443)`: CPU `0x24f580`, GPU `0x20f540`, with matching
depth `0xfffc76`. The initial CPU and GPU replays each matched their captured
arrays exactly. The same initial replay passed on llvmpipe. A single dispatch
and fragment retained the native failure. Instrumented W, reciprocal, Q and
S/T values matched; the projected quotient differed by one float step and
crossed a signed 17.7 sampling boundary.

After the two rounded divisions, the complete original draw passed on both
drivers with 11 hardware dispatches, 68,775 fragments and zero color or depth
differences against the captured CPU result. The frozen core and graphics
sources, exact invocation receipts, red and green logs and private capture
remain under `local-artifacts/galaxy-draw-capture-20261004/`. This validates that
draw and the arithmetic regressions. It does not establish equivalence for a
complete commercial run or playable gameplay.

A separate frozen continuation with only the projected-division correction
completed at field 690 with 8,425,629,213 successful instructions, 12,512 GPU
dispatches, 935,052,581 GPU fragments and zero fallback rectangles. All 23
checkpoints, taken every 30 fields, match the software control's complete
reported state apart from the three backend counters. Their PPM files also
match byte for byte. Final main and secondary RAM dumps, frame bytes, PCM
bytes and persisted user files are identical. The supplied image, replay and
frozen sources remain unchanged. The receipt is
`local-artifacts/galaxy-projection-continuation-20261004/completed-003.json`.
This verifies the corrected startup prefix through the first recorded frame
difference. Later fields and gameplay remain outside that comparison.

An extension with the same frozen core, the projected-division correction and
the exact state-upload cache completed through field 1,050. All 35 checkpoints
and final reported state match the software control apart from the three
backend counters. Full CPU dumps, both complete RAM dumps, PPM files, PCM
bytes and persisted user files match exactly. It executed 12,747,664,586
successful instructions, 268,063 GPU dispatches and 1,487,715,052 GPU fragments.
Its 406 fallback rectangles produced seven CPU fragments. The image, replay
and frozen sources remain unchanged. The receipt is
`local-artifacts/galaxy-state-projection-continuation-20261004/completed.json`.
This closes the recorded frame and secondary-RAM differences at that bound.
It uses the shader before projected-coordinate reuse and establishes no later
gameplay or full-speed result.

The state-upload regression passed on both drivers in strict MSVC 19.51 and
GCC 13.3 Release builds. Five runs of its 200,000-begin benchmark had native
medians of 0.156990 seconds before caching and 0.0324661 seconds after caching.
The llvmpipe medians were 0.0849154 and 0.0265423 seconds. Other builds and
commercial validation were active during these measurements. They measure
unchanged-state submission overhead and do not establish a commercial frame
rate. The exact logs remain under
`local-artifacts/graphics-fidelity-20261004/`.

The projected-coordinate reuse checks and the complete original captured draw
passed on both drivers. Five runs of the 16-stage rectangle benchmark had RTX
3060 medians of 0.0349868 seconds before reuse and 0.0098433 seconds after
reuse. The llvmpipe medians were 0.269940 and 0.248199 seconds. Each timed
result includes submission and GPU completion after a separate warm-up draw.
The measurements share the same workload and exact pixel checks, with other
validation active on the host; they do not measure commercial gameplay. The
field-690 commercial continuation above uses the frozen shader before this
reuse change.

On October 4, 2026, both renderers ran from the same copied Windows executable,
configured as `integrated-runtime-private-20261004`. Its SHA-256 was
`c53402c5e7d9f1fb5d8c6505a360c96d0b4b489ee70b771057397c2996e0d74a`.
The sealed source manifest was
`15474d0e1a8f2cc4f414f902ccc9762aac653e3a927fa96a2fff357b99b84852`,
including working changes after commit `563d1a8853a351184d35f0f0e35d6b4f0a13392a`.
Each pair used identical copied user seeds and input replays, `--headless`,
`--audio none`, a 1,800-second safety limit, and `--graphics software` or
`--graphics opengl`. Sunshine also used identical copied font blocks. The
OpenGL startup logs identified `NVIDIA GeForce RTX 3060/PCIe/SSE2`.

| Game | Step-attempt limit | GX commands | GPU dispatches | GPU fragments | CPU fallback rectangles | Software elapsed | OpenGL elapsed |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Super Mario Sunshine | 3,000,000,000 | 65,917 | 424 | 18,956,440 | 0 | 336.259 s | 334.128 s |
| Super Mario Galaxy | 1,000,000,000 | 65,203 | 764 | 108,037,440 | 0 | 160.137 s | 118.061 s |

All four runs exited normally at their requested instruction limits. Within
each pair, every emitted final-state JSON field matched except the three
backend counters: `hardware_dispatches`, `hardware_fragments`, and
`hardware_fallback_rectangles`. This includes CPU state and RAM hashes,
registers, exceptions, GX and VI counters, frame hashes and PCM hashes. The
final PPM files and all persisted user-file bytes also matched exactly.
After-run checks confirmed that both supplied images, copied replays and font
blocks were unchanged.

Sunshine presented 320 fields. Its final 640×224 image displayed the opening
“Nintendo Presents SUPER MARIO SUNSHINE” branding. It generated 338,464 PCM
samples, including 62,194 nonzero samples. Galaxy presented 76 fields. Its
final 640×228 image displayed the wrist-strap safety screen before the title
screen. All 85,376 generated Galaxy PCM samples were zero. These observations
establish GPU execution and exact software equivalence for the bounded startup
workloads; they do not establish title-menu interaction, gameplay, host audio,
saving or full-speed execution.

The backend pairs ran concurrently while other validation and build work was
active. Their elapsed times are records of those runs, not isolated speed
measurements. Private receipts, exact arguments, input hashes, state dumps,
images and timing records are retained under the ignored local directory
`local-artifacts/hardware-commercial-final-20261004/`.

## Derivative-independent texture sampling

The compute sampler skips projective derivatives when minification and
magnification use the same filter and mip selection is fixed. A disabled mip
filter always samples level zero; enabled mip filtering can skip derivatives
when the minimum and maximum LOD are equal. Different minification and
magnification filters still select the filter from the unclamped LOD before
applying the bounds. Both paths share the same mip sampling code.

Required-device suites pass on the RTX 3060 and Mesa with 1,280 varying-W/Q,
mip and fixed-LOD profiles compared pixel for pixel against the CPU. The
complete captured Galaxy field-664 draw also matches every EFB color and depth
pixel on both drivers: 11 dispatches and 68,775 fragments. Five alternating
native pairs change the fixed-LOD benchmark median from 0.033539 to 0.012348
seconds. Replaying the complete captured draw 64 times changes from 0.058950
to 0.049951 seconds. The Mesa samples conflict and establish no throughput gain.
These measurements cover rendering components under a shared host, not a
commercial frame rate. Exact sources, samples and required-device logs remain
under `local-artifacts/graphics-next-continuation-20261004/`.

## Empty raster coverage

After the existing state, texture-coordinate and edge-range admission checks,
the hardware path rejects rectangles lying entirely outside a strict triangle
edge. Rectangles containing at most 64 pixels also receive an exact host
coverage scan. If no sample is covered, the renderer completes the work
without uploading parameters or issuing a dispatch. Empty coverage leaves
EFB values, fragment counters and retained texture samples unchanged.

Six captured Galaxy rectangles, strict zero-edge boundaries and six visible
neighbors verify that distinction. The fixture starts with nonzero retained
texture state and checks its preservation. The earlier renderer fails the
new unnecessary-dispatch assertion on both drivers. The complete required
hardware suite passes with the repair on the RTX 3060 and Mesa; it retains
all color, depth, bounding-box, texture, fog, projection and XFB checks.
The shader is unchanged. Sources, red/green logs, dependency hashes and
executable receipts remain in `.cache/graphics-gameplay-20261004/empty-fix/`.

The combined RAM/empty-coverage Sunshine runtime completes field 10,000 and
renders ordinary airstrip movement and jumping. All 86 shared checkpoints
through field 5,160 retain exact guest state and raw frame bytes. GPU execution
counters have their own scope: the existing 4,094-dispatch cap is tested before
coverage admission, so removing empty dispatches can change which later
rectangles use the checked CPU fallback. Per-empty-work fragment and retained
state invariance does not imply identical whole-run GPU/CPU counters.
[Compatibility](compatibility.md) records the complete capture and its limits.

## Exact fog division on the Linux physical driver

A WSLg child process using `GALLIUM_DRIVER=d3d12` exposes the physical RTX 3060.
The original full pixel suite finds a one-step blue-channel error at pixel
(52, 53) of fog profile 1,384. Its perspective quotient differs by one binary32
step even when the division is written in double precision. The original
failure and unsuccessful precision-only repair remain retained.

The fog helper now selects the nearest binary32 quotient by exact midpoint
comparisons, with ties to even. It decodes operands from bits so subnormal
values survive conversion. A midpoint and denominator require at most 49
significand bits; their product fits exactly in binary64. Eight local
corrections precede a bounded binary search when the estimate is unusable.
Signs, signed zero, subnormal boundaries and the overflow boundary retain
their separate handling. Only four fog divisions change; texture projection
and renderer admission remain unchanged.

The complete unchanged pixel suite passes on native RTX, physical D3D12 RTX
and Mesa llvmpipe. An independent 65,536-case division fixture passes on all
three drivers, comparing 65,524 defined results bit for bit and checking the
classification of 12 invalid NaN cases separately. Captured fog pixels,
neighbors, full EFB/depth and fragment/bounding-box state match the CPU.
The shader SHA256 is
`28c18602a2b5b6aba26d180db245882cbb1e0892f1e484e147f2305b63c597f9`.
The exact proof and original failures remain in
`.cache/graphics-gameplay-20261004/fog-d3d12/immutable-v2/`.
Physical Linux D3D12 on the RTX 3060 also completes a 320-field Sunshine startup.
Every recorded raw frame and guest state matches the CPU renderer after the
three backend counters are excluded. The ordinary CLI and its checkpoint
observer match all 60 final values, including every renderer counter, plus
frame and storage bytes. The longer ordinary-input capture is still running.

## Ordered small rectangles

The renderer batches at most 16 rectangles of at most 256 pixels each. Their
union may contain at most 4,096 pixels and at most four times the sum of their
individual areas. Each GPU invocation owns one EFB pixel and applies admitted
records in order. Projection coordinates and cached samples reset for each
record; blend, depth and retained texture state keep their ordinary sequence.
State changes, texture revisions, framebuffer access and XFB operations flush
pending work. The original large-rectangle kernel remains byte-identical.

Raster dispatch statistics count actual GPU calls. A separate 4,094-record
logical cap preserves the existing latch-tag limit. XFB and clear dispatches
are excluded from the raster count. Compile-time checks require identical fog
helper text and all four call sites in both kernels.

Full suites, 4,176 independent boundary rectangles and captured small fog
neighbors pass on Windows RTX, Linux llvmpipe and physical Linux D3D12/RTX.
Deliberately broken ordering, projection and texture barriers fail the private
oracles. An initial stock-only captured-fog oracle used pixel bounds instead of
the required complete 2x2 quad bounds; its failed log remains recorded, and the
corrected expectation passes both renderers on all three drivers. The initial
reused executable and source bytes were overwritten before sealing and are not
claimed to remain reproducible.

A pair using the 224-file CPU/fog snapshot reaches Galaxy field 1,050. All 35
checkpoint graphics snapshots and raw frames, final graphics state, 2,124
non-dispatch JSON values and storage match. Actual raster calls fall from
240,351 to 112,118; accepted fragments remain 1,487,715,052 with 406 CPU fallback
rectangles. The private 563-file production packet, separate 54-file compile
guard packet and 296 execution files were rehashed before integration. Receipts
remain in `.cache/gpu-ordered-batch-20261005/`. The complete combined source
matrices and a full-speed gameplay result remain pending.

## Prepared texture uploads

Hardware cache fills decode complete texture tiles from the already validated
source span. Image dimensions, mip layout, bank selection and palette state are
computed once per image. The prepared decoder handles all eleven supported
formats, including split-bank RGBA8, indexed palettes and compressed blocks.
Short source or destination spans retain explicit validation. The original
scalar texture implementation remains byte-identical in its own translation
unit; normal CPU texture sampling keeps that path.

Strict MSVC checks compare 555,456 prepared and scalar texels exactly. They cover
mips, odd image sizes, RAM and manual texture memory, palettes and invalid spans.
The unchanged complete hardware suite passes on the RTX 3060 with the new
cache-fill path. The separate authored CPU-to-VI guest probe confirms three
physical raster batches and 768 shader fragments on both consoles, with exact
frame pixels, controller redraw and zero fallback rectangles.

Three alternating component rounds use the same CPU affinity and eleven
256-by-256 format captures. Every complete output matches. Prepared decoding
ratios range from 1.41 to 2.36 compared with repeated scalar decoding. Concurrent
gameplay and compilation affect these timings; demand-sampling controls vary
from 0.76 to 1.19 despite identical scalar source. This establishes a prepared
cache-fill measurement, with no stable demand or whole-game speed claim.
Exact sources, captures, raw samples and rejected slower experiments remain in
`.cache/gpu-texture-isolated-final-20261005/` and its recorded predecessor
directories.

## Exact division identities

Both raster shaders bypass estimated division when a finite numerator has a
signed unit divisor, or when finite nonzero operands have equal magnitudes.
The result follows directly from the operand bits. Existing guards still handle
zero, infinity and NaN first; other divisions retain midpoint correction.
This avoids correction work for common unit projection factors without changing
subnormal values or signed zero.

The normal hardware suite checks 25,165,824 signed cases around every exponent's
mantissa boundaries and midpoint. The optional
`--division-identities-exhaustive` mode checks all 17,179,869,184 signed unit and
equal-magnitude divisions, including special values. Expected values come from
independent host-written buffer inputs. Two deliberately corrupted oracles must
fail independently before the positive sweep counts. Both shaders' helper
bodies remain compared at compile time.

The `--division-rounding-only` mode exercises 71,796 cases, including unequal
finite operands, subnormal and overflow boundaries, signed special values and
2,048 deliberately poor estimates. An independent integer quotient/remainder
oracle supplies the expected bits. The test generates its inputs in C++; the
shaders receive operands and estimate seeds, while expected answers stay on the
host. Ordinary and forced-estimate programs each run with two output poison
fills and check the quotient, its complement and a row marker.

This focused gate passes in the Windows MSVC, Linux GCC and Linux Clang
ASan/UBSan builds, with leak detection enabled for the sanitizer run. Each run
checks 861,552 GPU output words. The normal suite and `--projection-only` also
include this gate. It exercises the estimate/correction path that the unit and
equal-magnitude identity sweeps bypass; complete fragment validation remains a
separate check.

The complete hardware renderer suites also pass in these three builds.

The exhaustive mode passes on Intel Iris Xe and Linux llvmpipe, including the
ASan/UBSan build. Full hardware and hardware guest-video tests also pass on
Windows and Linux. These checks establish exact arithmetic for the tested
identities; [performance measurements](performance.md) record the separate
component timings.

The same frozen `59edc60` core also passes the full hardware renderer,
GameCube/Wii hardware guest-video suite and exhaustive signed division sweep
on `NVIDIA RTX 2000 Ada Generation Laptop GPU/PCIe/SSE2`. Separate executables
add only the two adapter exports; their PE tables contain both 32-bit values
set to one. A paired context probe selects Intel Iris Xe without the requests
and NVIDIA with them. Compiler, core, test-object, executable and log hashes
remain in `.cache/continuation-20261022/tev-count-study/vendor-gates/`.
These checks cover the recorded synthetic workloads. Commercial gameplay
speed and AMD hardware remain unverified.

The rebuilt shipping core supplies both exports without a separate export
object. Its naturally linked probe selects NVIDIA, and the rebuilt full
hardware and hardware guest-video suites pass in 65.53 and 30.91 seconds.
The rebuilt exhaustive sweep also passes. Four stock CLI runs compare the
default Intel binary with this NVIDIA-requesting binary across both supplied
games. Every reported state value, raw frame byte and persisted file matches
at the existing 320-field Sunshine and 76-field Galaxy bounds. Both binaries
retain `59edc60` metadata; source and PE receipts identify the added requests.
See `gpu-hint-final/` and `windows-gpu-hint-stock-pairs/` under the same private
artifact root. Adapter choice does not by itself establish a speed gain.

## CPU buffer visibility

Software texture and XFB copies synchronize the EFB once before encoding.
Their pixel callbacks read the synchronized CPU buffers directly.
Copy-trigger clear uses the same buffers after encoding, preserving color,
alpha and depth masks. CPU EFB peeks still synchronize before reading.

Software raster fragments also read their destination directly. The draw path
drains queued GPU work before a software fallback begins, including a fallback
inside an otherwise eligible draw. No new GPU command is submitted between
these fragment reads. This avoids repeated synchronization and statistics
construction for every copied or software-shaded pixel.

## GPU texture copies and bounding-box resets

Texture copies support all thirteen tile formats with the same checked layout
plan as the CPU encoder. The shader preserves six-bit averaging, depth-half
channel order and intensity conversion. Each invocation owns one output word;
the host reads only the compact encoded rows. Applying those rows in order
preserves overlapping strides. RAM becomes visible before optional masked
clear. Source padding, destination validation and the software diagnostics
retain their existing contracts.

Copies retain pending fragment counts, bounding boxes and traversal latches.
Later GPU draws, texture-cache checks and CPU fallback observe the normal
ordered state. The encoding program compiles on first use and restores the
already initialized scalar fragment program, or program zero.

BP bounding-box resets retire queued rectangles before replacing the selected
coordinate pair. They reset only that axis's GPU accumulation words, preserving
the other axis, accepted-fragment count and retained shader keys. This avoids
a full EFB readback while preventing earlier fragments from extending newly
written bounds. Repeated and fully masked BP writes still perform the reset.

## Behavior mappings

The behavioral checkout is pinned at
`562875dd60fd0ff50b65dbbb611a2096802cd1b9`. Paths below are relative to its
`Source/Core/` directory. The implementation uses the product's existing
validated graphics modules and an independently written compute shader.

| Product implementation | Behavioral files and functions | Behavior read and translated |
| --- | --- | --- |
| Windows graphics adapter requests | `VideoCommon/VideoBackendBase.cpp`, executable data exports | Driver-recognized 32-bit requests for the high-performance graphics adapter. |
| `HardwareRenderer`, context and dispatch ordering | `VideoBackends/OGL/OGLGfx.cpp`, `DispatchComputeShader`; `VideoCommon/VertexManagerBase.cpp`, `DrawCurrentBatch`, `UploadUniforms` | Shader dispatch, framebuffer ordering and state upload. |
| `RasterWork` and compute coverage/interpolation | `VideoBackends/Software/Rasterizer.cpp`, `Clipper.cpp`; `VideoCommon/VertexShaderGen.cpp`, `GenerateVertexShaderCode` | Fixed-point edge tests, interpolation planes, depth range and sample placement. |
| GPU TEV combiner, alpha and depth textures | `VideoBackends/Software/Tev.cpp`, `Draw`, `DrawColorRegular`, `DrawColorCompare`, `DrawAlphaRegular`, `DrawAlphaCompare` | Stage sequencing, integer arithmetic, retained texture samples, comparisons and depth-texture ordering. |
| GPU indirect maps, matrices, bump alpha and retained state | `VideoBackends/Software/Tev.cpp`, `Indirect`, `Draw`, `WrapIndirectCoord`; `VideoBackends/Software/Rasterizer.cpp`, `CalculateLOD`, `Draw`; `VideoCommon/PixelShaderGen.cpp`, `WriteStage` | Map order and scale, signed matrix arithmetic, 24-bit coordinate wrapping, per-stage bump alpha, traversal latches and documented differences between behavioral paths. |
| GPU fog, quantized curve tables and viewport-width transfer | `VideoBackends/Software/Tev.cpp`, `Draw`; `VideoCommon/BPMemory.h`, `FogParams`, `FogRangeKElement`; `VideoCommon/PixelShaderGen.cpp`, `WriteFog`; `VideoCommon/PixelShaderManager.cpp`, `SetConstants`, `SetFogParamChanged` | Perspective/orthographic depth, all curve selectors, center and coefficient interpolation, paired infinities, and final integer color weighting. The compute path follows the validated CPU factor truncation and 24-bit depth scale; the generated reference shader rounds the factor and uses a 2^24 depth scale. |
| GPU projective texture-coordinate precision | `VideoBackends/Software/Rasterizer.cpp`, `Slope::GetValue`, `BuildBlock`, `Draw`, `CalculateLOD`; `VideoBackends/Software/Tev.cpp`, `Draw`; `VideoBackends/Software/TextureSampler.cpp`, `Sample`, `SampleMip` | Preserve separately rounded float reciprocal, Q product, quotient and S/T products before signed 17.7 conversion and derivative LOD. Integer quotient/remainder rounding avoids approximate native division. Extreme staged intermediates retain CPU execution. |
| Decoded texture cache and GPU sampling | `VideoBackends/Software/TextureSampler.cpp`, `Sample`, `SampleMip`; `VideoCommon/TextureCacheBase.cpp`, `LoadImpl`, `GetTexture`, `GetSamplerState`, `BindTextures`; `VideoCommon/TextureInfo.cpp`, `FromStage`; `VideoCommon/TextureDecoder_Common.cpp`, `TexDecoder_GetPaletteSize`; `VideoCommon/VertexManagerBase.cpp`, `Flush` | Texture decoding, reachable source/palette extents, independent sampler state, wraps, fixed filtering weights, mip selection and exact content invalidation. |
| Raw EFB storage, GPU blending and CPU access | `VideoBackends/Software/SWEfbInterface.cpp`, `BlendTev`, `SetColor`, `SetDepth`, `ZCompare`; `VideoCommon/EFBInterface.cpp`, `HardwareEFBInterface::PokeColor`, `PokeDepth`; `VideoCommon/FramebufferManager.cpp`, `PokeEFBColor`, `PokeEFBDepth`; `Core/PowerPC/MMU.cpp`, `EFB_Write` | Pixel packing, blend/logic masks, depth ordering, ARGB pokes, bounds and unconditional poke stores. |
| GPU XFB encoding, packed readback and copy-trigger clear | `VideoBackends/Software/SWEfbInterface.cpp`, `EncodeXFB`, `SetColor`, `SetDepth`; `VideoBackends/Software/EfbCopy.cpp`, `ClearEfb`; `VideoCommon/TextureCacheBase.cpp`, `GetRAMCopyFilterCoefficients`, `CopyRenderTargetToTexture`; `VideoCommon/TextureConversionShader.cpp`, `WriteXFBEncoder`; `VideoCommon/FramebufferManager.cpp`, `FlushEFBPokes`, `InvalidatePeekCache` | Filter coefficients, gamma quantization, YUYV packing, scaled staging rows, RAM visibility and clear ordering. The compute path retains the validated software chroma weights where the generated encoder differs. |
| GPU tiled texture copies | `VideoBackends/Software/TextureEncoder.cpp`, `Encode` and color/depth encoders; `VideoBackends/Software/EfbCopy.cpp`, `ClearEfb`; `VideoCommon/BPStructs.cpp`, copy trigger handling | Tile layout, six-bit filtering, intensity, depth-half channel order, compact RAM staging and masked clear ordering. |
| Queued bounding-box resets | `VideoCommon/BPStructs.cpp`, `BPWritten`, clear-bounds cases and pipeline flush; `VideoCommon/BoundingBox.cpp`, `Set`, `Flush` | Complete earlier raster work before replacing either coordinate pair, including repeated writes. |

See [graphics behavior mappings](graphics-reference.md) for the independent
CPU differential tests that establish the reused decoding, rasterization,
fragment and copy behavior.

## Extreme projected coordinates

Projected texture admission leaves extreme staged reciprocals, Q products and
projection factors on the CPU. Integer exponent checks retain a normal-value
margin without adding floating-point products. A small final coordinate alone
cannot establish that every intermediate stays normal on the host GPU.

When the continuous-range guard declines a small rectangle, an additional check
can validate its actual samples. It visits at most 64 even-aligned 2x2 quads,
including uncovered neighbors needed for derivatives. All generated texture
coordinates retain separate reciprocal, Q multiplication, projection division,
S/T multiplication and derivative stages. A pole between samples can therefore
remain on the GPU when every sampled stage is safe. Zero Q keeps its reciprocal
branch. Invalid bounds, inexact integer offsets, unsafe intermediates and
extreme unused coordinates retain CPU execution. The check preserves the
caller's floating-point controls and sticky flags.

The patterned regression covers 124 signed, used/unused-coordinate,
scalar/batch and exponent-neighbor cases. Thirty-two remain GPU eligible and
92 decline. A serialized four-draw sequence checks queued GPU work, large-Q
CPU fallback, retained-texture consumption, later GPU work and all 512 copied
bytes. These authored cases reproduce and correct native-GPU discrepancies;
their commercial-game frequency and performance impact remain unqualified.

## Batch handoff and empty coverage

Synchronization gathers counters and the selected retained texture samples into
one 19-word GPU packet. The gather restores the current program and storage
binding before returning to drawing. Null-recipient drains reset the counters,
so a later clear-only handoff preserves the recipient's texture state.

Coverage validation bounds rectangle coordinates, edge steps and the signed
edge base before arithmetic. A proven outside edge or an exact empty scan of
at most 256 sample positions can finish before texture projection. Depth, color
and fog diagnostics retain their earlier checks. Covered work still obeys the
staged projection guard. These paths have component regression coverage; matched
gameplay and sustained performance require separate validation.

A fixed 8,000-field GameCube replay matches the preceding build's final CPU/RAM
and guest-audio hashes, final frame and stored files. Hardware work counters
change with the batch boundaries. Per-frame equality, endpoint audio, completed
levels and sustained full-speed gameplay still require separate qualification.
