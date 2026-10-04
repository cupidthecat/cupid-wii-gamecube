# CLI

Run `cupid-wii-gamecube --help` for the complete option list. Options that take values require a separate argument. Integer bounds and disc offsets accept decimal or `0x` hexadecimal values.

## Launching software

```sh
cupid-wii-gamecube --game "/path/to/program.dol" --console gamecube \
  --user-dir "/path/to/userdata" --max-instructions 10000000 --run-seconds 30
```

`--game` accepts DOL, ELF, RVZ, ISO, or GCM files. Disc metadata selects the console in `auto` mode. Executables default to GameCube mode; select `--console wii` when required. A disc's console must agree with an explicit selection.

`--graphics software` selects the CPU rendering path. SDL displays a minimal game-output window. `--headless` disables that window. `--audio sdl` resamples and mixes guest PCM into a 48 kHz stereo stream; `--audio none` leaves host audio disabled. `--remote-speakers on` includes decoded Remote packets and requires SDL audio; the default is `off`. Headless runs can enable audio. A headless-only build excludes the SDL output backend. See [host audio](host-audio.md) for buffering and verified scope.

`--display-aspect standard` uses the VI active-area ratio for a standard television signal. `widescreen` expands that ratio by four thirds; `pixels` uses the raw framebuffer dimensions. The default is `standard`. The window centers the image with black bars when needed, and mouse pointing follows that displayed area. Frame dumps retain the original guest framebuffer dimensions.

`--bios PATH` validates a recognized 2 MiB GameCube IPL dump and installs its decoded bytes for EXI reads. `--validate-bios --bios PATH` validates it without running software. It does not supply Wii firmware. See [boot and system data](boot.md).

`--ipl-fonts PATH` loads separately validated font blocks from a 2 MiB container, with `--validate-fonts` available for a validation-only command. Select this or `--bios`. See [font validation](fonts.md) for the exposed ranges and checks.

## Bounds and diagnostics

| Option | Meaning |
| --- | --- |
| `--max-instructions N` | Absolute CPU step-attempt bound, including apploader execution, interrupts, faults and zero words. Default: 1,000,000. Zero disables it. |
| `--run-seconds N` | Wall-clock bound. Default: 10 seconds. Zero disables it. |
| `--max-frames N` | Bound on frames actually presented by VI. Zero disables it. |
| `--dump-state PATH` | Final JSON machine state, including boot stage, CPU state, device fields, graphics commands, frames, and audio sample count. |
| `--trace PATH` | Instruction state as JSON lines. |
| `--trace-count N` | Maximum trace records. Default: 1,000. |
| `--trace-start N` | Begin at the given absolute instruction count. |
| `--trace-after-boot` | Also require completion of the guest apploader before tracing. |
| `--log PATH` | Progress and diagnostic messages. |
| `--frame-dump PATH` | Last actual guest frame as binary PPM, if a frame was presented. |

The runtime prints final state to stdout even when `--dump-state` is absent. Traces contain register state after each CPU step, together with its previous PC and the most recently fetched opcode. An interrupt or fault before fetching can retain the earlier opcode value. Step and time bounds also apply during boot. Final `step_attempts` counts every CPU step entered; `instructions` counts successfully executed nonzero instructions. Each trace record retains that successful count in `step` and supplies a separate `attempt` sequence. Replay events and `--trace-start` still use the successful instruction count.

Time checks and interactive polling occur at startup and at most every 4,096 attempted steps. Repeated guest faults or zero-cycle fetches therefore cannot freeze polling or evade an enabled step bound. Zero disables only the selected bound; disabling every bound allows an indefinite run.

Audio diagnostics count individual signed 16-bit sample values, so two values form one stereo frame. `audio_nonzero_samples` counts values with nonzero amplitude; `audio_peak` is the largest absolute value, including 32768 for the negative limit. `audio_hash` is FNV-1a over canonical big-endian sample bytes in arrival order. These measurements include headless runs with host audio disabled. A nonzero value establishes signal data in the tested span; it does not verify correct mixing or audible playback.

Those fields retain the main DMA values before host mixing. Remote output leaves them unchanged. `host_audio_frames`, `host_audio_nonzero_samples` and `host_audio_dropped_granules` separately count host callback frames, signal values and queue overflow. Their wall-clock timing makes them unsuitable for deterministic guest-state comparisons.

`page_table_write_errors` counts rejected backing writes when translation updates page-table reference/change bits. The translation and local TLB flags can remain valid even when a bounded host write rejects allocation padding. This counter is separate from PI interrupt requests and guest storage exceptions. See [memory boundaries](memory.md).

`instruction_backing_read_errors` counts rejected whole-word or whole-line instruction copies. Such a rejection leaves the destination unchanged and raises no PI request. A later cache hit can return retained words without another backing-copy attempt. Invalid locked misses instead report an unsupported runtime condition. A fetched zero advances the PC without dispatching an instruction or adding guest cycles.

```sh
cupid-wii-gamecube --game "/path/to/game.rvz" --headless --audio none \
  --user-dir "/path/to/test-data" --max-instructions 30000000 --run-seconds 60 \
  --trace "/path/to/startup.jsonl" --trace-after-boot \
  --trace-start 3000000 --trace-count 10000 --dump-state "/path/to/state.json"
```

## Input

`--input-config PATH` reads `action=key` lines. Empty lines and lines beginning with `#` are ignored. The keys use SDL key names. Keyboard, mouse, controller, Remote, Nunchuk, tilt, and shake defaults are documented in [input](input.md), together with the verified test scope and remaining transport limitations.

`--input-replay PATH` reads controller events scheduled by absolute instruction count or VI field count. It works headless and replaces live input sampling. See [the replay format](input.md#input-replay) for actions, ranges, and ordering.

## Inspecting a disc

```sh
cupid-wii-gamecube --game "/path/to/game.rvz" --inspect
cupid-wii-gamecube --game "/path/to/game.rvz" --list-files
cupid-wii-gamecube --game "/path/to/game.rvz" --extract-file "/opening.bnr" \
  --output "/path/to/output.bnr"
```

`--inspect` prints metadata. `--list-files` prints FST entries as JSON lines. File extraction uses an exact file path from that list, with optional `--offset` and `--length`. Omitting length extracts the remainder of the file.

`--read-disc` reads raw disc bytes; `--read-game` reads plaintext bytes from the selected game partition. Both require an output path and a length between 1 and 33,554,432 bytes. Select one inspection or extraction command at a time. Output paths that alias an input asset are rejected.

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Inspection, extraction, self-test, or a bounded run completed. |
| 2 | Invalid CLI usage. |
| 3 | Input could not be opened or validated. |
| 4 | Unsupported guest behavior or runtime failure. |
| 5 | Filesystem operation failed. |
| 6 | Synthetic self-test failed. |

An exit at a run limit does not establish that a game boots, reaches its title screen, or is playable. Guest exception loops can continue until a bound; inspect the trace and state to identify them.
