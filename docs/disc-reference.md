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
| AES-CBC and runtime instruction selection | `Source/Core/Common/Crypto/AES.cpp`: `ContextGeneric::Crypt`, `ContextAESNI::Crypt`, `CreateContext` |
| SHA-1 blocks, padding, and digest | `Source/Core/Common/Crypto/SHA1.cpp`: `BlockContext::Update`, `BlockContext::Finish`, `ContextMbed`, `CalculateDigest`; `Source/Core/DiscIO/VolumeWii.cpp`: `HashGroup` |
| IOS metadata field | `Source/Core/Core/IOS/ES/Formats.h`: `TMDHeader`; `Formats.cpp`: `TMDReader::GetIOSId` |
| Ticket, TMD, certificate chain, and contents | `Source/Core/DiscIO/VolumeWii.cpp`: lazy metadata readers; `Source/Core/Core/IOS/ES/Formats.cpp`: `TicketReader::IsValid`, `GetTitleId`, `TMDReader::IsValid`, `GetContents` |
| File system structure, paths, and lookup | `Source/Core/DiscIO/FileSystemGCWii.cpp`: `FileInfoGCWii` accessors and `IsValid`, `FileSystemGCWii` constructor and `FindFileInfo`; `Source/Core/Common/StringUtil.cpp`: `SHIFTJISToUTF8` |
| File content reads | `Source/Core/DiscIO/DiscExtractor.cpp`: `ReadFile` |
| Boot callers | `Source/Core/Core/Boot/Boot_BS2Emu.cpp`: Wii game partition selection and apploader reads |

Physical Wii reads from WIA/RVZ reconstruct plaintext, H0/H1/H2 hashes, and hash
exceptions for the complete 64-sector group before returning bytes. Each sector has its own
AES-CBC hash and data chains, so the cache encrypts only requested sectors and
records which sectors are ready. It retains 2 MiB of plaintext and hashes beside
the 2 MiB encrypted group. Changing partitions or groups invalidates the cache;
plaintext reads can replace the decoded chunk without changing retained sectors.
An invalid exception elsewhere in the group still rejects the physical read.

The plaintext chunk walk also collects the exception records needed by that
group. It preserves list and record order, then validates their group coordinates
and applies them after the complete plaintext walk and hash computation. This
avoids decoding the same chunks again for exceptions. A malformed later chunk
still takes precedence over an earlier exception with an invalid group
coordinate. The temporary collector uses 32 bytes per record, plus vector
capacity; groups without exceptions allocate no record storage.

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

On 2026-10-04, isolated before and current builds used the same complete source
snapshot and tests, with only `src/disc/image.cpp` differing. A separate executable
compiled the actual reader sources at the same pinned commit. Both builds matched
that reader across five generated RVZ codecs: uncompressed, Bzip2, LZMA, LZMA2,
and Zstandard. Each fixture contains 130 Wii sectors split after sector 64. Its
28 reads cover physical and plaintext boundaries, hash headers, sector 63/64,
sector 127/128, the partial final group, and repeated reads with intervening
plaintext reads. Each codec compared 517,056 bytes with SHA-1
`2837751139d6bfe72aeec5ed3951fe95f00d91dc`.

The expanded supplied-image comparison added compressed chunk boundaries and
physical hash and encryption boundaries to the earlier random samples. It also
repeated the complete FST, file sample, and metadata checks above.

| Local input | Matching reads | Compared bytes | SHA-1 |
| --- | ---: | ---: | --- |
| `GMSE01` | 106 | 5,313,779 | `72ce49aa9516c01e51cde64ccee663a3a09865bf` |
| `RMGE01` | 143 | 6,364,155 | `fb9d0edb133811b264b19b0016041077a92845c2` |

Two generated fixtures put an invalid exception in sector 63 or 64. The actual
pinned reader rejected the first physical read from the affected group, even
when the requested bytes came from its first sector. Both builds rejected
repeated attempts and preserved the output buffer. Public tests also alternate
two partition keys, read across their boundary, and decrypt a future sector whose
exception changes its data IV. The GCC 13.3 Release pair passed all 36 executable
public tests; 23 optional tests were explicitly skipped. The current disc tests
also passed Clang 18.1.3 AddressSanitizer and UndefinedBehaviorSanitizer checks.

