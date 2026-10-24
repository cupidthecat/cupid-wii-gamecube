# Experimental native integer execution

The x86-64 backend executes fetched integer instructions. The interpreter still
owns instruction fetch, translation, exceptions, interrupts, cycle accounting
and performance counters. The default backend remains the interpreter.

Native coverage includes immediate arithmetic and comparisons, add/subtract
families with carry and overflow, negate, high and low multiplication, register
comparisons, logical operations, shifts, leading-zero counts, sign extension and
rotates. Record forms update the selected condition field and preserve sticky
summary overflow. Generated entries use volatile registers on Windows and Linux
and require no optional host instruction-set extension.

Code pages switch between writable and executable protection between operations.
Compilation is bounded to 1,024 distinct instruction words by default. A full
cache uses the interpreter until reset. The direct hint table also caches
unsupported instructions and capacity rejections, retaining the fallback
counters and clearing every hint on reset. The rejection kind occupies existing
hint-entry padding. `cached_fallbacks` counts those hint returns separately from
compiled-code hits.

## Resident integer and RAM groups

The runtime can execute up to eight one-cycle register operations from an
already resident instruction-cache line. The interpreter admits groups of two
or more; native execution requires four. Native sequences concatenate validated
leaf operations into one host call. Repeatedly overwritten destinations can
remain in a volatile host register between operations; independent destinations
retain their ordinary stores. Their separate cache has 256 bounded slots
and replaces old sequences without exhausting the individual-instruction cache.
The state dump reports compilation, execution, cache-hit and replacement counts
for these sequences.

Scalar groups also admit one-cycle byte, halfword and word loads/stores with
integer neighbors and valid branches. Indexed integer and floating transfers
use the same RAM admission and retirement rules as their immediate forms.
Effective addresses wrap at 32 bits; index/base/destination aliases retain the
ordinary instruction ordering. Reversed, reservation and string transfers,
and integer-word floating stores remain stepped. A not-taken conditional branch can
continue through the remaining admitted words; any taken branch ends the
group, including a taken branch whose target is its sequential address.
Per-offset cache hints require RAM in an eligible prefix before the first
branch. Update forms and signed halfword
loads preserve the ordinary register order. Both instruction lines must already
be resident; crossing uses the same current BAT entry. Passive inspection does
not fill a line or change replacement state. A retiring instruction commits
its normal cache-hit effects. Data admission requires unchanged DBATs, cached
plain RAM and a complete single-page transfer. If a later address declines,
only its already-executed prefix retires; ordinary stepping handles the next
instruction at its exact PC and timing boundary.

A taken load/immediate-compare/branch wait can repeat within the supplied quiet
budget when the loaded register, invariant address and branch decision remain
stable. It excludes update loads and CTR-decrementing branches. All retired
instructions advance guest cycles, timebase and decrementer phases. Coalesced
iterations count as CPU fallback retirements; native execution counts only
actual emitted calls. The runtime retains its 4,096-attempt host-poll bound and
all earlier interrupt, replay and device deadlines.

A resident `dcbf`, one-cycle integer operation and conditional branch can also
execute together. This exact three-instruction group accounts for seven cycles
and retains CR, CTR, link and absolute-branch behavior. It relies on the current
coherent RAM model in which `dcbf` has no data-cache side effects. Other cache
operations and branch shapes use ordinary stepping.

Taken relative CTR loops containing `dcbf`, `addi rD,rD,imm` with a nonzero
destination, and `bdnz` can repeat within the supplied instruction and cycle
quotas. The repeated interval preserves wrapped integer arithmetic, CTR exit,
the link address, final instruction diagnostics, and both hidden clock phases.
It stops before the decrementer transition or the terminal untaken branch.
Skipped host calls count as fallback retirements; native counters record only
instructions that actually execute emitted code.

Admission requires stable ordinary RAM backing, an enabled and unlocked
instruction cache, and an unchanged instruction BAT snapshot when relocation is
active. It performs no fetch, page-table walk or cache fill. A rejected group
leaves guest and instruction-cache state unchanged. Little-endian mode,
performance monitoring and enabled pending interrupts use ordinary stepping.
Groups end before decrementer transitions, device events, replay changes and
the existing 4,096-attempt host poll. Instruction and frame limits keep their
ordinary step boundaries. An open instruction trace disables grouping, and
the public `Machine::step()` still attempts exactly one instruction.

After a successful group, `Machine::run()` can execute further resident groups
inside the same event-free interval. It subtracts their actual instructions
and cycles from the initial quotas and advances graphics, DSP, IOS and devices
once in their existing order. A declined group flushes the completed interval
before ordinary execution can observe MMIO or an interrupt. A throwing
admission also flushes prior completed groups. No deferred interval survives
a public return or exception. Device and replay events, traces, the host poll,
and run limits retain their existing boundaries.

The earlier focused group suite checks 351,262 complete CPU states and ordered
bus observations across both consoles and backends, executing 57,533,610 grouped
instructions. Branch cases cover every BO and BI value, CTR zero and wrap,
link and absolute forms, decrementer phases and both quotas. Additional cache
reuse checks require two compilations and 998
hits from 1,000 alternating sequences. Strict GCC and MSVC builds and Clang
address/undefined-behavior sanitizers pass these cases. Sanitizers check the C++
runtime and test driver; emitted machine-code bytes are checked by differential
execution. The integrated runtime suite also compares grouped `run()` with
repeated `step()` at interrupt, replay, device, trace and run-limit boundaries.

