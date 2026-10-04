# Graphics scheduling

The command processor recomputes CP and pixel-engine interrupt causes when
their register state changes. An instruction interval without a pending pixel
event or a readable FIFO burst clears the accumulated command cost and applies
those retained causes to PI. It still reapplies active causes after software
acknowledges PI, and clears inactive causes without changing another device's
bits. A zero-cycle advance performs the same level update.

Pending token and finish events keep their countdown and boundary behavior.
Readable FIFO bursts use the command parser and update the interrupt causes
after consumption. Register writes, gather bursts and FIFO reset use those
same paths, so a disabled or blocked FIFO can resume immediately when its
control or pointer changes.

## Behavior and checks

The behavioral checkout is pinned at
`562875dd60fd0ff50b65dbbb611a2096802cd1b9`. Paths below are relative to
`Source/Core/`.

| Product path | Behavioral functions and callers | Behavior |
| --- | --- | --- |
| `CommandProcessor::advance`, `advance_pending`, `update_interrupts` | `VideoCommon/CommandProcessor.cpp`, `SetCPStatusFromCPU`, `SetCPStatusFromGPU`, `UpdateInterrupts`, `RegisterMMIO`; `VideoCommon/PixelEngine.cpp`, token and finish callbacks; `VideoCommon/Fifo.cpp`, `RunGpu` | Read-enable, watermark and breakpoint conditions, event completion and register-driven FIFO work. |
| `Devices::set_interrupts` | `Core/HW/ProcessorInterface.cpp`, `SetInterrupt`, `RegisterMMIO` | Applying owned cause bits while preserving unrelated PI causes. |

Public graphics tests exercise PI acknowledgement and reassertion, inactive
cause clearing, unrelated cause preservation, split token/finish countdowns,
zero-cycle updates and resuming a disabled FIFO on both consoles.

A separate authored driver compares the preserved and accelerated command
processors over 16,512 register checkpoints. It varies enables, watermarks,
breakpoints, FIFO bursts, tokens, finish events, PI acknowledgement and split
advances. Every emitted value and both complete final RAM hashes match; the
two retained trace files are byte identical. This compares the implementation
before and after the scheduling change. It is separate from the inspected
behavioral functions and does not establish physical-console timing.

The paired GCC 13 Release component workload runs 100 million varying advances
per case and acknowledges PI every 4,096 calls. Three interleaved repetitions
give the following medians on the i9-10900F host:

| Console and case | Preserved seconds | Accelerated seconds |
| --- | ---: | ---: |
| GameCube, no causes | 1.056250 | 0.211389 |
| GameCube, CP underflow | 1.198440 | 0.305029 |
| GameCube, CP and both PE causes | 1.125360 | 0.304787 |
| Wii, no causes | 1.058150 | 0.207863 |
| Wii, CP underflow | 1.181940 | 0.299017 |
| Wii, CP and both PE causes | 1.150890 | 0.274707 |

Command counts, PI causes and tokens match in every measured case. Other
builds and game runs shared the host. These durations measure the scheduling
call; commercial gameplay and full speed require separate evidence.
Six paired 100-million-instruction Sunshine runs retain all 44 reported
state fields and four persistent files exactly. Their elapsed times range
from 8.94 to 21.35 seconds under concurrent host work, so they do not establish
a reliable whole-runtime speed gain. A longer three-billion-instruction pair
takes 248.19 seconds before the change and 250.86 seconds after it. Both reach
320 fields with identical state, frame bytes and persistent files, including
65,917 graphics commands, 18,956,440 fragments and 338,464 guest audio values.
The component improvement therefore has a verified behavior match, but this
commercial run does not show an elapsed-time improvement.
Frozen source manifests, commands, trace files and measurements remain in the
ignored `local-artifacts/cp-advance-20261004` directory.
