# Primitive assembly and rasterization

`Rasterizer` accepts BP/XF state and transformed vertices. Its fragment callback receives EFB coordinates, 24-bit depth, interpolated colors, projected texture coordinates and derivatives. The caller owns shading, alpha/depth tests and blending. Keep the rasterizer alive across commands: its remembered depth plane implements zfreeze. `reset` restores the initial plane.

The behavioral reference is pinned at `562875dd60fd0ff50b65dbbb611a2096802cd1b9`. Paths below are relative to that checkout. The implementation uses this project's vertex and fragment interfaces.

| Behavior | Reference implementation and callers | Local implementation |
| --- | --- | --- |
| Skipped vertices and compacted primitive inputs | `Source/Core/VideoCommon/VertexLoader.cpp`: `SkipVertex`; `VertexLoaderManager.cpp`: `RunVertices`, `AddIndices(num_loaded)` | `draw` |
| Quads, trailing triangle, lists, strips and fans | `Source/Core/VideoCommon/IndexGenerator.cpp`: primitive index generators; `VideoBackends/Software/SetupUnit.cpp` | `draw` |
| Homogeneous clipping, culling, interpolated clipping colors | `Source/Core/VideoBackends/Software/Clipper.cpp`: `CalcClipMask`, `ClipTriangle`, `ProcessTriangle`; `NativeVertexFormat.h`: `Lerp` | `triangle`, `interpolate` |
| Line and point expansion, texture offsets | `Source/Core/VideoBackends/Software/Clipper.cpp`: `ClipLine`, `ProcessLine`, `ProcessPoint` | `line`, `point` |
| Wrapped scissor ranges and draw order | `Source/Core/VideoCommon/BPFunctions.cpp`: `ComputeScissorRanges`, `ScissorResult` | `scissors` |
| Fixed coverage, slope sampling, zfreeze and 2×2 texture derivatives | `Source/Core/VideoBackends/Software/Rasterizer.cpp`: `DrawTriangleFrontFace`, `Slope`, `BuildBlock`, `UpdateZSlope` | `rasterize`, `update_depth`, `Plane` |

Skipped indexed vertices are removed before assembly; they do not restart strips. Quads accept a trailing set of three vertices as a triangle. Triangle clipping follows the reference CPU path, including its positive-Z mask and W-plane compatibility behavior. Disabling clipping allows an unclipped polygon when all clip W values are nonnegative. Points follow the reference's expansion without homogeneous clipping. Zero W and non-finite coordinates raise explicit errors where division or integer conversion would be undefined.

Coverage rounds to 28.4 coordinates, applies the reference's minus-nine adjustment, and uses its edge inclusion rule. Attribute slopes use the 0.495 sample offset. Colors interpolate affinely and truncate after clamping. Texture coordinates receive BP scales before clipping, then interpolate as STQ divided by clip W. Derivatives are differences from the same aligned 2×2 block for every covered pixel in that block. Lines use horizontal or vertical caps according to their dominant axis; line/point widths and optional texture offsets come from BP state.

Scissor rectangles can wrap in both axes. Each valid wrapped region is rendered in the reference's viewport-area order. The depth plane updates even when culling or scissoring rejects a primitive, and it remains unchanged after trivial clip rejection or a zero-area primitive. The zero-area guard avoids a non-finite slope followed by undefined float-to-integer conversion in the reference CPU path. This is a defined local behavior, not a claim that an undefined host result reflects console behavior.

`tests/rasterizer_tests.cpp` passed native Windows GCC with warnings treated as errors and Linux GCC with address and undefined-behavior sanitizers. It checks exact small-triangle and quad coverage, single shared-edge writes, compacted skipped input, strip/fan assembly, color/depth slopes, projected STQ, BP scales, 2×2 derivatives, scissor rejection/wrapping, clipping, zfreeze persistence and reset, line/point coverage and texture offsets. Multisample coverage remains unfinished and raises an explicit error.

`tests/rasterizer_reference_checks.cpp` links the actual pinned `Clipper.cpp`, `Rasterizer.cpp` and `BPFunctions.cpp`, with a test collector replacing downstream shading. It passed 81,357 emitted sample comparisons over 10,000 generated triangle, line and point commands, comparing coordinates, depth, both color channels and eight texture inputs at their s17.7 transfer boundary. The corpus varies clipping, culling, scissoring, W, vertex coordinates, BP texture scales, line/point sizes and texture offsets, and frozen depth. Each case starts from reset state; frozen cases first seed a defined depth plane. This avoids carrying the reference's undefined zero-area slope result into later cases. No failed case is counted as a successful comparison.

The optional harness requires the external `Source/Core/` include directory, C++23, fmt and strict floating options. Test-only backend hooks fail if unrelated application entry points retained by a linker are called. Function-section garbage collection can discard those unused paths. It does not vendor reference files. These checks cover the reference CPU path; real hardware rasterization has not been validated.
