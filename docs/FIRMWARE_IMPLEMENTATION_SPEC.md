# MECON 1.0 firmware implementation specification

This document is the **functional implementation checklist** for rebuilding mecon-firmware from the released MeshCore 1.18 baseline. Together with `docs/contract/`, it must be sufficient to determine what the firmware is expected to do without reading the private MVP source. The private MVP is evidence and a test oracle only.

## Release dependency and foundation

MECON 1.0 is blocked on MeshCore 1.18 being released/merged to upstream `main`. Pin a specific 1.18 revision, prove stock Companion and Repeater on Heltec V3/V4, then migrate those targets to pioarduino / Arduino-ESP32 3.x / ESP-IDF 5.x and NimBLE. Prove stock-derived behaviour again before adding MECON.

MECON must extend 1.18 facilities rather than recreate them: Wi-Fi/runtime configuration, native Companion/Repeater commands, radio settings, board preferences, UI and remote CLI/message primitives remain upstream-owned where they already solve the requirement.

## Supported targets

- Heltec V3 Companion
- Heltec V3 Repeater
- Heltec V4 Companion
- Heltec V4 Repeater

V3 and V4 are first-class targets. Hardware-specific USB, display, buttons, radio and power code belongs behind target abstractions. A target is supported only after the real-hardware gate passes.

## Native MeshCore behaviour

MECON is additive. With Wi-Fi, MQTT and all backends unavailable, the device must still perform its normal MeshCore 1.18 role. Companion remains a normal Companion; Repeater remains a normal Repeater. MECON must not introduce a proprietary RF role.

The Companion contact capacity is a measured resource limit, not a hard-coded product requirement. Determine it after IDF5/NimBLE migration under realistic Wi-Fi+TLS+MQTT+BLE load. Managed/protected contacts must not be evicted by ordinary learned contacts.

## Wi-Fi

- Store up to three ordered Wi-Fi profiles.
- Extend the MeshCore 1.18 Wi-Fi/configuration model rather than maintaining a parallel private implementation.
- Try configured profiles in user-entered priority order, while avoiding needless roaming away from a working connection.
- Automatically reconnect/fail over when the active network disappears.
- A configuration change must not strand the device: preserve a known-working profile until replacement connectivity is proven where practical.
- Wi-Fi loss must not affect RF or local USB/BLE operation.
- Wi-Fi/DNS/discovery operations must be non-blocking with respect to radio servicing.

## MQTT

MECON uses **one MQTT client/session at a time**.

- A provisioned cloud broker profile is the fallback/default remote path.
- The device automatically discovers a compatible local broker through mDNS when on a LAN.
- A trusted/compatible discovered local broker is preferred over cloud.
- If local becomes unavailable, reconnect to cloud; if an eligible local broker later becomes available, selection may return to local according to the connection policy.
- Broker switching must not duplicate state-changing jobs or RF transmissions.
- MQTT uses TLS/authentication and explicit authority grants.
- MQTT failure never stops RF.
- Publish connection state, selected path/profile identity and errors through status/health.

## Common management/event model

MQTT, USB and BLE are transports for one logical MECON capability model. Do not implement separate product semantics per transport.

Expose, where supported by role/transport:

- identity, firmware/base versions, hardware, role, capabilities and profile versions;
- health and runtime status;
- raw packet observations with reception metadata;
- relevant decoded/native Companion events;
- logs/diagnostics;
- contact/channel inventory;
- configuration schema, reads and atomic validated writes;
- messaging operations;
- allowed administrative actions;
- OTA lifecycle and results;
- framebuffer export for automated display verification.

Reuse native MeshCore 1.18 operations whenever they already provide the required semantics. MECON-specific functionality uses the versioned public envelopes in `docs/contract/`.

## USB and BLE

USB is the universal local recovery/configuration path. Companion retains standard MeshCore Companion USB framing and adds the MECON extension/tunnel only for operations not adequately represented upstream. Repeater exposes a documented direct USB management/observation path mapped to the same logical operations.

Companion BLE uses NimBLE, retains standard MeshCore interoperability, starts independently of Wi-Fi/MQTT, and exposes applicable MECON operations. BLE credentials/PIN are per-device, not universal. Repeater BLE is capability-driven; 1.0 does not require it unless the supported target advertises it.

## Device UI

Keep the MeshCore 1.18 Companion/Repeater UI and button semantics as intact as possible.

- Show `MECON <version>` at boot; no build date is required on the display.
- Add compact Wi-Fi, MQTT and BLE connection state to the existing front screen.
- When BLE is available but not connected, show the pairing PIN; when connected, show connected state instead.
- No MECON secondary/status screen in 1.0.
- DMs and messages on favourited channels may wake/show on the display.
- Public/unfavourited channel traffic does not wake the display merely because MECON observed it.
- No new normal MECON button semantics in 1.0.
- Framebuffer export is a test/management capability, not another screen.

