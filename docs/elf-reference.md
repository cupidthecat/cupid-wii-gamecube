# ELF behavioral reference

The behavioral reference is pinned to commit
`562875dd60fd0ff50b65dbbb611a2096802cd1b9`.

| Implemented behavior | Behavioral reference |
| --- | --- |
| ELF32 header and program/section table layout | `Source/Core/Core/Boot/ElfTypes.h`: `Elf32_Ehdr`, `Elf32_Phdr`, `Elf32_Shdr` |
| Big-endian parsing and checked file ranges | `Source/Core/Core/Boot/ElfReader.cpp`: `Initialize` |
| Absolute executable restriction, load address selection, segment order, and BSS clearing | `Source/Core/Core/Boot/ElfReader.cpp`: `LoadIntoMemory` |
| Executable boot and entry handoff | `Source/Core/Core/Boot/Boot.cpp`: executable construction and executable boot visitor |

The loader uses original parsing and validation code. It preserves the reference's
ordered segment copies and zero fill. Its explicit PowerPC machine, alignment,
RAM bounds, and executable entry checks reject malformed files earlier. Relocation
and debugger symbol loading are not implemented. Public tests exercise a synthetic
PowerPC executable and malformed variants; source inspection is not a differential
execution test.
