# Integrating with mecon-firmware

MECON 1.0 is based on released MeshCore 1.18 and is designed for MeshContinuum and independent integrations.

## Start with capabilities

Do not infer behaviour from board name, role or firmware version. Read protocol/profile versions, role, hardware target and capabilities. Native MeshCore 1.18 operations should be used where they already provide the required semantics; MECON-specific behaviour uses the public contract.

## Integration paths

1. **MQTT** — remote status, observations, messaging and management through the currently active local/cloud session.
2. **USB** — native/local access plus MECON-specific tunnel operations where required.
3. **BLE** — NimBLE-based local Companion access where advertised.
4. **Hybrid browser bridge** — browser transports the same logical MECON envelopes to a backend.

## Provisioning

Public builds do not compile in a MeshContinuum service dependency. Provisioning supplies up to three Wi-Fi profiles, cloud MQTT endpoint/credentials/namespace, local-broker discovery/authority configuration, device identity metadata and authorization grants.

The device maintains one MQTT session at a time. Local discovery changes the preferred endpoint, not the authority model.

## Compatibility

Integrations gate on contract/profile versions and capabilities, not MECON firmware numbers. `meshcore_base_version` identifies the exact 1.18-derived base. Unknown fields are forward-compatible; unknown capabilities never imply authority.