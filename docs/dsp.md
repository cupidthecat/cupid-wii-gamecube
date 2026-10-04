# DSP tasks and audio

The DSP engine handles the boot ROM mailbox protocol, initialization, CARD and the DAC audio task family. The runtime connects its mailbox and control registers through the device bus. The device layer keeps DMA status, interrupt masks and interrupt acknowledgments.

CPU mailbox writes become a complete message when the low half is written. The busy bit clears after the engine handles that message. DSP mailbox reads return the pending message's high half without consuming it; reading its low half consumes the message and clears the ready bit. A halted DSP keeps its previous readable message and leaves pending messages queued.

Reset starts the boot ROM protocol. Clearing the initialization bit starts the initialization task, which replies with its boot acknowledgment. The initialization-code bit clears after 130 time-base ticks. Task updates use the console's CPU clock and a one-millisecond interval.

ROM uploads record the source address, byte length, instruction-memory address, data length and entry point. The engine identifies the uploaded task from a computed hash of its bytes. CARD uses its mailbox completion and return-to-ROM behavior; it does not generate audio.

The audio task accepts setup, frame rendering, voice synchronization and task-switch messages. Setup reads coefficient tables and parameter-block addresses from guest RAM. Rendering waits for the CPU's voice masks, mixes eighty samples per channel and writes signed big-endian samples into the guest's left and right buffers. The guest interleaves those buffers and starts audio DMA. Host audio receives the bytes read by that DMA.

The mixer supports PCM8, PCM16, AFC with four-bit or two-bit sample codes, main-memory PCM streaming, square and saw waves, and supplied waveform patterns. It uses guest resampling and AFC coefficients, maintains decoder history and loop state, and writes back the mutable part of each voice block. Mixing includes volume ramps, positional volumes, low-pass and biquad filters, and four reverb rings. Wii audio reads the main-memory region selected by its base-address command; GameCube audio reads the same ARAM storage used by the device DMA engine.

Both ordinary and light mailbox protocols are implemented, including the smaller voice-block layout and explicit volume steps used by one boot firmware variant. Persistent audio state can be suspended for another task and resumed through the task protocol. Combined render commands consume their two reserved arguments without applying an additional effect, matching the behavioral reference.

The optional sample-history request count in a voice block is retained while ordinary decoding, mixing and mutable voice-state writes proceed. The behavioral reference leaves that requested history retention unmodeled in this profile. This is a deliberate compatibility behavior: no sample-history transfer is performed or claimed.

Unknown task hashes fail with the computed hash and upload descriptor. A general DSP instruction interpreter, variable FIR filtering, special command-12 variants and command-13 filtering remain unfinished. Task switches that request DSP data-memory transfers also fail explicitly. Main-memory PCM requests that outlast their remaining sample count are rejected because the behavioral reference's terminal-padding branch leaves part of the sample buffer undefined. These limitations can stop software that requests them; they are not replaced by silence or a completion acknowledgment.

The public DSP tests use synthetic byte sequences and mailbox transactions. They check halting, read consumption, queued interrupts, reset, initialization timing, upload parsing, CARD dispatch, supported audio decoding, voice-state changes, synchronization, task resume and malformed requests. An original guest program interleaves decoded PCM and programs audio DMA; the test checks the samples delivered through the device callback. The synthetic task fixture contains no console firmware. Differential results and source mappings are in [DSP behavioral reference](dsp-reference.md).
