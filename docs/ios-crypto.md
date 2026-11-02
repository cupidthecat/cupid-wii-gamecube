# IOS cryptographic devices

The kernel exposes `/dev/aes` and `/dev/sha` through ordinary IPC descriptors.
Open, close and ioctlv use the existing request/acknowledgment/reply path. Other
syscalls and unknown crypto operations return IPC error `-4`.

## AES

The AES device implements copy, AES-128-CBC encryption and decryption. Keys and
initialization vectors come from guest RAM; no title keys or firmware are bundled.
The implementation reuses the project's AES primitive and its CPU feature dispatch.

The selected source has several observable buffer rules:

- Copy snapshots the input and writes its full length, independent of the output
  vector's advertised length. Overlapping guest buffers therefore work.
- Encryption and decryption require two input and two output vectors. Key and IV
  sizes must be sixteen bytes, with four-byte-aligned addresses.
- Crypt processes the smaller data-buffer length. Extra output bytes are zero.
  The updated IV is written after the output, preserving overlap ordering.
- If that smaller length is not block aligned, the primitive fails but the source
  service ignores the failure. The service returns success with zeroed output and
  an unchanged IV. This compatibility behavior is confined to the IOS device;
  the underlying CBC primitive still rejects invalid buffer lengths.
- Empty data buffers require no guest-memory access, including when their unused
  addresses do not point to RAM.

## SHA-1

The SHA device implements context initialization, contribution and finalization.
The opaque context holds five little-endian state words and a two-word byte count.
Finalization updates the state words without adding padding bytes to that count.
The twenty-byte digest uses canonical byte order. The fixed context and digest
writes follow the source even when vector lengths advertise smaller buffers, as
long as the actual guest backing is valid.

The source context omits the partial-block buffer used by its hashing library.
Contributions that leave an incomplete block have defined state/count output, but
a later operation that needs the omitted bytes has no stable result. This
implementation stops with a diagnostic before reading or inventing those bytes.
Block-aligned contributions followed by an arbitrary final fragment are supported.

Keys, IVs, input bytes and context state are captured before overlapping output
writes. IOS reply headers retain the originally dispatched command instead of
reading it back from potentially overwritten guest RAM. That rule also applies
to delayed STM, Bluetooth and disc replies.

## Qualification

Review uses pin `2c805fee8f232d090c454381bb1b8422242345e1`, including
`Source/Core/Core/IOS/Crypto/{AesDevice,Sha}.{cpp,h}`, `IOS/Device.cpp`,
`IOS/IOS.cpp`, `Common/Crypto/AES.cpp` and the linked SHA-1 implementation.

The actual source kernel/device/primitive comparison passes 4,997 cases. Each
checks return values and both complete 64 KiB authored RAM regions. Cases cover
length combinations, both RAM banks, misaligned keys, unaligned data/context,
overlap, IV updates, short advertised buffers, empty transfers, saved contexts,
byte-count carry, arbitrary starting states and defined partial contributions.
Another 128 comparisons include the actual kernel reply builder and request-header
overlap, with product reply delivery checked against the source delay.

Public tests retain known ciphertext/digest values, CBC batch boundaries,
in-place decryption, context storage and relocation, unsupported partial-block
requests, normal IPC descriptor use and delayed reply-header preservation.
These are synthetic checks, not title-level or physical-console qualification.

## Decryption throughput

The x86 AES path decrypts eight independent CBC blocks together, then stores them
backwards to preserve exact in-place chaining. Smaller tails use the existing
single-block path. The software fallback and encryption path are unchanged.

On the shared Linux x86-64 host with GCC 14.2.0, a paired component test processed
20 GiB per run using 256 KiB buffers. Six runs per version, in repeated ABBA order,
measured median elapsed times of 2.559 seconds before and 2.348 seconds after,
about an 8.3% reduction. Checksums matched, and the independent source comparisons
and in-place boundary tests passed. This is not a game-FPS or cross-host speed claim.

Final local qualification passes fourteen focused Release/LTO runtime, IPC,
cryptographic, disc, configuration and CLI suites. IOS, crypto and disc tests also
pass GCC and Clang ASan/UBSan with fatal findings enabled; local leak checking is
disabled under the host tracing restriction. Bounded Sunshine and Galaxy startup
pairs retain guest state, raw frames and persisted files at one billion
instructions. Revision differs, and Galaxy native-execution telemetry differs.
These are regression observations, not gameplay completion or speed qualification.
