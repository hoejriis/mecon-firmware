# mecon-firmware

mecon-firmware is MeshCore 1.18.0 with secure management, diagnostics and signed updates added. The mesh keeps working on its own. A manager can configure, monitor and update a device over USB, Bluetooth, MQTT or LoRa, under one authorization model.

- [What it does, and how it compares with stock MeshCore](../README.md)
- [Fleet profiles: Personal Companion, Repeater Gateway, RF-only Repeater](../README.md#fleet-profiles)
- [Why mecon-firmware](WHY_MECON_FIRMWARE.md)
- [Documentation index](README.md): using a device task by task, and the reference pages
- [Integrating a manager](INTEGRATION.md) and the [device contract](contract/README.md)

These pages describe the target product. [Availability](AVAILABILITY.md) says what you can get today, and [hardware evidence](HARDWARE_VERIFIED.md) records what has been checked on real boards.

## MeshCore protocol references

These pages come from upstream MeshCore and apply unchanged:

- [Frequently asked questions](./faq.md)
- [CLI commands](./cli_commands.md)
- [Companion protocol](./companion_protocol.md)
- [Packet format](./packet_format.md)
- [QR codes](./qr_codes.md)
