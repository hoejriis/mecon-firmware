# MeshContinuum backend migration to MECON 1.0

## Next P3 integration boundary — agreed 24 October 2026

The [approved firmware programme](CLEAN_REFOUNDING_1_18.md#approved-next-p3-programme--24-october-2026) progresses platform consolidation, one common device interface, enrollment/trust and full telemetry before release timing is decided. A standalone public release is not a dependency.

The Reader currently **depends on a Backend** and bridges USB/BLE devices to it. Backend ingestion must normalize MQTT and bridged USB/BLE into the same capability/versioned device and telemetry model, preserving device provenance, authorization, event identity and correlation across path changes. Offline Reader design is outside this scope.

Telemetry includes raw observations, runtime/resources, radio/connectivity, inventories, battery/power, environment/location, logs/events and screen/framebuffer data. Screen write-back is device → Backend diagnostic/logging telemetry, not remote display control. Apply access rules to sensitive screen content and bounded collection/retention to all streams. Firmware is specified and conformance-tested first; legacy adapters belong at the Backend boundary.

Later references in this target document to direct operation without infrastructure describe native/device capabilities or future designs, not an implemented offline Reader.

This document is the **single place for backend-side changes required because the public MECON 1.0 firmware differs from the deployed/private MVP**. The firmware and public contract documents describe the desired end state; they must not be polluted with legacy behaviour merely to make migration easier.

"Legacy" in this document means the wire the private 0.9x reference firmware speaks today (its shipped behaviour is summarised in [Current-to-target adjustments](CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line)). The public 0.8.x line and that private line are separate lines: version numbers do not order across them.

The current backend must continue to support deployed private-MVP firmware during rollout. Implement compatibility at the backend boundary and remove it only when the old fleet is retired.

## Migration rule

For each difference below, the backend should support **old and new simultaneously** where practical:

1. detect the device's protocol/profile versions and capabilities;
2. parse/serve the appropriate legacy or MECON v1 representation;
3. normalize both into one backend domain model;
4. emit the new MECON v1 representation to v1 devices;
5. retain legacy output only for legacy devices;
6. remove the adapter after the old fleet is retired.

Do not make the new firmware emit private legacy names merely to avoid backend work.

*Sequencing:* the public firmware speaks the v1 vocabulary below only, and does not emit the legacy names (`gateway_id`, `deimos/gw/…`). The firmware's functionality comes first; a Backend adopts the v1 interfaces afterwards. Until then, the public firmware is verified against the conformance suite and simulated boards, not against a legacy Backend.

## Identity and naming

### Legacy

The private MVP uses/deployed variants of:

- `gateway_id`;
- `deimos_firmware_version`;
- `deimos_mqtt_protocol_version`;
- `deimos.*` settings;
- historical Deimos/default topic namespaces;
- role/product labels such as Companion Gateway and Repeater Observer.

### MECON 1.0

Use backend-neutral MECON vocabulary:

- stable `device_id` plus native `node_public_key_hex`;
- `mecon_firmware_version`;
- independent MECON protocol/profile versions;
- `mecon.*` settings;
- provisioned namespace rather than a private service compiled default;
- native roles `companion` / `repeater`, with observation expressed as a capability.

### Backend change

Accept legacy fields/topics while deployed, normalize them to the new device model, and emit canonical MECON v1 names for v1 devices. Enrollment/database record IDs remain backend-local and must not be confused with firmware identity.

## MQTT connection model

### Legacy

Earlier private builds held several broker profiles and, in some versions, simultaneous broker sessions with independent authority. Since reference firmware 0.51.0 the device opens one session at a time, in the order local, primary, secondary broker. Up to four broker profiles are still stored, each with its own capability grants (`cap_receive_admin_config`, `cap_receive_backfill`, `cap_publish_events`).

### MECON 1.0

The device has **one active MQTT session**. It automatically prefers an eligible local broker discovered by mDNS and otherwise connects to the configured cloud broker.

### Backend change

- Stop assuming a device maintains simultaneous cloud+LAN sessions.
- Treat local and cloud connectivity as alternate paths to the same device identity.
- Preserve job IDs/idempotency across path changes.
- Do not re-enqueue a state-changing command merely because the device reappears through another broker.
- Provision/remember the authority/profile identity needed for a locally discovered broker without requiring a second concurrent session.
- Reader/admin UX should show the currently reported path (`local`/`cloud`) rather than two independently online broker connections.

## MQTT topic/envelope model

### Legacy

Historical topics include variants such as `raw`, `rx`, `contacts`, `channels`, `result`, `config_schema`, `config_result`, `backfill_result`, `sync_status`, `cmd`, `get_config`, `apply_config`, `sync_config` and `backfill`, with legacy gateway/enrollment fields.

### MECON 1.0

Use the canonical surfaces and typed envelopes in `contract/MQTT_PROTOCOL.md`: status, observations, events, inventory, results, config, commands and OTA. Envelopes carry protocol/profile versioning, stable message/job identity and backend-neutral device identity.

### Backend change

Implement a protocol adapter mapping every deployed legacy topic/payload into the canonical backend model. For v1 devices subscribe/publish only the v1 topic set. Ensure the adapter preserves raw packet bytes, timestamps, RSSI/SNR, contact/channel identity, job/result correlation and delivery state.

## Settings/configuration

### Legacy

Companion, Repeater and MQTT configuration paths have historically used different framing/commands and legacy `deimos.*` names.

### MECON 1.0

One logical settings contract is carried over MQTT, USB and BLE. MECON extends native MeshCore 1.18 configuration where possible. Three ordered Wi-Fi profiles, cloud MQTT configuration, local discovery policy, outage/private-channel settings and applicable native radio/channel settings are represented through the common schema.

### Backend change

- Generate UI/forms from the v1 schema/capabilities where practical rather than firmware-version-specific hard coding.
- Translate legacy settings to the canonical backend model during mixed-fleet operation.
- Stop depending on Repeater-only text command semantics when talking to v1 devices.
- Treat secrets as write-only and never expect them back from a v1 config read.
- Honor declared live/reconnect/restart effects and explicit job results.

## Wi-Fi

### Legacy

The private firmware implements its own multi-profile Wi-Fi machinery.

### MECON 1.0

Three ordered profiles remain a product requirement, but the implementation extends MeshCore 1.18's Wi-Fi/runtime configuration.

### Backend change

The user-visible three-profile behaviour remains. Backend code must rely on the public schema/capabilities rather than legacy implementation-specific fields. Preserve ordering and safe replacement semantics.

## BLE/direct Reader

### Legacy

Direct support evolved incrementally; some operations use native Companion commands, some use MECON/private tunnels. In the 0.9x firmware the passkey is random per device, re-rolled once on upgrade from a non-secret value, settable by a Deployment-signed `ble_passkey` command or by USB command 37, and BLE startup follows documented timing rules; older builds differ.

### MECON 1.0

Companion uses NimBLE, per-device PIN, standard MeshCore Companion interoperability plus a versioned MECON extension for operations not represented upstream. BLE starts independently of Wi-Fi/MQTT. USB/BLE/MQTT share logical MECON operations.

### Backend/Reader change

- Negotiate capabilities/profile versions before choosing native versus MECON operations.
- Support the v1 tunnel/framing documented in `contract/DIRECT_READER_PROTOCOL.md`.
- Do not assume a universal BLE PIN.
- Do not require Wi-Fi/MQTT to be online for direct Reader operation.
- Normalize direct and MQTT results/events into the same backend/Reader domain objects.

## Contacts and resource limits

### Legacy

Earlier private builds used a fixed/reduced contact-table target (including 64 and temporary lower values during memory mitigation). The 0.9x firmware enforces 50 contacts on Heltec V3 and V4 and 350 on the T1000-E, and reports the runtime capacity.

### MECON 1.0

Contact capacity is measured after IDF5/NimBLE migration and reported as a capability/resource limit. Managed/protected contacts must survive normal eviction.

### Backend change

Remove assumptions that every MECON Companion has exactly 32/64/etc. contacts. Read the reported capacity. Enrollment/provisioning UI must respect the device limit and understand protected/managed contact semantics.

## Device UI

The backend has no direct migration requirement for the minimal display changes, but automated hardware/UI tests should expect MECON version at boot, Wi-Fi/MQTT/BLE status on the existing front screen, BLE PIN while disconnected, no secondary MECON screen, and DM/favourite-only wake behaviour.

## Observations

### Legacy

Raw packet reception is exposed through historical `raw`/`rx` paths and role-specific mechanisms.

### MECON 1.0

The logical object is an `observation`: raw MeshCore bytes plus receiver/time/RSSI/SNR/provenance metadata, regardless of transport or role.

### Backend change

Normalize legacy raw/rx and v1 observations into the same observation store. Do not infer a separate Observer RF role. Preserve multiple receptions of the same canonical packet as separate observations/evidence where the backend model supports that distinction.

## What the Backend already speaks to the 0.9x firmware

Shipped inputs the current Backend must keep handling until the legacy fleet is retired: the Deployment-signed wrapper on every management message (naming `gateway_id`, or the target node public key for a repeater; whether the Backend sends the latter form is not established); the write-once USB-auth secret provisioned over USB so a Reader can unlock a restricted device; signed uplink (`usig_boot`, `usig_seq`, `usig`) latched per device; `ota_ack_signed` and the `ota/relay/*` topics; and the first bare `deployment_provision` that must precede every other write on a device with no Deployment record. A proposed `device_operations_version` is emitted by no build.

## Messaging, inventories and provisioning

MECON 1.0 reuses native MeshCore 1.18 operations where they already solve Companion messaging/contact/channel behaviour and uses MECON typed operations only for additional semantics.

Backend/Reader code must capability-negotiate whether an operation is native or MECON rather than keying off old product labels. Existing message/contact/channel history should remain one domain model regardless of transport.

## Outage sync and private-channel resilience

The private MVP's outage/backfill behaviour remains a required product capability, but its public contract must be backend-neutral.

Backend changes:

- map legacy `backfill`, `backfill_result`, `sync_status` and related payloads into the v1 outage/delivery model;
- recognize DM copies forwarded through the configured private MeshCore channel and prevent loops/double presentation when the IP copy later arrives;
- ingest the device's periodic private-channel status report (in the 0.9x firmware a signed status beacon, group-data type 0x4402) as health evidence without presenting it as an ordinary user message unless the product explicitly chooses to;
- preserve stable message/delivery identity across RF and MQTT paths.

## MQTT as alternate message path

A v1 device may receive authorized DM/channel work through MQTT when the RF path is unavailable. The backend must treat this as another delivery path for the same logical message, not create duplicate conversations/messages. Delivery state must distinguish accepted-by-device from confirmed RF/remote delivery where those are separately knowable.

## Health/status/logging

Backend parsers and UI must move from legacy health/status field assumptions to capability/versioned v1 payloads. Preserve at least firmware/base versions, role/hardware, uptime/restart reason, memory/resource pressure, Wi-Fi, selected MQTT path/state, BLE state, radio state, packet counters, OTA state and relevant errors. Logs are a separate bounded diagnostic stream/event surface, not arbitrary remote shell access.

## OTA

The private firmware already ships signed-manifest OTA: Ed25519-signed manifests, revert of an unconfirmed update, a signed `ota/ack` (capability `ota_ack_signed`), relay of a full image over one LoRa hop to ESP32 gateways (`ota/relay/*`) and a staged-only LoRa image on the Wio repeater. MECON 1.0 makes managed OTA part of the public contract.

Backend changes:

- publish/select only release metadata compatible with reported hardware/role/variant;
- send OTA through the v1 authorized job surface;
- track accepted/progress/succeeded/failed lifecycle;
- do not expose arbitrary URL flashing as normal remote administration;
- retain legacy OTA handling only for legacy devices during migration;
- sign `ota/ack` for devices that advertise `ota_ack_signed`.

## MeshCore 1.18 capability changes

MECON 1.0 is built on released MeshCore 1.18. Backend/Reader code must not recreate functionality that v1 firmware exposes through native 1.18 operations. During implementation, audit each private backend firmware command against 1.18 and classify it as:

1. native 1.18 operation — use native operation;
2. MECON v1 extension — use public MECON contract;
3. backend-only workflow — remove from firmware contract.

This audit is a release blocker because it prevents the public firmware from reimplementing obsolete 1.17/private mechanisms solely for backend compatibility.

## Rollout acceptance

Before replacing the private MVP fleet, demonstrate against the current MeshContinuum backend/Reader:

- enrollment/provisioning of all four V3/V4 role targets;
- mixed legacy + v1 fleet operation;
- three Wi-Fi profiles;
- local MQTT discovery and cloud fallback without duplicate jobs;
- observations and inventories;
- config read/write and reconnect-safe changes;
- Companion messaging and provisioning;
- direct USB/BLE Reader operation;
- outage/private-channel behaviour and daily status;
- OTA lifecycle;
- backend handling of dynamic contact/resource capacity;
- legacy adapters can be identified and later removed independently of the v1 implementation.

## Removal criterion

Legacy compatibility code may be removed only after no supported deployed device requires the private-MVP protocol. Removal should delete adapters, legacy topic subscriptions and legacy field aliases without changing the canonical MECON v1 contract.
