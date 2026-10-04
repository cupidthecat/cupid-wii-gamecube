# Implementation milestones

The goal is playable gameplay in both supplied games on Windows and Linux. Every milestone needs executable behavior and a repeatable test.

| Slice | Current evidence | Next completion condition |
| --- | --- | --- |
| Build and packages | Native MSVC, Linux GCC/Clang, sanitizer builds, public suites, and extracted CLI packages have passed intermediate snapshots. | Repeat all checks on the final integrated revision and publish artifacts only when authorized. |
| Interpreter and memory | Integer/FPU/paired/MMU/cache fixtures pass; estimate and fused helpers have direct bit comparisons. Locked-cache register and physical-transfer regressions pass the actual Sunshine startup failure. | Broader instruction/state differential execution, data-cache visibility, and appropriate redistributable hardware tests. |
| Disc and executable boot | Both RVZ discs' decoded reads, filesystems, and metadata were compared directly. Both apploaders complete. | Resolve required system data and continue real guest startup to title screens. |
| Devices and storage | Synthetic interrupt/DMA/VI/SI/EXI/card/IOS fixtures pass, including regressions for actual startup faults. | Validate guest save creation, persistence, reopening, and interrupt timing in both games. |
| Graphics | Commands, vertex transforms, rasterization, textures, TEV, EFB writes, texture copies, and scaled XFB copies have focused and direct comparisons. Guest CPU framebuffer reads, VI aspect, and host display have regressions. | Resolve remaining defined pixel behavior and unsupported geometry; verify real guest title and gameplay rendering. |
| Audio | DSP protocol/mixing comparisons and guest PCM-to-audio-DMA fixtures pass. Both platforms capture identical nonzero PCM in Sunshine startup. | Verify gameplay audio in both games, audio timing, and host playback. |
| Wii services and input | Title context, FS/ES/DI, network clock/configuration, Bluetooth/HID transport, host motion mapping, and deterministic input replay have executable tests. | Reach Galaxy's menus with accepted Remote/Nunchuk/IR/motion input, then controllable gameplay. |
| Commercial playability | Sunshine reaches its intro title card with matching final state, pixels, and PCM hash on Windows and Linux. Galaxy renders its wrist-strap safety screen. Neither interactive title screen is verified. | Rendered gameplay, audio, working controls, and save/load demonstrations in both games on both platforms. |

Continue from the first real divergence after each fix. Preserve unrelated work and private assets. Public CI must remain independent of console dumps and commercial discs.
