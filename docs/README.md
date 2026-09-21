# mecon-firmware documentation

These documents describe the **MECON 1.0 public release target**. MECON 1.0 has an explicit upstream dependency on **MeshCore 1.18 after 1.18 is merged/released on upstream `main`**. The moving MeshCore `dev` branch may be used for investigation and early validation, but is not the production source baseline.

The private MVP is a behavioural/test reference. Public implementation and contracts are rebuilt from the released MeshCore 1.18 baseline rather than copied from the private firmware.

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

The contract documents are normative for MECON-specific behaviour. Where MeshCore 1.18 already supplies a suitable native operation, MECON reuses it rather than defining a duplicate operation.

## Migration

[Current-to-target contract adjustments](CONTRACT_MIGRATION_NOTES.md) records lessons and differences from the private MVP. It is deliberately non-normative.