# Architecture

The product executable links `cupid_core`, an integrated C++20 runtime. `runtime::Machine` owns guest memory, the interpreter, hardware devices, graphics command processor, DSP engine, and Wii IPC services. Each subsystem has its own directory under `include/cupid/` and `src/`.

## Execution and memory

The interpreter fetches guest instructions through its instruction cache, translates effective addresses through BATs or hashed page tables, and reads or writes the physical bus. Memory owns 24 MiB of real MEM1 within a 32 MiB allocation, Wii MEM2, and locked-cache storage. [Memory operations](memory.md) distinguish CPU data mirrors from bounded host and DMA spans. Instruction-cache identity normalization leaves cold backing reads subject to those bounds. Translation faults enter guest exception vectors; unresolved physical data requests raise the PI memory-access cause. Explicitly unfinished device behavior produces a diagnostic.

After every instruction, Machine advances graphics events, DSP work, IOS requests, and hardware devices by the returned cycle count. It checks external interrupt state before the next instruction. Host input and wall-clock bounds are polled every 4,096 step attempts. Replay events use their next instruction and field deadlines. This scheduling is observable and deterministic for the tested fixtures, but full hardware edge timing remains unverified.

The optional native integer backend emits x86-64 code for fetched immediate
and rotate instructions. It shares the interpreter's fetch, exceptions,
performance-monitor updates and cycle accounting, and returns to Machine
after each instruction. Other operations use the existing handlers. Its
bounded code cache keys actual fetched words, preserving retained instruction
cache visibility after guest stores. Code pages alternate writable and
executable protection on the owning CPU thread. This is an experimental
single-instruction backend; the default remains the interpreter.

## Boot and discs

The disc reader supplies random raw reads and plaintext game-partition reads. Its filesystem reader resolves FST paths without changing the disc. Machine executes the disc's actual guest apploader and fulfills its transfer requests, then continues from the returned entry point. The DOL and ELF loaders validate their sections before modifying memory. The current bootstrap does not execute the complete IPL boot sequence.

Wii boot selects the disc's requested IOS memory profile and establishes the title context from its ticket and TMD. IPC services operate on explicit user storage and actual disc bytes. Implemented services have protocol-specific error handling; unfinished requests fail with a named diagnostic.

## Graphics and audio

CPU write-gather bytes enter the PI/CP FIFO ring. Graphics commands load CP, XF, and BP state, decode vertices, transform them, and assemble and clip primitives. The selected software or [OpenGL compute renderer](hardware-rendering.md) rasterizes covered pixels and runs TEV and pixel operations. The GPU path synchronizes raw EFB data for CPU pokes, reads, copies and checked software fallback. EFB copies write guest RAM. VI presentation reads guest YUYV framebuffer data; the SDL backend displays those bytes after color conversion.

DSP program protocols produce planar PCM in guest RAM. Guest code can interleave it and configure audio DMA. The [host audio backend](host-audio.md) resamples main DMA, decoded [disc streaming](disc-audio.md) and optional Remote speaker packets into a fixed-rate SDL callback. Separate guest and host diagnostics retain the origin of the samples.

## Input and storage

The host output module polls keyboard, mouse, and mapped controller state. GameCube SI reports pads to guest software and forwards motor commands. Wii host mappings supply Remote and Nunchuk buttons, stick state, accelerometers, and infrared points; IPC carries their HID reports through USB/Bluetooth transport. [Input](input.md) records current behavior and unverified motion requirements. The product has no launcher or settings GUI.

GameCube SRAM and memory cards live in the selected user directory. Wii files live below its `wii/` directory with persistent metadata and title ownership. SRAM and card updates use temporary files and replacement after successful writes. Input discs, firmware, and the behavioral checkout are read-only inputs.
