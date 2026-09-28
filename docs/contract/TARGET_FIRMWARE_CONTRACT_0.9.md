# MECON Target Firmware Contract v0.9

**Status:** architecture-frozen v0.9 target contract, updated 2026-09-28 to incorporate the settled P2 posture/connectivity model and the P3 Deployment-authority boundary. Exact command spelling/encoding remains subject to the pinned MeshCore 1.18 surface.

This document is the consolidated target contract for the MECON firmware re-founding. Detailed wire/profile documents under `docs/contract/` remain normative where referenced. Cross-repository Deployment authority/continuity contracts owned by MeshContinuum are normative for the device-facing fields they define.

## 1. Foundation

MECON firmware extends pinned upstream MeshCore 1.18. MeshCore owns RF protocol, native Companion/Repeater roles, contacts/channels and native configuration/CLI semantics wherever upstream provides a suitable operation.

MECON adds Deployment integration, management, connectivity, posture, health/observability, resilience and transport adapters. It does not create a parallel RF role or a second authoritative copy of native configuration.

## 2. One command/configuration plane

**MeshCore CLI/configuration semantics are the canonical device-management semantics.**

USB, BLE, MQTT and secure MeshCore RF are transports to the same logical command/configuration plane. They MUST NOT define independent setting names, validation rules or side effects for the same operation.

Native operations reuse/extend upstream commands. MECON-specific settings extend the same plane under a MECON namespace. Transport-specific differences are authentication, framing, MTU/chunking, retry/timeout, discovery and posture availability — not product semantics.

No transport exposes unrestricted shell, arbitrary NVS/memory access or arbitrary firmware execution.

## 3. Deployment authority

Transport reachability and device authority are separate.

Firmware consumes the frozen MeshContinuum Deployment authority contract produced by deimos-mesh #632 / firmware #66. A device stores the Deployment identity/trust material required by that contract. Backend instance keys are trusted only when authorized by the Deployment trust root and current authority generation/revocation state.

Broker discovery, DNS/mDNS, network location, TLS establishment or possession of MQTT credentials do **not** independently grant authority to issue device-management commands.

Deployment membership is orthogonal to security posture: a Hardened device remains enrolled and can recognize authorized same-Deployment management.

## 4. Security posture

Every conforming implementation that supports managed posture provides persistent **Normal** and **Hardened** states plus a non-user-selectable fail-secure recovery state for corrupt/missing posture persistence.

### 4.1 Normal

Normal preserves stock MeshCore interoperability. Applicable stock USB/WebSerial, BLE and user-facing Companion messaging/UI remain available according to target capability and Normal connectivity mode.

### 4.2 Hardened

Hardened is a managed-infrastructure posture for locations where physical access/proximity must not imply trust.

A Hardened target MUST:

- not initialize BLE at all;
- on Wi-Fi-capable hardware expose only infrastructure/outbound Wi-Fi services needed for MQTT/DNS/NTP/OTA;
- not expose stock-app TCP/HTTP/Web management;
- not expose stock MeshCore USB/WebSerial administration;
- retain authenticated MECON management over supported MQTT, secure RF and same-Deployment Reader USB;
- never enable BLE or another convenience-management interface merely because infrastructure is unavailable;
- provide no unauthenticated physical state-changing escape hatch.

A Hardened Companion MUST NOT display received message content/sender/channel identity or allow locally initiated user messaging. It MAY display sanitized operational state: device/role/posture, power, uptime, RF activity, Wi-Fi/MQTT state, GPS/fix state, aggregate counters and non-sensitive diagnostics.

Authenticated Deployment management MAY expose the same applicable detailed telemetry, diagnostics and contacts/channels administration as Normal.

### 4.3 Posture transition

Posture changes require authenticated MECON management, persist, and apply through a controlled reboot.

Normal → Hardened preserves ordinary operational state but destroys usable protected continuity/recovery authority.

Hardened → Normal restores Normal surfaces but does not automatically recreate continuity material. A Wi-Fi-capable target returns to the Normal default `MQTT` connectivity mode.

### 4.4 Fail-secure recovery

If persisted posture cannot be validated, firmware MUST NOT silently choose Normal. Enter a distinct recovery state with no BLE, no stock inbound Wi-Fi/app management and no stock USB administration. Minimal identity/sanitized diagnostics and still-valid authenticated MECON recovery paths may remain available.

## 5. Normal connectivity mode

The connectivity-mode selector is a **Normal-posture setting only**.

Wi-Fi-capable Normal targets support:

- `MQTT` — **default**; initialize Wi-Fi/MQTT, not BLE.
- `BLE` — contingency/direct access; initialize BLE, not Wi-Fi/mDNS/MQTT.
- `Both` — explicit high-memory mode; initialize both where hardware resources permit.

Entering Hardened discards the prior Normal mode. Hardened has no hidden operational BLE/Both choice. Returning to Normal starts at `MQTT` on Wi-Fi-capable hardware.

Non-Wi-Fi targets expose only capabilities their hardware supports.

## 6. MQTT and broker selection

MECON uses **one active MQTT client/session at a time**.

A provisioned remote/cloud endpoint is the fallback path. A compatible local broker may be discovered through mDNS and preferred when reachable. Local discovery is reachability only; the selected path still requires transport security and commands still require Deployment authority.

Failover/recovery MUST NOT require two simultaneous full MQTT/TLS sessions on constrained targets. Broker switching MUST NOT duplicate state-changing jobs or RF transmissions. MQTT failure never stops RF.

