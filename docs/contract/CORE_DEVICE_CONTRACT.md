# Core device contract

## Identity

A device reports `device_id`, native `node_public_key_hex`, `hardware_target`, native role (`companion`/`repeater`), build variant, `mecon_firmware_version`, exact `meshcore_base_version`, and MECON contract/profile versions.

For MECON 1.0, supported release builds identify a pinned **MeshCore 1.18** base from upstream `main`.

## Profiles and capabilities

`core` is mandatory. Optional profiles may include messaging, remote administration, health, OTA and direct Reader functionality. Capabilities are discovered explicitly. A client must not infer a MECON capability from board/role/version alone.

## Native boundary

Companion and Repeater retain their MeshCore 1.18 semantics. MECON does not define proprietary replacement RF roles. Where 1.18 already exposes a suitable operation, the public contract references/reuses it rather than creating a duplicate MECON command.

## Core status

Supported builds report identity/version/role, capabilities/profiles, health/status, native RF role state, Wi-Fi state, MQTT state/path (`local` or `cloud` where applicable), BLE state where supported, and machine-readable resource limits.

## Resource declaration

Contact capacity and other material limits are measured after the IDF5/NimBLE migration and reported by the build. MECON 1.0 does **not** normatively fix contact capacity to the private MVP's historical 32 or 64 slots.