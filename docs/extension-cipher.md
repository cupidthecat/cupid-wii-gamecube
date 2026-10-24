# Extension encryption

The first-party extension cipher derives its two eight-byte tables from the sixteen bytes written
to extension registers `0x40..0x4f`. It reverses the nonce and response portions independently,
checks seven possible selectors in order, and uses the defined fallback when none matches.
Encryption subtracts one table byte and XORs the other. Decryption reverses those operations;
the register address chooses the phase modulo eight. Byte arithmetic wraps modulo 256.

`make_extension_key` returns an immutable value. `ExtensionKey::encrypt` and `decrypt` operate on
the entire supplied span, accept unaligned addresses, and preserve phase across address wrap.
A caller handling a partially readable register range must include its zero-filled unread suffix
in that span. The helper does not manage register storage, attachment, encryption enablement or
the dirty-key lifecycle. Those belong to the controller bus.

The fixed protocol substitutions are expressed as disjoint permutation cycles and expanded at
compile time. Expansion rejects repeated or missing byte values. The fallback reuses selector
permutations seven and zero with a common XOR of `0x80`; all 512 fallback entries were checked
against the behavioral reference. The module contains original executable logic and no imported
implementation files. Protocol constants retain Hector Martin's 2008 attribution under
GPL-2.0-or-later.

## Behavioral reference

The pinned revision is `562875dd60fd0ff50b65dbbb611a2096802cd1b9`.

| Path | Functions or callers | Behavior |
| --- | --- | --- |
| `Source/Core/Core/HW/WiimoteEmu/Encryption.cpp` | `KeyGen::GenerateFromExtensionKeyData`, `KeyGen1stParty::GenerateKeyData`, `GenerateTables`, `GenerateFallbackTables` | Register byte order, selector matching, normative substitutions and fallback |
| Same path | `EncryptionKey::GenerateTables`, `Encrypt`, `Decrypt` | Table wiring, byte wrap and address phase |
| `Source/Core/Core/HW/WiimoteEmu/Extension/Extension.cpp` | `EncryptedExtension::BusRead`, `BusWrite`, `Reset` | Lazy key generation, dirty writes, encryption of the complete requested read |
| `Source/Core/Core/HW/WiimoteEmu/Extension/Nunchuk.cpp` | `Nunchuk::Reset`, `Update` | Extension register initialization and report updates |

## Verification and limits

An external harness compiled the complete, unchanged `Encryption.cpp`, suppressing only its
diagnostic logging. It compiled the exact public cipher source in the same harness and compared
all 2,304 normative substitution bytes, all 512 fallback entries, generated tables, encryption
and decryption. Authored records cover every selector, all nonce byte values at each position,
random valid records, random fallback records, all 256 data bytes, all eight phases and a
32-bit address wrap.

The run passed **33,297 key cases and 665,951 checkpoints**, comparing **153,968,144 bytes**.
The SHA-1 of the explicitly serialized compared bytes was
`96ee408f9a25f3acda686eff999fc5345f9388fa`. This is a compiled-source comparison, not a
real-hardware measurement. It includes the defined fallback compatibility behavior.

A second harness compiled the unchanged `Extension.cpp` and `Encryption.cpp` and compared the
controller's register read/write helpers with their bus behavior. Across 256 controlled register
banks it checked every register address, reads of 1, 2, 6, 16 and 32 bytes, encrypted and plain
reads, partial reads, overlapping key writes, enable changes and lazy key state. It passed
**335,616 checkpoints and 5,304,320 compared bytes**, with SHA-1
`dee08e2dbbb6195a304ce6557cdec7dc1b2f7186`. Diagnostic, save-state and controller UI adapters
were outside the comparison; the initial register banks were synthesized explicitly. This run
does not validate Nunchuk motion or its reset calibration against compiled source.

Public tests retain ten fixed synthetic input/table/cipher vectors from that harness, split-read
phase continuity, empty spans, wrapping addresses and complete-span processing. Focused GCC,
MSVC and Clang ASan/UBSan runs passed. No commercial key record is present in these fixtures.

Only the first-party extension profile is implemented. Third-party key generation and actual
hardware validation remain outside this module's coverage. Commercial startup progress is
recorded separately in [compatibility](compatibility.md).
