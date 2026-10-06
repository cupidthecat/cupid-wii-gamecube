# Memory access boundaries

Behavioral checkout pin: `562875dd60fd0ff50b65dbbb611a2096802cd1b9`. Paths below are relative to `Source/Core/`. CPU data transfers, host spans and instruction-cache lookup have different address rules. The bus exposes those operations separately so one caller's aliases do not broaden another caller's backing storage.

## CPU data and host spans

MEM1 has 24 MiB of real RAM and a 32 MiB backing allocation. CPU data accesses in physical `0x00000000` through `0x07ffffff` mask their address to that allocation, including its padding. Wii MEM2 supplies 64 MiB at `0x10000000`; that region is absent on GameCube. Locked-cache data uses the explicit `0xe0000000` region. Effective-address translation occurs before these physical data rules.

Host RAM spans and DMA copies discard the top two address bits, then require the complete range to fit in real RAM. They reject allocation padding, lower physical mirrors and transfers crossing the real end. The product also exposes a separate internal locked-cache span for its implemented DMA path. That internal span is outside the target's general host RAM-span API.

An unresolved CPU data read requests the PI memory-access interrupt and returns zero; an unresolved store requests that interrupt and discards its data. Successful address translation is preserved without manufacturing a DSI or changing DAR. Invalid host spans produce a `BusFault`. The target's host helpers alert and return no pointer; the product uses an exception to prevent its caller from continuing with an invalid span. MMIO, graphics FIFO and EFB transactions keep their existing dedicated paths. CPU byte and halfword device reads narrow the callback result to the requested width; EFB binary64 reads retain the zero-extended color word.

An aligned `dcbz` line translates once, then clears 32 bytes directly when the
bus supplies plain CPU RAM backing. This includes allocated MEM1 padding and
its physical mirrors, Wii MEM2 and the implemented locked-cache region. A
declined transfer retains the eight ordered word stores, including their fault
offsets. Alignment and translation faults occur before admission. Observable
buses decline by default. The capability changes no host-span or fetch bounds.

`plain_cpu_ram` separately exposes a complete mutable CPU data span for widths
one, two, four or eight within one 4 KiB page. It uses the same CPU RAM bounds
and declines devices, missing memory and split-page accesses without callbacks,
interrupts, error counters or memory changes. The CPU caller owns translation
and cache policy; a returned span is valid for its current instruction group.

## Page-table updates and instruction fetch

Page-table searches read through the CPU data path. Reference/change-bit updates use a bounded host write. A table in MEM1 allocation padding can therefore supply a valid translation while rejecting the backing R/C update. The interpreter retains its local translated flags and TLB state. `page_table_write_errors` records rejected backing updates independently of PI requests and guest exceptions; clearing memory resets it.

Instruction-cache lookup uses a normalized identity while a miss still reads from the original physical address. Lookup selects the virtual category first when physical bit `0x20000000` is set, then MEM2 when bit `0x10000000` is set, then MEM1. The category's allocation mask determines identity even when that backing category is inactive. A hit can consequently find an existing aliased line without granting a cold fetch access to invalid RAM. Locking, replacement and invalidation use that same identity.

Instruction backing reads validate the complete four-byte word, 32-byte line or bounded byte span against real RAM before changing their destination. `read_instruction_bytes` accepts one through 32 bytes; it preserves byte offsets and uses the same strict alias and range rules as word copies. A rejected copy leaves the whole destination untouched and increments `instruction_backing_read_errors`, with no device callback or PI request. With the cache disabled, the initialized fetch word remains zero. An unlocked failed fill retains the selected way's words but installs the requested tag and validity; invalidation and reset retain those backing words. An invalid locked miss has an undefined target value and produces an explicit unsupported diagnostic in the product.

An instruction fetch reads four consecutive bytes at its physical address. Cached reads can span two lines, updating each accessed line's replacement state in order. A locked miss uses a bounded backing byte copy for that portion instead of allocating a line. The inspected interpreter and MMU callers do not add an instruction-PC alignment fault, so odd PCs retain these byte-addressed fetch rules. Decoded instructions still apply their own data-alignment checks. This follows the pinned software behavior and does not assert physical-console instruction-alignment semantics.

Fetched-zero execution skips dispatch, advances the PC and adds no guest cycles. A separate actual interpreter comparison covers that delivery with clear pending-exception state. The backing comparisons check fetch values and cache metadata; their guest execution cases use valid retained opcodes. Data-cache visibility remains unfinished.

