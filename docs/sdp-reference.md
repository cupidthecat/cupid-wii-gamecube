# Service discovery source map

Behavioral target pin: `562875dd60fd0ff50b65dbbb611a2096802cd1b9`.

| Behavior | Target file and functions | Implementation and checks |
| --- | --- | --- |
| HID service search | `Source/Core/Core/IOS/USB/Bluetooth/WiimoteDevice.cpp`: `HandleSDP`, `SDPSendServiceSearchResponse` | `src/hw/sdp.cpp` validates the PDU, echoes its transaction and returns the HID record handle. Public tests check the wire response and malformed inputs. |
| Attribute records and continuation | `Source/Core/Core/IOS/USB/Bluetooth/WiimoteDevice.cpp`: `ParseCont`, `ParseAttribList`, `SDPSendServiceAttributeResponse`; `Source/Core/Core/IOS/USB/Bluetooth/WiimoteHIDAttr.cpp`: `GetAttribPacket` | Typed data elements encode both records. Public tests cover selection, fragment sizes and continuation bounds. |
| HID report descriptor | `Source/Core/Core/IOS/USB/Bluetooth/WiimoteHIDAttr.cpp`: HID record descriptor fields | The descriptor is generated from named HID items and report ID/length pairs, rather than stored response packets. |
| ACL channel routing | `Source/Core/Core/IOS/USB/Bluetooth/WiimoteDevice.cpp`: `ExecuteL2capCmd`, `HandleSDP` | The public transport fixture negotiates an SDP channel, checks peer CID routing and continuation, and consumes HCI events before deferred USB bulk input completes. |

`tests/sdp_reference_checks.cpp` links the actual `GetAttribPacket` implementation into an opt-in test executable. The driver compares every byte of all four HID response fragments and the identity record, across eight transaction IDs and three byte limits of at least 118. The MSVC and Linux comparisons each passed 120 packets, covering 14,448 response bytes. The runtime links only the project's implementation.

The target's captured fragments use a fixed 118-byte limit. The implementation also supports smaller requested limits and attribute filtering; those paths have public protocol tests and are outside the captured-fragment comparison. Neither result establishes physical hardware accuracy.
