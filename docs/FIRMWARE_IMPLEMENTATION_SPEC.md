# MECON 1.0 firmware implementation specification

This document is the **functional implementation checklist** for rebuilding mecon-firmware from the released MeshCore 1.18 baseline. Together with `docs/contract/`, it must be sufficient to determine what the firmware is expected to do without reading the private MVP source. The private MVP is evidence and a test oracle only.

The five historical private firmware contracts have been audited field-by-field/function-by-function at product-contract level in [`LEGACY_CONTRACT_AUDIT.md`](LEGACY_CONTRACT_AUDIT.md). Any implementation ambiguity should be resolved from the public contracts plus that classification, not by silently copying private source.

## Settled architecture baseline — 2026-09-28

The following decisions supersede older wording in migration/audit material and are normative for the target firmware:

1. **Deployment authority is transport-independent.** Firmware consumes the frozen MeshContinuum Deployment authority contract from deimos-mesh #632 / firmware #66. Broker discovery, network location and TLS transport do not grant device authority.
2. **Normal and Hardened are persistent security postures.** Normal preserves stock interoperability. Hardened is a managed-infrastructure appliance posture where physical access/proximity does not imply administrative trust.
3. **Normal connectivity mode is `MQTT | BLE | Both`.** MQTT is the default on Wi-Fi-capable MECON devices; BLE is contingency/direct access; Both is explicit and resource-expensive.
4. **Hardened has no connectivity-mode selector and no BLE stack.** Wi-Fi-capable Hardened devices use infrastructure/outbound Wi-Fi/MQTT; non-Wi-Fi devices remain manageable through secure RF and authenticated Reader USB.
5. **MQTT uses one active session.** Local and remote broker endpoints are alternative rendezvous paths; local discovery proves reachability only. Deployment authority decides which commands are trusted.
6. **MQTT, authenticated Reader USB, BLE where permitted, and secure RF are transports for one logical management/configuration model.** MECON should extend the MeshCore CLI/remote-command model rather than grow independent transport-specific semantics.
7. **Firmware provides an opaque protected continuity-storage boundary.** The posture layer does not define the P3 continuity package schema. Normal → Hardened destroys usable continuity authority while preserving Deployment membership/authority needed to administer the Hardened device.
8. **Backend federation is not a firmware protocol.** Firmware needs the device authority contract and later continuity/broker contracts, not Backend HLC/cursor/snapshot/user-replication mechanics.

## Release dependency and foundation

MECON 1.0 is based on MeshCore 1.18. Pin a specific compatible upstream revision, prove stock Companion and Repeater behaviour on supported hardware, then layer MECON functionality through upstream extension points rather than recreating native capabilities.

MECON extends upstream facilities for Wi-Fi/runtime configuration, native Companion/Repeater commands, radio settings, board preferences, UI and remote CLI/message primitives wherever they already solve the requirement.

## Supported targets

Initial first-class targets are:

- Heltec V3 Companion
- Heltec V3 Repeater
- Heltec V4 Companion
- Heltec V4 Repeater

The portability architecture also anticipates non-Wi-Fi targets such as Seeed/SenseCAP-class hardware. Hardware-specific USB, display, buttons, radio, GNSS, networking and power code belongs behind target/capability abstractions. A target is supported only after its real-hardware gate passes.

## Native MeshCore behaviour

MECON remains an extension of MeshCore rather than a proprietary RF role. When MECON infrastructure is unavailable, the device continues the RF behaviour appropriate to its role **subject to its configured security posture**. Normal Companion retains stock user-facing interoperability; Hardened Companion deliberately suppresses local user messaging/UI exposure while continuing its managed RF/network role.

Capability discovery, not hard-coded board/role assumptions, determines available operations.

## Security posture

### Normal

Normal preserves ordinary stock MeshCore interoperability. Stock USB/WebSerial remains available. BLE is available according to the Normal connectivity mode. Normal Companion may display/send messages according to upstream behaviour. Normal may hold protected Deployment continuity material.

