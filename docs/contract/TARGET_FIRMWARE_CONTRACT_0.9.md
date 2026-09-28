# MECON Target Firmware Contract v0.9

**Status:** design-complete pre-1.0 target contract. Implementation remains gated on released MeshCore 1.18 and validation of its final public CLI/configuration surface.

This document is the consolidated target contract for the MECON firmware re-founding. It defines the architectural boundary between MeshCore and MECON and the behaviour expected from a conforming target. Detailed wire/profile documents under `docs/contract/` remain normative where referenced; if they conflict with this v0.9 architectural contract, resolve the conflict before implementation rather than creating a second device model.

## 1. Foundation

MECON firmware is an extension of released, pinned upstream MeshCore 1.18. MeshCore owns the RF protocol, native Companion/Repeater roles, native contacts/channels, native configuration semantics and the CLI/configuration implementation wherever upstream provides a suitable operation.

MECON adds management, connectivity, deployment integration, health/observability, resilience and transport adapters. It does not create a parallel RF role or a second authoritative copy of native MeshCore configuration.

A target remains a useful normal MeshCore device when MECON IP services, MQTT and Backends are unavailable.

## 2. One command/configuration plane

### 2.1 Canonical rule

**MeshCore CLI/configuration semantics are the canonical device management semantics.**

USB, BLE, MQTT and MeshCore/LoRa are transports to the same logical command/configuration plane. They MUST NOT define independent setting names, validation rules or side effects for the same operation.

For a native setting or operation exposed by released MeshCore 1.18, MECON MUST reuse or extend that upstream operation. Examples include radio, TX power, node name, location, role-applicable routing/GPS/system settings and other operations present in the released CLI.

MECON-specific settings and operations extend the same plane under a `mecon` namespace rather than creating a second configuration architecture.

### 2.2 CLI is semantic authority, not a raw-shell contract

The MECON public contract is not unrestricted arbitrary text tunnelling. Transports use a versioned operation envelope around an allowlisted CLI/configuration operation. The logical request contains at least:

- `request_id` or state-changing `job_id`;
- target identity when the transport can address more than one node;
- operation/command;
- arguments or payload;
- authority context required by the transport/profile.

The logical result contains at least:

- matching request/job identity;
- `accepted`/`succeeded`/`failed` state as applicable;
- structured result or safe CLI result text;
- stable error class/code where failed;
- optional native diagnostic text.

A transport MAY encode this compactly. LoRa in particular need not carry verbose JSON or literal command strings when a compact representation maps unambiguously to the same canonical operation.

### 2.3 Transport equivalence

Where a target supports an operation, executing it through USB, BLE or MQTT MUST have the same validation, persistence and side effects. A remote RF/LoRa CLI request MUST reach the same command/configuration implementation after authentication and routing.

Transport-specific differences are limited to authentication, framing, MTU/chunking, timeout/retry behaviour, discovery and availability. They do not redefine the operation.

### 2.4 Configuration persistence

Native MeshCore settings use upstream authoritative storage (`NodePrefs`/`ConfigSerializer` or the released 1.18 equivalent). MECON MUST NOT mirror native settings into a second authoritative store.

MECON-specific persistent state may use MECON-owned storage, but it must be surfaced through the common command/configuration plane. Secrets are write-only.

### 2.5 Schema/capability discovery

Clients discover capabilities and applicability rather than infer them from board name or role. A conforming target reports which command/configuration profiles and transports it supports, resource limits and relevant profile versions.

Schema metadata MAY provide a structured view for UI generation, validation and atomic transactions. It is a description/transaction layer over the canonical configuration semantics, not a competing settings system.

## 3. Remote CLI over MeshCore/LoRa

MeshCore RF is a first-class remote-management transport where released 1.18 supports authenticated CLI/command delivery.

The intended path is:

`Backend/Reader -> MQTT/USB/BLE -> local Companion -> MeshCore RF -> target CLI/configuration -> authoritative prefs`

A MECON implementation MUST prefer released MeshCore remote CLI mechanisms over inventing a proprietary RF configuration protocol.

Remote RF management MUST provide:

- explicit target identity;
- authentication/authorization appropriate to the native MeshCore mechanism plus MECON policy where applicable;
- replay protection;
- request/result correlation;
- bounded request/reply sizes and chunking or explicit rejection;
- bounded retries/timeouts suitable for LoRa airtime;
- no unrestricted shell, arbitrary memory/NVS access or arbitrary firmware execution.

MECON may add compact envelopes, correlation and auditing around the native operation without changing its semantics.

## 4. MECON extension namespace

MECON-only behaviour is exposed as extensions to the common command/configuration plane. Expected areas include:

- `mecon.identity` / deployment enrollment metadata;
- `mecon.connectivity` / Wi-Fi profile and path policy where supported;
- `mecon.mqtt` / broker endpoint, credential and discovery policy where supported;
- `mecon.resilience` / private outage-sync configuration and generation;
- `mecon.health` / reporting and diagnostic controls;
- `mecon.ota` / managed update policy/status;
- `mecon.recovery` / capability and USB-only recovery operations.

Exact 1.0 command/path spelling is frozen only after released MeshCore 1.18 is pinned, so MECON extensions do not collide with or duplicate new upstream facilities.

## 5. Identity, version and capabilities

Every target reports:

- stable MECON `device_id`;
- native MeshCore public node identity;
- hardware target;
- native role;
- build variant;
- MECON firmware version;
- exact pinned MeshCore base version/revision;
- target-contract version (`0.9` for this design contract, `1.0` when frozen for release);
- independently versioned supported profiles;
- supported management transports;
- machine-readable material resource limits.

