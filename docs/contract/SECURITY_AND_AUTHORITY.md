# Security and authority contract

## Authority classes

A connection or credential is granted explicit capabilities rather than blanket device ownership. Standard authority classes are:

- `observe` — receive packet observations/status;
- `read_config` — read non-secret configuration/schema;
- `message` — Companion message send/receive operations;
- `manage` — change allowlisted configuration;
- `admin` — sensitive lifecycle actions such as reboot/advertise/credential rotation;
- `ota` — request installation of an approved firmware release.

A deployment may combine classes, but implementations must not infer a stronger class from a weaker one.

## Authentication

MQTT uses TLS plus broker credentials. Device-level authorization binds the broker profile/credential to grants. A shared enrollment token alone is not treated as per-message cryptographic authentication.

Direct USB/BLE sessions use the local transport's security model plus explicit operation restrictions. BLE pairing credentials are device-specific.

## Replay and idempotency

Remote state-changing jobs carry stable identifiers and replay protection. The protection domain must remain correct with two brokers and direct transports so replaying the same logical job through another path cannot duplicate an RF send or configuration change.

## Credential management

Credentials are write-only. Rotation is explicit and recoverable. A management connection changing its own broker credentials must define how the terminal result is delivered before/after reconnect.

## OTA authority

OTA authority permits only installation of firmware represented by an approved, signed manifest compatible with the device's hardware, role and variant. It does not permit arbitrary URL or arbitrary binary execution.