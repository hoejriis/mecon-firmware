# Security model

## Principles

- MeshCore 1.18 RF security remains MeshCore's responsibility.
- Transport access and device authority are separate.
- Observation, messaging, management and OTA are separate grants.
- Secrets are write-only through ordinary management APIs.
- No remote transport grants an unrestricted shell or arbitrary execution surface.

## MQTT trust

MECON 1.0 maintains one active MQTT session. A locally discovered broker may be preferred over cloud, but discovery alone never grants trust or authority. The active endpoint must satisfy the provisioned authentication/authority model.

Remote state-changing jobs use stable identifiers and replay/idempotency protection across endpoint changes and direct transports.

## Secrets

Wi-Fi passwords, MQTT credentials/tokens and private MeshCore identity material are never returned in clear text by ordinary status/configuration reads.

## USB/BLE

USB is physical/local access but still uses explicit operations. BLE uses NimBLE and device-specific pairing credentials. The pairing PIN may be displayed locally while BLE is unconnected; it is not exposed as a remotely readable secret.

## OTA

Managed OTA installs only approved firmware represented by signed/integrity-checked release metadata compatible with hardware, role and variant. MECON 1.0's release chain is rooted in the pinned MeshCore 1.18-derived source baseline plus documented MECON changes. USB remains the ultimate recovery path.

## Backend independence

No MeshContinuum-specific hostname, account or credential is compiled into generic public firmware.