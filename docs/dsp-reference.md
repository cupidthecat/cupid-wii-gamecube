# DSP behavioral reference

The behavioral reference is pinned at `562875dd60fd0ff50b65dbbb611a2096802cd1b9`. The source paths below are relative to that checkout. The implementation was written for this project's memory and device interfaces.

| Implemented behavior | Source files and functions read |
| --- | --- |
| Mail readiness, consumption, halting and interrupt ordering | `Source/Core/Core/HW/DSPHLE/MailHandler.{h,cpp}`, `PushMail`, `ReadDSPMailboxHigh`, `ReadDSPMailboxLow` |
| DSP register dispatch and update timing | `Source/Core/Core/HW/DSPHLE/DSPHLE.{h,cpp}`, mailbox/control methods, `DSP_UpdateRate`; `Source/Core/Core/HW/DSP.cpp` callers |
| ROM uploads, initialization and CARD | `Source/Core/Core/HW/DSPHLE/UCodes/{ROM,INIT,CARD}.{h,cpp}`, initialization/mail/update handlers |
| Task hashes, switching and resume packets | `Source/Core/Core/HW/DSPHLE/UCodes/UCodes.{h,cpp}`, `UCodeFactory`, `PrepareBootUCode`, `NeedsResumeMail`; `DSPHLE.cpp`, `SetUCode`, `SwapUCode`; `Source/Core/Common/Hash.cpp`, `HashEctor` |
| DAC profiles, command packets and synchronization | `Source/Core/Core/HW/DSPHLE/UCodes/Zelda.{h,cpp}`, profile table, `HandleMailDefault`, `HandleMailLight`, `RunPendingCommands`, `SendCommandAck`, `RenderAudio` |
| Voice blocks, decoding, resampling and mixing | `Zelda.cpp`, voice-block layout, `FetchVPB`, `StoreVPB`, `LoadInputSamples`, PCM/AFC/MRAM download methods, `DecodeAFC`, `Resample`, `AddVoice`, filter/reverb methods, `FinalizeFrame`; fixed-point helpers in `Zelda.h` |
| JoyBoot authentication in light audio tasks | `Source/Core/Core/HW/DSPHLE/UCodes/GBA.{h,cpp}`, `ProcessGBACrypto`; `Zelda.cpp`, `SUPPORTS_GBA_CRYPTO` profiles, `HandleMailLight`, `RunPendingCommands`; `UCodes.cpp`, `UCodeFactory` callers |
| Scoped two-word command `0x0c` compatibility | `Source/Core/Core/HW/DSPHLE/UCodes/Zelda.cpp`, `WEIRD_CMD_0C` profile mappings, `HandleMailLight`, `RunPendingCommands`, `SendCommandAck`; `MailHandler.cpp`, pending-mail and interrupt handling |
| Standalone handheld task, pending-mail IRQ and task controls | `GBA.{h,cpp}`, `Initialize`, `HandleMail`, `Update`; `UCodes.{h,cpp}`, `PrepareBootUCode`, task constants and hash mapping; `DSPHLE.cpp`, `SetUCode`, `SwapUCode`; `Source/Core/Common/Hash.cpp`, `HashEctor` |

The selected hash profiles include `0x56d36052` with per-frame synchronization and `0xd643001f` with main-memory audio addressing and combined render packets. The other recognized DAC profiles use the same implemented family with their documented protocol and layout differences. Recognition is not a claim that every command used by each title is supported.

## Compiled differential checks

On 2026-10-03, a separate local validation executable compiled the pinned audio protocol/mixer and mailbox source files directly. It supplied checked RAM and ARAM storage adapters and interrupt counters; no decoding, mixing or command handling was replaced. Save-state serialization was excluded. Upload switching and special command-12 helpers threw if reached, so the comparison did not exercise those paths.

The deterministic fixture seed was `0x43555049`. Four profiles (`0x56d36052`, `0xd643001f`, `0x24b22038`, `0x6ba3b3ea`) each ran 25 cases with two audio frames. Every checkpoint compared 24,576 bytes of RAM, all queued mailbox words and interrupt counts.

The run passed 525 checkpoints covering 12,902,400 compared RAM bytes. SHA-1 over the checkpoint bytes in execution order was `1799aab37f8c95703bd6468b7b9800723d578ea0`.

Cases covered square/saw waves, constant patterns, PCM8/PCM16 looping, both AFC formats, AFC cached samples and looping, main-memory streaming and wrap, constant samples, volume ramps, positional mixing, low-pass and biquad filters, reverb rings, and resampling ratios of one-half, one, four and nearly sixteen. The snapshots included output buffers, coefficient data and mutable voice fields. The comparison exposed an AFC terminal remaining-length mismatch; the implementation and a public regression now preserve that field.