The sealed GCC Release pair measured 120 random 64 KiB physical Wii reads with
seed `0x4355504944525632`. Six sequential processes ran in before/current/current/
before/before/current order. Every process returned the same 7,864,320 bytes,
with SHA-1 `e97c58dda718365151d8e69ac2ec5cdc3f2236c1`. The timer excluded image
construction and the final digest. Before times were 32.416, 45.457, and 41.845
seconds; current times were 9.896, 8.458, and 5.585 seconds. Their medians fell
from 41.845 to 8.458 seconds, a 79.8% reduction. Other builds shared the host
during measurement. This measures physical sector reconstruction; game partition
plaintext reads use a different path, so it does not establish a gameplay speedup.

A paired headless `RMGE01` run used independent copies of the same user seed,
software graphics, audio disabled, and one billion step attempts per build. Both
ended with 999,994,000 executed instructions and identical values for all 44 final
state fields. Their final frame bytes and all six persisted user files (33,958
bytes) matched exactly. This is a bounded state comparison. The changing host
load made the two elapsed times unsuitable for a gameplay performance comparison.

A second isolated pair tested collecting exceptions during the plaintext walk.
Profiling the same 120 physical reads found that the old exception pass decoded
2,000 chunks and consumed 182,063,899 stored bytes, repeating the plaintext pass.
The collector eliminated those second-pass decodes. Six uninstrumented GCC 13.3
Release processes ran in before/current/current/before/before/current order.
Before times were 3.640, 3.600, and 3.629 seconds; current times were 2.830, 2.806,
and 3.076 seconds. Their medians fell from 3.629 to 2.830 seconds, a 22.0%
reduction. All six returned the same 7,864,320 bytes and the SHA-1 above. This is
another physical reconstruction measurement under shared host load.

The actual pinned reader also matched both implementations on six larger
generated fixtures: uncompressed, WIA purge, Bzip2, LZMA, LZMA2, and Zstandard.
Each uses 4 MiB chunks, 194 Wii sectors, and a segment split after sector 128.
The fixtures contain multiple exception lists and overlapping records that
change a later sector's data IV. Each codec's 41 physical and plaintext reads
compared 775,504 bytes with SHA-1 `3059bd47c7a8b551f6a64b537a0ac384b41349aa`.
The earlier generated and supplied-image comparisons remained identical.

Public tests decrypt all 194 physical sectors and check the overlapping records
against literal expected bytes. Additional malformed fixtures check error
precedence, three failed retries with an unchanged output buffer, and valid
physical and plaintext reads between failures. Both implementations passed the
expanded disc suite. The collector also passed Clang 18.1.3 AddressSanitizer and
UndefinedBehaviorSanitizer with system compression libraries. Direct runs
without a timeout took 16.168 seconds at `-O2` and 152.834 seconds at Debug `-O0`.
Both enabled leak detection and stopped on the first sanitizer error.

