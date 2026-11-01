# mecon-firmware documentation

mecon-firmware is MeshCore 1.18.0 with secure management, diagnostics and signed updates added. Start with the [project README](../README.md) for what it does and how it compares with stock MeshCore. These pages describe the finished product. For what a given release contains, see [availability](../README.md#getting-it).

## Using it, task by task

1. [Choose hardware and a role](SUPPORTED_HARDWARE.md): what each board and role can do, and its limits.
2. [Install and set up a device](GETTING_STARTED.md): build, flash, and the first settings over USB.
3. [Connect to Wi-Fi and MQTT](CONNECTING.md): Wi-Fi profiles, broker profiles and TLS, and BLE or Wi-Fi mode.
4. [Manage a device](MANAGING.md): over USB, BLE, MQTT and LoRa, and with a private management channel.
5. [Monitor a device](MONITORING.md): telemetry streams, logs and screen snapshots.
6. [Update and recover](UPDATING_AND_RECOVERY.md): signed updates, confirm-or-revert, resets and recovery.

## Reference

- [CLI reference](CLI_REFERENCE.md): every stock and mecon-firmware command, who may run it, and from where
- [Command-line engine](CLI.md): one privilege table for every way a command reaches the device
- [Security postures](STANDALONE_POSTURES.md): Normal and Hardened, the audit log, recovery
- [Wi-Fi profiles](WIFI.md), [MQTT broker profiles](MQTT.md), [connectivity mode](CONNECTIVITY_MODE.md)
- [Contact memory](CONTACTS.md)
- [Screens and buttons](UI.md)
- [Hardware evidence](HARDWARE_VERIFIED.md): what has been checked on real boards

## Integrating a manager

- [Contract index](contract/README.md): which document is normative for what
- [Device interface](contract/DEVICE_INTERFACE.md): the wire contract over USB, BLE and MQTT, with [schemas](contract/schema) and [fixtures](contract/fixtures)
- [Management authority](contract/AUTHORITY.md), [managed operations](contract/OPERATIONS.md), [telemetry](contract/TELEMETRY.md), [signed updates](contract/UPDATES.md), [management channel](contract/MANAGEMENT_CHANNEL.md), [management over LoRa](contract/LORA_MANAGEMENT.md)
- [Integration guide](INTEGRATION.md) and [security model](SECURITY_MODEL.md)

## Developing

- [Contributing](../CONTRIBUTING.md)
- [Architecture](ARCHITECTURE.md) and [why mecon-firmware](WHY_MECON_FIRMWARE.md)
- [Simulated-board test harness](SIMULATION.md)
- [Runtime platform](PLATFORM.md): framework, ESP-IDF and BLE stack per target
- [Upstream delta register](UPSTREAM_DELTA.md) and [upstream and porting](UPSTREAM_AND_PORTING.md)
- [Versioning and releases](VERSIONING_AND_RELEASES.md)

## Upstream MeshCore documents

MeshCore's own documents in this folder ([companion protocol](companion_protocol.md), [CLI commands](cli_commands.md), [packet format](packet_format.md), [FAQ](faq.md) and the others) are kept as upstream wrote them. Where mecon-firmware changes behaviour, the pages above say so.