### Hardened

Hardened is intended for infrastructure devices placed where physical access does not equal trust.

At boot:

- do not initialize BLE at all;
- on Wi-Fi targets, initialize only infrastructure/outbound Wi-Fi services required for MQTT/DNS/NTP/OTA;
- do not expose stock-app TCP/HTTP/Web management;
- do not expose stock MeshCore USB/WebSerial administration;
- retain authenticated MECON management over supported MQTT, secure RF and same-Deployment Reader USB paths.

A Hardened Companion does not display received message content or sender/channel identity and cannot initiate user messages locally. Its display may show sanitized operational information: identity/role/posture, battery/power, uptime, RF activity, Wi-Fi/MQTT state, GPS/fix state, aggregate traffic counts and non-sensitive reset/error diagnostics.

There is no unauthenticated physical factory-reset/posture escape hatch. External full flash erase/reflash remains outside the enrolled firmware trust model.

### Posture transition

Posture is persistent and changes only through authenticated MECON management. A change is applied through a controlled reboot.

Normal → Hardened preserves ordinary operational state such as contacts, channels, favourites and Wi-Fi/MQTT configuration, but destroys usable protected continuity/recovery authority.

Hardened → Normal restores Normal functionality but does not automatically recreate continuity material. On Wi-Fi-capable targets it starts with the Normal connectivity default `MQTT`.

If persisted posture cannot be validated, enter a distinct fail-secure recovery state: no BLE, no stock inbound Wi-Fi/app management, no stock USB administration, minimal identification/diagnostics, and still-valid authenticated MECON recovery paths where possible.

## Normal connectivity modes

The `MQTT | BLE | Both` selector is **Normal-only**.

### MQTT — default

Wi-Fi/MQTT are initialized; BLE is not. This is the default on Wi-Fi-capable MECON devices and the preferred steady-state mode on constrained hardware.

### BLE — contingency

BLE is initialized; Wi-Fi/mDNS/MQTT are not. This provides direct access without paying the IP/TLS/MQTT heap cost.

### Both — explicit

BLE plus Wi-Fi/MQTT are initialized where target resources permit. This is the highest-memory mode and must be explicitly selected.

Entering Hardened discards the previous Normal connectivity selection. Returning to Normal starts in MQTT on Wi-Fi-capable devices.

## Wi-Fi

- Store up to three ordered Wi-Fi profiles.
- Extend the MeshCore Wi-Fi/configuration model rather than maintaining a parallel implementation.
- Try configured profiles in user-entered priority order while avoiding needless roaming away from a working connection.
- Automatically reconnect/fail over when the active network disappears.
- Preserve a known-working profile until replacement connectivity is proven where practical.
- Report actual active slot, associated SSID and nullable RSSI separately from configured values.
- Wi-Fi/DNS/discovery operations must not block RF servicing.

## MQTT and broker selection

MECON uses **one MQTT client/session at a time**.

- A provisioned cloud/remote broker profile is the fallback path.
- A compatible local broker may be discovered through mDNS and preferred while reachable.
- Discovery proves reachability only; it grants no authority.
- Broker/TLS credentials establish transport security, not Deployment command authority.
- Local failure returns the connection manager to an eligible remote endpoint; local availability may later be reprobed without requiring a second full MQTT/TLS session.
- Broker switching must not duplicate state-changing jobs or RF transmissions.
- MQTT failure never stops RF.
- Publish selected path/profile, connection state and relevant errors through status/health.

## Deployment authority

Firmware consumes the frozen Deployment authority contract defined by MeshContinuum #632 and implemented through firmware #66.

A device stores the Deployment identity/trust state required by that contract. Backend instance authority is explicitly granted under the Deployment trust root and follows monotonic generation/revocation rules. A command arriving through a reachable broker is accepted only when its authority proof is current and valid for the requested operation.

