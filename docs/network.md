# IOS KD services

The `/dev/net/kd/request` and `/dev/net/kd/time` services use guest IPC requests. Their buffers are checked for alignment, size and mapped RAM before any write. The clock uses the device bus RTC, including elapsed guest CPU time.

KD time supports universal-time reads, the reference clock-bias operation, RTC counter updates and clock differences. Operation `0x16` returns its defined negative result. Requests to update the unfinished `misc.bin` persistence format fail explicitly. Closing and reopening the device preserves the IOS session's clock state.

KD request supports scheduler suspension and resumption, time-trigger queries and scheduler-status reads. Status includes the initial permission mask and a bounded error history. With no configured console identity, mail checking reports the defined disabled-service failure. Download requests report malformed lists, invalid indexes and missing entries; transferring an existing entry remains unfinished.

WC24 identifier generation reads the console's settings and supplied device identity. It writes the 1,024-byte configuration and backup through the scoped Wii filesystem callbacks. Configuration parsing checks magic, version, checksum, generation and creation stage. Existing malformed or unreadable files are preserved and reported. Only an absent configuration can be initialized. A missing identity or setting produces a failure rather than a made-up console number. Identifier generation can be tested through an injected synthetic identity; no identity is supplied by default.

Configuration writes report storage failures. The backup is written before the main configuration, so a failed main-file write preserves the previous active configuration. The two files are separate writes; a failed operation may leave a newer backup. Registration, socket operations, live mail, content transfers and unknown commands remain explicit unsupported operations. This module does not manufacture online registrations or successful downloads.

Public tests use synthetic clocks, settings and storage. They check buffer boundaries, output preservation, RTC state, negative clock bias, scheduler errors, configuration persistence and reload, checksum failures, identifier vectors, and storage failures. IPC tests separately exercise the bound services through the guest mailbox. See [Network behavioral reference](network-reference.md) for compiled comparisons and their limits.
