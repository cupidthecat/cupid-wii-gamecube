# Data-cache visibility

`--data-cache` enables a separate 32 KiB data cache. It is opt-in; the existing
coherent-backing mode remains the default. Both execution backends use the same
cache path, and grouped RAM execution is disabled while this mode is enabled.
This is an emulated memory-visibility feature, not a host performance option.

The cache has 128 sets, eight ways and 32-byte lines. Reads and writes update
binary-tree replacement state. Dirty lines reach backing RAM on eviction,
`dcbst`, `dcbf`, or disabling the mode. `dcbi` discards a resident line without
writing it back. Reset discards validity and dirty state while retaining the
line bytes. Locked misses access backing directly without allocating a line;
locked hits retain ordinary cached visibility. As in the pinned caller, the
host option controls ordinary RAM caching; HID0.DCE separately gates cache-line
zero. This does not claim a cycle-accurate model of cache latency.

MEM1 CPU mirrors use the allocation's normalized identity. Wii MEM2 has a
separate identity. MMIO and the separately addressed locked-cache region do
not enter this cache. Aligned word writes and reads through BAT write-inhibited
mappings bypass it. Misaligned or subword WI writes retain the source caller's
duplication behavior: the expanded word writes reenter the ordinary cached
physical path. Host spans,
DMA and instruction fetch continue to use their existing backing paths, so
unwritten dirty data is not visible to those callers.

Cache-line zero dirties cached RAM instead of clearing backing immediately.
Maintenance operations retain translated-address precedence and direct-store
handling. Failed store/flush translations generate DSI; failed invalidation
translation is ignored. Privilege checks still apply to `dcbi`.

Backing copies remain bounded by real RAM rather than the larger MEM1
allocation. Rejected unlocked fills retain the selected way's bytes and install
the requested tag. Rejected writebacks do not fabricate RAM or request PI.
A locked miss with rejected backing has no defined source read value and fails
explicitly. These are software compatibility rules, not physical-console
measurements.

## Source and validation

Revision `2c805fee8f232d090c454381bb1b8422242345e1` supplies the implementation
reference: `PowerPC/PPCCache.cpp` (`Cache`), `PowerPC/MMU.cpp` (RAM admission,
reads, writes and cache maintenance), `Interpreter_LoadStore.cpp` (instruction
callers), and `PowerPC.cpp` (`RefreshConfig`, `Reset`). Paths are below
`Source/Core/Core/`.

The private source-comparison target links the unchanged cache, MMU and memory
translation units. It checks 100,512 cache operations, including dirty eviction,
locked access, aliases, split lines, externally changed backing, resets and
retained failed fills. A further 24,000 comparisons exercise the actual MMU
with the cache enabled against guest loads, stores and maintenance instructions,
including normal, mirrored, cached BAT and write-inhibited BAT accesses. Public
tests exercise the CPU instructions and both
execution backends. Whole-game compatibility and timing with this optional
mode still need qualification.

Bootstrap reads of guest-written callback and transfer descriptors also use
the CPU's current MMU and data cache. A synthetic disc fixture checks both
execution backends with cache visibility enabled and disabled; its loader and
payload are original test code. Reading those descriptors directly from backing
RAM previously missed dirty guest writes and rejected the transfer length.
The bootstrap reporting callback patch updates both its resident data-cache
line and backing RAM, following the source bootstrap host-write caller. The
fixture preloads that line with a dirty value and verifies that later writeback
cannot undo the callback patch.

A bounded Sunshine run with the option enabled loads all fifteen sections and
reaches the game entry, but produces no VI fields within one billion attempted
steps. Inspection finds an early storage exception during a cache-flush request
for effective address zero with data relocation enabled. This mode is not
qualified for Sunshine gameplay. Ordinary coherent-mode menu and intro captures
do not establish that the optional mode works through the same sequence.
