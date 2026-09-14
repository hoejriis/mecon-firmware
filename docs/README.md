# mecon-firmware documentation

These documents describe the **public release target**, not the temporary state of the private development repositories.

## Product and architecture

- [Why mecon-firmware](WHY_MECON_FIRMWARE.md)
- [Architecture](ARCHITECTURE.md)
- [Supported hardware and roles](SUPPORTED_HARDWARE.md)
- [Connectivity and operating modes](CONNECTIVITY.md)
- [Integration guide](INTEGRATION.md)
- [Security model](SECURITY_MODEL.md)
- [Upstream and porting](UPSTREAM_AND_PORTING.md)
- [Versioning and releases](VERSIONING_AND_RELEASES.md)

## Public integration contract

- [Contract index](contract/README.md)
- [Core device contract](contract/CORE_DEVICE_CONTRACT.md)
- [MQTT contract](contract/MQTT_PROTOCOL.md)
- [Direct USB/BLE contract](contract/DIRECT_READER_PROTOCOL.md)
- [Settings contract](contract/DEVICE_SETTINGS.md)
- [Security and authority contract](contract/SECURITY_AND_AUTHORITY.md)

The contract documents are normative for the public target. Backend-specific implementation details are not part of the firmware contract.

## Migration

[Current-to-target contract adjustments](CONTRACT_MIGRATION_NOTES.md) records known differences between the current private development implementation and this public target. It is deliberately separate from the normative contract so legacy behavior does not become the public design by accident.