The fallthrough extension passes 1,210,222 complete CPU/RAM/ordered-bus
comparisons and 58,317,018 grouped instructions under strict GCC and MSVC.
Separate Clang sanitizer checks retain the expanded execution and boundary
cases. New cases cover eight-word copies across resident lines, all branch
options, taken sequential targets, later MMIO/fault declines, stale mappings,
native counters and executed polling prefixes. Twelve ordinary supplied-game
captures match complete guest state, frames, checkpoints and storage.
Private Machine controls preserve all 70 earlier comparisons and add 182
event, replay, limit and fault boundaries. Only the extended variant executes
the observed full eight-word group. Exact source and proof remain in
`.cache/cpu-fallthrough-20261004/final-proof.json`; the following integrated
matrix has separate scope.

The cache-flush extension passes 1,217,464 complete CPU/RAM/ordered-bus
comparisons and 58,455,150 grouped retirements in fresh strict MSVC and Clang 18
address/undefined-behavior builds. Its added cases exercise all twelve actual
decrementer phases, instruction and cycle residues, CTR wrap and termination,
integer wrap, link behavior and rejected loop shapes. The Windows runtime
fixture passes 634 grouped-versus-single-step comparisons, including longer
varying-address RAM copies, device deadlines, replay replacement, trace bytes
and complete reported state. These checks do not establish commercial speed
or complete playability. [Performance](performance.md) records the isolated
dispatcher measurement; receipts remain under
`.cache/cpu-production-20261005/` and `.cache/continuation-runtime-20261005/`.

## Verification

The native suite compares 11,969,024 complete guest states against interpreter
execution on both consoles: 5,046,272 register-operation cases, 4,170,240
immediate/comparison cases and 2,752,512 earlier immediate/rotate cases. It checks
every register array, CR, MSR, FPSCR, instruction diagnostics, timing counters,
ordered bus records and final RAM. Operands include integer boundaries, register
aliases, both incoming carry values, all SO/OV combinations, OE, Rc and all
six-bit variable shift counts. Separate controls cover rejected hints, cache
capacity, reset, fetch failures, interrupts and performance-monitor deadlines.

The frozen final suite passes strict GCC Release, native MSVC `/W4 /WX
/fp:strict`, and Clang 18 address/undefined-behavior sanitizers. GCC CTest takes
11.78 seconds; the Clang instrumented fixture takes 27.39 seconds. These times
include competing builds and game runs.

A private dispatch driver executes unchanged compiled instruction handlers
against 299,904 authored cases. It requires 184,192 instructions to execute
generated code and checks the other cases through interpreter fallback. Added
cases include destination/source/count aliases, signed shifts with all 64 count
values, immediate multiply/subtract and carry/OE boundaries. It compares integer
and exception registers, returned cycles and ordered backing observations. Both
the expanded emitter and final rejection-hint variant pass with zero divergences.
Device execution, scheduler equivalence and complete floating state are outside
this comparison.

## Measurements

Three alternating repetitions per console and loop execute 65 million authored
instructions after warming the instruction cache. Matched GCC 13.3 builds use
`-O3`, strict floating-point flags, no LTO and fixed CPU affinity on an Intel
Core i9-10900F under WSL. Every complete-state checksum matches. This first series
compares the expanded emitter before the separate rejection-hint change.

| Console and loop | Interpreter median seconds | Earlier native median seconds | Expanded native median seconds |
| --- | ---: | ---: | ---: |
| GameCube immediate control | 1.341 | 1.844 | 2.132 |
| GameCube logical/shift | 1.750 | 1.964 | 1.737 |
| GameCube carry/multiply | 1.630 | 1.745 | 1.630 |
| GameCube compare | 1.466 | 1.946 | 1.590 |
| Wii immediate control | 1.380 | 1.381 | 1.708 |
| Wii logical/shift | 1.670 | 2.044 | 1.651 |
| Wii carry/multiply | 2.154 | 2.485 | 1.923 |
| Wii compare | 1.938 | 2.635 | 1.864 |

The coverage change reduces the earlier native fallback cost in the register
loops. It does not establish an overall gain over the interpreter. The immediate
control regressions remain recorded. A later matched hint series varies
substantially and includes slower native register-loop samples.

A separate native-dispatch component benchmark repeats 100 million rejected
instruction words, five alternating pairs per workload. Guest state and every
fallback/capacity count match. Unsupported rejection medians change from
0.430268 to 0.205470 seconds; capacity rejection medians change from 1.451375 to
0.227509 seconds. These measurements cover rejection dispatch alone.

Three alternating runs per variant and supplied game then reach 100 million
attempted startup steps with native CPU execution, headless software graphics,
disabled host audio, ordinary input replay and identical copied user data.
All reported guest-state fields, CPU/RAM/frame/audio hashes and persistent files
match within each game. Native statistics differ as expected.

| Startup workload | Earlier native median seconds | Expanded native median seconds | Final native hints median seconds |
| --- | ---: | ---: | ---: |
| Sunshine | 4.108286 | 4.629616 | 4.590145 |
| Galaxy | 6.832657 | 7.699257 | 7.541955 |

All three variants fill the 1,024-entry cache. Sunshine executes 31,097,910
native instructions with earlier coverage and 31,820,467 with expanded coverage;
capacity fallbacks increase from 5,274 to 16,282. Galaxy native instructions
increase from 9,167,126 to 25,773,869, while capacity fallbacks increase from
4,287,139 to 11,481,898. Both prefixes produce zero VI fields. The expanded
native backend is slower in these startup medians. Rejection hints alone improve
the expanded medians by 0.9 and 2.1 percent, with substantial shared-host timing
variation. These results establish no whole-game speedup or playability.

Frozen sources, exact build and run arguments, all samples, compiled-handler
results, final states and executable hashes remain in the ignored
`.cache/cpu-native-register-20261004/` directory. Larger code coverage and faster
rejection dispatch leave per-instruction host calls and fetch/timer processing
in place. Full-speed execution still requires further CPU and device work.
