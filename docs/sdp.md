# Remote service discovery

The remote connection path answers Bluetooth Service Discovery Protocol service-search and service-attribute requests. The encoder constructs data elements from typed attributes and generates the HID report descriptor from report IDs and payload sizes.

HID service `0x10000` advertises the control and interrupt PSMs, report formats, reconnect flags, language information and controller identity. Device identity service `0x10001` advertises its vendor, product and version fields. Service search currently accepts the HID UUID `0x1124`.

Attribute replies retain the transaction ID and use two-byte continuation offsets. Each response carries at most 118 attribute bytes and respects a smaller requested byte limit. Single attributes, inclusive ranges and multiple selectors are supported. Invalid lengths, unsupported operations, reversed ranges and out-of-range continuations raise an explicit error.

Public tests reconstruct both complete records with byte limits from 7 to 65,535, check attribute selection and reject malformed requests. An optional direct comparison checks the encoded HID and identity records against the pinned behavioral target; see [the source map](sdp-reference.md). This verifies the service-discovery records and packet encoding, not a complete wireless session or physical controller.

The transport fixture opens the USB controller through IPC, connects and configures an SDP channel, and reconstructs both records through ACL packets with 31-byte continuation chunks. It checks local and peer CID routing, transaction IDs, and deferred bulk input while an HCI event remains queued. Malformed SDP produces an explicit bus diagnostic, and an unknown CID returns an error. These are synthetic controller transactions without a physical radio.
