# Host and guest input

Interactive output polls SDL keyboards, mice, and up to four mapped game controllers. GameCube controller port zero combines keyboard input with the first host controller. Other ports use their assigned host controller. New devices occupy the first free slot; disconnecting one keeps the other ports stable. Escape or a window close ends the run.

SI exposes the state through guest register and command responses, including analog sticks, triggers, controller detection, and motor commands. Motor requests use the host controller's rumble API when the device supports it.

## Defaults

`--input-config PATH` accepts one `action=SDL key name` per line. Empty lines and lines starting with `#` are ignored. Unknown actions and key names produce errors.

| GameCube action | Key |
| --- | --- |
| A / B / X / Y | Z / X / A / S |
| Start / L / R / Z | Return / Q / W / E |
| Main stick | Arrow keys |
| C-stick | I / K / J / L for up / down / left / right |

| Wii action | Key or mouse |
| --- | --- |
| Remote A / B | Z / X; left / right mouse buttons |
| Plus / Minus / Home | Return / Backspace / Home |
| One / Two | 1 / 2 |
| Remote D-pad | Arrow keys |
| Remote shake | Space |
| Remote tilt left / right / forward / back | J / L / I / K |
| Nunchuk stick | W / S / A / D |
| Nunchuk C / Z | C / Left Shift |
| Nunchuk shake | V |
| IR pointer | Mouse position inside the displayed game area |

Wii configuration actions use `remote-` or `nunchuk-` prefixes, for example `remote-shake=Space` and `nunchuk-z=Left Shift`. All action names are listed in [the output module](../src/platform/output.cpp).

Host controller A/B/X/Y map to GameCube A/B/X/Y. Start maps to Start, the right shoulder to Z, the left shoulder to L, D-pad buttons to the guest D-pad, and the two sticks to the main and C sticks. Trigger axes provide analog L/R and press the digital trigger near full travel.

For Wii input, the first controller supplies A/B, One/Two, Plus, Minus, D-pad, and the Nunchuk stick. Left and right trigger presses supply C and Z; the right shoulder supplies a Remote shake. Keyboard or mouse input can supply tilt and IR alongside it. Physical motion sensors are not currently read.

## Motion and transport

Keyboard tilt changes raw accelerometer axes around the calibrated gravity state. Holding shake alternates the selected acceleration axis at approximately 30 samples per second. Mouse pointing supplies two bounded infrared points with screen position transformed into camera coordinates. Window scaling and black bars are accounted for; pointing outside the image supplies no visible points. These are host mappings for the guest protocol; actual game acceptance and physical sensor calibration remain unverified.

Remote report generation implements buttons, accelerometers, IR, Nunchuk state, EEPROM requests, report selection, acknowledgements, LEDs, rumble and speaker register traffic. The Wii USB/Bluetooth path negotiates HCI and L2CAP before exchanging HID reports. [Speaker decoding](remote-speaker.md) produces mono PCM with rate and gain metadata through a separate callback when host speaker processing is enabled. Processing defaults to disabled; the host speaker queues and resampler remain unfinished. [First-party extension encryption](extension-cipher.md) covers key writes and phase-dependent Nunchuk reads, including partial-transfer errors. Third-party key profiles, interleaved reports and broader radio behavior remain unfinished. See [device mappings](device-reference.md) and [SDP](sdp.md).

## Input replay

`--input-replay PATH` supplies deterministic controller events in interactive or headless runs. It replaces live input sampling while preserving window-close and Escape handling. Events enter the normal SI or Remote/HID path; they do not change guest memory or skip instructions.

Each line starts with `instruction N` or `field N`, followed by one or more `device.action=value` assignments. Counters are absolute from boot: instructions include the apploader, and fields count VI device fields, including fields without a presented framebuffer. Counters must be nondecreasing within their own type. Events run before the next CPU instruction; simultaneously ready events follow file order. Values persist until replaced. `#` starts a comment.

```text
instruction 0 remote0.connected=1 nunchuk0.connected=1
field 60 remote0.a=1
field 65 remote0.a=0 nunchuk0.stick-y=220
field 90 nunchuk0.stick-y=128 remote0.accel-x=768
field 91 remote0.accel-x=256
field 92 remote0.accel-x=512
```

Ports range from zero through three. Port zero starts connected with a neutral GameCube controller and a Remote with a Nunchuk; other ports start disconnected. Actions are:

| Device | Actions and ranges |
| --- | --- |
| `padN` | `connected`; `a`, `b`, `x`, `y`, `start`, `l`, `r`, `z`, `left`, `right`, `up`, `down` use 0/1. `stick-x`, `stick-y`, `c-x`, `c-y`, `l-trigger`, `r-trigger`, `analog-a`, `analog-b` use 0 to 255. |
| `remoteN` | `connected`; `a`, `b`, `one`, `two`, `plus`, `minus`, `home`, `left`, `right`, `up`, `down` use 0/1. `accel-x`, `accel-y`, `accel-z` use 0 to 1023; `battery` uses 0 to 255. `ir0-x` through `ir3-x` use 0 to 1023, their `-y` components use 0 to 767, and `-size` uses 0 to 15. A component value of -1 marks it unavailable. |
| `nunchukN` | `connected`, `c`, `z` use 0/1. `stick-x`, `stick-y` use 0 to 255; `accel-x`, `accel-y`, `accel-z` use 0 to 1023. |

Integers accept decimal or `0x` hexadecimal. Invalid devices, actions, values, or event ordering fail with the offending line number. A replay file is limited to 100,000 lines of at most 4,096 characters each.

## Validation

`tests/input_tests.cpp` attaches four real SDL virtual game controllers under the dummy video driver. It injects button and axis values, reads their guest SI reports, checks Remote/Nunchuk state, sends guest motor commands to the SDL driver callback, and checks disconnection and replacement. It also verifies SI's retained error latch and explicit guest acknowledgement.

This test passed Windows MSVC and Linux GCC/Clang with warnings treated as errors. It does not exercise a physical controller, keyboard scan state, mouse focus, motion sensors, or commercial gameplay. Headless-only builds explicitly skip the SDL backend test. The separate Remote packet and guest IPC transport tests validate the device protocol with authored inputs.

`tests/output_tests.cpp` separately checks fitted presentation in all three display modes, window resizing, mouse IR coordinates, and exclusion of the black bars using SDL's dummy driver. These checks establish the host coordinate transform, not physical controller calibration or game acceptance.

`tests/input_replay_tests.cpp` checks parsing, range failures, instruction/field ordering, reset behavior, and simultaneous events. Its authored DOL runs real CPU instructions to request SI reports before and after a replay event on both consoles. The focused Linux GCC run passed; other current build results are recorded in [testing](testing.md).