Capabilities, not hardware inference, determine available features.

## 6. Target profiles

### 6.1 Core

All MECON targets provide, where the underlying platform/role makes the information meaningful:

- identity/version/capability discovery;
- common command/configuration dispatch;
- health/status;
- bounded diagnostics/logging;
- native inventory exposure appropriate to the role;
- stable request/job identity and replay/idempotency for MECON state-changing work.

### 6.2 Companion

A MECON Companion remains a standard MeshCore Companion. Contacts, channels and ordinary DM/channel semantics remain upstream-owned. MECON may expose those capabilities through Reader/backend transports and may add resilience/alternate-delivery behaviour without creating a second message database on the device.

### 6.3 Repeater

A MECON Repeater remains a standard MeshCore Repeater. MECON adds management/health/observation connectivity appropriate to capabilities. Remote configuration should use the same CLI semantics locally and over RF.

### 6.4 IP profile

IP-capable targets may provide Wi-Fi and MQTT. MECON 1.0 uses one active MQTT session, preferring an eligible authenticated local broker and falling back to configured cloud endpoints according to policy. IP failure never stops RF or local management.

### 6.5 Direct profile

USB and BLE expose the same logical operation model. Standard MeshCore Companion interoperability is preserved. MECON extensions use an explicit extension/tunnel only where upstream framing cannot express the operation directly.

### 6.6 Non-IP targets

A target without Wi-Fi/MQTT does not emulate them. Reader/backend software may bridge BLE/USB to infrastructure. Capability discovery makes this explicit.

## 7. Configuration transaction semantics

Simple native CLI operations retain their native semantics. MECON may additionally expose a structured transaction operation for UIs/backends that need to change several related fields safely.

A structured apply transaction MUST:

1. validate the complete request before commit;
2. map native fields to their upstream canonical operations/storage;
3. map MECON fields to MECON extensions;
4. report partial application as failure unless an operation is explicitly defined as non-atomic;
5. state whether application is live, reconnect-required or restart-required;
6. preserve/restore a known management path where practical for connectivity-affecting changes;
7. be idempotent under duplicate delivery.

`get_schema` and structured `get_config` remain useful UI/API surfaces, but they describe and orchestrate the common command/configuration plane rather than replace it.

## 8. Health and observations

The target exposes the health/status set defined by `CORE_DEVICE_CONTRACT.md`, including runtime, RF, applicable Wi-Fi/MQTT/BLE, resource headroom and observation publisher counters. `null`, absent and zero retain distinct meanings.

Where authorized and supported, raw MeshCore packet observations include reception provenance, timestamp when known, RSSI/SNR and byte-safe packet data. Observation backpressure must drop/account rather than block RF.

## 9. Messaging and resilience

Native MeshCore messaging remains authoritative. MECON state-changing send jobs have stable IDs and cannot execute twice because of MQTT reconnect, transport failover or duplicate submission.

The private Deployment resilience channel remains an optional MECON extension for outage forwarding, daily bounded health evidence and recovery of selected message delivery. It does not replace ordinary MeshCore messaging and its shared channel key is not Deployment trust-root authority.

## 10. Security and authority

Observation, configuration read, configuration write/management, messaging, resilience/backfill, OTA and recovery are separable authorities.

Transport authentication does not automatically grant every operation. Command dispatch is allowlisted and capability-gated. Secrets are never returned by ordinary configuration/status reads.

Recovery Package export is a distinct privileged USB-only operation. It is never available over BLE, MQTT or RF and is not implemented as a general CLI command reachable through all transports.

## 11. OTA

OTA is a logical management capability with target-specific implementation. An authorized OTA operation references an approved signed/integrity-checked release compatible with target/role/variant. It does not provide arbitrary code execution. USB remains the ultimate recovery path where supported.

## 12. UI and hardware-specific behaviour

MECON preserves native MeshCore UI/button behaviour where practical. Display, LEDs, haptics, GPS, Wi-Fi, BLE and OTA mechanism are capabilities, not universal requirements. Platform-specific implementations remain behind target adapters and MUST NOT leak into the Core contract.

## 13. Upstream boundary and 1.18 freeze gate

This v0.9 contract deliberately describes the target architecture before MeshCore 1.18 is released. Before freezing contract 1.0:

1. pin the released upstream 1.18 revision;
2. inventory the final Companion and CommonCLI/configuration APIs;
3. map every native MECON configuration requirement to an upstream CLI/config operation where one exists;
4. identify only the true MECON extensions;
5. freeze exact extension command/path names and compact remote-envelope encoding;
6. prove identical semantics through USB, BLE, MQTT and applicable LoRa paths;
7. update the detailed protocol documents to match the frozen mappings;
8. test stock MeshCore operation with all MECON infrastructure unavailable.

Do not code against moving `dev` APIs merely to satisfy this contract early.

## 14. Initial 1.0 release targets

The planned first-class 1.0 targets remain:

- Heltec V3 Companion;
- Heltec V3 Repeater;
- Heltec V4 Companion;
- Heltec V4 Repeater.

Non-ESP targets such as SenseCAP T1000-E are capability-driven follow-on/experimental targets and must validate the portability of this contract rather than introduce target-specific product semantics.

## 15. Conformance rule

A target conforms when a client can discover what it supports and invoke a supported logical operation without needing transport-specific product knowledge. For configuration specifically, there must be one authoritative semantic path:

**MeshCore CLI/configuration where upstream owns the feature; MECON CLI/configuration extensions where MECON owns the feature; USB/BLE/MQTT/LoRa only carry those operations.**
