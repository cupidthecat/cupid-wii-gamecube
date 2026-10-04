# Disc behavioral reference

The behavioral reference is pinned to commit
`562875dd60fd0ff50b65dbbb611a2096802cd1b9`. The implementation uses its own
parsing, bounds checks, chunk decoder, SHA-1, AES-CBC, and cache code.

| Implemented behavior | Behavioral reference files and functions |
| --- | --- |
| Container layout and codec properties | `docs/WiaAndRvz.md`; `Source/Core/DiscIO/WIABlob.h` structures |
| Header integrity and data ranges | `Source/Core/DiscIO/WIABlob.cpp`: `Initialize`, `HasDataOverlap` |
| Raw chunks and plaintext Wii reads | `Source/Core/DiscIO/WIABlob.cpp`: `Read`, `ReadWiiDecrypted`, `ReadFromGroups`, `ReadCompressedData` |
| Exception lists and packing | `Source/Core/DiscIO/WIABlob.cpp`: `Chunk::Read`, `HandleExceptions`, `GetHashExceptions`, `ApplyHashExceptions`; `Source/Core/DiscIO/WIACompression.cpp`: decompressor methods |
| Padding sequence | `Source/Core/DiscIO/LaggedFibonacciGenerator.cpp`: `SetSeed`, `Initialize`, `Forward`, `GetBytes` |
| Wii directories and sector reads | `Source/Core/DiscIO/VolumeWii.cpp`: constructor, `Read` |
| Wii hash hierarchy and encryption | `Source/Core/DiscIO/VolumeWii.cpp`: `HashGroup`, `EncryptGroup`; `Source/Core/DiscIO/WiiEncryptionCache.cpp`: `EncryptGroup`, `EncryptGroups` |
| IOS metadata field | `Source/Core/Core/IOS/ES/Formats.h`: `TMDHeader`; `Formats.cpp`: `TMDReader::GetIOSId` |
| Ticket, TMD, certificate chain, and contents | `Source/Core/DiscIO/VolumeWii.cpp`: lazy metadata readers; `Source/Core/Core/IOS/ES/Formats.cpp`: `TicketReader::IsValid`, `GetTitleId`, `TMDReader::IsValid`, `GetContents` |
| File system structure, paths, and lookup | `Source/Core/DiscIO/FileSystemGCWii.cpp`: `FileInfoGCWii` accessors and `IsValid`, `FileSystemGCWii` constructor and `FindFileInfo`; `Source/Core/Common/StringUtil.cpp`: `SHIFTJISToUTF8` |
| File content reads | `Source/Core/DiscIO/DiscExtractor.cpp`: `ReadFile` |
| Boot callers | `Source/Core/Core/Boot/Boot_BS2Emu.cpp`: Wii game partition selection and apploader reads |

On 2026-10-03, a separate Linux GCC 13.3 test executable compiled the actual
pinned reader, compression, padding, sector hash, and encryption sources. It
compared their bytes with `disc::Image` from this working tree. The harness and
its build directory stayed outside this repository; the reference checkout was
unchanged. Its host logging adapter printed diagnostic messages. No disc or
cryptographic behavior was replaced in the reference path.

The deterministic test used seed `0x4355504944`. It compared disc headers,
raw chunk boundaries, game headers, apploader bytes, DOL and FST headers, random
64 KiB plaintext ranges, the last plaintext range, and the Wii data segment split.
It also compared 16 random 64 KiB physical Wii reads, including reconstructed
sector hashes and encryption. SHA-1 below covers the concatenated compared bytes
in harness order, rather than the whole game image.

| Local input | Matching reads | Compared bytes | SHA-1 |
| --- | ---: | ---: | --- |
| `GMSE01` | 100 | 5,313,299 | `041e98c59439df6b69cdec0626ad0729810f54b7` |
| `RMGE01` | 117 | 6,362,075 | `9f559644bb04d4d7eb923dec8c1c762462aedbbd` |

The public padding regression uses seed bytes 0 through 67, skips 18 bytes, and
generates 32,750 bytes. The actual pinned `LaggedFibonacciGenerator` produced
SHA-1 `12da0f2a402648485e770243af4cdcc53b129ec4`. This checks the unusual
18-bit output shift, byte alignment, and repeated 521-word state advances.

These are byte-level software comparisons, not real hardware observations or
playability results. They sample the supplied local images and do not constitute
a complete image integrity scan. Synthetic expected data, standard cryptographic
vectors, and these behavioral reference observations remain separate test sources.

The same external harness then compiled `FileSystemGCWii.cpp` and `Filesystem.cpp`.
It compared every entry's path, directory flag, and file offset and size with the
actual pinned `FileInfoGCWii` implementation. It compared the whole FST's bytes
and read up to 4 KiB from each non-directory entry whose table index was a multiple
of 17. Both local images matched.

| Local input | Matching FST entries | FST SHA-1 | File reads | File bytes | File byte SHA-1 |
| --- | ---: | --- | ---: | ---: | --- |
| `GMSE01` | 181 | `8a730bc0a2969aa2c5a9faeaf1093708911bd40c` | 10 | 40,960 | `583e4a92d9a0d5160cc65b0a73a6f3aa03efc34e` |
| `RMGE01` | 2,447 | `5102ca2e9788b5c95dba58137c86e10fe6bef364` | 138 | 525,472 | `167c7486736cd17b36d81e1d440d59abe9a04c2b` |

The harness also compiled the actual `Source/Core/Core/IOS/ES/Formats.cpp` and
compared the Wii metadata against `TicketReader` and `TMDReader`. The supplied
`RMGE01` image matched the title ID, IOS 33 requirement, title version, boot index,
and all fields of its one content record. This checked metadata parsing, not
cryptographic signature verification.