Normal/Hardened posture is orthogonal to Deployment membership. Hardened devices remain enrolled and capable of authenticating authorized Backend/Reader management.

## Common management and CLI model

MECON has **one logical capability/configuration model**. MQTT, USB, BLE and secure RF expose applicable subsets of the same operations according to posture, role, capability and authority.

The long-term contract is an extension of the MeshCore CLI/remote-command model introduced upstream: native MeshCore commands remain native; MECON adds versioned commands/fields rather than creating four independent configuration APIs.

Expose where supported and authorized:

- identity, firmware/base versions, hardware, role, posture and capabilities;
- health/runtime status and sanitized/detailed diagnostics according to access level;
- raw packet observations with reception metadata;
- applicable native Companion events;
- contact/channel inventory;
- configuration schema, reads and atomic validated writes;
- messaging operations where posture permits them;
- administrative actions;
- resilience/backfill lifecycle;
- OTA lifecycle/results;
- framebuffer/display diagnostics where supported.

## USB, BLE and secure RF

### Normal USB

Companion preserves standard MeshCore USB/WebSerial interoperability and adds MECON extension operations only where upstream does not represent the capability. Repeater direct USB management is capability-driven.

### Hardened USB

Unauthenticated USB exposes only minimal identity/version/posture guidance. Stock CLI/configuration, contacts/channels, messages, detailed logs and sensitive state are unavailable.

A deployed Reader may obtain the MECON management surface after proving current authority from the same Deployment. Do not create a separate per-device Reader ACL merely for USB.

### BLE

Normal BLE uses NimBLE and device-specific pairing credentials. Hardened never initializes BLE. Loss of infrastructure never enables BLE automatically.

### Secure RF management

Secure management/configuration over MeshCore RF is supported where the target permits it and follows the same logical MECON management contract. It is the essential remote management path for non-Wi-Fi repeaters. RF management must authenticate authority and must not become an unauthenticated radio CLI.

## Protected continuity storage

Firmware provides a bounded protected storage interface for future Deployment continuity/recovery material.

At this layer the blob is opaque. Do not encode P3 package fields such as package generation or sync-key generation into the posture abstraction.

- Normal may hold the protected blob.
- Hardened has no usable continuity/recovery authority.
- Normal → Hardened cryptographically destroys the local wrapping/decryption material so stale flash bytes are unusable; physical overwrite is not required.
- Hardened does not export the blob through any transport/status/logging surface.
- Hardened → Normal leaves the slot empty until an authorized P3 provisioning/recovery flow provides new material.

Deployment membership/authority state is separate and remains available in Hardened.

## Device UI

Keep upstream UI/button semantics where they do not conflict with posture.

Normal may show ordinary Companion messages and exposes its Normal connectivity-mode control. On Wi-Fi-capable Normal devices MQTT is the default, BLE is contingency, and Both is explicit.

Hardened has no connectivity-mode selector. It may show operational status and sanitized diagnostics but not message content/identities or security-changing controls.

## Observations and inventories

Supported builds may expose received MeshCore packet observations when authorized. Preserve raw packet bytes plus reception time, RSSI, SNR and receiving device identity. Multiple receivers hearing the same packet produce separate observations.

Companion exposes applicable contacts/channels/native state to authorized management. Repeater exposes applicable radio/runtime state. Capability discovery determines what a client may request.

## Messaging and outage resilience

Normal Companion preserves native MeshCore messaging and may additionally accept authorized MECON messaging jobs. Stable job/message identity prevents duplicate RF transmissions/conversations when infrastructure and RF paths overlap.

Hardened Companion is not a local messaging terminal: it neither displays message content nor initiates user messages locally. Infrastructure/RF processing needed for its managed role may continue under authenticated policy.

