# Compatibility

Commercial gameplay and full-speed execution remain unverified. Both supplied games reach file selection and generate nonzero guest PCM on Windows and Linux. Sunshine accepts menu movement, jumping and slot input. Galaxy accepts pointer and slot input; native runs also create game-data files, read back a complete saved file and open its restored slot menu. These observations use private local dumps and normal guest input.

| Game | Platform | Strongest observed behavior | Still unverified |
| --- | --- | --- | --- |
| Super Mario Sunshine, USA (`GMSE01`) | Windows, MSVC Release | Accepted Start, menu movement, jumping and slot A; rendered opening movie. | Controllable level gameplay, guest save creation and reopening, commercial host audio playback, full speed. |
| Super Mario Sunshine, USA (`GMSE01`) | Linux, GCC Release | Accepted menu controls and slot A; the 48-billion-step run progresses through the opening airstrip/movie scenes. | Same playability requirements as Windows. |
| Super Mario Galaxy, USA (`RMGE01`) | Windows, MSVC Release | Pointer, slot and icon selection accepted. Guest FS requests create `GameData.bin` and `banner.bin`; a fresh run reads and closes all 48,640 game-data bytes with an exact match to its copied seed file, opens the restored `4 Mario` slot menu, and progresses through Play into the opening storybook. | Controllable gameplay, complete gameplay save/load behavior, commercial host playback, full speed. |
| Super Mario Galaxy, USA (`RMGE01`) | Linux, GCC Release | Title, pointer, file-slot hover and slot selection accepted; file-creation confirmation reached. | Accepted live controls through gameplay, complete save creation/reload, commercial host playback, full speed. |

Game observations above use software graphics, headless output, disabled host audio, bounded execution and private copies of user storage. Sunshine uses structurally validated USA IPL font slots. Galaxy uses its real partition metadata and required IOS33 profile. Synthetic host-audio and hardware-rendering tests have separate scope; they do not change a game's compatibility milestone.

Exact older bounds, frame/audio hashes, controls and source snapshots are preserved in [the run history](compatibility-history.md). Menu-input checkpoints include `c140c115aa881c19338936ca565668884578698e` and the subsequent validated input-follow snapshots. That history distinguishes an executable's configured revision string from the source snapshot actually used to build it.

## Current continuation runs

The October 4, 2026 Sunshine native continuation uses a sealed working snapshot after `563d1a8853a351184d35f0f0e35d6b4f0a13392a`, including aligned cache reads, carry corrections and queued card-DMA completion. Its separate Linux build passes the public headless suites. Both plain CLIs and forwarding observers produce identical complete reported state at the 100-million-attempt bound on both platforms. Retained commercial prefixes at fields 30, 120, 300 and 450 also match the earlier Linux observer apart from configured revision metadata. This verifies those prefixes, not the unfinished run's future outcome.

The fresh native Galaxy continuation uses the archived `563d1a8853a351184d35f0f0e35d6b4f0a13392a` source and a separately copied guest-created save directory. Its executable retains configured `5632c6a1df64` metadata; source and binary hashes are recorded together. The guest's read reply at instruction 7,427,126,331 returns all 48,640 game-data bytes, and the later close succeeds. The retained reply bytes equal the copied seed exactly; the seed is unchanged. Field 750 visibly shows the full title logo and nonzero PCM. Field 1,350 shows the restored `4 Mario` slot with zero stars and star bits, plus Play This File, Copy, Icon and Erase after ordinary slot input. Its state has 16,337,093,317 instructions, 1,445,504 PCM values, 706,533 nonzero values and zero DAR/DSISR. This establishes guest file I/O, restored slot selection and menu rendering. Controllable gameplay and later saves still need evidence.

Both continuations use ordinary replay input through the public input-follow path. Private traces, images, save bytes and exact commands remain local. A process being live or reaching a bound is not a compatibility result; report its captured behavior separately.

The native Galaxy continuation's field 1,770 shows the opening storybook after the normal Play press at field 1,530. Its state has 21,313,875,794 instructions, 1,893,952 PCM values, 1,093,482 nonzero values and zero DAR/DSISR. The current native Sunshine continuation shows the aircraft at field 2,820 and the opening airstrip at 2,940. Retained guest SI reads establish movement, jumping, physical Start and the later A confirmation through the normal pad path. Its SRAM and card bytes still match the copied seed. These are opening-sequence observations; controllable level gameplay remains unverified.

## Firmware and performance

The three local IPL candidates have the expected 2 MiB size but fail recognized full-ROM checksums. Their boot-ROM bytes are not accepted as firmware. The USA container's decompressed font slots pass independent structure checks and supply bounded EXI font reads. It does not provide Wii system data. See [font validation](fonts.md) and [boot requirements](boot.md).

The tested host has an Intel Core i9-10900F and NVIDIA RTX 3060. [Hardware-rendering tests](hardware-rendering.md) execute real compute work and pass exact pixel comparisons on the RTX 3060 and Mesa. The [performance page](performance.md) records startup and component workloads separately. None establishes full-speed commercial gameplay.

## Recording a result

Record the source revision and worktree status, platform/compiler, graphics and audio backend, console, input, storage seed, run bounds, final state and visible behavior. Preserve exact source and executable hashes when configured revision metadata predates compilation.

Use separate milestones: apploader completed, title reached, gameplay reached, and playable. Playable requires rendered gameplay, usable controls, audio and demonstrated save/load. Unit tests, a title frame and a successful bounded exit establish only their named observations.
