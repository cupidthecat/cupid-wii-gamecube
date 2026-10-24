# EFB texture copies

`gfx::copy_efb_texture` encodes the current BP copy registers into physical RAM. The caller
supplies canonical `0xRRGGBBAA` colors and unsigned 24-bit depth through `EfbCopySource`.
The encoder returns the destination, logical and padded dimensions, block-row count,
bytes per block row, and configured stride. Command processing owns copy triggers and
the subsequent optional EFB clear; XFB conversion uses its separate path.

`gfx::plan_efb_texture` validates the complete layout without sampling or
changing RAM. The OpenGL backend uses that plan to encode tiles directly from
GPU EFB storage and reads back only the encoded bytes. It preserves the CPU
encoder's row ordering when strides overlap and performs masked GPU clear
after RAM delivery. The ordinary callback encoder retains its validation,
callback and error ordering.

BP `0x43` selects the source representation. RGB8 supplies opaque alpha. RGBA6 restores
the six-bit channels before either expansion or filtering. The RGB565 source setting
currently uses the behavioral reference's RGB8 storage treatment; native RGB565/Z16
EFB quantization remains unimplemented. Z24 selects the depth callback.

BP `0x49` and `0x4a` hold the origin and size minus one, `0x4b` the destination in
32-byte units, `0x4d` the block-row stride in 32-byte units, and `0x52` the trigger.
The four target-format bits are rotated to obtain the format number. Intensity
conversion requires both intensity and automatic-conversion bits. The half-scale
bit selects a 2×2 filter; other copy-filter and gamma settings do not alter this
texture encoder, matching the target software path.

| Target | Tile | Encoding |
| --- | --- | --- |
| R4 / I4 / Z4 | 8×8, 32 bytes | First pixel in the high nibble |
| R8 aliases / I8 / Z8H | 8×4, 32 bytes | Red, intensity, or high depth byte |
| RA4 / IA4 | 8×4, 32 bytes | Alpha high nibble, red/intensity low nibble |
| RA8 / IA8 | 4×4, 32 bytes | Alpha followed by red/intensity |
| RGB565 | 4×4, 32 bytes | Big-endian RGB 5:6:5 |
| RGB5A3 | 4×4, 32 bytes | Opaque RGB5 when alpha ≥224, otherwise A3RGB4 |
| RGBA8 / Z24 | 4×4, 64 bytes | 32-byte AR plane followed by 32-byte GB plane |
| A8, G8, B8 | 8×4, 32 bytes | Selected channel |
| RG8, GB8 | 4×4, 32 bytes | Green/red or blue/green byte pairs |

Intensity uses `(4096 + 66R + 129G + 25B) >> 8`. Half-scale RGB8 and depth average
each byte independently with truncation. RGBA6 first sums four six-bit components,
then expands each sum as `sum + (sum >> 6)`. Averaging already expanded eight-bit
colors can produce a different result and is deliberately avoided.

Depth copies support targets 0, 1, 6, 8, 9, 10, 11, and 12. The pinned half-scale
depth routine swaps the high and low depth channels for the RGBA8 target and selects
middle/low for RG8 and high/middle for GB8. These software-path details are preserved
and tested; they are not claims of independent hardware measurements. Undefined
targets and the remaining depth targets fail explicitly.

Complete tiles are written even when their pixels extend beyond the nominal source
rectangle. Odd half-scale dimensions therefore use `ceil(size / 2)` in this encoder,
as determined by the actual encoder's size-minus-one registers. The padded source
footprint must fit within 640×528. Out-of-range footprints fail explicitly instead
of attempting an undefined read. The upstream behavioral caller clips oversized
rectangles, while the encoder itself still sizes tiles from the raw BP registers;
this module does not invent pixels for that inconsistent edge.

Rows are placed using the configured stride. Gaps retain their previous RAM bytes.
Overlapping rows, including zero stride when invoked directly, are deterministic:
later block rows overwrite earlier rows. Command processing can suppress a zero-stride
copy as its caller does. The encoder follows the complete-tile write footprint,
including bytes beyond a short stride. The separate staging readback caller can
truncate such rows; that readback policy is not part of this direct RAM encoder.

All formats, source bounds, address arithmetic and the complete RAM extent are
validated before callbacks run. Encoded blocks are staged until all samples succeed,
then committed in row order. A callback exception or invalid destination leaves RAM
unchanged. This protects both MEM1 and Wii MEM2 destinations.

Behavioral mapping at commit `562875dd60fd0ff50b65dbbb611a2096802cd1b9`:

- `Source/Core/VideoBackends/Software/TextureEncoder.cpp`: `Encode`, `EncodeEfbCopy`,
  `SetBlockDimensions`, `SetSpans`, color/depth encoders and box-filter helpers.
- `Source/Core/VideoBackends/Software/SWEfbInterface.cpp`: `GetPixelPointer`,
  `GetPixelColor`, and `GetPixelDepth`, including packed RGBA6 representation.
- `Source/Core/VideoCommon/BPStructs.cpp`: EFB trigger dispatch, source clipping,
  intensity selection and zero-stride handling.
- `Source/Core/VideoCommon/TextureCacheBase.cpp`: `CopyRenderTargetToTexture` and
  `WriteEFBCopyToRAM`, including the distinction between encoding and staging readback.
- `Source/Core/VideoCommon/BPMemory.h`: copy-register fields and target-bit rotation.

The authored public checks passed with GCC 13 under warnings as errors. They cover
all color targets, supported depth targets, intensity gating, alpha thresholds,
six-bit filtering, tile order, RAM gaps and overlaps, full-EFB boundary sampling,
and atomic failures. The opt-in comparison separately compiles the unmodified
behavioral `TextureEncoder.cpp` and invokes its public `Encode` entry point.
Its original test-only storage adapters allocate RAM and reject unrelated backend
paths; they do not calculate expected encoded bytes. The executed comparison passed
267,659,520 byte checks across 180,480 generated cases with both scaling modes,
all supported source/target pairs, offsets, odd sizes and four stride arrangements.

The same public checks also passed with Clang 18 and GCC address/undefined-behavior
sanitizers. The executed authored guest-video test passed in both console modes:
guest FIFO commands copy RGBA8 into RAM, clear the EFB and redraw by sampling that
texture. Eight guest CPU loads verify the AR/GB planes, and the restored image reaches
real VI timing and headless output with the expected frame hash. No host pixel writes
or manual presentation calls are used.
