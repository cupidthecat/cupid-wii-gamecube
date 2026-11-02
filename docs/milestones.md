# Implementation milestones

The goal is complete GameCube core behavior first, then Wii, with both software and hardware rendering. Playable games and sustained speed are acceptance checks, not substitutes for CPU, memory, graphics, audio and device coverage. Every milestone needs executable behavior and a repeatable test. Launcher and quality-of-life work is outside this scope.

| Slice | Current evidence | Next completion condition |
| --- | --- | --- |
| Build and packages | Native MSVC, Linux GCC/Clang, sanitizer builds, public suites, and extracted CLI packages have passed intermediate snapshots. | Repeat all checks on the final integrated revision and publish artifacts only when authorized. |
| Interpreter and memory | Integer/FPU/paired/MMU/cache fixtures pass; estimate and fused helpers have direct bit comparisons. Guarded resident RAM groups pass 351,262 complete CPU/RAM/ordered-bus comparisons and 57,533,610 grouped instructions on strict GCC, MSVC and Clang sanitizers. Sunshine stock, recorded, repeated-step and native startup controls agree; the interpreter remains default. | Preserve fetched-word visibility and interrupt/device timing while reducing measured admission costs; measure whole-runtime gains and extend data-cache visibility checks. |
| Disc and executable boot | Both RVZ discs' decoded reads, filesystems, and metadata were compared directly. Both apploaders complete and both games reach file selection on Windows and Linux. | Complete defined boot/system-data behavior and continue normal guest execution into gameplay. |
| Devices and storage | Synthetic interrupt/DMA/VI/SI/EXI/card/IOS fixtures pass, including overlapping card completion and actual startup-fault regressions. Seventy Machine fixtures check event cuts, DMA-visible RAM changes, IOS replies, replay boundaries and grouped execution. Current Sunshine captures show card-creation confirmation; Galaxy launches its copied slot. | Validate gameplay save creation, persistence, reopening, and interrupt timing in both games. |
| Graphics | Commands, transforms, textures, TEV, EFB access and copies have focused and direct comparisons. Required OpenGL pixel suites pass on RTX 3060 and Mesa. A frozen Galaxy projection/state-cache run matches software through field 1,050, including all RAM bytes. The repaired Sunshine transition completes and renders airstrip gameplay through field 10,000 on Windows. | Extend gameplay coverage on both platforms; diagnose remaining GPU fallback causes and measure rendering acceleration in complete runs. |
| Audio | Captures contain nonzero guest PCM for both games on both platforms. Disabled main DMA now supplies timed silence; device checks cover both consoles and rates. The Windows gameplay endpoint capture still fails qualification, and the failed shared WSLg transport retains its original scope. | Verify current gameplay audio and timing in both games, and reliable physical Linux playback. |
| Wii services and input | Title context, FS/ES/DI, network clock/configuration, Bluetooth/HID transport, host motion mapping, and deterministic input replay have executable tests. Exact settled camera projection passes both platforms. Galaxy's Linux raw-IR replay visibly supplies pointer input, hover and accepted slot selection. | Cross-platform game acceptance of live Remote/Nunchuk/IR/motion input and controllable gameplay. |
| Commercial playability | Frozen Windows runs show Sunshine FLUDD use, goop removal, a coin and save completion; Galaxy shows movement, jumping, a Star Bit and the attack cinematic. A later Sunshine depth-fix replay retains checkpoint images through field 49,620 but lacks final state and exit status. Linux gameplay remains unqualified. See compatibility for each source and run scope. | Completed levels, clean gameplay audio, restoration of saved level progress and full-speed demonstrations in both games on both platforms using the current implementation. |

Continue from the first real divergence after each fix. Preserve unrelated work and private assets. Public CI must remain independent of console dumps and commercial discs.


## Remaining core work

The runtime is not a complete core implementation. Known gaps include:

- Data-cache visibility, dirty writeback, eviction and locking. Instruction-cache
  checks do not qualify these separate data-cache behaviors.
- A general DSP instruction interpreter and broader audio-task coverage. Unknown
  task hashes and task data-memory transfers still stop explicitly.
- Remaining defined MMIO, device and IOS operations currently rejected by their
  handlers. A successful startup does not exercise all those paths.
- Complete timing and interrupt qualification across CPU, DMA, graphics, audio
  and controllers, including event boundaries during grouped execution.
- Current-source physical-GPU and audio-endpoint qualification on both host
  platforms, followed by completed levels and saved-progress restoration.

Continue in the same implementation PR. Compare each new behavior with its
pinned source implementation and callers, retain regression evidence, and keep
undefined source behavior separate from implemented compatibility choices.
