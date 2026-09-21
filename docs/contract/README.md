# Public device contract

These documents define the canonical **MECON-specific** contract for MECON 1.0. The firmware itself is based on released MeshCore 1.18 from upstream `main`; native 1.18 operations remain native and are not duplicated here merely to create a MECON equivalent.

- [Core device contract](CORE_DEVICE_CONTRACT.md)
- [MQTT protocol](MQTT_PROTOCOL.md)
- [Direct USB/BLE protocol](DIRECT_READER_PROTOCOL.md)
- [Device settings](DEVICE_SETTINGS.md)
- [Security and authority](SECURITY_AND_AUTHORITY.md)

## Principles

1. MeshCore 1.18 native behaviour is reused where suitable.
2. MECON contracts are backend-neutral.
3. Capabilities and independently versioned profiles describe MECON behaviour.
4. MQTT, USB and BLE do not redefine logical MECON operations.
5. Authority is explicit and least-privilege.
6. Unknown fields are forward-compatible; unknown authority is never granted.
7. Public names use `mecon`, never private deployment vocabulary.
8. Resource limits such as contact capacity are reported from the measured supported build, not hard-coded from the private MVP.

Legacy/private behaviour is documented only in `../CONTRACT_MIGRATION_NOTES.md`.