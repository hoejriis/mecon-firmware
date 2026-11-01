# Why mecon-firmware?

MeshCore is valuable because the mesh keeps working without any infrastructure. mecon-firmware starts from that property and keeps it: everything it adds sits on top of an ordinary MeshCore node and can be absent without breaking the node.

## Design goals

- **MeshCore first.** A Companion, Repeater or Room server does its normal radio job with no Wi-Fi, broker, manager or Internet connection. Losing any of them never stops the mesh role.
- **Built on released upstream.** The firmware is built on MeshCore 1.18.0 and extends its Wi-Fi, configuration, command, UI and board facilities rather than replacing them. Native frames, CLI commands and wire formats stay as they are, so stock apps and nodes interoperate.
- **A modern ESP32 runtime.** The Heltec V3 and V4 build on Arduino-ESP32 3.1.3 / ESP-IDF 5.3 with NimBLE, on IDF libraries rebuilt for more heap ([runtime platform](PLATFORM.md)).
- **One management model.** USB, BLE, MQTT and LoRa carry the same operations under the same rules. A transport is a way in, never a permission.
- **Local first.** Up to 10 Wi-Fi profiles in priority order, and one MQTT session at a time. The device picks a broker by the role a manager assigns it (local, primary, secondary), then by priority within a role ([MQTT](MQTT.md)).
- **Secure by default.** Untrusted links can message but not manage. Default passwords and PINs never open remote administration, and secrets are never read back ([postures](STANDALONE_POSTURES.md)).
- **Measured resource limits.** Companion contact capacity follows the board's free flash and heap, and the device reports it ([contacts](CONTACTS.md)). The T1000-E Companion keeps the stock fixed table.
- **Backend neutral.** The public contracts belong to this project. Any manager that implements them can use the device ([contract index](contract/README.md)).
- **Upstream friendly.** Every change to an upstream file is recorded in the [upstream delta register](UPSTREAM_DELTA.md), so later upstream releases merge in the ordinary way.
- **Useful piece by piece.** Security postures, duty-cycle enforcement, atomic saves, the Wi-Fi and MQTT controllers, contact memory and the single CLI engine need no manager, so other projects can use them on their own ([README](../README.md#why-use-it)).

## Relationship to MeshContinuum

[MeshContinuum](https://github.com/hoejriis/MeshContinuum) is a sister project: a Backend with a browser Reader that manages mecon-firmware devices. It is one manager among any that implement the contracts, not a requirement. Fleet features it builds on top, such as connected messaging, backfill and LoRa image delivery, belong to it and are not part of this firmware's feature set.
