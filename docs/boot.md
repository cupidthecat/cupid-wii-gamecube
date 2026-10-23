# Boot and system data

## Executable and disc boot

The runtime loads checked DOL or ELF sections into guest RAM and begins interpreter execution at their entry point. Disc boot initializes common low memory and CPU registers, loads the guest apploader from the selected disc, executes its entry/init/main/close functions, and fulfills the requested disc transfers. It then continues at the guest's returned game entry.

Both supplied local integration discs have completed that sequence with their original RVZ data. Completing the apploader is a boot milestone; neither game has verified controllable gameplay.

## GameCube firmware

`--bios PATH` requires exactly 2,097,152 bytes and a recognized full-ROM CRC32. The loader identifies the revision, decodes the firmware's scrambled region, and installs the result for EXI ROM reads. The current runtime uses apploader boot and does not execute the complete IPL boot sequence.

| CRC32 | Recognized revision |
| --- | --- |
| `6dac1f2a` | NTSC 1.0 |
| `d5e6feea` | NTSC 1.1 |
| `d235e3f9`, `86573808` | NTSC 1.2 |
| `667d0b64` | MPAL 1.1 |
| `4f319f43` | PAL 1.0 |
| `ad1b7f16` | PAL 1.2 |

An unrecognized checksum is rejected with a diagnostic. A correct file size or copyright header alone is not enough to identify a retail revision. Games that read IPL fonts or version data need the corresponding EXI data even after their apploader has completed.

Firmware images remain private local inputs. The project does not distribute firmware or fetch it from the network.

The refreshed local check at `df978ec` reads the three `../bios/*/IPL.bin`
files without modifying them. Each is 2 MiB; their CRC32 values are
`dd8cab7c` (EUR), `8bdabbd4` (JAP) and `6d740ae7` (USA). None matches the
recognized full-ROM table, including after applying the scrambled-region
transform to check for an already decoded image. Both font streams pass
structural validation in every container. These inputs can supply validated
font slots through `--ipl-fonts`; complete firmware remains unidentified.
Commands, executable and input hashes, diagnostics and the transform check
remain in `.cache/continuation-20261022/bios-recheck-new-goal/`.

Font reads have a separate path: `--ipl-fonts PATH` validates the two compressed font blocks and their glyph metadata, then exposes those font slots. It supports GameCube and Wii software without accepting the container as validated full firmware. See [font validation](fonts.md).

## Wii system data

GameCube IPL is not Wii firmware. Wii boot uses the requested IOS version's checked memory profile and the selected partition's actual ticket and TMD. The local Galaxy disc requests IOS33. The runtime implements specific IOS services itself; it does not claim to run a complete installed IOS image or NAND.

Use `--user-dir PATH` for Wii files. Services create or read their data below `PATH/wii/`, preserve filesystem metadata, and use the active title's UID and group context. Installed-title queries count actual content and ticket files rather than assuming that private system data exists.

Wii services, Remote transport, motion input, and system-title dependencies remain incomplete. A service diagnostic names the next unsupported request. Supplying unrelated firmware will not resolve it.
