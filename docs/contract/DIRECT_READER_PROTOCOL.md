# Direct USB/BLE Reader protocol

## Goal

Direct access preserves native MeshCore 1.18 interfaces and adds only the MECON-specific operations needed for the public capability model.

## Companion

- USB preserves standard MeshCore Companion framing.
- BLE preserves standard Companion interoperability but the public ESP32 implementation uses **NimBLE**.
- MECON-specific operations use a reserved/versioned tunnel rather than duplicating native Companion operations.

The direct path may expose MECON status/capabilities, configuration, observations, health and allowlisted administration where supported.

## Repeater

USB provides recovery and MECON management/observation access where advertised. BLE is capability-dependent and must not be assumed solely from the Repeater role.

## UI/pairing

BLE credentials are device-specific. On display-capable targets, the pairing PIN is shown on the existing front screen while BLE is available but unconnected; connected state replaces the PIN. No secondary MECON screen is required for 1.0.

## Security

Direct access never exposes unrestricted execution. Sensitive values remain write-only. Native MeshCore identity operations remain governed by their native/security semantics.