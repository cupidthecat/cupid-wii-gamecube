# Mouse pointing and infrared projection

Behavioral checkout pin: `562875dd60fd0ff50b65dbbb611a2096802cd1b9`. Source paths below are relative to `Source/Core/`.

Mouse coordinates are normalized inside the displayed game area, with horizontal input positive rightward and vertical input positive upward. The default pointing pose places the Remote two metres from a sensor bar with LED clusters 0.2 metres apart. It uses a 10 cm top-bar offset, 25 degrees of total yaw and 20 degrees of total pitch. The camera projects the two LEDs into its 1024 by 768 coordinate space, truncates coordinates and calculates their reported sizes. The other two objects remain unavailable.

The center pose reports `(580,452)` and `(443,452)`, each with size one. Those coordinates follow the projection and preserve LED order; they are not fixed offsets around a mouse pixel. Points outside the view remain unavailable. Unfocused mouse input and positions in the window's black bars also supply no visible points. The pure helper accepts either bar orientation; interactive output currently uses the default top orientation.

The previous host mapping supplied a constant 64-pixel separation. A private guest SDK check interpreted that separation as more than four metres and rejected it. Ordinary wider IR replay then produced a visible Galaxy pointer and slot hover. Those game captures use raw replay points and a separately preserved runtime; they do not validate the new live mouse mapping or commercial gameplay.

## Source mapping

| Product operation | Behavioral implementation and callers |
| --- | --- |
| `platform::pointer_camera` | `Core/HW/WiimoteEmu/Camera.cpp`, `CameraLogic::GetCameraPoints`; `Camera.h`, camera resolution, field of view and LED separation; `Common/Matrix.cpp`, rotation, translation and perspective operations. |
| Default normalized pose | `InputCommon/ControllerEmu/ControlGroup/Cursor.cpp`, constructor, `GetTotalYaw`, `GetTotalPitch`, `GetVerticalOffset`; `Core/HW/WiimoteEmu/Dynamics.cpp`, `EmulatePoint`, `GetRotationalMatrix`; `WiimoteEmu.cpp`, `GetTransformation`, `GetTotalTransformation`, `PrepareInput`. |
| Default bar orientation | `Core/Config/SYSCONFSettings.cpp`, `SYSCONF_SENSOR_BAR_POSITION`; the product's default `BT.BAR` setting also places the bar on top. |
| Fitted host coordinates and visibility | `src/platform/output.cpp`, `Output::poll`; the [runtime mapping](runtime-reference.md) records the presentation rectangle and SDL input callers. |

The helper projects the settled pointing pose. Angular acceleration, combined swing/tilt/IMU camera transforms, a configurable live bar orientation and physical controller calibration remain unfinished. Keyboard acceleration and shake continue through their separate report inputs.

## Validation

The opt-in `pointer_reference_checks` target links the unchanged camera and matrix translation units from the pinned checkout. It calls the actual projection routine using poses prepared from the reviewed default callers. It passed 232,098 exact coordinate, size and unavailable-object comparisons on native MSVC and Linux GCC: a 257 by 257 grid in both bar orientations and 100,000 deterministic random positions. A separate fully instrumented Clang ASan/UBSan build of the driver, product projection and both external units passed the same cases.

An earlier private original prototype also passed 114,075 projection cases over nine distances, three heights and an extended cursor grid. That comparison includes defined visibility and size cases outside the final helper's fixed default-distance interface.

Public pure tests check the default pose, both orientations, axis direction, normalized edges and invalid inputs. SDL output tests require the exact center coordinates after resizing in each display mode and reject black-bar input. The updated SDL regression failed against the preceding constant-separation implementation and passed the repair. These checks establish the host projection and presentation seam; game acceptance, full motion equivalence and physical-console measurements remain separate requirements.
