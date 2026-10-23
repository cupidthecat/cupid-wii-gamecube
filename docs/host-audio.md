# Host audio

`--audio sdl` opens one signed 16-bit stereo output stream at 48 kHz. Guest DMA
and disc audio plus four Remote speaker queues feed a callback that resamples and mixes their
samples. `--remote-speakers on` enables decoded Remote packets; it defaults to
`off` and requires SDL audio. Headless runs can still select audio output.

Device DMA values arrive in host byte order with the guest's right/left channel
order. The backend converts them to left/right before enqueueing. Its exact
108 MHz rate divisors are 3,375 and 2,250 for Wii, and 3,372 and 2,248 for
GameCube. Printed integer rates are diagnostic labels, rather than a second
rate calculation. Speaker queues start at divisor 36,000 (3 kHz) before the
first packet. Remote packets carry their own divisor and channel gains.
Speaker processing is a host setting separate from the HID speaker-status bit.

The main DAC supplies eight stereo frames on every scheduled tick. When its
DMA enable bit is clear, it supplies sixteen zero samples without reading RAM
or advancing the DMA descriptor, remaining block count or completion interrupt.
The AI streaming sample counter has its own clock and enable bit. Disabling
main DMA therefore inserts timed silence into its host queue rather than
leaving the mixer to fill a missing source interval.

Each queue stores overlapping, windowed 256-frame granules with a 128-frame
stride. A six-point cubic Hermite interpolator produces output samples.
The configured 80 ms buffer adjusts the bounded queue; excess backlog is
trimmed and queue overflow drops a granule while retaining incoming history.
During shortages, the normal running setting repeats the buffered tail and
fades it with the defined 8 ms rise and 64 ms fall. Active sources are mixed and
clipped in order, retaining their quantization error. The callback skips the
four permanently empty speaker queues when Remote playback is disabled.
This behavior does not establish useful playback speed when guest execution
is slow.

