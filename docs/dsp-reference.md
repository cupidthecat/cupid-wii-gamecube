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

The selected hash profiles include `0x56d36052` with per-frame synchronization and `0xd643001f` with main-memory audio addressing and combined render packets. The other recognized DAC profiles use the same implemented family with their documented protocol and layout differences. Recognition is not a claim that every command used by each title is supported.

## Compiled differential checks

On 2026-10-03, a separate local validation executable compiled the pinned audio protocol/mixer and mailbox source files directly. It supplied checked RAM and ARAM storage adapters and interrupt counters; no decoding, mixing or command handling was replaced. Save-state serialization was excluded. Upload switching and special command-12 helpers threw if reached, so the comparison did not exercise those paths.

The deterministic fixture seed was `0x43555049`. Four profiles (`0x56d36052`, `0xd643001f`, `0x24b22038`, `0x6ba3b3ea`) each ran 25 cases with two audio frames. Every checkpoint compared 24,576 bytes of RAM, all queued mailbox words and interrupt counts.

The run passed 525 checkpoints covering 12,902,400 compared RAM bytes. SHA-1 over the checkpoint bytes in execution order was `1799aab37f8c95703bd6468b7b9800723d578ea0`.

Cases covered square/saw waves, constant patterns, PCM8/PCM16 looping, both AFC formats, AFC cached samples and looping, main-memory streaming and wrap, constant samples, volume ramps, positional mixing, low-pass and biquad filters, reverb rings, and resampling ratios of one-half, one, four and nearly sixteen. The snapshots included output buffers, coefficient data and mutable voice fields. The comparison exposed an AFC terminal remaining-length mismatch; the implementation and a public regression now preserve that field.

The main-memory PCM exhaustion branch was excluded after a failed comparison and source inspection found that it overwrites downloaded samples and leaves part of its temporary input buffer unwritten. Requests entering that branch now fail explicitly. No stable reference result or hardware observation supports a terminal-padding policy yet.

`AddVoice` retains the voice's optional sample-history request count and renders normally; it does not implement the requested history transfer. This project follows that specific compatibility behavior. Public regressions compare flag-on and flag-off PCM and voice state across two frames, including ordinary and smaller blocks. The field itself remains unchanged. This does not establish hardware sample-history behavior.

On 2026-10-04, the compiled comparison was expanded to 33 cases per profile. New cases set request counts of 1, 20, 80 and 65,535 with streamed, constant and positional voices. All 693 checkpoints passed, comparing 17,031,168 RAM bytes together with mailbox words and interrupt counts. SHA-1 over the checkpoint bytes was `5bdf4ed62e68e1a0c1344be4938263ea8b150c47`. The existing upload, main-memory exhaustion and hardware-observation limits still apply.

A separate paired run held all input data constant and varied only the history request count. It compared each execution against the compiled renderer and compared flag-on RAM against its flag-off counterpart, excluding only the retained count itself. All 336 checkpoints passed over 8,257,536 RAM bytes; SHA-1 was `28b795bc017ff2106bbe0605bbe3f46994139f04`. Mailbox words and interrupt counts matched in each execution.

## Public tests and limits

Public tests check known PCM/AFC sample values, decoder history, loop positions, smaller parameter blocks, deferred commands, skipped voices, reverb/filter state, task suspension/resume and invalid requests. The guest DMA regression executes an original PowerPC interleaving loop and register writes before checking the device's delivered audio samples. These are synthetic software tests, not commercial-game audio or real-hardware validation.

The fixed-point mixer deliberately follows the behavioral reference's eighty-step volume ramp and sixteen-bit mixing-buffer wrap. The reference notes a different ramp step count on hardware. No hardware test was run here, and hardware equivalence is not claimed.