## Observations and inventories

Every supported build can expose received MeshCore packet observations when authorized. Preserve raw packet bytes plus receiver metadata such as reception time, RSSI/SNR and receiving device identity. Observation delivery is best-effort while no authorized transport exists; do not promise an unbounded offline packet queue.

Companion exposes contacts/channels and related native state required by a Reader/backend. Repeater exposes its applicable radio/runtime state. Capability discovery, not role-name guessing, determines what a client may request.

## Messaging

Companion preserves native MeshCore messaging and additionally permits authorized MECON messaging jobs. Sending requires explicit authority and idempotent job handling. Contacts/channels may be provisioned on demand where required by the native MeshCore operation.

When the RF mesh is unavailable, authorized DM/channel messages may be submitted through MQTT for delivery through MECON infrastructure. This is an alternate path, not a change to the MeshCore RF protocol.

## Outage resilience

The 1.0 target retains the private MVP's resilience intent:

- when MQTT/IP is unavailable, selected DM traffic can be copied/forwarded to a configured private MeshCore channel;
- loop/duplicate prevention is mandatory when connectivity returns;
- the device can publish an autonomous daily health/status report to that configured private channel;
- delivery/outage synchronization must preserve messages that can be recovered after a temporary backend/device disconnect, within explicitly bounded storage and protocol rules.

The exact wire semantics are normative in the outage/messaging contract; no implementation may invent backend-specific behaviour outside the public contract.

## Configuration

One logical settings API applies across MQTT, USB and BLE. It covers at least:

- native MeshCore name/radio/TX/location settings exposed by 1.18;
- channels and applicable contact/channel provisioning;
- three Wi-Fi profiles and ordering;
- cloud MQTT endpoint/credentials/namespace/authority;
- local-broker discovery policy and remembered profile/authority identifiers;
- private outage/status channel;
- logging/health/reporting settings;
- OTA policy/trust metadata where configurable.

Secrets are write-only. Apply validates the complete requested transaction before persistence. Each field declares type, constraints, role applicability and runtime effect. Requested configuration is not reported as successful until it is actually accepted/applied.

## Health, status and logs

Expose enough state to operate a remote device without serial access: uptime/restart reason, free memory/resource pressure, role/radio state, Wi-Fi state/profile/RSSI where available, MQTT state/path/error, BLE state, packet counters, firmware/base/contract versions, OTA state and relevant failure counters. Logs are bounded and must not block RF processing.

## OTA

Managed OTA is a supported 1.0 capability. MQTT may trigger OTA, but authority permits only an approved release represented by signed/integrity-checked metadata compatible with hardware, role and variant. Report accepted/progress/result states. Preserve a documented recovery/rollback path; USB remains ultimate recovery.

## Security and authority

Separate observation, config read, messaging, management/admin and OTA authority. A broker connection alone does not grant all operations. State-changing jobs use stable IDs, expiry where applicable and replay/idempotency protection. Secrets are never returned in ordinary status/config reads. No management transport exposes arbitrary firmware execution, unrestricted shell or raw NVS merely because it is authenticated.

## Runtime/resource requirements

- pioarduino / Arduino-ESP32 3.x / ESP-IDF 5.x.
- NimBLE.
- One active MQTT client.
- Non-blocking DNS/mDNS/network/OTA work relative to RF servicing.
- Measure V3 and V4 memory budgets after runtime migration before fixing contact capacity and buffer sizes.
- Test realistic persisted contacts/channels/configuration and simultaneous Wi-Fi+TLS+MQTT+BLE on Companion.
- Avoid copying private-MVP static allocations where the new runtime permits a better design.

## Release and compatibility

Report MECON firmware version, exact MeshCore base, hardware target, role/build variant and independent contract/profile versions. Clients gate behaviour on capabilities/profile versions, not firmware-number inference. All supported board/role targets pass real-hardware gates including RF role, USB, applicable BLE, all Wi-Fi slots, local/cloud MQTT switching, observations, config, messaging, reconnect, memory headroom and OTA.

## Backend compatibility

MECON 1.0 intentionally changes parts of the private MVP wire contract. The firmware should implement the clean public contract rather than preserve private names/architectural debt internally. **All backend work required to move the current MeshContinuum backend from the private MVP to MECON 1.0 is tracked in `BACKEND_MIGRATION.md`.** Compatibility adapters may exist during rollout, but they are migration code and not part of the canonical firmware architecture.
