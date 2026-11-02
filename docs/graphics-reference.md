# Graphics behavior mappings

Behavioral checkout pin: `562875dd60fd0ff50b65dbbb611a2096802cd1b9`. Paths below are relative to its `Source/Core/` directory. Product source is implemented in its own modules; optional comparison targets compile the unchanged external functions as independent oracles.

| Product path | Behavioral files and functions | Checked behavior |
| --- | --- | --- |
| `src/gfx/command_processor.cpp`, `gather`, `consume_fifo`, `decode` | `Core/HW/GPFifo.cpp`, `VideoCommon/CommandProcessor.cpp`; `VideoCommon/OpcodeDecoding.cpp`, `RunFifo`, `OnDisplayList`; `Core/HW/Memmap.cpp`, `GetSpanForAddress`, `GetPointerForRange` | Gather bursts, FIFO state, opcode lengths, fragmented uploads, indexed XF data, and display lists with physical RAM pointer aliases. |
| `CommandProcessor::write_bp`, PE register access | `VideoCommon/BPStructs.cpp`, `LoadBPReg`, `BPWritten`; `VideoCommon/PixelEngine.cpp` | One-use masks, TEV and konst register types, TLUT/preload transfers, delayed token/finish events, and interrupt acknowledgements. |
| `src/gfx/vertex.cpp` | `VideoBackends/Software/TransformUnit.cpp`; `VideoCommon/VertexLoader*`, `XFMemory.h` | Attribute interpretation, matrices, projection, lighting, and texgen. Detailed map: [vertex transforms](vertex-reference.md). |
| `src/gfx/rasterizer.cpp` | `VideoBackends/Software/Clipper.cpp`, `Rasterizer.cpp`; `VideoCommon/BPFunctions.cpp` | Clipping, primitive assembly, sample coverage, interpolation, culling, and scissoring. Detailed map: [rasterization](rasterizer-reference.md). |
| `src/gfx/texture.cpp` | `VideoCommon/TextureDecoder_Common.cpp`, texel and TMEM decoders; `VideoBackends/Software/TextureSampler.cpp`, `Sample`, `SampleMip` | Tile layouts, palettes, CMPR endpoints, wrap modes, seven-bit bilinear weights, and mip selection. |
| `src/gfx/fragment.cpp` | `VideoBackends/Software/Tev.cpp`, `SetRasColor`, `Indirect`, `DrawColorRegular`, `DrawColorCompare`, `DrawAlphaRegular`, `DrawAlphaCompare`, `Draw`; `VideoCommon/BPMemory.cpp` | TEV arithmetic, signed registers, disabled-texture latches, swizzles, indirect coordinates, depth textures, alpha tests, and fog. |
| `src/gfx/hardware_shader.inc`, `HardwareRenderer::begin`, `HardwareRenderer::draw`, `fog_factor` | `VideoBackends/Software/Tev.cpp`, `Draw`; `VideoCommon/BPMemory.h`, `FogParams`, `FogRangeKElement`; `VideoCommon/PixelShaderGen.cpp`, `WriteFog`; `VideoCommon/PixelShaderManager.cpp`, `SetConstants` | GPU linear and exponential fog, signed parameters, range coefficients and viewport width. Exact GPU/CPU profiles and quantization boundaries are recorded in [hardware rendering](hardware-rendering.md). |
| `CommandProcessor::fragment`, `color`, `write_color`, copy clear | `VideoBackends/Software/SWEfbInterface.cpp`, `BlendTev`, `SetColor`, `SetDepth`, `ZCompare`, pixel packing helpers; `VideoBackends/Software/EfbCopy.cpp`, `ClearEfb` | Raw EFB formats, depth ordering, blends and logic, channel masks, dithering, and clear state. |
| `CommandProcessor::read_efb`, physical memory bridge | `Core/PowerPC/MMU.cpp`, `EFB_Read`, `ReadFromHardware`, `DMA_MemoryToLC`; `VideoCommon/EFBInterface.cpp`, `PeekColor`, `PeekDepth`, `ShouldSkipAccess`; `VideoBackends/Software/SWEfbInterface.cpp`, `PeekColorInternal`, `PeekDepthInternal`; `VideoCommon/PixelEngine.h`, `UPEAlphaReadReg` | Address selectors and aliases, ARGB packing, PE alpha modes, depth, typed reads, and locked-cache DMA. Scope and unsupported writes: [CPU EFB access](efb-access.md). |
| `src/gfx/efb_copy.cpp` | `VideoBackends/Software/TextureEncoder.cpp`, `Encode`, all color and Z24 encoders; `VideoCommon/BPStructs.cpp`, copy trigger handling | Color/depth copy targets, block traversal, half-scale averaging, intensity conversion, stride and channel-order quirks. Detailed map: [texture copies](efb-copy.md). |
| `src/gfx/xfb_copy.cpp`, copy trigger | `VideoBackends/Software/SWEfbInterface.cpp`, `EncodeXFB`, `VerticalFilter`, `GammaCorrection`, `ConvertColorToYUV`; `VideoCommon/BPFunctions.cpp`, `CopyRegion`; `VideoCommon/BPStructs.cpp`, copy trigger handling | Filter/gamma/YUYV conversion, vertical scaling, compact intermediate storage, and staging readback. Direct actual encoder comparisons and explicit undefined-footprint bounds are recorded in [XFB copies](xfb-copy.md). |
| VI and host presentation | `Core/HW/VideoInterface.cpp`, field output and `GetAspectRatio`; `VideoCommon/Present.cpp`, `CalculateDrawAspectRatio`, `UpdateDrawRectangle`; `VideoCommon/TextureDecoder_Common.cpp`, `TexDecoder_DecodeXFB` | Timing-derived active-area aspect, standard/widescreen/raw-pixel modes, RAM framebuffer decoding, and fitted host display. The host fit uses fractional SDL rectangles, with no cropping or geometry changes. |

