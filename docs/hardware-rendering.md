# Hardware rendering

`--graphics opengl` selects the OpenGL 4.3 compute renderer. The default
`--graphics software` uses the validated CPU renderer. Both choices render the
guest's GX commands into the same 640 by 528 EFB. The hardware backend uses a
hidden SDL OpenGL context, so it also works with `--headless` when the host has a
video driver that can create that context. A `CUPID_HEADLESS_ONLY` build excludes
SDL and reports hardware rendering as unavailable.

CPU code decodes vertices, transforms and lights them, assembles primitives,
clips them, applies culling, and prepares interpolation planes. Compute shaders
then evaluate fixed-point triangle coverage and fragment inputs, run TEV and
alpha/depth tests, blend the result, and store raw color/depth EFB bits. Triangle
dispatches have storage barriers to preserve GX command order. This backend
does not yet move vertex transforms or primitive setup onto the GPU.

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

CPU reads, CPU pokes, texture copies, bounding-box reads and software fallback
synchronize pending EFB writes. Software writes
mark the GPU copy stale, and the next hardware rectangle uploads the current
raw EFB arrays. Format changes reinterpret the same stored color bits.

Enabled texture sampling also writes a retained TEV texture-state record.
Separate GPU counters select the last texture sample, indirect coordinates and
each enabled indirect map in the CPU rasterizer's traversal order. Disabled
stages keep earlier samples even when several draws remain pending. Alpha and
late depth rejection still update those latches; early depth rejection leaves
them intact. Synchronization restores the complete sampled state to the CPU
shader before a continuation that needs retained values.

Color and depth pokes obey the CPU framebuffer address selectors and update
their selected EFB pixels independently of GX blend/depth write masks.
Out-of-range pixels are ignored. Combined color/depth accesses remain
unsupported. Texture copies run the checked CPU encoder after readback.

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

## Behavior mappings

The behavioral checkout is pinned at
`562875dd60fd0ff50b65dbbb611a2096802cd1b9`. Paths below are relative to its
`Source/Core/` directory. The implementation uses the product's existing
validated graphics modules and an independently written compute shader.

| Product implementation | Behavioral files and functions | Behavior read and translated |
| --- | --- | --- |
| `HardwareRenderer`, context and dispatch ordering | `VideoBackends/OGL/OGLGfx.cpp`, `DispatchComputeShader`; `VideoCommon/VertexManagerBase.cpp`, `DrawCurrentBatch`, `UploadUniforms` | Shader dispatch, framebuffer ordering and state upload. |
| `RasterWork` and compute coverage/interpolation | `VideoBackends/Software/Rasterizer.cpp`, `Clipper.cpp`; `VideoCommon/VertexShaderGen.cpp`, `GenerateVertexShaderCode` | Fixed-point edge tests, interpolation planes, depth range and sample placement. |
| GPU TEV combiner, alpha and depth textures | `VideoBackends/Software/Tev.cpp`, `Draw`, `DrawColorRegular`, `DrawColorCompare`, `DrawAlphaRegular`, `DrawAlphaCompare` | Stage sequencing, integer arithmetic, retained texture samples, comparisons and depth-texture ordering. |
| GPU indirect maps, matrices, bump alpha and retained state | `VideoBackends/Software/Tev.cpp`, `Indirect`, `Draw`, `WrapIndirectCoord`; `VideoBackends/Software/Rasterizer.cpp`, `CalculateLOD`, `Draw`; `VideoCommon/PixelShaderGen.cpp`, `WriteStage` | Map order and scale, signed matrix arithmetic, 24-bit coordinate wrapping, per-stage bump alpha, traversal latches and documented differences between behavioral paths. |
| GPU fog, quantized curve tables and viewport-width transfer | `VideoBackends/Software/Tev.cpp`, `Draw`; `VideoCommon/BPMemory.h`, `FogParams`, `FogRangeKElement`; `VideoCommon/PixelShaderGen.cpp`, `WriteFog`; `VideoCommon/PixelShaderManager.cpp`, `SetConstants`, `SetFogParamChanged` | Perspective/orthographic depth, all curve selectors, center and coefficient interpolation, paired infinities, and final integer color weighting. The compute path follows the validated CPU factor truncation and 24-bit depth scale; the generated reference shader rounds the factor and uses a 2^24 depth scale. |
| Decoded texture cache and GPU sampling | `VideoBackends/Software/TextureSampler.cpp`, `Sample`, `SampleMip`; `VideoCommon/TextureCacheBase.cpp`, `LoadImpl`, `GetTexture`, `GetSamplerState`, `BindTextures`; `VideoCommon/TextureInfo.cpp`, `FromStage`; `VideoCommon/TextureDecoder_Common.cpp`, `TexDecoder_GetPaletteSize`; `VideoCommon/VertexManagerBase.cpp`, `Flush` | Texture decoding, reachable source/palette extents, independent sampler state, wraps, fixed filtering weights, mip selection and exact content invalidation. |
| Raw EFB storage, GPU blending and CPU access | `VideoBackends/Software/SWEfbInterface.cpp`, `BlendTev`, `SetColor`, `SetDepth`, `ZCompare`; `VideoCommon/EFBInterface.cpp`, `HardwareEFBInterface::PokeColor`, `PokeDepth`; `VideoCommon/FramebufferManager.cpp`, `PokeEFBColor`, `PokeEFBDepth`; `Core/PowerPC/MMU.cpp`, `EFB_Write` | Pixel packing, blend/logic masks, depth ordering, ARGB pokes, bounds and unconditional poke stores. |
| GPU XFB encoding, packed readback and copy-trigger clear | `VideoBackends/Software/SWEfbInterface.cpp`, `EncodeXFB`, `SetColor`, `SetDepth`; `VideoBackends/Software/EfbCopy.cpp`, `ClearEfb`; `VideoCommon/TextureCacheBase.cpp`, `GetRAMCopyFilterCoefficients`, `CopyRenderTargetToTexture`; `VideoCommon/TextureConversionShader.cpp`, `WriteXFBEncoder`; `VideoCommon/FramebufferManager.cpp`, `FlushEFBPokes`, `InvalidatePeekCache` | Filter coefficients, gamma quantization, YUYV packing, scaled staging rows, RAM visibility and clear ordering. The compute path retains the validated software chroma weights where the generated encoder differs. |

See [graphics behavior mappings](graphics-reference.md) for the independent
CPU differential tests that establish the reused decoding, rasterization,
fragment and copy behavior.
