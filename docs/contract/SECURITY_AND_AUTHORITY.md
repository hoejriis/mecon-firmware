# Security and authority contract

## Principles

Reachability is not authority. Discovering a broker, connecting to MQTT, being on the same Wi-Fi, holding a resilience-channel key or having physical proximity MUST NOT silently grant broader device administration.

MECON device authority is rooted in the **Deployment** trust model frozen by the MeshContinuum Deployment authority contract (`DEPLOYMENT_AUTHORITY_CONTRACT.md`). This document defines firmware-side invariants; the cross-repository Deployment authority contract defines the exact authority objects/signatures/generations.

Authority is explicit, versioned and enforced by the device. Security posture determines which local surfaces exist; it does not replace Deployment authority.

## Authority classes

The capability model distinguishes at least:

- observation/read public operational state;
- safe configuration/schema read;
- messaging/send;
- configuration management;
- resilience/backfill administration;
- privileged administration/reboot/posture;
- OTA;
- continuity/recovery operations.

A weaker capability never implies a stronger one. Unsupported/unauthorized state-changing jobs are denied without execution.

## Deployment authority

A device persists the Deployment identity/trust state required by the frozen Deployment authority contract. Backend instance authority is valid only when authorized under the Deployment trust root and current generation/revocation state.

The following do not grant command authority by themselves:

- MQTT broker discovery;
- successful TCP/TLS/MQTT connection;
- DNS/mDNS identity;
- network location;
- possession of a private resilience-channel key;
- physical USB connection.

Normal → Hardened MUST preserve the Deployment membership/authority state needed to authenticate authorized Backend/Reader/RF management.

## MQTT authentication and path switching

Only one MQTT session is active at a time. Local broker discovery selects a candidate endpoint but does not establish command trust. Endpoint transport security and Deployment command authority are separate checks.

Switching between eligible local/remote endpoints MUST NOT broaden authority, reset replay protection or duplicate state-changing work.

## Security posture

### Normal

Normal preserves applicable stock MeshCore local interoperability. BLE/USB/user messaging surfaces may exist according to target capability and Normal connectivity mode.

### Hardened

Hardened is a managed-infrastructure posture. It MUST:

- not initialize BLE, including on non-Wi-Fi targets, where USB then answers only identification and the authenticated-Reader handshake;
- not expose stock inbound Wi-Fi/app management;
- not expose stock USB/WebSerial administration;
- retain authorized MECON MQTT, secure-RF and same-Deployment Reader USB management where supported;
- never weaken itself because infrastructure is unavailable;
- prevent unauthenticated physical UI operations from changing posture/security/enrollment.

A Hardened Companion does not expose message content/identity or user send functionality locally. Authenticated management may retain applicable detailed telemetry/configuration.

Posture changes require authenticated management and a controlled reboot.

If persisted posture is invalid, firmware enters fail-secure recovery rather than Normal.

## Reader USB authority

Physical USB attachment is not administrative authority on Hardened devices. Unauthenticated Hardened USB exposes only minimal non-sensitive identity/version/posture guidance.

A MECON Reader may obtain the management surface after proving current authority from the same Deployment. Do not create a separate per-device Reader ACL merely because the transport is USB.

## Secure RF management

Secure MeshCore RF is an authorized management transport where supported, particularly for non-Wi-Fi repeaters. RF requests reach the same logical command/configuration plane after authentication/routing and MUST include replay protection and request/result correlation appropriate to LoRa constraints.

## Protected continuity material

Firmware provides a protected opaque continuity-storage boundary.

- Normal may hold continuity material.
- Hardened has no usable continuity/recovery authority.
- Normal → Hardened cryptographically destroys the local ability to use/decrypt the continuity blob.
- Hardened never exports continuity material over any management/status/logging surface.
- Hardened → Normal does not recreate it automatically.
- The device-local key protecting the blob MUST be made unusable by firmware on destroy. Where the target permits it SHOULD live in dedicated erasable storage outside the filesystem and be destroyed by physical erase with verification, since a filesystem unlink can leave bytes in flash (target-dependent). A physical flash dump of an unprotected device is outside this protection: the package itself is sealed by an operator passphrase and a compromised device is revoked.

The blob's schema, package generation, sync-key generation and Join/Recovery semantics are defined by the P3 Deployment continuity contract, not here.