## Independent comparisons

`tests/vertex_reference_checks.cpp`, `rasterizer_reference_checks.cpp`, `texture_reference_checks.cpp`, `fragment_reference_checks.cpp`, `efb_copy_reference_checks.cpp`, and `xfb_copy_reference_checks.cpp` supply original generated inputs and compare outputs with separately compiled target functions. Adapter code supplies memory and captures outputs; it does not replace the compared computations.

The texture comparison covers all implemented encodings and palette formats, RAM mip decoding, TMEM banks, and millions of base-level nearest/bilinear samples. The fragment comparison checks exact RGBA, discard decisions, and final depth across regular, comparison, indirect, depth-texture, and fog profiles. These scopes do not imply that every combination of filtering, LOD, multisampling, or framebuffer-copy geometry has been validated.

Some source edge paths perform undefined host operations. The rasterizer retains a valid prior plane for a zero-area setup instead of converting a non-finite slope to an integer. Indirect coordinate arithmetic uses wider signed multiplication and explicit low-bit wrapping. Texture-copy padding beyond the physical EFB fails explicitly. Each decision is recorded alongside the owning module's test scope.

Invalid graphics RAM ranges stop with an explicit graphics diagnostic. This is the product's checked-range policy for malformed GPU commands, rather than a claim that the behavioral reference uses the same host error handling. A nested graphics memory fault is kept separate from the CPU store that supplied FIFO bytes, so it cannot create a false guest DSI.


## Fixed texture coordinates

The guarded coordinate conversion was checked against revision
`2c805fee8f232d090c454381bb1b8422242345e1`:
`VideoBackends/Software/Rasterizer.cpp`, `Draw`, scales UVs by 128 before
integer conversion; `Tev.cpp`, `Draw` and `Indirect`, consume those fixed
coordinates. The product retains its explicit signed-24 wrapping and defined
fallback for finite values outside the direct conversion range.

`src/gfx/fixed_coordinate.hpp` keeps the existing double scaling and truncation.
When the truncated value is within one signed modulus, the remainder is
unnecessary and conversion is safe. Wrapped values still use the original
modular call, preserving its host floating-point status behavior. An initial
wider guard produced identical coordinate values but changed MSVC inexact
flags; it was rejected rather than weakening the floating-environment checks.

The public `fixed_coordinate` suite compares 1,051,833 raw/structured inputs
against the retained earlier algorithm, checks 104,224 rounding/flush/sticky-flag
combinations, and checks 3,240 complete coordinate pairs through the actual
fragment shader. It covers fractional values, signed zeros, subnormals, modulus
and conversion seams, maximum finite values, infinities and NaNs. These checks
pass under MSVC Release, GCC Release and Clang 18 ASan/UBSan. The retained
algorithm is a product regression oracle; these results are distinct from
external-source or physical-device comparisons.

## Manual texture-memory mipmaps

The manual-mip layout follows the accelerated texture path at revision
`2c805fee8f232d090c454381bb1b8422242345e1`:
`VideoCommon/TextureInfo.cpp`, its constructor, `GetMipMapLevels`, `MipLevel`
constructor and iterator; `VideoCommon/TextureCacheBase.cpp`, the base-level
and subsequent mip decode/upload calls; and the existing
`VideoCommon/TextureDecoder_Common.cpp` decoders.

Level zero starts at the even-bank address. Subsequent levels alternate banks,
advancing each bank by its preceding full, tile-aligned source levels. Dimensions
halve with a floor and stop at one; the final level is bounded by the largest
base dimension. RGBA8 splits only its base level between AR and GB banks. Later
levels use ordinary interleaved RGBA8 tiles. In particular, the even-bank iterator
advances past the full encoded base size before level two, not half that size.

This is an explicit compatibility choice for both product renderers. The pinned
standalone software sampler instead advances its first source span consecutively
and does not advance the separate RGBA8 odd span. Its non-base manual-mip behavior
is not the target here. Existing RAM mip and base-level manual sampling behavior
is unchanged.

The optional texture comparison compiles the actual layout constructor and mip
iterator against their original class declaration, then compares demanded and
prepared texels with the actual decoder. All 944,064 new comparisons pass across
all eleven encodings, eight units, three palette encodings, power-of-two and
uneven dimensions, and three generated input patterns. Digest:
`5341134971038465509`. Existing 2,741,760 texel and 3,290,112 sampling comparisons
also pass. Layout diagnostics are excluded from these valid-source comparisons;
public tests separately check bank bounds, partial writes and maximum-level
clamping. They reject the preceding implementation's unsupported-mip path.

The OpenGL fixture compares 276 manual-mip samples and cache transitions with
software output, without CPU fallback for the valid draws. It covers small
batched rectangles and rectangles above the direct-compute threshold, nearest
and bilinear filtering, trilinear blending, terminal LOD, uneven sizes, all
encodings, odd-bank address changes and each bank's invalidation boundaries.
It also checks that an out-of-range bank declines before GPU submission. The
local run and the full renderer suite pass using an offscreen Mesa llvmpipe
context. Fragment interlock was unavailable, so this validates compute execution,
not a physical GPU's speed or ordered-raster execution.
