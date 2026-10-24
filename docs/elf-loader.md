# ELF loading

`runtime::load_elf` loads absolute ELF32 PowerPC executables with big-endian
encoding. It returns the entry address, number of loaded segments, and number of
file bytes copied. The caller initializes CPU state and starts execution at the
returned entry point.

Each `PT_LOAD` program segment uses its nonzero physical load address, mapped
through the bootstrap RAM aliases. A zero physical address uses the virtual
address. Bytes between a segment's file size and memory size are cleared to zero.
Segments load in program table order; later segments overwrite overlapping bytes.
The loader supports MEM1 and, with a Wii memory instance, MEM2.

The entire input is checked before RAM changes. Checks cover magic, encoding,
machine and file type, header tables, section ranges, segment file and memory
sizes, address overflow, mapped RAM, alignment, and entry placement. The entry
must be instruction aligned and map to four bytes in an executable segment's
file data. Alignment must be zero, one, or a power of two, with matching file and
virtual address offsets. Invalid input raises an error.

Relocatable files, shared objects, ELF64, little-endian files, relocation records,
and extended section or program counts are unsupported. Symbol tables are checked
as ordinary section data; debugger symbol import is not implemented. The stricter
machine, alignment, and entry checks are deliberate input validation beyond the
behavioral reference's parser.

Public tests create their own guest executable. It loads two segments, clears
BSS, runs five interpreted PowerPC instructions, and writes the value 7 into
guest RAM. Other cases check address selection, overlapping segments, MEM2,
non-loadable segments, no-data sections, malformed ranges, and unchanged RAM when
validation fails. These tests use synthetic expectations and have not been
compared with an independently executed ELF reference loader.
