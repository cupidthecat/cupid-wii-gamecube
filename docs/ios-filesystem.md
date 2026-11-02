# IOS filesystem controls

The filesystem resource manager implements guest file and directory operations,
metadata, usage queries and timed IPC replies. File storage remains below the
selected user directory's `wii/` directory. Existing host-path, filename and symlink
guards remain in place.

## Format and handle lifetime

Privileged ioctl `1` formats the selected emulated Wii filesystem. It requires
UID zero, rejects absent storage and a host filesystem root, erases the emulated
filesystem, recreates its root and persists reset root metadata. **Formatting
removes files inside that emulated filesystem.** Sibling GameCube storage and
other files outside it are not format targets.

Backend file slots are separate from IPC descriptors. Formatting closes the
backend slots without deleting the frontend descriptor map. A later open can
reuse a slot still referenced by an older IPC descriptor, matching the selected
source behavior. Closing either alias invalidates that backend slot for both.
File data, offset, mode and validity now belong to the backend slot; frontend
flush bookkeeping keeps its own identity.

Tests format only newly generated temporary filesystems and retain an outside
marker to check the deletion boundary. No supplied game saves, firmware or project
archive is used by these format tests.

## Other controls

- Shutdown ioctl `13` acknowledges without shutting down the host or closing the
  filesystem descriptors, matching the source's filesystem shutdown handler.
- Version-control ioctl `10` requires a 74-byte parameter block and acknowledges
  without changing file data or metadata. This is the selected source's defined
  compatibility path, not an implementation of filesystem versioning.
- Unknown filesystem ioctl and ioctlv operations return filesystem error `-101`
  rather than stopping the emulator.
- Fixed-size path readers accept a complete 64-byte path buffer without requiring
  a terminating zero. Backend path lookup permits that length, while file open
  retains its distinct rejection of paths of 64 bytes or more.

## Reply timing

Lookup timing includes normal and split-parent paths, root paths and early
trailing-slash rejection. Attribute lookup includes the split lookup cost on
success and failure. Delete and rename include the source's modeled superblock
cost after a valid parameter block, including failed operations.

The selected create-file wrapper does not forward its tick tracker. Consequently,
valid parameter blocks produce a zero-delay reply, including backend errors;
a short parameter block still pays the filesystem IPC overhead. This behavior is
kept for source compatibility. It is not a measured hardware creation latency.
The IOS-version-specific format and superblock costs remain separate.

Root-only files remain excluded from the selected source's directory listing and
usage model. Opening or querying such a file directly is distinct from listing it.

## Validation and scope

Review uses pin `2c805fee8f232d090c454381bb1b8422242345e1`:
`Source/Core/Core/IOS/FS/FileSystemProxy.{cpp,h}`, `FileSystemCommon.cpp`,
`HostBackend/FS.{cpp,h}`, `HostBackend/File.cpp`, kernel/device request dispatch
and memory access callers.

A private harness links the actual emulation kernel, filesystem device and host
filesystem implementation. Across IOS versions 9, 28, 30 and 80, 164 comparisons
match return values, guest RAM and the exact reply-delivery cycle. Cases include
formatting, stale and reused backend slots, create/stat/write/close, attribute
lookup, malformed block sizes, unknown operations, bounded nonterminated paths
and unsuccessful delete/rename requests.

Public tests additionally check UID-zero authorization, preservation of files
outside the format target, unchanged data for version-control acknowledgment,
shutdown behavior, backend aliasing and reply boundaries. Invalid backend file
reads return a checked error; the source's timing helper dereferences an invalid
status in that fringe case, so no matching source result is claimed for it.

This is host-backed filesystem emulation, not raw NAND flash, bad-block, journal
or power-loss emulation. The comparisons do not establish every host error path,
unsafe filename behavior, physical-console timing or complete IOS parity.

Local checks pass eleven focused Release/LTO runtime, IOS, network, configuration
and CLI suites, plus the IOS suite under GCC and Clang ASan/UBSan with fatal
findings enabled. Leak checking is disabled under this host's tracing restriction.
Bounded Sunshine and Galaxy startup pairs each execute one billion guest
instructions. Guest state, raw frames and persisted files match `90131b1`; build
revision differs, and Galaxy's native-execution telemetry counters differ. This
is a startup regression observation, not completed gameplay or speed qualification.
