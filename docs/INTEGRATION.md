# Integrating with mecon-firmware

> **Target documentation (planned and MECON-specific work; not implemented here unless a section says so).** The tagged releases v0.1.0 to v0.1.2 are source-only pre-releases without the device interface. Current `main` implements the generic [device interface v1](contract/DEVICE_INTERFACE.md) and the standalone features in the [README](../README.md), with real-board evidence in [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md). The generic work still planned (Issues #95, #96, #109, #110, #114 to #117) and the MECON-specific work (Issues #118 to #123) described here are not implemented. Release timing is a separate decision. This page describes the target for the public line, not the private 0.9x reference firmware, whose shipped behaviour differs in places (see [Current-to-target adjustments](CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line)). See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

The public line is based on released MeshCore 1.18.0 and is designed for MeshContinuum and independent integrations. **Start with [device interface v1](contract/DEVICE_INTERFACE.md): it is built on `main`, works with any conforming manager, and wins over the vocabulary below where they differ.** It uses `device_id` from `hello` (or the CLI line `get device.id`), the topics `mecon/<device_id>/req|resp|ev/<stream>|status`, command `0x7F` on USB and BLE, and `mecon> ` lines on the Repeater and Room server console. The vocabulary below is the target; a backend that must talk to the private 0.9x reference firmware today needs the legacy vocabulary described in [Current-to-target adjustments](CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line).

## Start with capabilities

Do not infer behaviour from board name, role or firmware version. Read protocol/profile versions, role, hardware target and capabilities. Native MeshCore 1.18 operations should be used where they already provide the required semantics; MECON-specific behaviour uses the public contract.

## Integration paths

1. **MQTT** — remote status, settings and the `health` stream through the active session (built); observations and messaging are target.
2. **USB** — native/local access plus the interface in command `0x7F` (built). The private reference firmware instead carries MECON operations in a JSON tunnel (`CMD_MECON_JSON`, command 120); that is legacy wire, not the current contract.
3. **BLE** — NimBLE-based local Companion access where advertised; the same `0x7F` frames as USB (built).
4. **Hybrid browser bridge** — browser transports the same logical envelopes to a backend (the existing MECON Reader does this and requires a Backend; independent managers do not need one).
5. **LoRa** — management over direct and channel LoRa and compact LoRa telemetry are planned and not built: see [Management over LoRa (planned)](ARCHITECTURE.md#management-over-lora-planned). Today, RF management is the stock remote-admin CLI ([CLI](CLI.md)).

## Provisioning

Public builds do not compile in a MeshContinuum service dependency. Provisioning supplies Wi-Fi profiles (the standalone firmware stores up to ten), cloud MQTT endpoint/credentials/namespace, local-broker discovery/authority configuration, device identity metadata and authorization grants.

The device maintains one MQTT session at a time. Local discovery changes the preferred endpoint, not the authority model.

The reference firmware is provisioned over USB with a key/value companion command (46) for the Wi-Fi slots, broker, label, connectivity mode and the write-once identifier, enrolment and Reader-unlock secrets. A first bare Deployment provisioning follows, after which every write must be signed. It stores up to four broker profiles and opens one session at a time.

## Compatibility

Integrations gate on contract/profile versions and capabilities, not MECON firmware numbers. `meshcore_base_version` identifies the exact 1.18-derived base. Unknown fields are forward-compatible; unknown capabilities never imply authority.