Continuity material MUST NOT be confused with Deployment membership/authority state: the latter remains available in Hardened.

## Replay and idempotency

Remote state-changing jobs carry stable identifiers. The device prevents duplicate execution across MQTT redelivery/reconnect/path changes and supported direct/RF retries when the same logical job identity is used.

A management line refused before execution (for example unsigned where signed is required) consumes no request identifier and does not move any signer's watermark.

## Device identity and private-key custody

On ownership reassignment a device rotates to a new mesh identity generated on the device, triggered by a Deployment-signed request (or an authenticated direct Reader in a restricted posture). Rotation keeps configuration, connectivity, enrolment, the Deployment record, channels and posture, and drops contacts and queued messages; the old and new public keys are reported.

The backend needs the private key to decrypt direct messages addressed to the device. Before provisioning into a Deployment the key may be read over a physically connected USB link only. After provisioning it can be read only by a Deployment-signed request (fleet-owner authority), answered only to the requester's channel. Stock key export/import is disabled on every transport once provisioned, import is never available, and no end user can read the key.

## Direct Reader authentication accounting

The Reader's single unsigned readiness probe on attach is answered but not counted as an authentication rejection. Every other unsigned request on a Deployment-aware device is refused and counted.

## Restricted-posture recovery boundary

Where the bootloader accepts images over USB, a recovery path outside OTA clears the posture record and continuity material while preserving identity, enrolment and the Deployment record. Because such a bootloader accepts unsigned images, physical USB access can always replace the application: restricted posture protects the application's command surface, not against reflashing.

## Repeater signed-writes switch

A per-device setting, off by default. When on, unsigned RF management writes are refused and reads stay available. It changes only by a Deployment-signed request or the physical console, and is cleared when the Deployment record is erased.

## Credentials and secrets

Wi-Fi passwords, MQTT credentials, resilience keys, MeshCore private identity material, Deployment private material and continuity/recovery secrets are write-only and are not returned through ordinary status/configuration/logging surfaces. The two narrow exceptions are the device private key under the custody rules above and the BLE pairing passkey below.

BLE pairing passkeys are random per device, drawn from the hardware RNG and persisted; never derived from public data such as the public key and never a shared or stock default. A device holding a non-secret value (unset, stock default, or key-derived) re-rolls it once on upgrade, and existing bonds survive. The passkey is never in advertisements or periodic status; it is available on the device's own display, as a read-only setting over authenticated management (MQTT is a trusted transport for this purpose) and over physical USB. If no secret passkey can be established, BLE does not start (fail closed). Bonds can be counted and cleared over physical USB only; a clear requested while BLE is not running is persisted and executed at the next BLE start before advertising, then verified, and is immediate where the bond store is reachable without the stack. Hardened has no BLE stack.

## TLS trust

Firmware uses an explicit supported trust policy/bundle/pin identifiers. Unknown trust identifiers fail closed. TLS trust establishes transport security and MUST NOT be treated as a substitute for Deployment command authority.

## Outage/resilience channel security

The private resilience channel is a deliberate confidentiality trade-off. Its shared channel key authenticates channel membership, not individual sender identity and not Deployment administration. Every holder can read/construct channel-authenticated content. Replay protection does not turn attribution hints into cryptographic identity.

## Logging and diagnostics

Logs, crash records, framebuffer export and raw observations MUST NOT contain configured secrets. Diagnostic streams are bounded and must not block RF.

Hardened local/unauthenticated diagnostics are sanitized; authenticated management may receive the detailed operational set.

## OTA

OTA authority permits only an approved authenticated/integrity-checked release compatible with hardware, role and variant. Arbitrary URL/binary execution is not OTA authority.

Hardened may accept OTA through supported authenticated management transports. Normal `Both` may temporarily stop BLE to recover heap during OTA without changing the persisted connectivity mode.

## Backend federation boundary

Backend federation membership does not itself grant device authority. A federated Backend must still hold current Deployment authority before firmware accepts its commands. Firmware does not implement Backend HLC/cursor/snapshot/user-replication mechanics.

## Failure behaviour

Security-sensitive parsing and persistence fail closed. Malformed credentials, invalid signatures/generations, unknown authority/trust identifiers, invalid key lengths, unsupported operations, corrupt posture state and wrong types MUST NOT be coerced into broader/default authority.