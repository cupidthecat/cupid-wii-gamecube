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
alpha tests; all depth comparisons and early/late depth writes; depth textures;
orthographic and perspective linear fog;
all blend factors, subtraction and logic operations; color/alpha masks;
constant alpha; and RGBA6 dithering.

Textures use the existing checked tile decoder for I4, I8, IA4, IA8, RGB565,
RGB5A3, RGBA8, C4, C8, C14X2 and CMPR. Decoded pixels and mip offsets are uploaded
to shader storage. GPU sampling applies the console's fixed-point coordinates,
wrap rules, seven-bit bilinear weights, derivative-based LOD, point mip
selection and trilinear blending. Base-level manual TMEM textures and split-bank
RGBA8 preloads use the same path. Cache hits require exact register and source
byte equality; the byte hash only avoids unnecessary comparisons after a
definite mismatch. Cache revisions control GPU uploads.

Exponential fog, fog range adjustment, indirect texture stages and retained texture operands before an enabled
sample use the CPU path. So do manual mip chains, mip ranges beyond the checked
ten-level limit, interpolation poles, non-finite planes, and triangles whose
edge equations exceed the shader's checked 32-bit range. Prefetching an unmapped
texture falls back to demand sampling, which preserves the CPU diagnostic if a
fragment actually accesses those bytes. Existing unsupported software states,
including multisample coverage, still fail explicitly.

Depth-texture operations without an enabled sample also fall back, since they
can read the raw texture value retained from an earlier draw. Invalid fog
parameters and potentially undefined zero-divided-by-zero fog calculations
retain CPU diagnostics.

The final JSON includes hardware dispatch, fragment and fallback-rectangle
counts. These counters identify work executed through OpenGL;
an OpenGL software driver can produce them too. Check the reported renderer
before treating them as evidence of physical GPU acceleration.

## EFB visibility

CPU reads, CPU pokes, framebuffer copies, copy-triggered clears, bounding-box
reads and software fallback synchronize pending EFB writes. Software writes
mark the GPU copy stale, and the next hardware rectangle uploads the current
raw EFB arrays. Format changes reinterpret the same stored color bits.

Enabled texture sampling also writes a retained TEV texture-state record. A
GPU counter selects the last shaded pixel in the CPU rasterizer's traversal
order, including fragments rejected by alpha. Synchronization restores that
sampled state to the CPU shader, so later disabled-texture fallback sees the
same latched color. Early depth rejection leaves the latch intact.

Color and depth pokes obey the CPU framebuffer address selectors and update
their selected EFB pixels independently of GX blend/depth write masks.
Out-of-range pixels are ignored. Combined color/depth accesses remain
unsupported. Texture and XFB copies still run the checked CPU encoders after
readback.

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

On October 4, 2026, the expanded suite passed under WSL with Mesa
`llvmpipe (LLVM 20.1.2, 256 bits)`: 1,799 dispatches and 2,789,052 shader fragments.
The optional benchmark verified all pixels after 64 full EFB quads. Its CPU
rendering time was 2.413 seconds and its OpenGL time was 2.969 seconds. This
software-driver result validates execution and pixel equivalence; physical GPU
performance needs a separate driver run. Related graphics, fragment,
rasterizer and CPU EFB guest tests also passed in that Linux build.

These synthetic checks do not establish commercial-game rendering, full-speed
gameplay, audio, input or saving.

The expanded suite also passed on Windows with
`NVIDIA GeForce RTX 3060/PCIe/SSE2`: 1,799 dispatches and 2,789,052 shader fragments.
The final 64-quad benchmark took 3.060 seconds on the CPU path and 0.0826 seconds
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
| Decoded texture cache and GPU sampling | `VideoBackends/Software/TextureSampler.cpp`, `Sample`, `SampleMip`; `VideoCommon/TextureCacheBase.cpp`, `GetXFBFromCache`; `VideoCommon/TextureDecoder_Common.cpp` | Texture decoding, wraps, fixed filtering weights, mip selection and content invalidation. |
| Raw EFB storage, GPU blending and CPU access | `VideoBackends/Software/SWEfbInterface.cpp`, `BlendTev`, `SetColor`, `SetDepth`, `ZCompare`; `VideoCommon/EFBInterface.cpp`, `HardwareEFBInterface::PokeColor`, `PokeDepth`; `VideoCommon/FramebufferManager.cpp`, `PokeEFBColor`, `PokeEFBDepth`; `Core/PowerPC/MMU.cpp`, `EFB_Write` | Pixel packing, blend/logic masks, depth ordering, ARGB pokes, bounds and unconditional poke stores. |

See [graphics behavior mappings](graphics-reference.md) for the independent
CPU differential tests that establish the reused decoding, rasterization,
fragment and copy behavior.
