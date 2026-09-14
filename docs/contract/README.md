# Public device contract

These documents are the canonical target contract between mecon-firmware and compatible backends/Readers.

- [Core device contract](CORE_DEVICE_CONTRACT.md)
- [MQTT protocol](MQTT_PROTOCOL.md)
- [Direct USB/BLE protocol](DIRECT_READER_PROTOCOL.md)
- [Device settings](DEVICE_SETTINGS.md)
- [Security and authority](SECURITY_AND_AUTHORITY.md)

## Contract principles

1. Contracts are backend-neutral.
2. Capabilities and independently versioned profiles describe behavior.
3. Transport does not redefine the logical operation.
4. Authority is explicit and least-privilege.
5. Unknown fields are forward-compatible; unknown authority is never granted.
6. MeshCore-native operation remains available without this contract.
7. Public target names use `mecon`, never a private deployment name.

The current private implementation contains legacy spellings and transport-specific mechanisms. Those are migration concerns recorded in `../CONTRACT_MIGRATION_NOTES.md`, not normative behavior here.