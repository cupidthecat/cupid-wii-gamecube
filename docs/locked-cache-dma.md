# Locked-cache DMA

Guest SPR writes control transfers between RAM and locked-cache storage. DMAU
(SPR 922) holds the aligned memory address and the upper five length bits. DMAL
(SPR 923) holds the aligned cache address, two lower length bits, load direction,
trigger and retained flag. The length combines the two fields into a count of
32-byte blocks; encoded zero selects 128 blocks. A successful trigger clears its
trigger bit while preserving the other register bits.

The CPU dispatches these requests through `Bus::locked_cache_dma`. The memory
implementation validates both contiguous RAM and cache ranges before copying.
RAM aliases mask the upper two address bits. The cache's initial offset masks to
256 KiB and maps into the existing `e0000000` storage window. A contiguous RAM
transfer that would cross that cache allocation fails explicitly, preserving RAM
and the triggering CPU state. Addresses are physical and do not use CPU BAT or
page translation. Ordinary guest loads and stores of cache contents still use the
guest's configured address translation.

The EFB and MMIO regions use word transfers through the existing physical bus
operations, wrapping each cache word within the cache allocation. These operations
retain the addressed device's supported behavior and explicit failures. Their
device side effects differ from the fully prevalidated RAM copy; the interface
does not claim atomic rollback of already executed device accesses.

Transfers complete during the triggering instruction, matching the pinned
software path. Queue occupancy, asynchronous transfer duration and hardware
error signals are not modeled. A bus that has no DMA implementation fails
explicitly. Invalid RAM ranges and unsupported device accesses retain the
trigger register and report the opcode and PC; they do not become silent guest
storage exceptions.

Behavioral mapping at commit `562875dd60fd0ff50b65dbbb611a2096802cd1b9`:

- `Source/Core/Core/PowerPC/Gekko.h`: `UReg_DMAU` and `UReg_DMAL` bit fields.
- `Source/Core/Core/PowerPC/Interpreter/Interpreter_SystemRegisters.cpp`:
  `mtspr` decoding, zero-length selection and trigger clearing.
- `Source/Core/Core/PowerPC/MMU.cpp`: `DMA_MemoryToLC`, `DMA_LCToMemory`,
  cache offsets, EFB/MMIO word order and contiguous RAM transfers.
- `Source/Core/Core/HW/Memmap.cpp`: cache allocation, `CopyFromEmu`, `CopyToEmu`,
  `GetPointerForRange` and `GetSpanForAddress` alias and extent handling.

The authored CPU regression initially failed at the unsupported DMAU write.
After implementation, the full CPU suite passed strict GCC with warnings as
errors and GCC address/undefined-behavior sanitizers. It executes the guest SPR instructions in both console modes and checks
both directions, split-length boundaries through 128 blocks, MEM1 and MEM2,
memory aliases, cache guard bytes, trigger and flag reads, non-trigger writes,
guest BAT-mapped cache loads, privilege rejection, unchanged state on an invalid
RAM extent and failure on a bus without DMA support. These are synthetic guest
checks; no independent hardware timing measurement is claimed.
