# Security model

## Principles

- MeshCore RF security remains MeshCore's responsibility.
- Transport access and device authority are separate concepts.
- Observation, messaging and administration are separate grants.
- Secrets are write-only through ordinary management APIs.
- No backend receives an unrestricted shell, serial console or arbitrary NVS interface merely because it can manage the device.
- Direct local access must not silently inherit remote administrator authority.

## MQTT trust

Each broker profile has its own TLS/authentication material and authorization grants. Connecting successfully to a broker does not by itself authorize every command that can reach the device.

Remote jobs use stable identifiers, expiry where applicable and replay/idempotency protection. Results are tied to the originating job.

## Secrets

Wi-Fi passwords, broker passwords/tokens, private MeshCore identity material and equivalent credentials must never be returned in clear text by status or configuration reads. APIs may report safe metadata such as configured/not-configured, SSID, host and redacted identifiers.

## Direct USB/BLE

USB is treated as physical/local access but still uses explicit protocol operations rather than an implicit arbitrary command bridge. BLE uses per-device pairing credentials and permits one supported central session at a time unless a future contract states otherwise.

The MECON direct tunnel carries allowlisted logical operations. It must not turn a browser connection into arbitrary firmware execution.

## OTA

Supported OTA releases are selected from approved manifests and validated for hardware target, role and build variant. Firmware images are integrity checked and release manifests are signed. Arbitrary URL flashing is not part of the remote-management contract.

Recovery through USB remains available when OTA cannot recover a device.

## Backend independence

No MeshContinuum-specific trust root, hostname, account or credential is compiled into the generic public firmware. A third-party backend can establish its own broker and management authority using the same documented mechanisms.