Existing private-channel outage/resilience behaviour remains a separate managed capability until superseded by the P3 Deployment continuity design. Its channel/key/generation semantics must not be confused with the opaque posture continuity-storage slot.

## Configuration

One logical settings/CLI contract applies across applicable transports. It covers at least:

- native MeshCore name/radio/TX/location settings exposed upstream;
- contact/channel visibility and applicable provisioning operations;
- Wi-Fi profiles/order;
- MQTT endpoint/credentials and broker-selection policy;
- Deployment authority identifiers/state;
- Normal connectivity mode;
- posture, through authenticated management only;
- resilience/outage settings where retained;
- logging/health/reporting settings;
- OTA policy/trust metadata where configurable.

Secrets are write-only. Validate complete requested transactions before persistence. Each field declares type, constraints, role/posture applicability and runtime effect.

## Health, status and logs

Expose enough state to diagnose a remote device without serial access. Detailed authenticated management telemetry is available in both Normal and Hardened where the target can measure it, including:

- uptime/reset reason/abnormal reset and bounded crash history;
- current/minimum heap and relevant stack/headroom measurements;
- battery/power and temperature where measurable;
- Wi-Fi active slot/SSID/RSSI;
- MQTT selected path/state/counters;
- Normal connectivity mode or Hardened posture state;
- BLE state in Normal, and evidence that BLE is not initialized in Hardened;
- RF packet/error/activity counters and RX age;
- observation-publisher counters;
- contact count/capacity/protected/evicted where applicable;
- GPS/fix data where hardware provides it;
- OTA state/errors.

Local/unauthenticated Hardened diagnostics are a sanitized subset. Logs are bounded and must not block RF or leak secrets.

## OTA

Managed OTA is supported. Authority permits only an approved release represented by authenticated/integrity-checked metadata compatible with hardware, role and variant. Report accepted/progress/result states and confirm post-reboot firmware version.

Hardened may accept OTA through authenticated management paths supported by the target; Hardened does not imply USB-only OTA.

Normal `Both` mode may temporarily stop BLE before OTA to recover heap, without changing the persisted connectivity selection. If BLE was active because of `Both`, it returns according to the normal post-update/reboot mode. MQTT mode does not load BLE.

## Runtime/resource requirements

- One active MQTT client/session.
- Do not initialize unused connectivity stacks.
- Hardened must not initialize BLE.
- Non-blocking DNS/mDNS/network/OTA work relative to RF servicing.
- Measure free heap, minimum-ever free heap and largest contiguous block on constrained targets for Normal MQTT/BLE/Both and Hardened boots.
- Treat V3 heap pressure as an architectural constraint, not merely a tuning problem.
- Do not require a simultaneous second MQTT/TLS session merely to probe broker failover.

## Backend federation boundary

Backend federation is owned by MeshContinuum. Firmware does **not** implement federation event logs, HLC conflict resolution, peer cursors, snapshots, replicated users/sessions or raw-history synchronization.

Firmware is affected only where federation produces contracts visible to devices: Deployment authority, broker/rendezvous information, continuity/recovery material and authenticated management commands.

A federated Backend still requires valid Deployment authority before a device trusts its command.

## Release and compatibility

Report MECON firmware version, exact MeshCore base, hardware target, role/build variant and independent capability/contract versions. Clients gate behaviour on capabilities/profile versions, not firmware-number inference.

All supported targets require real-hardware validation for their applicable RF role, USB, BLE/Normal modes, Wi-Fi, broker switching, observations, configuration, authority enforcement, posture, secure RF management, health diagnostics, reconnect behaviour, memory headroom and OTA.

## Backend compatibility

MECON intentionally replaces private-MVP transport-specific assumptions with a clean public contract. Compatibility adapters may exist during rollout but are migration code, not canonical firmware architecture. Backend work required for migration remains tracked in `BACKEND_MIGRATION.md`; when that document conflicts with the settled architecture above, this specification and the frozen cross-repository contracts take precedence.