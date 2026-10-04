# Graphics

The software backend executes GX commands and renders into a 640 by 528 EFB. Guest commands supply the geometry, textures, shader state, copy destinations, and VI configuration. The output window displays framebuffers read from guest RAM.

## Command and rendering path

PI write gathering feeds the CP ring, including linked pointers, wrapping, watermarks, and breakpoints. The parser handles fragmented commands, CP and XF uploads, indexed XF loads, display lists, BP masks, texture-memory transfers, and draw commands. Token and draw-finish events set PE interrupt state after a scheduled delay.

Display-list addresses are aligned to 32 bytes and normalized to their physical RAM address by clearing the top two bits. All four pointer aliases reach the same MEM1 bytes, and Wii MEM2 uses the same rule. Lists cannot read the FIFO, MMIO, locked cache, or a range beyond physical RAM. The CPU memory interface continues to use physical addresses supplied by its MMU.

Vertices can use direct or indexed attributes. The vertex stage applies transforms, lighting, projection, and texture-coordinate generation. The rasterizer assembles triangles, strips, fans, quads, lines, and points, clips them, applies culling and scissoring, and interpolates fragment inputs. Multisample rendering is still unsupported.

The texture stage decodes I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8, C4, C8, C14X2, and CMPR. Paletted formats use uploaded TLUT data. RAM textures support mip layouts and point or bilinear sampling; manual TMEM supports base-level sampling and split RGBA8 banks. Manual mip chains remain unsupported.

TEV supports regular and comparison color/alpha operations, register destinations, konst selectors, swizzle tables, indirect coordinates and matrices, alpha tests, depth textures, and fog. The pixel stage applies early or late depth tests, blend factors, subtraction, logic operations, update masks, constant alpha, and RGBA6 dithering.

## EFB and copies

The EFB stores 24 raw color bits per pixel. RGBA6 reads expand each six-bit component; switching formats reinterprets those same bits. The RGB565/Z16 path deliberately follows the pinned software behavior, which retains RGB8/Z24 precision. This is a compatibility choice, not a real-hardware precision claim.

[CPU EFB reads](efb-access.md) use the physical framebuffer window, color/depth selectors, and PE alpha-read mode. Reads through the guest MMU and locked-cache DMA reach the current rendered pixels. CPU EFB writes and combined color/depth access remain unsupported.

[Texture copies](efb-copy.md) encode tiled color, channel, intensity, and depth data into RAM, including half-scale filtering and block-row strides. Invalid footprints are rejected before destination writes. Copy-triggered clearing honors color, alpha, and depth update masks.

[XFB copies](xfb-copy.md) perform the vertical filter, gamma correction, RGB-to-YUV conversion, horizontal chroma filtering, and nearest-row vertical scaling. Direct and inverted scale registers are supported for defined source and staging footprints. Odd widths, nonzero horizontal origins, and footprints that would expose unwritten intermediate bytes fail before sampling or destination writes. Copy-triggered clearing remains part of the command processor.

VI presents YUYV data according to its field and framebuffer registers and derives the display aspect from active signal timing. The SDL window fits that aspect to the available area. Raw field dimensions do not determine the standard display shape. The CLI also offers explicit widescreen and raw-pixel display modes; captures preserve guest pixels. Multisampling and manual mip chains remain unfinished.

## Evidence

Public tests check command fragmentation, register side effects, texture uploads, blending, depth, masks, and format reinterpretation. Optional comparisons compile the actual pinned transform, rasterizer, texture, shader, and encoder functions separately from the runtime. The shader comparison checks 500,000 generated fragments; the texture-copy comparison checks 267,659,520 bytes across 180,480 cases. XFB comparisons check 43,255,200 bytes across 5,487 defined cases. See [the mappings](graphics-reference.md), [texture-copy details](efb-copy.md), [XFB details](xfb-copy.md), and [tests](testing.md) for their scope.

The [authored guest-video program](guest-video.md) checks the integrated CPU-to-VI path in both console modes. Synthetic rendering evidence does not establish commercial-game rendering or playability.

The framebuffer access fixture also executes a cached display list through guest byte stores to a translated FIFO alias and checks its visible EFB clear. Direct command tests check all RAM pointer aliases, MEM2, fragmented calls, preserved source bytes, and invalid ranges. These regressions passed after the previous implementation rejected cached display-list pointers and incorrectly allowed GPU locked-cache reads.