## Source mapping and checks

| Product operation | Behavioral implementation and callers |
| --- | --- |
| `Memory::read_cpu_data`, `write_cpu_data` | `Core/PowerPC/MMU.cpp`, `ReadFromHardware`, `WriteToHardware`; `Core/HW/Memmap.cpp`, RAM allocation sizes and masks. Interpreter load/store callers perform translation and transaction splitting. |
| `Memory::range`, `copy_in`, `copy_out` | `Core/HW/Memmap.cpp`, `GetSpanForAddress`, `GetPointerForRange`, `CopyToEmu`, `CopyFromEmu`. `Core/HW/DVD/DVDInterface.cpp`, `ExecuteReadCommand` and `ScheduleReads`, and `Core/HW/DVD/DVDThread.cpp`, `StartReadToEmulatedRAM` and the completion callback, retain the DMA address passed to the host copy. |
| `Memory::locked_cache_dma` | `Core/PowerPC/MMU.cpp`, locked-cache DMA callers and RAM host-copy paths. See [locked-cache DMA](locked-cache-dma.md) for register and MMIO scope. |
| `Memory::write_page_table32` | `Core/PowerPC/MMU.cpp`, `TranslatePageAddress` and TLB update callers; `Core/HW/Memmap.cpp`, `Write_U32` and its bounded copy helper. |
| `Memory::instruction_cache_key`, `read_instruction_words`, `read_instruction_bytes`, CPU instruction cache | `Core/PowerPC/PPCCache.cpp`, `InstructionCache::ReadInstruction`, `Cache::Read`, `Cache::GetCache`, `Invalidate`, `Reset`; `Core/HW/Memmap.cpp`, `Read_U32`, `CopyFromEmu` and backing-range validation. See [CPU mappings](cpu-reference.md) for caller coverage. |

The registered `physical_memory_reference_checks` target links four unchanged external translation units separately from the product: `Memmap.cpp`, `MMU.cpp`, `PPCCache.cpp` and `MMIO.cpp`. It constructs the actual memory manager through ordinary constructor, initialization and shutdown calls; its original host arena holds backing bytes. Unused services fail explicitly.

The complete comparison passed strict MSVC and GCC. It checks 722 CPU RAM cases, 320 host ranges, 32 DMA cases, 36 unresolved PI cases, padding-table R/C behavior, 57,856 defined instruction-backing cases and 526,736 cache reads across 600,000 mixed operations. A fully instrumented GCC ASan/UBSan comparison also passed. Public CPU regressions exercise 602 RAM-boundary cases and 1,152 instruction-cache cases through authored guest programs, plus retained instruction execution after a failed fill. Device integration tests add twelve disc transfers across the four host aliases of GameCube MEM1 and Wii MEM1/MEM2 while checking unchanged DMA registers and callback addresses.

A full rebuild exposed a lost byte/halfword narrowing step in the new CPU device bridge. The new focused callback-width regression failed before correction and passed afterward; the existing authored EFB guest assertions also pass on MSVC and GCC without changes. The original failing matrix and its unchanged-input receipts are retained separately from the corrected validation.

The separate `cpu_dispatch_reference_checks` target executes 20 unchanged translation units and the real interpreter dispatch table. Its 5,632 controls cover 1,344 strict byte backing reads, 512 warm/cold byte-cache reads, 192 retained rejected fills and 3,584 selected aligned/odd guest steps. Native MSVC and strict GCC pass every value/state case; full GCC address/undefined-behavior instrumentation passes too. GNU/Linux additionally records and forwards the actual backing-copy calls, comparing their order with the product. Native keeps the same value/state cases and authored transfer-order assertions without claiming that source-call interception. Public core tests verify all supported byte widths, complete failure preservation, aliases, real RAM ends, excluded regions, reset and absence of device/PI callbacks. A separate runtime trace regression replaces the old prefetched alignment assumption with obtained odd-PC zero words and adds a decoded `lwarx` data-alignment fault. The original trace failure and corrected complete GCC runtime pass are retained.

These are controlled software comparisons and authored integration tests. The memory comparison excludes data-cache visibility, complete MMIO/EFB width behavior, invalid locked-fetch opcode values, full interrupt scheduling and physical-console equivalence. [CPU mappings](cpu-reference.md) records the separate zero-word execution checks, and [tests](testing.md) records the broader toolchain matrix.
