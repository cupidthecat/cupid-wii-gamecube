# IPL font validation

`--ipl-fonts PATH` reads a user-supplied 2 MiB font container and exposes only its validated Shift JIS and Western font slots through EXI. `--validate-fonts --ipl-fonts PATH` checks it without booting a program. This option is usable in either console mode. It does not identify or install a complete firmware revision.

Each slot must contain a valid Yay0 stream. The loader bounds mask, link, and literal reads; checks overlapping back-references; caps decoded allocation; and rejects references that exceed the output. The decoded glyph header must match the slot, with valid sheet geometry, image bounds, width tables, and glyph indices. The current loader accepts the packed two-bit glyph-sheet encoding.

The exposed slots begin at `0x1aff00` and `0x1fcf00`. The loader copies only the consumed encoded font bytes and zero-fills the rest of each font slot, matching the font-only loading path. Other ROM addresses remain unavailable. It never treats an unknown full-ROM checksum as a recognized BIOS.

## Behavior and provenance

The pinned behavioral files are `Source/Core/Core/HW/EXI/EXI_DeviceIPL.cpp`, `CEXIIPL::LoadFontFile`, its constructor, and `TransferByte`, at `562875dd60fd0ff50b65dbbb611a2096802cd1b9`. They define font offsets, separate loading, padding, and byte access. The product adds structural validation rather than blindly exposing a font file.

The stream and glyph-header layout were also checked against the [libogc system header](https://github.com/devkitPro/libogc/blob/ed9b8fcf77a696a3c0e9ee727cc18e5d9e221b7c/gc/ogc/system.h) and [font-reading and decoding routines](https://github.com/devkitPro/libogc/blob/ed9b8fcf77a696a3c0e9ee727cc18e5d9e221b7c/libogc/system.c). The implementation and public fixtures are newly written. No SDK implementation, font payload, or console image is vendored.

Public tests generate one-glyph fonts with literal, short-reference, and extended-reference streams. They compare decoded checksums and reject malformed masks, offsets, references, glyph sizes, indices, and widths. They verify that bytes outside the encoded streams are excluded. Local console fonts remain private inputs.
