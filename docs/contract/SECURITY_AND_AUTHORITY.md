# Security and authority contract

## Principles

Reachability is not authority. Discovering a LAN broker, connecting to MQTT, being on the same Wi-Fi or possessing a resilience-channel key must not silently grant broader device administration.

Authority is least-privilege, explicit, versioned and enforced by the device.

## Authority classes

The public capability model distinguishes at least:

- observe/read public operational state;
- read safe configuration/schema;
- message/send;
- manage configuration;
- resilience/backfill administration;
- privileged device administration/reboot;
- OTA.

A weaker class never implies a stronger one. Unsupported/unauthorized jobs receive an explicit denial when a job identity is available rather than being silently executed or retried forever.

## MQTT authentication and path switching

Only one MQTT session is active at a time. Local broker discovery selects a candidate endpoint but **does not establish trust**. The endpoint must satisfy provisioned authentication/authorization and firmware TLS policy. Switching between local and cloud paths must not broaden authority or reset replay protection.

Local and cloud are alternate transports for the same logical authority in MECON 1.0. Independent command authorities minting colliding job identities are not supported unless a future federation profile explicitly defines them.

## Device identity and enrollment

`device_id`, native MeshCore node identity and backend enrollment/database identity are distinct. Enrollment provisions the credentials/authority required for remote management. Device-specific enrollment secrets are write-only and are not returned through ordinary status/config reads.

New MECON 1.0 firmware emits canonical MECON names only; legacy `gateway_id`, `enrollment_token` and `deimos_*` representations are handled by migration adapters as appropriate.

## Replay and idempotency

Remote state-changing jobs carry stable identifiers. The device prevents duplicate execution across:

- MQTT QoS redelivery;
- reconnects;
- local/cloud endpoint changes;
- supported direct transport retries where the same logical job identity is used.

A completed send/config/OTA job must not execute twice merely because connectivity changed.

## Credentials

Wi-Fi passwords, MQTT passwords/tokens, resilience channel keys and similar secrets are write-only. Reads expose only non-secret configured/state metadata.

BLE pairing credentials are device-specific. On display-capable devices the PIN may be shown locally while BLE is unconnected, but it is not exposed as an ordinary remotely readable setting.

## TLS trust

Firmware ships an explicit trust policy/bundle or supported pin/CA identifiers. A remote configuration may select among supported trust anchors where the release exposes that capability, but arbitrary certificate material must not silently become trusted merely because a backend sent it. Unknown trust identifiers fail closed.

## Outage/resilience channel security

The private resilience channel is a deliberate confidentiality trade-off:

- the channel key authenticates membership, not individual sender identity;
- every member holding the channel key can read forwarded DM content and can construct channel-authenticated content;
- origin/source/destination/forwarder hints inside the resilience envelope are correlation claims, not cryptographic identity proof;
- replay protection prevents ordinary duplicate injection but does not turn hints into authenticated attribution;
- stock-readable duplicate copies are unauthenticated user-readable channel text and can never trigger authenticated DM injection.

The backend/operator UI must warn before enabling stock-readable copies and before enrolling parties who should not see forwarded DM content.

## Direct transports

USB/BLE MECON extensions are allowlisted and versioned. They do not expose arbitrary remote shell/code execution. Native MeshCore key/identity operations keep their native security semantics.

Direct reset/reprovision operations must name their scope: changing network configuration, clearing MECON enrollment/authority and clearing native MeshCore identity are different actions.

## Logging and diagnostics

Logs, crash records, framebuffer export and raw observations must not contain configured secrets. Diagnostic streams are bounded and must not block normal RF processing.

## OTA

OTA authority permits installation only of approved firmware compatible with the reported hardware, role and variant. The device verifies manifest integrity/signature before installation. Arbitrary URL/binary execution is not OTA authority.

MECON 1.0 release firmware is based on the pinned released MeshCore 1.18 lineage plus the documented pioarduino/Arduino-ESP32 3.x/IDF 5.x, NimBLE and MECON changes. Post-update status/version confirmation is part of the OTA lifecycle.

## Failure behaviour

Security-sensitive parsing fails closed: malformed credentials, unknown authority/trust identifiers, invalid key lengths, unsupported operations and wrong types are rejected rather than coerced to a broader/default authority.