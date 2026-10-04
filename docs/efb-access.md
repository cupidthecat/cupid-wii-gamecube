# CPU EFB access

The physical window from `0x08000000` through `0x0bffffff` reads the rendered EFB. Address bits 2 through 11 select X, and bits 12 through 21 select Y. Bit 22 selects 24-bit depth. Bits 24 and 25 alias the same pixels. The color path converts the current EFB format to ARGB, then applies the low two bits of PE's alpha-read register at `0x0c001008`: zero alpha, full alpha, or retained alpha.

Byte and halfword reads truncate the low bits of the selected pixel. They do not select byte lanes within its ARGB word. An eight-byte read zero-extends one pixel. Other device windows still reject eight-byte reads. Coordinates outside the physical 640 by 528 EFB return zero before pixel-format or alpha-mode evaluation.

Locked-cache loads from this window use ordered physical word reads. They apply the same pixel and alpha rules, and wrap each destination word within the cache backing range. RAM transfers use the separate contiguous transfer rules described in [locked-cache DMA](locked-cache-dma.md).

CPU EFB writes, combined color/depth access selected by bit 23, and alpha-read mode 3 fail explicitly. The pinned software pixel interface has empty poke methods; the runtime has not added a CPU write path on that basis. These limits do not affect GX drawing or copy-triggered clearing.

## Source map

The behavioral pin is `562875dd60fd0ff50b65dbbb611a2096802cd1b9`. Paths are relative to `Source/Core/`.

| Product implementation | Behavioral implementation |
| --- | --- |
| `src/gfx/command_processor.cpp`, `read_efb` | `Core/PowerPC/MMU.cpp`, `EFB_Read`; `VideoCommon/EFBInterface.cpp`, `ShouldSkipAccess`, `PeekColor`, `PeekDepth`; `VideoBackends/Software/SWEfbInterface.cpp`, `PeekColorInternal`, `PeekDepthInternal` |
| PE alpha selection | `VideoCommon/PixelEngine.h`, `UPEAlphaReadReg`; `VideoCommon/PixelEngine.cpp`, `RegisterMMIO`, `Init` |
| `src/core/memory.cpp`, `read`; `src/runtime/machine.cpp`, memory callbacks | `Core/PowerPC/MMU.cpp`, `ReadFromHardware`, `DMA_MemoryToLC` |

The tests use authored expectations from these inspected paths. They are not a direct full-runtime comparison or a real-hardware result.

## Validation

`tests/efb_access_tests.cpp` configures the actual PI/CP FIFO and clears pixels with BP commands, without supplying a host pixel fixture. On both consoles it checks all four physical aliases, RGBA6 and format reinterpretation, all supported alpha modes, depth, bounds, typed memory accesses, and locked-cache DMA reads across the cache boundary. Guest `lwz`, `lbz`, `lhz`, and `lfd` instructions read the pixels through boot BAT translation. Unsupported access tests preserve the pixels and keep ordinary device doubleword reads rejected.

The test first failed at an unmapped physical EFB read. The implemented bridge passes strict Windows MSVC and Linux GCC/Clang, plus GCC and Clang address/undefined-behavior sanitizer builds. Those results cover the NI/EFB snapshot before later DSP and Bluetooth fixes. See [testing](testing.md) for the suite scope.
