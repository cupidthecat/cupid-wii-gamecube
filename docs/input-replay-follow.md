# Following host input replays

Follow mode watches a complete replay file and uses each valid replacement as an ordinary host input source. The existing input format, device action ranges and instruction/field counters apply. Invalid or unreadable replacements leave the last accepted replay active, including held controls and scheduled future events. An empty valid file deliberately selects neutral controls.

Select `--input-replay-follow PATH` instead of one-shot `--input-replay PATH`. Initial validation failure prevents boot. Refresh checks occur on the existing 4,096-attempt host polling cadence, at most once per 100 milliseconds of wall-clock time. Each replacement starts from the default neutral state and applies all events already due at the current absolute instruction and VI-field counts. Counters do not restart when a file changes. Live SDL input sampling stays disabled while a replay supplies input.

The follower reads regular files up to 4 MiB, checks for changes during reading and validates the entire text before returning a replacement. It compares contents, so changes with identical file size and modification time remain detectable. Replacing the file atomically avoids accepting an intermediate edit that is already syntactically valid.

The Machine consumer installs a returned replay before its next ordinary CPU step. The follower does not access guest RAM, CPU registers, device clocks or report queues. SI and USB/HID requests receive input through the existing device paths. Host polling does not add guest instructions or cycles.

Accepted versions, distinct rejection outcomes and recovery messages appear on stderr and in `--log`. Repeated identical rejection does not flood the log. Final JSON fields `input_replay_updates` and `input_replay_rejections` count accepted source versions and distinct rejections in follow mode; both are zero in one-shot or live-input mode. Restoring the exact last accepted content clears the error without resetting held controls or future events.

This is a project-defined host interface. Its parser is `InputReplay::read`, `apply` and `update`; its device consumers are the existing Machine input dispatch, SI pad state and IOS Remote state. Their device behavioral reference mappings remain separate from this file-following interface.

The runtime checks cached deadlines before entering replay processing. Deadlines track the next instruction and field event independently; exhausted streams remain inactive even at the largest 64-bit counter. The initial state and every accepted replacement are still installed before the next guest step. Reset restores both event streams, and simultaneously due events retain source-line order.

The pure follower tests cover complete parsing, unchanged sources, same-size/same-time edits, distinct rejection reporting, restored-source recovery and retained future releases. Authored Machine tests on both console modes observe held buttons, axes, scheduled release and accepted replacements through real guest SI polling instructions. They compare every exposed CPU register, floating-register bit pattern, counter and trace observation at the replacement diagnostic boundary before the next ordinary instruction.

The separate USB/HID guest test executes an authored PowerPC loop to submit and
acknowledge IPC requests through MMIO. It configures a controller link, drains
both transport endpoints and checks held and released buttons plus exact IR
coordinates. A malformed replacement preserves the last received state;
restoring identical accepted text clears the rejection without another source
installation. Initial acceptance, replacement, rejection and recovery callbacks
each compare the complete exposed CPU state before the next instruction.
Strict native and GCC runs passed this fixture. Its descriptor setup uses
authored test RAM; replies and reports come through the ordinary guest device
path, with instruction execution supplying all device cycles.
