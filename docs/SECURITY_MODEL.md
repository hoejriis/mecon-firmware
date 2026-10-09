# Security model

> **Target documentation:** this public repository has no installable release yet. See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

## Principles

- MeshCore RF cryptography remains MeshCore's responsibility.
- **Transport access, broker reachability and device authority are separate concepts.**
- A MECON **Deployment** is the administrative trust domain.
- Device authority is rooted in a Deployment Authority Key (DAK); Backend instance authority is explicitly granted under that Deployment authority contract.
- Observation, configuration, messaging, management/admin, resilience/backfill and OTA are distinct capabilities and may be authorized separately.
- Secrets are write-only through ordinary management APIs.
- No management transport grants an unrestricted shell, arbitrary execution surface or raw secret-store access merely because it is authenticated.
- Security posture controls local exposure; it does not redefine who has Deployment authority.

The canonical Backend-side Deployment authority contract is maintained by MeshContinuum (`DEPLOYMENT_AUTHORITY_CONTRACT.md`, originating from deimos-mesh #632). Firmware implementations must consume that contract rather than inventing transport-specific trust rules.

## Normal and Hardened posture

### Normal

Normal preserves ordinary MeshCore interoperability and MECON convenience access. Stock USB/WebSerial and applicable BLE remain available according to the Normal connectivity mode. A Normal device may hold protected Deployment continuity material.

### Hardened

Hardened is a managed-infrastructure posture for devices where physical access/proximity must not imply trust.

Hardened requirements:

- BLE is not initialized at boot.
- Wi-Fi-capable targets expose infrastructure/outbound Wi-Fi/MQTT only, not stock-app or local Web management surfaces.
- Stock MeshCore USB/WebSerial administration is unavailable.
- Unauthenticated USB reveals only minimal non-sensitive identity/status and that an authenticated MECON Reader is required.
- Authenticated same-Deployment Reader USB, MQTT and secure RF management remain available where the target supports them.
- A Hardened Companion does not display received message content/identity and cannot send user messages from its local UI.
- No unauthenticated physical UI operation changes posture, management configuration or enrolled state.
- Connectivity failure never enables a weaker fallback such as BLE.
- Authenticated Deployment administration retains applicable telemetry, diagnostics and configuration capabilities; Hardened protects local/untrusted exposure rather than hiding operational state from the administrator.

Changing posture requires authenticated management and a controlled reboot.

A full external flash erase/reflash by a person with physical possession is outside the enrolled firmware trust model.

## Fail-secure posture recovery

Persist posture with integrity protection appropriate to the platform. If it is missing/corrupt and cannot be validated, enter a distinct fail-secure recovery state rather than silently selecting Normal or Hardened.

That state exposes no BLE, no stock inbound Wi-Fi/app management and no stock USB administration. Minimal identification/sanitized diagnostics and still-valid authenticated MECON management paths may remain available. It must be distinguishable from deliberately configured Hardened posture.

## Deployment authority

A device persists the Deployment identity/trust material required by the frozen Deployment authority contract. Backend instance keys are trusted only when authorized by the Deployment authority chain; MQTT broker discovery, DNS/mDNS results, network location or successful TLS establishment do not themselves grant device authority.

Revocation/generation rules are monotonic as defined by the Deployment authority contract. State-changing commands require current authority plus stable job identity/replay protection.

Normal/Hardened posture is orthogonal to Deployment membership: **Normal → Hardened does not unenroll the device or remove its ability to recognize authorized Backend/Reader management.**

## Protected continuity storage boundary

Firmware provides a bounded protected storage boundary for Deployment continuity/recovery material.

At the posture layer the contents are **opaque**: package schema, package generation, sync-key generation and Join/Recovery orchestration belong to the P3 Deployment continuity contract, not the posture implementation.

Security invariants:

- Normal may hold the protected continuity blob.
- Hardened has no usable continuity/recovery authority.
- Normal → Hardened cryptographically destroys local ability to use/decrypt the blob; physical flash-sector overwrite is not required if destruction of wrapping/decryption material makes stale bytes unusable.
- Hardened must not export continuity material over USB, MQTT, RF, status or diagnostics.
- Hardened → Normal leaves continuity material unprovisioned until an authorized provisioning/recovery flow supplies new material.

This continuity material is distinct from the Deployment membership/authority state required for a Hardened device to remain administrable.

## MQTT and broker trust

MECON uses one active MQTT session. A locally discovered broker may be preferred over cloud, but discovery is reachability only. The selected endpoint must satisfy the configured transport security requirements, while commands received through it must independently satisfy Deployment authority.

Remote state-changing jobs use stable identifiers and replay/idempotency protection across endpoint changes and direct transports.

## USB, Reader and RF management

Normal stock USB access remains available. Hardened stock USB does not.

A deployed MECON Reader may administer a Hardened device over USB only after proving current authority from the same Deployment. Do not create a separate per-device Reader ACL merely for USB; the Deployment authority model is the trust source.

Secure RF management is an authorized management transport, especially for repeaters without Wi-Fi. It follows the same logical management/configuration contract as MQTT/Reader rather than defining an independent security model.

## Secrets

Wi-Fi passwords, MQTT credentials/tokens, MeshCore private identity material, Deployment private authority material and continuity/recovery secrets are never returned in clear text by ordinary status/configuration reads or logs.

## OTA

Managed OTA installs only approved firmware represented by authenticated/integrity-checked release metadata compatible with hardware, role and variant.

Hardened posture permits OTA over authenticated management paths supported by the target; it does not require USB-only upgrades. Normal `Both` mode may temporarily stop BLE during OTA to recover heap, without changing the persisted Normal connectivity choice.

USB/full reflash remains the ultimate physical recovery path.

## Backend federation boundary

Backend federation and device authority are complementary but distinct. Backends may replicate Deployment state between trusted Backend instances, but federation membership alone does not grant device authority: an instance still requires authorization under the Deployment authority contract.

Firmware does not implement Backend-to-Backend state federation and does not need to understand federation HLCs, cursors, snapshots or replicated user/session semantics.

## Backend independence

No MeshContinuum-specific hostname, account or credential is compiled into generic public firmware.