AES-CBC now selects AES instructions at runtime on x64 hosts that report support;
other hosts use the portable implementation. Key expansion, object layout, buffer
validation, and exception messages remain unchanged. GCC and Clang restrict the
accelerated helpers to AES and SSE2 instructions, without AVX. The instruction
semantics and CPU feature check follow the
[Intel AES-NI reference](https://www.intel.com/content/dam/doc/white-paper/advanced-encryption-standard-new-instructions-set-paper.pdf).

An isolated GCC 13.3 pair repeated the same 120 physical reads and six-process
order above. Medians fell from 3.60026 to 2.65064 seconds, a 26.4% reduction, with
identical bytes and SHA-1. A separate 360-sector CBC workload fell from 1.11905
to 0.0122732 seconds at the median, a 98.9% reduction; all six runs returned
11,796,480 bytes with SHA-1 `ea273af94653f9181e9fe49e33349219eb6c884e`.
Host load varied, including one accelerated physical-read run of 6.97768 seconds.
These component measurements establish no gameplay speed result.

GCC 13.3, Clang 18.1.3, MSVC 19.51, and Clang sanitizers matched 3,716 checks
covering standard vectors, unaligned buffers, in-place and partial aliases, IV
aliases, and invalid sizes with unchanged output. The four-block vectors come
from [NIST SP 800-38A, Appendix F.2](https://nvlpubs.nist.gov/nistpubs/legacy/sp/nistspecialpublication800-38a.pdf).
Both builds also matched the complete pinned-reader comparison corpus above.
The expanded public disc suite passed both builds and Clang sanitizers at `-O2`
and Debug `-O0`; the direct Debug run took 81.132 seconds. Fallback checks forced
feature dispatch off on the same AES-capable host, rather than using a physical
host without AES instructions.

Portable SHA-1 now hashes full 64-byte blocks directly, retains a 16-word rolling
schedule, and selects its 80 rounds at compile time. Only the final one or two
blocks use a 128-byte stack buffer. It no longer allocates and copies a padded
version of the complete input. The change adds no CPU instruction requirement or
runtime dependency.

Public regressions check the two-block and million-byte vectors in
[FIPS PUB 180-1, Appendices B and C](https://nvlpubs.nist.gov/nistpubs/Legacy/FIPS/fipspub180-1.pdf).
Independent CPython/OpenSSL digests cover padding and full-block boundaries,
620-byte hash tables, 1,024-byte hash inputs, and 32 KiB sectors at eight input
alignments. Each check also verifies that the input remains unchanged. A private
deterministic corpus compared 17,408 messages and 65,744,918 input bytes with the
previous implementation and installed Mbed TLS on Linux. GCC 13.3, Clang 18.1.3,
Clang sanitizers, and MSVC 19.51 returned the same aggregate digest
`ddafc0f59bd7469fa348262d755a9afd2a2cb34d`. The MSVC corpus compared the previous
implementation; its public suite separately checked the literal vectors.

Both isolated source snapshots matched the complete pinned-reader corpus above,
including all generated codecs, malformed retry and error-order checks, both
supplied images, FST entries, sampled files, and Wii metadata. The expanded public
disc suite passed both GCC builds, MSVC, and Clang AddressSanitizer and
UndefinedBehaviorSanitizer at `-O2` and Debug `-O0`. The direct Debug sanitizer run
took 182.442 seconds with leak detection enabled.

Before timing the final components, isolated headless runs compared one billion
step attempts per build and game, using native CPU execution, software graphics,
audio disabled, and independent copies of the same user seeds. All 52 final state
fields, final frame bytes, and persisted user file bytes matched. The supplied
GameCube image ended at 72 VI fields and the Wii image at 76.

Six uninstrumented processes per workload ran in before/current/current/before/
before/current order. The 248,000-message SHA workload returned the same 4,960,000
digest bytes with SHA-1 `920550a9a5adbc162c073be8a7298b192f1fb4a4`. Its median fell
from 1.552920 to 0.616891 seconds on GCC, a 60.3% reduction. The same 120 physical
Wii reads and 7,864,320
bytes used in the earlier measurements retained SHA-1
`e97c58dda718365151d8e69ac2ec5cdc3f2236c1`. Their GCC median fell from
2.969370 to 2.079090 seconds, a 30.0% reduction. MSVC medians fell from
1.024680 to 0.783546 seconds for SHA (23.5%) and from 1.567120 to 1.527260 seconds
for physical reads (2.5%). Other builds and
tests shared the host. Linux used installed compression libraries; MSVC used the
pinned compression builds. These component timings establish no gameplay speedup.

A separate observer matched stock results for both images at three billion step
attempts: all 52 state fields, final frame bytes, and persisted user bytes. Guest
disc API time totaled 55.6 ms for 320 GameCube VI fields and 216.9 ms for 241 Wii VI
fields. Repeated chunk misses were four and three respectively. These boot/title
prefixes do not identify the one-entry chunk cache as a gameplay bottleneck. The
GameCube replay's first button press occurs at field 1,000, beyond those runs.
