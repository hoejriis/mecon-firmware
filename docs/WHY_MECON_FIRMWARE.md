# Why mecon-firmware?

MeshCore is valuable because the mesh keeps working without any infrastructure. mecon-firmware starts from that property and keeps it: everything it adds sits on top of an ordinary MeshCore node and can be absent without breaking the node.

## Design goals

- **MeshCore first.** A Companion, Repeater or Room server does its normal radio job with no Wi-Fi, broker, manager or Internet connection. Losing any of them never stops the mesh role.
- **Built on released upstream.** The firmware is built on MeshCore 1.18.0 and extends its Wi-Fi, configuration, command, UI and board facilities rather than replacing them. Native frames, CLI commands and wire formats stay compatible, so stock apps and nodes interoperate. Some commands behave differently for security reasons, and those restrictions are listed in the [comparison](../README.md#stock-meshcore-1180-and-mecon-firmware-compared) and the [delta register](UPSTREAM_DELTA.md).
- **A modern ESP32 runtime.** The Heltec V3 and V4 build on Arduino-ESP32 3.1.3 / ESP-IDF 5.3 with NimBLE, on IDF libraries rebuilt for more heap ([runtime platform](PLATFORM.md)).
- **One management model across supported transports, with consistent authorization and explicit capability limits.** An operation means the same on every path that carries it, under the same rules. A transport is a way in, never a permission. Which operations a path carries differs, for stated reasons ([what differs, and why](SUPPORTED_HARDWARE.md#what-differs-and-why)).
- **Local first.** Up to 10 Wi-Fi profiles in priority order, and one MQTT session at a time. The device picks a broker by the role a manager assigns it (local, primary, secondary), then by priority within a role ([MQTT](MQTT.md)).
- **Secure by default.** Untrusted links can message but not manage. Default passwords and PINs never open remote administration. Wi-Fi and broker credentials are write-only, and ordinary reads and logs carry no secret. The narrow exceptions are explicit: the private key under its custody rule, and the BLE PIN to a trusted caller ([postures](STANDALONE_POSTURES.md), [security contract](contract/SECURITY_AND_AUTHORITY.md#credentials-and-secrets)).
- **Measured resource limits.** Companion contact capacity follows the board's free flash and heap, and the device reports it ([contacts](CONTACTS.md)). The T1000-E Companion keeps the stock fixed table.
- **Backend neutral.** The public contracts belong to this project. Any manager that implements them can use the device ([contract index](contract/README.md)).
- **Upstream friendly.** Every change to an upstream file is recorded in the [upstream delta register](UPSTREAM_DELTA.md), so later upstream releases merge in the ordinary way.
- **Useful piece by piece.** Security postures, duty-cycle enforcement, atomic saves, the Wi-Fi and MQTT controllers, contact memory and the single CLI engine need no manager, so other projects can use them on their own ([README](../README.md#why-use-it)).

## Relationship to MeshContinuum

[MeshContinuum](https://github.com/hoejriis/MeshContinuum) is a sister project: a Backend with a browser Reader that manages mecon-firmware devices. It is one manager among any that implement the contracts, not a requirement, and its Reader needs its Backend. The firmware provides generic capabilities a manager asks for, including sending and collecting messages. MECON connected messaging, backfill, RF health monitoring, autonomous outage forwarding and firmware-image delivery over LoRa are later work built on them, and are outside the product described here ([where the line is](INTEGRATION.md#what-the-firmware-does-and-what-a-manager-does)).
