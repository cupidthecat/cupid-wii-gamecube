# XFB copies

`gfx::copy_efb_xfb` encodes a BP-controlled EFB region into physical RAM. The caller
supplies canonical `0xRRGGBBAA` pixels from its current EFB storage representation.
The result reports the destination, width, readback height, compact encoded height,
row byte counts, configured stride and vertical scale. Command processing owns the
copy trigger, zero-stride suppression and subsequent optional EFB clear.

The source origin and size-minus-one registers are BP `0x49` and `0x4a`. The source
rectangle is clipped to 640 by 528, while the caller's readback row count still uses
the raw height-minus-one register. BP `0x4b` selects the destination in 32-byte units
and `0x4d` the RAM stride in 32-byte units. BP `0x4e` supplies either `value / 256`
or, with the inverse-scale bit, `256 / value`. The texture half-scale flag has no
effect on this XFB path.

The seven six-bit filter coefficients in BP `0x53` and `0x54` form three weights:
coefficients 0 and 1 select the previous row, 2 through 4 the current row, and 5 and
6 the next row. The sum is divided by 64 and clamped to 255. The clamp bits select
the copy rectangle's vertical edges; otherwise sampling stops at the physical EFB
edges. This follows the target software path, including its edge substitution,
rather than claiming an independent measurement of empty-bus values.

Gamma follows filtering. Trigger encodings 0, 1, 2 and 3 select 1.0, 1.7, 2.2 and
2.2 respectively. Corrected RGB becomes YUV using integer coefficients and rounding,
then horizontal chroma filtering packs each pair as Y, U, Y, V. The left border
repeats its first pixel. Alpha does not contribute to the XFB image.

The compact encoder height is `int(clipped_source_height * scale)`. The copy caller
requests `int(1 + raw_height_minus_one * scale)` rows. Nearest-row selection uses
`round(row * clipped_source_height / encoded_height)`. These two height formulas
can differ. For example, a 16-row source at scale 2 produces 32 compact rows, while
the RAM readback contains 31 rows. The module evaluates only bytes observable in
that readback; it does not invent unused source-tail pixels.

Staging readback also has a distinct layout. Its row size is the width rounded up
to 16 pixels, multiplied by two bytes. Each row reads and writes the lesser of that
size and the configured stride. The compact encoded bytes are selected at
`row * stride + byte`, as in the actual caller. A short stride can therefore select
different slices of one compact row. Padding or scaled extents that would expose
unwritten staging bytes fail explicitly. RAM gaps retain their existing values.

The module rejects zero scales, undefined float-to-integer extents, odd source
widths, nonzero horizontal origins with undefined intermediate indexing, missing
source rectangles, invalid pixel formats, address wrapping and readback selections
that reach unwritten source data. All observable source and destination bounds are
validated before sampling. Conversion completes in temporary storage before RAM
changes, so invalid inputs and callback failures leave the destination unchanged.

Behavioral mapping at commit `562875dd60fd0ff50b65dbbb611a2096802cd1b9`:

- `Source/Core/VideoBackends/Software/SWEfbInterface.cpp`: `EncodeXFB`,
  `VerticalFilter`, `GammaCorrection`, `ConvertColorToYUV` and packed EFB reads.
- `Source/Core/VideoBackends/Software/CopyRegion.h`: nearest-neighbor row selection.
- `Source/Core/VideoBackends/Software/TextureEncoder.cpp`: XFB dispatch and the
  width passed as the encoder's intermediate row stride.
- `Source/Core/VideoCommon/BPStructs.cpp`: clipping, scale and gamma selection,
  readback height, zero-stride suppression and post-copy clearing.
- `Source/Core/VideoCommon/TextureCacheBase.cpp` and
  `Source/Core/VideoCommon/AbstractStagingTexture.cpp`: block dimensions,
  `WriteEFBCopyToRAM` and `ReadTexels` row selection and truncation.

The authored public checks passed with GCC address and undefined-behavior sanitizers.
They cover color and gamma values, filter groups, clamp behavior, both scale
directions, short strides, a full 640 by 528 source, clipped source edges, Wii MEM2
destinations and atomic failures.

The opt-in comparison separately compiles the unmodified `SWEfbInterface.cpp`.
Its original test-only driver populates packed EFB storage and invokes `EncodeXFB`;
it does not implement expected color conversion, filtering or scaling. Executed GCC
and MSVC comparisons each passed 43,255,200 byte checks across 5,487 defined generated
cases. Another 4,513 undefined extent, unwritten staging or unwritten source cases
were rejected before RAM mutation. The generated cases exercise all four supported
source representations, gamma selections, filter coefficients, vertical origins,
direct and inverse scales, and stride arrangements. These comparisons establish
compatibility with the pinned software behavior, not additional hardware results.

The authored guest-video test also passed in both console modes with the integrated
command processor. Guest FIFO commands copy a 16 by 16 image at scale 0.5 and inverse
scale 2. Guest CPU loads verify the scaled row boundary, copied height and untouched
tail row in RAM. The subsequent unit-scale copy reaches real VI timing and headless
output with its established frame hashes unchanged.
