# Network behavioral reference

The behavioral reference is pinned at `562875dd60fd0ff50b65dbbb611a2096802cd1b9`. All source paths below are relative to that checkout. The implementation uses this project's checked RAM, RTC and scoped filesystem interfaces.

| Behavior | Files and functions read |
| --- | --- |
| Device registration and open/close | `Source/Core/Core/IOS/IOS.cpp`, device registration; `Device.cpp`, `Open`, `Close` |
| KD clock, bias and RTC counter | `Source/Core/Core/IOS/Network/KD/NetKDTime.{h,cpp}`, `IOCtl`, `GetAdjustedUTC`, `SetAdjustedUTC`; `Source/Core/Core/HW/EXI/EXI_DeviceIPL.{h,cpp}`, clock callers |
| Scheduler queries, error history and offline failures | `Source/Core/Core/IOS/Network/KD/NetKDRequest.{h,cpp}`, constructor, `IOCtl`, `LogError`, mail/download handlers and their callers |
| Console identifier | `NetKDRequest.cpp`, `NWC24MakeUserID`, area/model decoding and generation handler |
| Configuration layout, checksum and creation state | `NWC24Config.{h,cpp}`, configuration read/reset/write and validation methods |
| Download-list validation and entry layout | `NWC24DL.{h,cpp}`, read/check methods, entry accessors and download callers |
| Settings decoding | `Source/Core/Common/SettingsHandler.{h,cpp}`, settings reader |

## Compiled comparisons

On 2026-10-03, a separate local executable compiled the complete pinned `NetKDTime.cpp` against checked RAM adapters and a controlled RTC. Logging was omitted; the clock, bias and request code were unchanged. Under a fixed UTC timezone, 52 checkpoints compared 425,984 bytes of input/output RAM and IPC results. Every checkpoint matched. SHA-1 over the compared bytes was `ee423b3da81e3579cd9485bfe7a39ac6a7dfb978`.

The clock cases covered operations `0x14` through `0x18`, four RTC epochs, counter differences, positive and negative biases, and zero selected time. The pinned setter stores the base time minus the supplied time, and the getter adds that bias. The implementation preserves this observed arithmetic. The comparison does not establish hardware clock equivalence, timezone behavior outside the fixed UTC fixture, or `misc.bin` persistence.

A second executable included the complete pinned `NetKDRequest.cpp` and called its actual identifier function. Unused service functions were discarded by the linker. It compared 65,536 combinations of hardware ID, generation, area and model. All 524,288 identifier bytes matched, with SHA-1 `fccf12b7a3e9c2234817a07c01fe04f0909cef29`. This comparison tested the identifier calculation, not online registration.

A third executable compiled the actual `NWC24Config.cpp` with an in-memory filesystem adapter. The default 1,024-byte configuration matched byte for byte, including its checksum. Its SHA-1 was `bab8210e0a079b551c5512a9ffa036a924f1da51`. The adapter only supplied storage; it did not calculate or replace configuration fields.

## Limits

The public scheduler tests verify state and error responses without starting host network workers. Several reference request handlers have unfinished branches that acknowledge work without performing it. Those branches are not sufficient evidence for a successful socket connection, registration, mail operation, download or shutdown, so requests entering them remain explicit unsupported operations here.

Malformed configuration preservation, checked buffers, failed-storage reporting and backup-before-main ordering are deliberate correctness checks beyond the reference's logging-only error paths. No hardware or live-server validation was run.
