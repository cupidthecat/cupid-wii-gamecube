# Instruction-based DSP

Supplying both `--dsp-rom PATH` and `--dsp-coeff PATH` selects DSP instruction
execution. The program ROM must contain exactly 8,192 bytes and the coefficient
ROM exactly 4,096 bytes, with big-endian words. Neither file is bundled. Without
these options, the existing task-level DSP remains the default.

The instruction backend is part of the runtime. It does not launch another
program. It connects to the same CPU mailbox registers, device interrupt latch,
main memory, ARAM and audio DMA as the task backend. ROM installation must happen
before execution starts. An invalid pair fails before replacing the task backend.

## Implemented paths

- Primary instruction dispatch and parallel extensions, register restrictions,
  40-bit accumulators, product arithmetic, status flags and address wrapping.
- Four hardware stacks, calls, returns, conditional branches, counted loops,
  exception delivery and external interrupts.
- Instruction ROM/RAM, data RAM, coefficient ROM and IFX accesses.
- CPU and DSP mailboxes, ready-bit consumption, interrupt requests, control reset
  and initialization-code loading with its delayed status-bit clear.
- Host-to-DSP and DSP-to-host data DMA, instruction DMA loading, instruction
  analysis refresh, masked transfers and odd byte counts.
- PCM and ADPCM sample acceleration, predictor/coefficient history, raw reads and
  writes, address limits, stop latches and end exceptions.
- Non-threaded interpreter slices, mailbox-read execution allowances, idle-loop
  detection and late scheduled-update correction.

The CPU scheduler grants DSP work at 12,600-cycle intervals. Execution uses the
source's six-to-one CPU/DSP budget conversion, retained remainder and mailbox
allowance. An eligible mailbox-high read consumes a 72-cycle allowance for twelve
DSP instructions. This is distinct from the default task backend's millisecond
updates. The interpreter's warmup, idle-probe and unchecked instruction batches
are retained, including their halt-check boundaries.

GameCube sample accesses use ARAM. Wii sample reads select MEM1 or MEM2 from the
address selector; the source's Wii write caller always selects MEM2. This is a
source-compatibility choice, not a new physical-console measurement.

## Bounds and limitations

This backend is an interpreter, not a DSP JIT. It does not establish complete
console parity or full-speed execution. Host audio remains separately selected
with `--audio sdl`; nonzero generated PCM does not prove audible or correct audio.

Instruction-memory output DMA has the selected source's defined no-effect path.
It is not an implemented IRAM readback transfer. Data transfers with rejected
host backing preserve their destination. Transfers beyond DSP RAM or supported
length bounds fail explicitly instead of reproducing an out-of-bounds host access
or a source process exit. Excessive scheduling lateness also fails explicitly.

Unmapped DSP accesses retain source return/no-write behavior and increment a
counter. This does not establish undocumented hardware behavior. No firmware,
game images or private test captures are included in the repository.

## Validation

The optional tests use revision `2c805fee8f232d090c454381bb1b8422242345e1`, selected
with `CUPID_BEHAVIOR_REFERENCE_ROOT`. They compile source translation units without
editing the checkout:

- `dsp_instruction_reference_checks`: 786,432 single-instruction dispatch cases,
  44 analyzed branch/loop steps and 1,320 non-threaded run-loop cases. Comparisons
  include registers, stacks, control state, data RAM and ordered IFX accesses.
  IFX is a controlled service in this instruction test.
- `dsp_accelerator_reference_checks`: 786,432 operations spanning every 16-bit
  sample format, history and stop-state changes, memory callbacks and exceptions.
- `dsp_hardware_reference_checks`: 34,208 IFX mailbox, interrupt, DMA and accelerator
  register cases using initialized source objects. This test requires Linux page
  protection and compares independent memory stores. It does not test the complete
  external scheduler or CPU control-register caller.

The instruction and accelerator comparisons compile source arithmetic with `-fwrapv` on GCC and
Clang. Its unsigned 16-bit multiplication otherwise promotes to signed `int`
before conversion, and optimized builds can produce different results for an
overflowing intermediate. The source ADPCM sum can also overflow a signed
32-bit intermediate. The project uses wide multiplication and explicit 32-bit
wrapping, respectively. This build qualification is part of the evidence, not a claim that
undefined C++ behavior is hardware behavior.

Public `dsp_instruction` tests need no firmware files. Authored instruction words
exercise register and loop behavior, sample boundaries, mailbox and DMA paths,
initialization and timing edges. An authored PowerPC guest reaches the instruction
DSP through ordinary MMIO on both console configurations.

A bounded private Sunshine pair using supplied ROMs reached 120 VI fields with
software and OpenGL compute rendering. The two runs used identical copied user
data. CPU and RAM hashes, audio hashes, DSP instruction counts (10,599,790), DSP PC
and the final raw frame matched. Both recorded zero unmapped DSP accesses
and 62,270 nonzero sample values. OpenGL used Mesa llvmpipe, not a physical GPU;
fragment interlock was unavailable. Independently created user directories had
different memory-card identity bytes, so their RAM hashes were not comparable.
Host audio was disabled. Longer gameplay, Wii titles and host-audible output
remain separate qualification work.
