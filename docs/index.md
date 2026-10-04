# Documentation

## Running and building

- [Build](build.md): Windows and Linux toolchains, dependencies, presets, and packages.
- [CLI](cli.md): launching programs, input mappings, diagnostics, and exit codes.
- [Boot and system data](boot.md): executable loading, apploaders, firmware validation, and Wii data.
- [Font validation](fonts.md): separately validated private IPL font blocks.
- [Input](input.md): keyboard, controller, mouse, and motion bindings with test scope.
- [Pointer camera](pointer-camera.md): mouse pose, infrared projection, source mapping and exact checks.
- [Following replays](input-replay-follow.md): changing ordinary headless input during a long run.
- [Compatibility](compatibility.md): local game observations and unverified gameplay requirements.
- [Debugging](debugging.md): bounded runs, trace capture, and finding the first fault.

## Implementation

- [Architecture](architecture.md): subsystem boundaries and runtime scheduling.
- [Milestones](milestones.md): remaining work connected to executable programs and game behavior.
- [CPU execution and mappings](cpu-reference.md).
- [Memory access boundaries](memory.md): CPU mirrors, host and DMA spans, page-table updates and instruction-cache identity.
- [Locked-cache DMA](locked-cache-dma.md).
- [Memory, boot, and platform mappings](runtime-reference.md).
- [Disc formats](disc-format.md) and [disc mappings](disc-reference.md).
- [ELF loader](elf-loader.md) and [ELF mappings](elf-reference.md).
- [Devices and system-service mappings](device-reference.md).
- [Wii system configuration](system-config.md): checked settings and controller registration.
- [Network services](network.md) and [network mappings](network-reference.md).
- [DSP audio](dsp.md) and [DSP mappings](dsp-reference.md).
- [Wii Remote speaker decoding](remote-speaker.md): registers, packet decoding and the separate host processing setting.
- [Host audio](host-audio.md): queueing, resampling, Remote routing and separate output diagnostics.
- [Extension cipher](extension-cipher.md): first-party keys and phase-dependent Nunchuk register transfers.
- [Graphics](graphics.md) and [graphics mappings](graphics-reference.md).
- [Hardware rendering](hardware-rendering.md): OpenGL compute coverage and driver requirements.
- [Vertex transforms](vertex-reference.md).
- [Rasterization](rasterizer-reference.md).
- [EFB texture copies](efb-copy.md).
- [CPU EFB access](efb-access.md).
- [XFB copies](xfb-copy.md).
- [Authored guest-video integration](guest-video.md).
- [SDP records](sdp.md) and [SDP mappings](sdp-reference.md).

## Validation and licensing

- [Tests](testing.md): public fixtures, sanitizers, private comparisons, and packages.
- [Performance measurements](performance.md): measured startup scope and device scheduling.
- [Licensing](licensing.md): project license, genuine third-party dependencies, and private assets.
