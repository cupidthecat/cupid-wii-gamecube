# Documentation

## Running and building

- [Build](build.md): Windows and Linux toolchains, dependencies, presets, and packages.
- [CLI](cli.md): launching programs, input mappings, diagnostics, and exit codes.
- [Boot and system data](boot.md): executable loading, apploaders, firmware validation, and Wii data.
- [Font validation](fonts.md): separately validated private IPL font blocks.
- [Input](input.md): keyboard, controller, mouse, and motion bindings with test scope.
- [Compatibility](compatibility.md): local game observations and unverified gameplay requirements.
- [Debugging](debugging.md): bounded runs, trace capture, and finding the first fault.

## Implementation

- [Architecture](architecture.md): subsystem boundaries and runtime scheduling.
- [Milestones](milestones.md): remaining work connected to executable programs and game behavior.
- [CPU execution and mappings](cpu-reference.md).
- [Locked-cache DMA](locked-cache-dma.md).
- [Memory, boot, and platform mappings](runtime-reference.md).
- [Disc formats](disc-format.md) and [disc mappings](disc-reference.md).
- [ELF loader](elf-loader.md) and [ELF mappings](elf-reference.md).
- [Devices and system-service mappings](device-reference.md).
- [Wii system configuration](system-config.md): checked settings and controller registration.
- [Network services](network.md) and [network mappings](network-reference.md).
- [DSP audio](dsp.md) and [DSP mappings](dsp-reference.md).
- [Graphics](graphics.md) and [graphics mappings](graphics-reference.md).
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