The main-memory PCM exhaustion branch was excluded after a failed comparison and source inspection found that it overwrites downloaded samples and leaves part of its temporary input buffer unwritten. Requests entering that branch now fail explicitly. No stable reference result or hardware observation supports a terminal-padding policy yet.

`AddVoice` retains the voice's optional sample-history request count and renders normally; it does not implement the requested history transfer. This project follows that specific compatibility behavior. Public regressions compare flag-on and flag-off PCM and voice state across two frames, including ordinary and smaller blocks. The field itself remains unchanged. This does not establish hardware sample-history behavior.

On 2026-10-04, the compiled comparison was expanded to 33 cases per profile. New cases set request counts of 1, 20, 80 and 65,535 with streamed, constant and positional voices. All 693 checkpoints passed, comparing 17,031,168 RAM bytes together with mailbox words and interrupt counts. SHA-1 over the checkpoint bytes was `5bdf4ed62e68e1a0c1344be4938263ea8b150c47`. The existing upload, main-memory exhaustion and hardware-observation limits still apply.

A later unchanged-source comparison checked the defined no-effect handling of variable-FIR
requests. Paired count-on/count-off cases cover counts 1, 2, 20 and 31, nonzero coefficient and
history data, ordinary low-pass and biquad filtering, streamed and constant input, positional
mixing, and four profile variants. All **336 checkpoints** passed, comparing **8,257,536 RAM
bytes**, mailbox words and interrupt counts. SHA-1 over the compared RAM was
`19d786235a60ce05f4b1d526e2a3c81619a856d8`. The optional FIR fields remain unmodeled; no FIR
history update or filtering was added. `ZeldaAudioRenderer::AddVoice` logs this request only
under its strict diagnostic build and proceeds through the ordinary renderer.

The named command `0x0d` was also compared through the complete unchanged mailbox and renderer
implementation. All nine standard-protocol profiles were exercised with varying sync values,
eight immediate commands, a command queued during rendering, completion and resume. All
**3,475 checkpoints** passed, comparing **85,401,600 RAM bytes**, mailbox words and interrupt
counts. SHA-1 over the compared RAM was `b530339a41384347b37e4ec848e078a9e3e69593`.
`ZeldaUCode::RunPendingCommands` defines either a parameter-free NOP or consumption of one
parameter followed by the ordinary acknowledgment, without an additional audio effect.
Public regressions cover both forms, delayed acknowledgment, unchanged guest RAM and rejection
of a missing required parameter. These compiled comparisons do not establish real-hardware
FIR or command effects.

The original JoyBoot authentication helper was compared with the complete unchanged `GBA.cpp`
implementation. Its memory adapter supplied checked byte-order reads and writes; the calculation
itself was compiled directly. All **1,769,472 cases** passed, comparing **226,492,416 RAM bytes**,
with SHA-1 `aa7d151f6a37df2e2208a4c4779f1a822a99a0b5`. Cases cover all 65,536 packet-pair counts
and eight alignment residues, all low-byte palette and signed-speed combinations at length
boundaries, random full-width fields, unsigned length rollover, and cached/uncached RAM aliases.
Each 128-byte comparison includes input words, both output words and untouched guard bytes.

A separate comparison compiled the full unchanged `Zelda.cpp`, `GBA.cpp` and `MailHandler.cpp`
caller path for profiles `0xdf059f68` and `0x4be6a5cb`. All **393,218 checkpoints** passed over
**50,331,904 RAM bytes**, queued mailbox words and interrupt counts. SHA-1 over the RAM was
`ac2077b16264e96bbd8976c66ef4daa5174a898a`. This covers initialization, 65,536 sequential requests
per profile, parameter receipt, acknowledgments, update calls and address aliases. Upload switching
and save-state adapters were outside these cases. The unknown command-12 variants remain explicit
failures; this does not establish standalone task, JoyBus transfer or hardware behavior.

The standalone task was then compared with the full unchanged `GBA.cpp`, `UCodes.cpp`,
`MailHandler.cpp` and `Hash.cpp` implementations. With seed `0x5441534b`, both console modes
ran 16,384 cases each. All **939,336 checkpoints** passed over **120,235,008 RAM bytes**,
mailbox readiness/read values, interrupt counts and task-transition hashes. SHA-1 over the
RAM was `1be64361ed70247e26b946409d871b8d61d6510b`. Cases cover all upper address nibbles,
pending, drained and halted queues, waiting-state controls, reset and ten-word uploads with
size truncation and address aliases.

