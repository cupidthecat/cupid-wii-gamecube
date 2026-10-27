# Disc images

The disc reader supports random reads from ISO, RVZ, and WIA images. RVZ supports
uncompressed, bzip2, LZMA, LZMA2, and Zstandard chunks, including literal and
pseudorandom padding packets. WIA also supports PURGE blocks. Images stay read only.

`disc::Image::read` uses physical disc offsets. For Wii RVZ/WIA partitions it
rebuilds sector hashes, applies stored hash exceptions, and encrypts each sector
with the partition key stored in the container. `read_game` uses plaintext game
partition offsets and omits the 0x400-byte hashes in each 0x8000-byte Wii sector.
These plaintext reads require no external key files for RVZ/WIA images.

Both read APIs throw `std::out_of_range` when the requested bytes exceed their
logical size, before changing the destination buffer. The device and IOS layers
use this distinction to report an out-of-bounds drive error separately from a
media read failure.

Wii game partition selection follows the four partition directory groups and
chooses the first type-zero partition. DOL and FST fields use byte offsets on
GameCube and four-byte units on Wii. The required IOS number comes from the game
partition TMD. Raw Wii ISO plaintext reads support unencrypted sectors, with or
without hashes. Encrypted raw Wii ISO plaintext reads report that a partition
title key is required; supplying such a key is not implemented.

`wii_metadata` reads the game partition ticket, TMD, certificate chain, content
records, title ID, IOS number, version, and boot index. It checks the RSA-2048
metadata layout, declared sizes, content record bounds, and matching ticket and
TMD title IDs. It does not verify signatures. The API returns data in memory and
does not save private metadata to host files.

`disc::Filesystem` parses the game partition FST, lists paths, finds entries, and
reads file contents through `read_game`. It converts Shift-JIS names to UTF-8
using the Windows code page 932 APIs or Linux `iconv` with `SJIS`. Lookup ignores
ASCII letter case and extra slashes. Directory paths end with a slash. File reads
check the requested file range before accessing the image.

The filesystem parser checks the directory hierarchy, parent and end indexes,
name termination, and each file's range. It rejects path traversal names and
embedded path separators. Its limits are a 128 MiB FST, one million entries,
1,024 nested directories, and 256 MiB of combined path text. Invalid text encoding
reports an error.

The reader checks container sizes, version compatibility, SHA-1 header and
partition table checksums, decoded table lengths, disc range overlaps, group
references, codec frame completion, packet lengths, and hash exception bounds.
Malformed input raises an error. Each metadata, compressed, decoded, or dictionary
allocation has a 256 MiB limit. Images needing larger chunks or dictionaries are
rejected explicitly. One reader owns its file and caches; callers must serialize
access to the same reader.

Public tests build synthetic images for all codecs, chunk crossings, short last
chunks, zero groups, packing, two Wii data segments, raw Wii ISO layouts, hash
exceptions, and corrupt input. SHA-1 and AES-CBC tests use published standard
vectors. The padding regression uses an actual behavioral reference result; its
source and local differential results are in [disc-reference.md](disc-reference.md).

Disc decoding is verified separately from guest boot and gameplay. Correct disc
reads do not establish that a game boots, renders correctly, or is playable.