## 7. Direct and RF management

### Normal USB/BLE

Normal Companion preserves applicable stock USB/WebSerial and BLE interoperability. MECON extensions use the common operation plane.

### Hardened USB

Unauthenticated USB exposes only minimal non-sensitive identity/version/posture guidance. Detailed status, CLI/configuration, contacts/channels, messages, logs and secrets are unavailable.

A deployed Reader may administer a Hardened device after proving current authority from the same Deployment. No separate per-device Reader ACL is required merely because transport is USB.

### Secure RF

Secure MeshCore RF management is a first-class MECON transport where supported, especially for non-Wi-Fi repeaters. It reaches the same logical CLI/configuration implementation after authentication/routing and MUST provide target identity, replay protection, request/result correlation and bounded airtime behaviour.

## 8. Protected continuity-storage boundary

Firmware provides a bounded protected storage interface for Deployment continuity/recovery material.

At this contract layer the contents are **opaque**. Package schema, package generation, sync-key generation, Join/Recovery orchestration and Backend continuity semantics belong to the P3 Deployment continuity contract.

Required invariants:

- Normal may hold one protected continuity blob plus minimal local metadata needed to manage it safely.
- The blob need not remain resident in runtime heap.
- Hardened has no usable continuity/recovery authority.
- Normal → Hardened cryptographically destroys local wrapping/decryption material; stale flash bytes may remain but MUST be unusable.
- Hardened MUST NOT export/read the blob over USB, BLE, MQTT, RF, status or diagnostics.
- Hardened → Normal leaves the slot empty/unprovisioned until an authorized future flow supplies new material.

This continuity material is distinct from Deployment membership/authority state, which remains in Hardened so the device can authenticate management.

## 9. Identity, version and capabilities

Every target reports applicable:

- stable MECON device identity;
- native MeshCore public node identity;
- hardware target and native role;
- build variant;
- security posture;
- Normal connectivity mode when applicable;
- MECON firmware version and exact MeshCore base;
- target-contract/profile versions;
- supported management transports/capabilities;
- material resource limits.

Capabilities, not board-name inference, determine available features.

## 10. Configuration semantics

Native settings use upstream authoritative storage. MECON MUST NOT mirror native settings into a second authoritative store.

MECON-owned persistent state includes only true extensions such as Deployment authority state, connectivity policy, posture, MQTT/Wi-Fi profiles, resilience configuration and protected continuity storage.

A structured apply transaction MUST validate before commit, map native fields to upstream operations/storage, map MECON fields to extensions, declare restart/reconnect effects, preserve a known management path where practical, and be idempotent under duplicate delivery.

Secrets are write-only.

## 11. Health and diagnostics

The target exposes the health/status set defined by `CORE_DEVICE_CONTRACT.md`, including runtime, RF, applicable network state, heap/resource headroom, reset/crash evidence, observation counters and target-specific power/GPS information.

Authenticated management gets applicable detailed telemetry in either posture. Hardened local/unauthenticated surfaces expose only the sanitized subset.

`null`, absent and zero retain distinct meanings. Diagnostic backpressure drops/accounts rather than blocking RF.

## 12. Messaging and resilience

Native MeshCore messaging remains authoritative in Normal Companion operation. MECON send jobs use stable IDs and cannot execute twice because of broker reconnect/failover or duplicate submission.

Hardened Companion is not a local messaging terminal. Its infrastructure/RF processing may continue as required by its managed role, but local display/send functionality is suppressed.

The existing private outage/resilience channel remains a separate managed capability until superseded by the P3 continuity design. Its shared channel key is not Deployment trust-root authority and MUST NOT be conflated with the opaque protected continuity slot.

## 13. OTA

OTA is an authenticated management capability. An authorized operation references an approved authenticated/integrity-checked release compatible with target/role/variant.

Hardened may receive OTA over authenticated management transports supported by the target; Hardened is not USB-only.

Normal `Both` may temporarily stop BLE to free heap during OTA without changing the persisted connectivity selection. MQTT mode does not initialize BLE.

External full flash erase/reflash remains the ultimate physical recovery path.

## 14. Runtime/resource rules

- one active MQTT session;
- unused connectivity stacks are not initialized;
- Hardened never initializes BLE;
- network/discovery/OTA work must not block RF servicing;
- constrained targets record free heap, minimum-ever free heap and largest contiguous block for relevant modes;
- V3 heap pressure is an architectural constraint;
- broker liveness probing must not require a second simultaneous full MQTT/TLS session.

## 15. Backend federation boundary

Firmware does not implement Backend-to-Backend federation event logs, HLC conflict resolution, cursors, snapshots, raw-history replication or user/session replication.

Federation matters to firmware only through device-visible contracts: Deployment authority, broker/rendezvous information, continuity/recovery material and authenticated management commands. A federated Backend still requires current Deployment authority before the device trusts it.

## 16. Initial target families

Initial first-class targets remain Heltec V3/V4 Companion and Repeater. Non-ESP/Seeed/SenseCAP-class targets validate portability through capabilities rather than introducing target-specific product semantics.

## 17. Conformance rule

A target conforms when a client can discover its capabilities/posture and invoke a supported logical operation without transport-specific product knowledge.

The authoritative semantic path is:

**MeshCore CLI/configuration where upstream owns the feature; MECON extensions where MECON owns the feature; MQTT/USB/BLE/secure-RF only carry authorized operations; posture determines which transports/surfaces may exist.**