The checked memory adapter supplied byte-order reads and writes. Task-selection adapters
recorded reset/swap requests and cleared pending mail; the selected next task was outside
this pure-task comparison. Dumping was disabled and logging discarded. The complete shared
upload parser, authentication calculation and instruction-byte hash were compiled unchanged.
Requested DSP data-memory transfers remain explicit failures in this project. Separate
authored PowerPC tests exercise the real Engine/device register path, periodic IRQ boundaries
and audio-to-task and task-to-audio return. These checks do not establish DSP instruction
execution, save-state serialization, a physical handheld link or commercial-game use of
the standalone task.

A separate paired run held all input data constant and varied only the history request count. It compared each execution against the compiled renderer and compared flag-on RAM against its flag-off counterpart, excluding only the retained count itself. All 336 checkpoints passed over 8,257,536 RAM bytes; SHA-1 was `28b795bc017ff2106bbe0605bbe3f46994139f04`. Mailbox words and interrupt counts matched in each execution.

A further comparison compiled the complete unchanged `DSPHLE.cpp` register and
task-switch caller, together with `ROM.cpp`, `GBA.cpp`, `Zelda.cpp`, `UCodes.cpp`,
`MailHandler.cpp` and `Hash.cpp`. Both consoles ran 4,096 cases with seed
`0x50455253`. All 802,816 checkpoints passed over 102,760,448 RAM bytes, mailbox
readiness and consumption, CPU mailbox values and interrupt counts. SHA-1 over
the RAM was `f468e83687a15fd1e4b3b5fe444e650de14b3d98`.

These cases boot through the ordinary ROM mailbox, alternate the initial task,
compute authentication results, upload task descriptors and return to the
suspended task. They check halted mailboxes, pending and drained updates, ignored
control words, reset and all upper address nibbles. The audio profile in this
caller comparison is `0x56d36052`; it checks task persistence and its deferred
resume word without rendering an audio frame.

The fixture supplied checked RAM, interrupt counters and a clock adapter that
calls one update per modeled interval. Serialization, diagnostic logging and
code dumping were excluded. Factory classes outside the tested ROM, handheld
and audio profiles rejected construction. The tested register, upload, factory
selection and suspension methods were compiled unchanged. The project DSP units
were rebuilt together against a copied coherent core and matching headers;
input hashes matched before and after execution. These checks cover software
task transitions, not physical link transfers or scheduler timing on hardware.

## Two-word command compatibility comparison

The unchanged full `Zelda.cpp` renderer and command handlers, together with
`MailHandler.cpp`, were compiled against checked RAM, ARAM and interrupt
adapters. For profiles `0x24b22038` and `0x42f64ac4`, the fixture used 25 sample
and voice-block cases each, with 32 two-word commands before rendering and eight
afterward. It compared partial command receipt, acknowledgments, interrupt
counts, decoded PCM and mutable RAM. Ten other profiles verified their existing
parameter-free `0x0c` paths over 256 synchronization values each; the separately
tested authentication profiles retained their computed one-parameter path.

All 16,234 checkpoints passed over 217,220,352 RAM bytes, with SHA-1
`7abcb7738bac25d77d5f826c668516dc8c01cf49`. Source and fixture hashes matched
before and after execution. The previous implementation's explicit rejection
was reproduced before the correction. Serialization and task switching were
outside this fixture; their adapters did not replace the tested command,
renderer or mailbox methods. This comparison establishes the named profiles'
defined two-word compatibility behavior, not the unknown operation's purpose.

The authored nine-instruction PowerPC regression passed on both consoles in
strict GCC and MSVC builds. The changed DSP units and guest fixture also passed
Clang AddressSanitizer and UndefinedBehaviorSanitizer checks while linked to a
copied coherent core. The guest sees no acknowledgment until both arguments are
written, reads the exact reply, verifies unchanged RAM and executes another
command. Unrelated light-protocol command rejection remains checked.

## Public tests and limits

Public tests check known PCM/AFC sample values, decoder history, loop positions, smaller parameter blocks, deferred commands, skipped voices, reverb/filter state, task suspension/resume and invalid requests. The guest DMA regression executes an original PowerPC interleaving loop and register writes before checking the device's delivered audio samples. These are synthetic software tests, not commercial-game audio or real-hardware validation.

The fixed-point mixer deliberately follows the behavioral reference's eighty-step volume ramp and sixteen-bit mixing-buffer wrap. The reference notes a different ramp step count on hardware. No hardware test was run here, and hardware equivalence is not claimed.
