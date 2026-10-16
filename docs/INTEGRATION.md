# Integrating with mecon-firmware

> **Target documentation:** this public repository has no installable release yet. Public releases start at **0.1.0**, a standalone enhanced MeshCore on 1.18.0 (see [Clean refounding on MeshCore 1.18](CLEAN_REFOUNDING_1_18.md)); the first version to serve a MECON fleet is planned as 0.8.0 (see [Versioning and releases](VERSIONING_AND_RELEASES.md)). This page describes the target for the public line, not the private 0.9x reference firmware, whose shipped behaviour differs in places (see [Current-to-target adjustments](CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line)). See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

The public line (first release mecon-firmware 0.8.0) is based on released MeshCore 1.18.0 and is designed for MeshContinuum and independent integrations. The vocabulary below is the target; a backend that must talk to the private 0.9x reference firmware today needs the legacy vocabulary described in [Current-to-target adjustments](CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line).

## Start with capabilities

Do not infer behaviour from board name, role or firmware version. Read protocol/profile versions, role, hardware target and capabilities. Native MeshCore 1.18 operations should be used where they already provide the required semantics; MECON-specific behaviour uses the public contract.

## Integration paths

1. **MQTT** — remote status, observations, messaging and management through the currently active local/cloud session.
2. **USB** — native/local access plus MECON-specific tunnel operations where required. The reference firmware carries them in a JSON tunnel (`CMD_MECON_JSON`, command 120) with request kinds for commands, configuration, authority updates and update chunks.
3. **BLE** — NimBLE-based local Companion access where advertised; the same tunnel is available.
4. **Hybrid browser bridge** — browser transports the same logical MECON envelopes to a backend.

## Provisioning

Public builds do not compile in a MeshContinuum service dependency. Provisioning supplies up to three Wi-Fi profiles, cloud MQTT endpoint/credentials/namespace, local-broker discovery/authority configuration, device identity metadata and authorization grants.

The device maintains one MQTT session at a time. Local discovery changes the preferred endpoint, not the authority model.

The reference firmware is provisioned over USB with a key/value companion command (46) for the Wi-Fi slots, broker, label, connectivity mode and the write-once identifier, enrolment and Reader-unlock secrets. A first bare Deployment provisioning follows, after which every write must be signed. It stores up to four broker profiles and opens one session at a time.

## Compatibility

Integrations gate on contract/profile versions and capabilities, not MECON firmware numbers. `meshcore_base_version` identifies the exact 1.18-derived base. Unknown fields are forward-compatible; unknown capabilities never imply authority.