The symmetric window is generated from the 129-element Slepian eigenproblem
with time-bandwidth product four, convolved with a 128-element rectangle and
normalized across its overlapping halves. Its checked float bits are stored
in the source. Run `python tools/generate-audio-window.py --check` to reproduce
them. The generating method follows the
[DPSS eigenproblem](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.windows.dpss.html);
the interpolation polynomial is described on page 43 of
[Olli Niemitalo's interpolator paper](https://yehar.com/blog/wp-content/uploads/2009/08/deip.pdf).

The output mutex serializes queue writes, rate and gain changes, callback
mixing and host-stat snapshots. The device closes before its queues are
destroyed. Callback exceptions produce a zero buffer and are rethrown on the
emulation thread at its next output check. Invalid stereo frames, zero
divisors and rate steps outside the portable integer range fail explicitly.

## Diagnostics and validation

### Current gameplay observation

A frozen `d8a88fc` Windows Sunshine saved-card restart reaches controllable
Airport gameplay and ends normally at 12,000 fields. Actual SDL WASAPI output
uses signed 16-bit stereo at 48 kHz. One armed 120-second window records the
real callback's 5,760,000 frames in 5,625 packets, without observer overflow,
invalid lengths, duplicate arming or control errors. Startup and load playback
are outside that callback recording.

The selected Realtek endpoint remains unmuted, with its existing volume near
29 percent and native stereo float32 at 48 kHz. The separate 150-second
loopback retains 7,199,520 frames in 14,999 packets. Device positions are
contiguous, timestamp errors are zero, and only the first packet has a
discontinuity flag. Format, gain and mute remain stable across 151 snapshots;
no sampled foreign session has a nonzero peak. These are sampled metadata
observations, without an acoustic or exclusive-endpoint claim.

Audio qualification fails two independent gates. The product reports 16
cumulative dropped granules across the whole run; their timing relative to
the capture window is unknown. The complete callback span also fails exact
signed16/32768-to-float identity at every possible integer-frame alignment.
No gain normalization, segment alignment or omitted middle samples qualify
that comparison. A separate single-offset gain/residual fit is diagnostic
only and has substantial residual error. Neither failure establishes its
cause or demonstrates full-speed playback.

The result receipt SHA256 is
`322ccb7d5c40459cd336b6e2d3428ad6853d641e7df0cf4f9b26b9ef4db18baa`.
Raw callback/endpoint PCM, packet timing, endpoint/session metadata, authored
analyzer positive and negative controls, unchanged input hashes and original
failures remain under
`.cache/continuation-20261022/windows-gameplay-audio/saved-restart-20261023T030024865931Z/`.
No endpoint settings, services or drivers are changed. This observation does
not qualify unchanged delivery, clean gameplay audio or physical Linux audio.

### Later queue-rejection diagnostic

A separate frozen-d8 private observer records a timestamp and source only
after an existing FIFO rejection. Fixed storage and lifecycle checks retain
all six queues without changing their counters or PCM. Authored collector,
ordering, overflow and close-lifetime checks pass under MSVC, GCC and Clang
ASan/UBSan. An audio-disabled stock/observer startup pair matches complete
JSON, frame and storage without exclusions.

The one authorized playback run then ends naturally at 12,000 fields. It
reports zero product drops, zero rejection events and zero per-source counts.
The final trace is complete and readable after the actual device close, with
six unique bindings, no omitted events and no recorder errors. This does not
reproduce, localize or reconstruct the earlier sixteen drops; no product fix
or new audio qualification follows.

One callback window is armed after reviewed Airport movement. The ordinary
input sequence has ended by the arm, so this is not sustained movement during
the whole window. Its 120-second QPC deadline contains 5,758,976 stereo frames
in 5,624 complete signed-16-bit, 48 kHz packets. All 23,035,904 bytes and packet
offsets are accounted for, with no overflow or duplicate arm. A deadline at
callback boundaries need not contain exactly 5,760,000 nominal frames. The
PCM SHA256 is
`b6067bcad59023a6724547e84bdcf8d6cf08b3f8ec5a2d6e620fe40d80dbc891`.

Forty-nine read-only endpoint snapshots retain the raw foreign-session peaks,
including small nonzero browser values. An external endpoint-gain change
occurs before the arm and remains recorded. The observer changes no endpoint
settings. No endpoint waveform is captured, and neither gain nor registered
effects identify the cause of the earlier mismatch. All four stored files
remain byte-identical to the copied seed; prior FLUDD/coin restoration is not
shown.

The diagnostic audit SHA256 is
`71239324c45e422d9243352c69befdef163ba0bf4f729351b2e5cd57b459d913`;
the final handoff SHA256 is
`9097ae6bdcf1e14ecb9b27496f1387f99c48d595607ca1b70887a302efc3c281`.
Independent review rehashes all 383 raw manifest entries, 63 frozen inputs
and 22 handoff artifacts, and recomputes all callback values and packet
bounds. Evidence remains under
`.cache/continuation-20261022/windows-gameplay-audio/drop-playback-plan-v3-20261023T091650Z/`.
The earlier failed gameplay-audio qualification remains open.

### Earlier startup observation

An earlier 224-file Windows Sunshine startup completes 320 fields with SDL's
WASAPI driver and the selected Razer Kraken V3 X endpoint. A private callback
observer records 2,155,520 stereo frames, including 363,651 nonzero samples.
All 4,311,040 signed-16-bit sample values divided by 32,768 match the endpoint's
float32 bits at one constant 64-frame offset. The 2,185,440 captured endpoint
frames are zero before and after that complete callback recording. No gain
adjustment, tolerance or omitted callback segment is used.

All 4,553 packets have contiguous device positions. The first packet has a
discontinuity flag when the inactive endpoint starts delivering data; later
packets have none, and timestamp errors are zero. Foreign audio sessions have
zero measured peaks. The inactive baseline produces no packets, so continuity
is asserted for the recorded active stream. Recording follows Microsoft's
[WASAPI loopback procedure](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording).

The observer first matches the unmodified audio-disabled CLI's complete state,
frame and storage. Enabling audio then preserves all 627 compared guest-state
values across ten checkpoints and the final state, every raw frame and all four
persisted files. Only the three host callback fields are excluded. Queue drops
remain zero. A 101-file sealed private packet and independent waveform receipt
remain in `.cache/goal-emulation-20261004/`. This capture uses the software
rasterizer and source manifest
`456efec2b1e1f10dfea0e8d9bb9915aea2de8250329e75fdf52727a310c03fbb`;
audio during controllable gameplay and real-time execution remain outstanding.

The existing `audio_samples`, `audio_nonzero_samples`, `audio_peak` and
`audio_hash` fields describe the original main DMA values before host mixing.
They include the scheduled zero samples while main DMA is disabled. Adding
those samples changes the count and hash even when host audio is disabled;
older count/hash captures retain their original implementation scope.
Remote packets and disc audio do not change them. The separate `disc_audio_samples`,
`disc_audio_nonzero_samples`, `disc_audio_peak` and `disc_audio_hash` fields
record delivered disc PCM before AI volume and host processing, including
scheduled silence. See [disc audio](disc-audio.md) for commands and decoder checks.
`host_audio_frames`,
`host_audio_nonzero_samples` and `host_audio_dropped_granules` separately
describe the callback output and queue loss. Callback timing makes the host
fields unsuitable for deterministic guest-state comparisons.

An independent original driver compares the complete unchanged compiled mixer
with these queues at the pinned commit
`562875dd60fd0ff50b65dbbb611a2096802cd1b9`. Its **17,653 checkpoints** compare
**103,037,732 bytes**, with aggregate FNV64 `3891361590004960915`. It passed
strict GCC and native MSVC, and Clang address/undefined-behavior sanitizers
instrumenting both mixer implementations. Cases vary output rates, divisors,
all channel gains, speed/pitch settings, buffering, pause/gap behavior,
overflow, partial packets, callbacks before the first packet, paced startup
and DMA/stream/four-speaker mixing. Expected values
come from executed target code. Surround processing, accessory streams,
savestate serialization and concurrent target configuration changes are
outside this comparison.

The 256 independently generated window coefficients match the compiled target
bits exactly; SHA256 over their little-endian float bytes is
`6267d107339b0e2537d3d92a67d50b46c9cd3caddd3f8dd39f45c8a9117a1e80`.
Public fixed vectors check those output expectations and overflow/drain
behavior. SDL tests verify each speaker port's channel gains, DMA channel
order at all four divisors, unchanged main DMA diagnostics and default-off
processing. An authored guest negotiates actual HCI/L2CAP channels and sends
HID setup/data through public Machine and MMIO APIs into the host callback.
It supplies each 20-sample packet at the declared 8 kHz rate, with the callback
consuming concurrently. A separate deterministic regression preserves the
target's startup shortage behavior: after 1,024 empty frames with an 8 kHz
input rate and 48 kHz output, a burst of 1,600 samples discards all 12 completed
granules and emits zeros.
The positive transport test therefore uses a sustained stream rather than
requiring that discarded burst to become audible.

A separate Windows WASAPI shared-endpoint loopback check received the native
SDL output. It detected 997 Hz in the left channel and 1,489 Hz in the right
for both DMA and Remote-only streams; cross-channel tone magnitudes were less
than one thousandth of the intended tones. Remote-only output retained zero
main DMA counts. This verifies the operating-system output path with synthetic
signals; it does not claim physical-speaker listening or commercial-game
audibility. The loopback method uses the documented
[Windows shared-mode API](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording).

That endpoint check also passed against the coherently rebuilt memory/task
runtime after `c4b1ed598f6a338e5b94bae5de5a77b71a3a4ec4`. Copied libraries and all
public headers had matching captured hashes before and after the test. Each
two-second source produced 96,000 captured stereo frames at 48 kHz and 191,216
nonzero host values. DMA retained 192,000 guest values; Remote-only output
retained zero. The independent channel tones satisfied the original spectral
assertions with strict MSVC compilation.

Linux's SDL dummy-backend tests pass. A separate strict GCC driver also passes
through SDL's PulseAudio backend into an isolated, clocked software sink.
Monitor capture detects the intended 997 Hz left and 1,489 Hz right tones for
both main DMA and Remote-only output, with the original spectral assertions.
The callback records 77,558 and 30,456 nonzero values respectively; main DMA
retains 192,000 guest values, and Remote-only output retains zero. All six
copied runtime artifacts and 38 public headers match their hashes before and
after the test. This checks the audio protocol and mixer through a software
sink; it does not establish physical output.

The shared WSLg sound server remains a separate failure. After `pactl info`
responded again, a bounded attempt to capture `RDPSink.monitor` timed out while
the output driver was running. The server then stopped answering the same
read-only query. The failed trace and captured build inputs are preserved.
Successful Linux output through that shared endpoint and commercial-game
audibility remain outstanding.

## Commercial endpoint observation

An October 4, 2026 Windows run verifies Sunshine's actual SDL output through
WASAPI loopback on `Speakers (Razer Kraken V3 X)`. It used MSVC Release with
link-time optimization, headless OpenGL on an RTX 3060, validated IPL fonts,
a copied private user directory and no input replay. The frozen runtime
reported `949784e21ac6-graphics-working`. A private observation executable
recorded each real SDL callback buffer; the product source and public APIs
were unchanged. Its SHA-256 is
`ed1774113535d096ab1c2ecbaeda744bfdd62288ef3e0df22f0a0945ceaf89c6`.

At three billion CPU attempts, it produced 320 VI fields, 338,464 guest DMA
sample values and 13,900,800 host callback frames. The DMA contained 62,194
nonzero values, with peak 13,760 and hash `16351903819501652029`. Guest CPU,
memory, DMA and frame hashes match the separate stock executable's run at
that same bound. Host callback timing varies between runs.

The loopback captured stereo 48 kHz float PCM. After one constant alignment
of 1,568 frames, all 27,805,696 observed sample values match the actual
callback's signed 16-bit values divided by 32,768 exactly. Each channel has
gain one and zero residual. Both paths contain 4,455,670 nonzero values.
The observation includes 2,048 silent callback frames after the final state
snapshot; the snapshot prefix matches its reported counts exactly. All
endpoint samples outside the callback observation are zero.

A three-second baseline had no signal. Session monitoring found no other
rendering session with a nonzero peak. The capture session's own meter mirrors
the endpoint mix, so the first helper diagnostic incorrectly classified that
meter as foreign audio. Its original receipt remains intact; a separate
analysis excludes that known capture-only session and checks the entire
waveform, including silence. Windows build 19045 lacks the newer
[process-specific loopback API](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/),
so this observation uses the shared endpoint API.

Local receipts and private PCM remain under
`local-artifacts/commercial-endpoint-20261004/`. `completed.json` records the
invocation, bounds, binary/helper hashes, endpoint, sessions and timing;
`inputs.json` records 48 frozen inputs checked before and after execution;
`waveform-analysis.json` contains the exact comparison. Callback PCM SHA-256
is `cb10db74f9435266ab2744c8210873784734e7f1f4bbfdc42adf757acbc91b2b`;
loopback WAV SHA-256 is
`414d4ad207715e03b04a99d015b134c13af07b5be7bee67342c7df58eb05cab6`.
These files remain private and are excluded from packages and public tests.
`windows-audio-driver-details.json` records the endpoint's Microsoft USB
audio driver, version `10.0.19041.5794`, using `wdma_usb.inf`.

This establishes that the commercial run's mixed samples reach the Windows
audio engine. It does not establish physical-speaker listening, correct game
timing, full-speed playback or playable gameplay. About 7.3 emulated seconds
took 290 wall-clock seconds; zero dropped granules does not establish that
the callback always had enough fresh guest audio.

The same day's WSLg probe captured 142,545 monitor frames, including 50,529
nonzero values at the intended 997 Hz and 1,489 Hz synthetic tones. Playback
drain exceeded eight seconds and the shared PulseAudio server then stopped
answering bounded `pactl info` queries. Those partial monitor results do not
verify commercial Linux endpoint playback. The probe, capture and timeout
receipt are retained alongside the Windows evidence.

The frozen Sunshine runtime also produced a separate commercial mixer trace
at three billion CPU attempts. It used SDL's dummy backend so its callbacks
did not share the physical endpoint. The private observer recorded each
enqueue and completed callback under the existing audio mutex, with an event
number and monotonic timestamp. All 21,154 enqueue records account for the
same 338,464 guest values and guest PCM hash as the endpoint run. Its CPU,
memory and frame hashes also match.

Replaying the captured order through the product FIFO and the unchanged
pinned mixer passes on both Windows MSVC and Linux GCC. All 11,160 callback
records, containing 22,855,680 sample values, match the recorded output
exactly. There are 3,818,244 nonzero values, zero dropped granules and PCM hash
`1169570902919312973`. A separate check verifies that the callback records
concatenate to the private PCM file and that the enqueue records reproduce
the guest diagnostics. This checks the captured asynchronous order; it does
not claim that different host callback schedules produce the same waveform.

The trace and results remain under
`local-artifacts/commercial-endpoint-20261004/sunshine-events/`:
`completed.json`, `event-analysis.json`, `native-replay.json` and
`linux-replay.json`. The event trace SHA-256 is
`fb40e384ef14dc8c4a78aae2b5ab20577392a6071c352779702536d902cb621c`;
callback PCM SHA-256 is
`3a240d5a942fd1b31e8bb03451d472ff172e9e817028b26572e5342908eda6ce`.
This separate observation executable has SHA-256
`42a728a62a486a5ee08a4212466d9c64b25005690c0df6da9898be299b4bd70a`.

An initial Galaxy observation used the same frozen Windows runtime, endpoint
and no-replay setup. At eight billion CPU attempts it reached 653 VI fields
and delivered 701,728 guest DMA values, all zero. All 41,223,168 observed
callback frames and 41,243,040 loopback frames are also zero, and every
monitored session has peak zero. This bounded silence cannot establish
waveform alignment, endpoint gain or nonzero Galaxy playback. Its receipts
are `galaxy/completed.json` and `galaxy/silence-analysis.json` in the same
artifact directory. Callback PCM SHA-256 is
`57f579351a965d128741c3753f64e2041806f21a9feedfce710e6db6fbd094a0`;
loopback WAV SHA-256 is
`8933cee1deabf8f6cfa90cbfba85bdb17b71dfcd3efb547b5529787e43808256`.
An input-free run at sixteen billion attempts also remained silent at
1,313 VI fields and 1,406,144 guest DMA values. Its final captured frame shows
the wrist-strap instructions. The input gate therefore matters when choosing
an audio observation span. Its separate receipts and PCM hashes remain under
`galaxy-16b/`; neither silent run establishes nonzero Galaxy playback.

A separate Galaxy run used the same frozen Windows executable and an
immutable copy of the ordinary startup input replay. At nine billion CPU
attempts it reached 737 VI fields and delivered 791,552 guest DMA values,
including 65,602 nonzero values, with peak 19,112 and hash
`11697351465341683324`. Its input replay SHA-256 is
`74f03aa39e1a1eeb902769257cc8dfdfb79e934aeab523cb39bbbd5cc781e7d7`.
The earlier input-free observations remain separate evidence of the startup
input gate.

After a constant 1,568-frame alignment, the first 35,178,656 callback frames
match WASAPI's float PCM exactly after division by 32,768: 70,357,312 sample
values, gain one and zero residual in both channels. All 7,409,720 nonzero
endpoint values belong to this exact callback prefix. All other endpoint
values are zero, and no monitored foreign rendering session has a nonzero
peak. The final 864 callback frames, or 18 ms, were not rendered; they contain
1,723 nonzero values. They follow the final host-stat snapshot. The strict
whole-callback comparison therefore failed, and its original failure is
preserved. SDL's WASAPI close path stops the client without draining its last
buffer, which is consistent with this canceled tail.

The receipts are under
`local-artifacts/commercial-endpoint-20261004/galaxy-replay-9b/`:
`completed.json`, `rendered-prefix-analysis.json`,
`waveform-tail-diagnosis.json` and the failed `full-callback-comparison.stderr`.
Callback PCM SHA-256 is
`74784afea804c92a2dc2092d7b87b3ec157d7b25fdb48e44d7396ddb5186f99e`;
loopback WAV SHA-256 is
`2192e2373728fbd2f95a9994f2f3292d194bb68c53e101cea78e38ab1c92b3d8`.
This verifies the rendered commercial waveform prefix through the Windows
audio engine. It does not establish complete callback rendering or physical
listening. About 12.46 emulated seconds took 735.19 wall-clock seconds, so it
also does not establish full-speed playback or playable gameplay.

A later coherent Linux build of the frozen `949784e21ac6-graphics-working`
runtime attempted Sunshine through SDL PulseAudio and the actual shared
WSLg `RDPSink`. Strict GCC 13.3 Release with link-time optimization used
system SDL 2.30.0 and libpulse 16.1, with headless OpenGL through llvmpipe.
All 203 frozen inputs match their hashes before and after execution. The
observation executable SHA-256 is
`406a01dc420a8def892e62cf567d9dcd7edf18f9885c00826d11fcf337873f7f`.

Before playback, PulseAudio `17.0-25-gc3305` answered queries and had no sink
inputs. The first live query identified only the observation process's
48 kHz stereo stream. Subsequent queries timed out seven times and then
reported connection refusal 324 times. The guest still reached its normal
three-billion-attempt limit, with the same CPU, memory, frame and guest DMA
hashes as the stock Sunshine observation. Its host callback stopped after
62,464 silent frames and reported 1,069 dropped granules. The process then
remained in audio shutdown until the owned wrapper's configured 680-second
outer bound; termination did not finish within its five-second grace period,
so the wrapper killed that process. The wrapper's measured wall duration was
737.01 seconds. The wrapper signaled only its own game and recorder; it
issued no shared service restart.

The retained callback file contains 61,440 silent frames; forced termination
left one 1,024-frame observer buffer unflushed relative to the final snapshot.
The Pulse monitor retained 78,255 silent frames. A simultaneous 730-second
Windows loopback observation received no packets, and all monitored session
peaks were zero. This does not establish a full-duration zero waveform,
alignment, gain or successful Linux commercial endpoint playback. The final
read-only server query still reported connection refusal. Process inspection
preserves the main and SDL audio threads' futex waits and the Pulse mainloop's
poll wait after the guest's normal stop message; it does not establish the
cause of the shared server failure.

These failed-attempt receipts are under
`local-artifacts/commercial-endpoint-20261004/linux-sunshine/`:
`completed.json`, `windows-completed.json`, `failure-analysis.json` and
`stalled-shutdown-inspection.json`. The analysis records binary, helper,
receipt and PCM hashes, the contemporaneous WSLg transport process and the
observer's incomplete final buffer. Callback PCM SHA-256 is
`9eb8fa54d87a9b9775dd55c07b3d164453884695bd1ed83fe2f63259ee9e6afc`;
Pulse monitor PCM SHA-256 is
`08bb3bbd1fbdb0cd7722cff1ec0f664ccc8dc9af7fc4988f625d57d4a0d3695e`.
The earlier synthetic partial capture and transport timeouts remain intact.
The separate `environment-after.json` records installed library versions
and ELF hashes. It also records a later PID 1 start and newly created Pulse
socket after the attempt ended; this later environment epoch is not evidence
that the failed playback recovered.

The callback's disabled-Remote branch was then checked against the complete
32,314-event Sunshine trace. The speaker setting is fixed for an Output's
lifetime, and disabled routing rejects all speaker packets. Its four empty
speaker FIFOs therefore add no signal or quantization error. Skipping their
interpolation leaves all 22,855,680 captured sample values and all drop counts
exact against the unchanged pinned mixer on both MSVC and GCC. This separate
trace replay measured callback mixing work at 0.451 seconds on Windows and
0.314 seconds on Linux, compared with 5.317 and 4.741 seconds in earlier
replays that processed the empty queues. These timings cover the recorded
mixer calls, not guest execution, endpoint latency or whole-runtime speed.

The guarded callback was also built from a coherent snapshot of
`62db09ec8438d963edb4458b1a6d9174be9aad09` plus the speaker-loop change.
The `host_mixer`, `output` and `host_audio` checks pass on strict MSVC and GCC.
They retain enabled-Remote coverage for all four ports and both channels,
main DMA channel order at all four divisors, and independent disc-audio
callback coverage. These later checks use SDL's dummy backend and do not
change the earlier endpoint observations. Their source manifest and native
and Linux logs remain under `local-artifacts/remote-loop-20261004/`, with hashes
in `completed.json`. Complete guarded trace comparisons remain under
`local-artifacts/commercial-endpoint-20261004/replay-remote-off/`.

## Pulse shutdown

The pinned SDL dependency includes a build patch that wakes Pulse playback and
capture waits before joining the audio worker. Close first pauses, disables and
marks the device for shutdown. The wake takes the same Pulse lock as the wait
predicate, so it cannot race past a worker entering its wait. Callback state,
queues and mixer locks stay alive until that worker has joined. Other SDL drivers
leave the new internal hook unset.

`cmake/SDL2Audio.cmake` checks the three original source hashes and stages an
altered copy inside the build directory. Shared source caches remain intact.
Installed SDL selected through `CUPID_USE_SYSTEM_DEPS` does not receive this patch.
The patch fixes close liveness for a stalled connection; it does not recover an
unavailable endpoint or bound an arbitrary blocked callback.

The public `host_audio_shutdown` test starts its own Pulse server and null sink,
feeds nonzero samples through the actual Output, then stops only that server.
It verifies stationary callback counters, close before server recovery, a shut
down SDL audio subsystem and one remaining application thread. Its red path
resumes the owned server before cleanup. The test requires an installed Pulse
server and reports an explicit skip when that optional executable is absent.
Linux output CI installs it. No commercial assets or shared audio services are
used by this fixture.

On October 4, 2026, the same fixture and integrated Output library were linked
against the untouched SDK and the staged SDK. The untouched build failed the
close-before-recovery assertion; the staged build passed while its server
remained stopped. Both attempts resumed and reaped only their owned server.
The strict GCC build, commands, source and library hashes, and both exit results
are retained in
`local-artifacts/pulse-audio-integrated-20261004/verification-1791148676023379195/`.
The receipt has SHA-256
`3dc90b1f140fbc190b21f825d943e8f0281c502b476da4c78400e844b0263862`.
This checks shutdown liveness and lifetime; commercial endpoint playback remains
outside that comparison.

A separate October 4 commercial run used the coherent frozen Sunshine runtime
with the staged SDL library and its actual callback observer. SDL's Pulse driver
played into an owned, clocked 48 kHz stereo signed-16-bit null sink. The shared
WSLg server still timed out on a bounded read-only query, so this run used no
shared endpoint and makes no physical-output claim.

At three billion CPU attempts, the run reached 320 VI fields. Its CPU, memory,
frame and guest DMA hashes match the earlier stock observation. Guest DMA
contained 62,194 nonzero values; the actual callback contained 3,002,084, and the
sink monitor contained 3,002,066. The process exited normally. A separate watcher
observed 0.140 seconds between the normal guest-stop message and process exit,
and the owned server reported no remaining playback stream after close. All
copied build inputs match their hashes before and after execution.

The strict comparison of the whole callback at one constant alignment failed.
The monitor instead matches four callback segments exactly, at offsets of 818,
908, 965 and 1,170 frames. Between these segments it inserts 90, 66 and 205 silent
frames and omits nine stereo callback frames containing 18 nonzero values.
All other captured values match, and no other playback stream used the owned
sink. This establishes nonzero commercial PCM delivery and clean shutdown
through the isolated Linux audio engine, while preserving the timing gaps as
a failed continuity check. It does not establish uninterrupted output, physical
listening or full-speed gameplay. This frozen Output precedes the newer
disabled-Remote optimization; the gaps alone do not identify a defect in the
current callback.

The private build receipt and copied inputs are under
`local-artifacts/linux-audio-runtime-20261004/`. The observation directory is
`run-1791151123132142772/`; it contains `completed.json`, the failed
`analysis.json`, `gap-analysis.json` and `shutdown-timing.json`. Callback PCM
SHA-256 is
`053d4fcfd008d5854a88bb5d459310698306d9f1a85c464105639498d0f44993`;
monitor PCM SHA-256 is
`9a72d5123eedc7610623f1e145d8e88b61b73a0e7b2961d8d7d2609f4ce74a58`.
The observation executable SHA-256 is
`8656fa85fe64f44bb57e737765bfd576aed0c806a7ea965b0279434b93024a14`.

The current Output and SampleFifo were then tested separately with the sealed
commercial trace's 338,464 guest values. A small driver feeds the captured DMA
through the public Output API at the original 3,372-divisor cadence, after the
owned sink starts clocking callbacks. It executes the current audio sources and
links the copied staged SDL library directly; it links no historical guest
core. Its private observer records the actual completed callback buffers.

Two independent runs pass the strict whole-callback comparison. Their 285,696
and 281,600 callback frames each contain 93,954 nonzero values and match the
48 kHz sink monitor exactly at a constant 769-frame offset. Guest values,
nonzero count and hash match the sealed trace. Both runs have zero dropped
granules, no extra endpoint signal, no other playback stream and normal close.
This verifies the current callback's delivery of captured commercial DMA at
that cadence; it does not replay the original guest's slower wall-clock
schedule or reproduce its historical continuity failure.

The source, strict GCC build and input hashes are in
`current-output-1791151862112394849/` under the same artifact root. The two
receipts and waveform comparisons are in `run-1791151909543897916/` and
`run-1791151968353042481/`. The driver SHA-256 is
`aa843b7519b7a16434573969e7b67a05719ab2d33ce29648fe7d68c2bacd009b`.

A later read-only probe found the shared WSLg server responding in a new
environment epoch. The same current driver was then tried through `RDPSink`
with a native 44.1 kHz monitor. It produced 50,176 callback frames containing
32,460 nonzero values; the monitor retained 84,644 frames containing 27,910
nonzero values. The stream stopped progressing, the FIFO reported 854 dropped
granules, and the driver's explicit no-drop assertion failed. It exited with
status one without reaching the 18-second outer bound. The final server query
reported connection refusal. The wrapper closed only its driver and recorder.
It did not restart a shared service.

This partial observation does not verify complete waveform delivery or
reliable shared-endpoint playback. The monitor's rate conversion also prevents
an unchanged 48 kHz sample comparison. Its receipt and private PCM are in
`shared-run-1791152103315790604/` beside the two successful current-Output runs.
Read-only environment records retain the changed PID 1 start times and socket
creation times; a newly responding server is not evidence that an earlier
failed playback recovered.

A private paired control isolates the queue-drop mechanism with the same
captured DMA and copied current audio sources. Both runs reach 55,296 callback
frames and 42,700 nonzero callback values before the intervention, with zero
drops. The healthy owned server continues to 282,624 callback frames and
consumes all guest values without drops. When only the owned server is paused,
the callback remains at 55,296 frames during 4.285 seconds of continued DMA and
the bounded FIFO reports 827 dropped granules. Guest values and PCM hash remain
unchanged. Output closes in 0.0443 seconds while that server is still stopped;
the wrapper then resumes and reaps its own server.

The producer and callback share the product audio mutex, while SDL's Pulse
write wait runs outside it. A stopped callback therefore allows guest enqueueing
to continue until the ring is full, which is where SampleFifo increments its
drop counter. This control reproduces the observed stalled-consumer symptom
without changing guest samples or mixer code. It does not identify why the
shared transport stopped. The private paired receipt is
`drop-diagnosis-1791152681943257116/paired-receipt.json` under the same artifact
root, with SHA-256
`f8f1a6c4e824e56573ce951e00a71351964728c8b56c0d2b5201f11fdf652033`.

A fresh read-only probe at `df978ec` receives one successful shared-server
reply identifying PulseAudio 16.1 and `RDPSink` at signed 16-bit stereo
44.1 kHz. The next query fails with connection refusal. No audio stream is
opened and no shared service is restarted. The first reply does not establish
transport recovery or waveform delivery. Both observations are recorded in
`.cache/continuation-20261022/linux-shared-audio-probe/receipt.json`.

## Behavior mapping

| Behavior | Pinned files and functions | Product |
| --- | --- | --- |
| Rate selection and guest channel order | `Source/Core/Core/HW/AudioInterface.cpp`: `Get48KHzSampleRateDivisor`, `Get32KHzSampleRateDivisor`; `Source/Core/AudioCommon/Mixer.cpp`: `PushSamples`, `PushStreamingSamples`, `PushWiimoteSpeakerSamples` | `src/hw/devices.cpp`: `audio_dma_block`; `src/platform/output.cpp`: `audio`, `remote_speaker` |
| Granules, overlap, buffering, interpolation and gain | `Source/Core/AudioCommon/Mixer.h`: `MixerFifo`; `Mixer.cpp`: `Enqueue`, `Dequeue`, `Mix`, `SetVolume`, `RefreshConfig` | `src/audio/host_mixer.cpp`: `SampleFifo` |
| Output order, initial speaker rate and default host rate | `Mixer.h`: `m_wiimote_speaker_mixers`; `Mixer.cpp`: `Mixer::Mix`; `Source/Core/AudioCommon/SoundStream.h`: `SoundStream` | `src/platform/output.cpp`: `Output`, `Impl::render_audio` |
| Separate host speaker setting and decoded packet path | `Source/Core/Core/HW/WiimoteEmu/Speaker.cpp`: `SpeakerData`; `WiimoteEmu/WiimoteEmu.cpp`: `RefreshConfig`; `Source/Core/Core/Config/MainSettings.cpp`: `MAIN_WIIMOTE_ENABLE_SPEAKER` | `src/runtime/machine.cpp`: `set_output`; `src/ios/ipc.cpp`: speaker handler binding; [speaker decoder](remote-speaker.md) |
| Host callback lifetime through stream destruction | `Source/Core/AudioCommon/CubebStream.cpp`: `CubebStream::~CubebStream`; `PulseAudioStream.cpp`: destructor and `PulseShutdown` | `src/platform/output.cpp`: `Impl` destructor; pinned SDL `close_audio_device`, `PULSEAUDIO_PrepareToClose`, playback and capture waits |
