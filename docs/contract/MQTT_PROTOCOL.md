# MQTT protocol

> **Target documentation:** this public repository has no installable release yet. The first public firmware will be mecon-firmware 0.8.0 (see [Versioning and releases](../VERSIONING_AND_RELEASES.md)). This page describes the target for the public line, not the private 0.9x reference firmware, whose shipped behaviour differs in places (see [Current-to-target adjustments](../CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line)). See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

This document defines the **target** MECON 1.0 network contract for the public line. It is normative for MECON-specific network behaviour of that line. The private 0.9x reference firmware speaks the legacy wire described in [Reference firmware today](#reference-firmware-today-legacy-wire) below. Released MeshCore 1.18 operations are reused where suitable rather than reimplemented as private MQTT-only device logic.

## Connection model

MECON 1.0 uses **one active MQTT client/session**. The device has a provisioned cloud endpoint. When an eligible compatible broker is discovered on the current LAN, that local endpoint is preferred; otherwise the device uses cloud. Discovery is non-blocking and proves reachability only: it grants no authority and cannot replace provisioned credentials/trust.

Local and cloud are alternate paths to the **same logical authority and device identity**. Job/replay state survives path changes. The private MVP's simultaneous multi-broker model is retired.

The active connection uses authenticated encrypted transport. Supported transport/TLS details are reported/configured capabilities rather than assumptions about one private broker deployment. Firmware owns its TLS trust bundle/pinning policy; a remotely supplied certificate must not silently become a new trust anchor.

## Namespace

The provisioned namespace contains one stable device subtree:

```text
{namespace}/{device_id}/status
{namespace}/{device_id}/inventory
{namespace}/{device_id}/observation
{namespace}/{device_id}/event
{namespace}/{device_id}/result
{namespace}/{device_id}/config/get
{namespace}/{device_id}/config/schema
{namespace}/{device_id}/config/apply
{namespace}/{device_id}/resilience/config
{namespace}/{device_id}/resilience/status
{namespace}/{device_id}/delivery/backfill
{namespace}/{device_id}/command
{namespace}/{device_id}/ota
```

Implementations may split typed inventory/events/results into subtopics, but the release manifest/contract profile must freeze the exact v1 topic set before shipping. The target v1 firmware does not emit the historical `deimos/gw/...` namespace; mixed-fleet support belongs in the backend adapter. (The 0.9x reference firmware does emit it today.)

## Common envelope

Every MECON network message carries enough information to authenticate/authorize it, identify the device and protocol profile, correlate it, and reason about time/replay. The canonical logical envelope contains:

- `device_id` — stable MECON device identifier;
- applicable MECON protocol/profile version(s);
- `ts` — Unix timestamp when known, otherwise explicit unknown/null semantics where the message permits it;
- stable `message_id` for events/messages or `job_id` for requested work;
- authority/authentication material as defined by the security profile.

Device identity is distinct from the native `node_public_key_hex` and from any backend database/enrollment row ID. Status reports both `device_id` and native node identity.

Unknown additive fields are ignored. Unknown operations or unsupported profile versions are rejected explicitly when a job can be identified.

## Delivery semantics

Consequential publishes use acknowledged MQTT delivery (QoS 1 in the reference implementation). Snapshot state such as current status/inventory/resilience enrollment may be retained; event streams and commands are not treated as durable state merely because MQTT can retain a payload.

MQTT acknowledgement means the broker accepted a publication. It does **not** prove that a backend consumed it or that an RF recipient received a message.

State-changing jobs are idempotent. Duplicate delivery through reconnect, local/cloud switching or another supported transport must not repeat a completed RF send, settings transaction or OTA start. Replay state is logical-device/job state, not broker-session state.

## Status

`status` is a complete current snapshot, published periodically and additionally after significant state changes subject to bounded coalescing. It includes the core identity/profile fields and the health contract, including:

- MECON and MeshCore versions, hardware, role and capabilities;
- uptime/reset/crash information;
- Wi-Fi association, active profile/SSID and RSSI;
- MQTT connected state and selected path (`local`/`cloud`);
- BLE state where supported;
- RF health/counters and counter epoch;
- current/minimum heap and loop-stack headroom;
- battery voltage where measurable;
- contact count/capacity and protected/eviction counters where applicable;
- observation publisher counters;
- resilience/outage-sync configuration state;
- OTA state when an update is active/recent.

Unknown measurements are `null`, not zero. Unsupported/older fields may be absent. Consumers must preserve the distinction between absent, null and numeric zero.

## Inventory

Inventory exposes the device's current native MeshCore contacts and channels where the role supports them. MECON 1.0 should source these from MeshCore 1.18 state/operations rather than maintain a second database.

Contact inventory and status expose actual runtime capacity. No backend may assume a fixed limit (the 0.9x reference firmware enforces 50 on Heltec V3 and V4 and 350 on the T1000-E; earlier private builds used 32/64/100). Protected/managed contacts, where supported, are not candidates for ordinary eviction.

Large inventory may be chunked; the exact v1 chunk envelope must carry snapshot identity/version so a backend cannot combine chunks from different snapshots.

## Observations

An `observation` represents one physical reception, not merely one canonical packet. It preserves at minimum:

- raw MeshCore packet bytes (`raw_hex` or equivalent byte-safe representation);
- RSSI;
- SNR;
- reception timestamp (`null` when the device has no valid clock);
- receiving `device_id`/native node provenance;
- any safely available parse/radio metadata.

Multiple devices hearing the same packet produce multiple observations. A backend may deduplicate packet identity for higher-level views but must not destroy reception evidence merely because raw packet bytes match.

The device does not provide an unbounded offline observation queue. When no authorized MQTT target exists, observations may be dropped and the health counters must reveal this (`tried`, accepted/refused, no-target, dropped or their v1 equivalents).

## Events and receive stream

User-visible receive events such as DMs/channel messages may be exposed where they add semantics not already delivered through a native MeshCore 1.18 interface. MECON must not create a second conflicting message history. Stable logical message identity is used to correlate the same content arriving through RF, MQTT alternate delivery or outage forwarding.

## Commands and messaging

Use native MeshCore 1.18 operations for ordinary Companion/Repeater commands when they satisfy the requirement. MECON `command` jobs cover additional remotely authorized semantics. Required outcomes include:

- send DM;
- send channel message;
- provision/resolve required contact or channel state where supported and safe;
- applicable administration/reboot/recovery actions;
- diagnostics explicitly allowlisted by capability.

Each job has an explicit result. `accepted` means accepted by the device; it must not be presented as proof of end-recipient delivery unless the underlying MeshCore operation provides such proof.

## Configuration

Configuration uses the common schema/transaction model in `DEVICE_SETTINGS.md`. Network surfaces provide get/schema/apply and explicit results. Configuration profile versioning is independent from the general MQTT profile so it can evolve without redefining observations.

## Resilience / outage sync

A configured Companion may forward received DMs through a configured private MeshCore channel when MQTT/backend connectivity is unavailable. Required semantics inherited from the MVP are:

- explicit opt-in/configuration and persisted config version;
- configured channel key and bounded member roster;
- authenticated/versioned group-data envelope carrying stable original-message identity, timestamp and correlation hints;
- replay/deduplication window;
- bounded forwarding rate (initial safety target: no more than one forwarding event per 30 seconds unless hardware validation changes and documents it);
- overlength authenticated-envelope messages are dropped rather than silently truncated;
- a receiver validates the envelope and injects it at most once;
- attribution hints are not cryptographic proof of the original sender;
- every member of the private channel can read forwarded content, and the UI/backend must make that privacy consequence clear.

The legacy envelope used `PAYLOAD_TYPE_GRP_DATA`, data type `0x4401`, a version byte, 8-byte origin hash, kind, original timestamp/source/destination hints, forwarder hint, sequence, UTF-8 text and an 8-byte truncated HMAC derived from the channel key. MECON 1.0 retains these semantics. The reference firmware's layout is frozen (a 19-byte header and an 8-byte MAC); for the public line it is validated against released MeshCore 1.18 and then becomes part of this v1 contract. The data types `0x4401` and `0x4402` (the status beacon) are not registered upstream.

### Stock-readable copy

An optional capability may send a second ordinary channel-text representation after the authenticated envelope so stock MeshCore clients can read it. It is **off by default**. It must never be accepted as an injectable authenticated DM, must not recursively forward, and enabling it requires an operator warning that it exposes content to every channel member and roughly doubles RF airtime for each forwarded event. Overlength stock copies are bounded/truncated safely on UTF-8 boundaries and marked rather than producing malformed text.

### Daily private-channel status

MECON 1.0 additionally sends a bounded periodic health/status report to the configured private resilience channel. It is operational evidence, not a user chat message. The final compact payload/format is part of the resilience profile and must fit normal MeshCore limits without fragmentation. The reference firmware's report is a signed status beacon (group-data type `0x4402`, schema 4, Ed25519-signed with the node key) sent at a random time every 20 to 23 hours, plus one at boot if the last automatic one is 12 hours old.

## MQTT as alternate message path

When the RF mesh path is unavailable, authorized infrastructure may deliver DM/channel work to a MECON device through MQTT. This is another path for the same logical message, not a second conversation. Stable message identity and replay protection prevent duplicate display/transmission when paths recover or overlap.

## Backfill

The backend may submit bounded missed-delivery/backfill work. Backfill is versioned, authorized and idempotent; each batch/job receives an explicit result. The device must not accept an unbounded backlog that can starve normal MeshCore operation. Historical `backfill`/`backfill_result` topic names are legacy adapters, not canonical v1 names.

## OTA

OTA is an authorized job surface. A request identifies an approved release/manifest, not arbitrary code execution. Firmware verifies compatibility with hardware/role/variant plus manifest integrity/signature before installation. Results expose accepted/progress/succeeded/failed states sufficient for a backend to distinguish download failure, validation failure and post-update version confirmation. (The reference firmware's state names differ; see below.)

## Logging/diagnostics

Logs are bounded diagnostic events/streams, not a remote shell. MQTT may expose allowlisted logs and counters. Raw packet logging is observational and must obey backpressure/drop accounting rather than block the RF loop.

## Compatibility

The target MECON 1.0 firmware emits canonical MECON vocabulary only. The backend migration layer accepts deployed legacy `deimos.*`, `deimos/gw/...`, `gateway_id`, historical topic names and historical broker/profile shapes. See `../BACKEND_MIGRATION.md` and `../LEGACY_CONTRACT_AUDIT.md`.

## Reference firmware today (legacy wire)

*Added 2026-10-15; informative, not normative for the public line.* The private 0.9x reference firmware speaks the following today.

- **Transport.** MQTT over WSS on port 443, one session at a time (order local, primary, secondary).
- **Topics**, under `deimos/gw/{gateway_id}/`. Device to backend: `status` (retained), `contacts`, `channels`, `rx`, `raw`, `result`, `config_schema`, `config_result`, `backfill_result`, `sync_status`, plus result topics for `authority_update` and `ota`. Backend to device: `cmd`, `get_config`, `apply_config`, `sync_config`, `backfill`, `authority_update`, `beacon` and `ota/offer|chunk|ack|req|result`, with `ota/relay/offer|chunk|ack|cmd|req|result` for the LoRa hop. A retained last will publishes `{gateway_id, online: false}`.
- **Envelope.** `gateway_id`, `enrollment_token`, `mecon_mqtt_protocol_version` (alias `deimos_mqtt_protocol_version`) and `ts`; since 0.87.0 also `usig_boot`, `usig_seq` and `usig`, an Ed25519 signature over the message (domain `MECON-UPLINK-v1`) that the Backend latches per gateway. `mecon_*` fields are emitted with their `deimos_*` aliases.
- **Signed downlink.** Every `cmd`, `apply_config`, `get_config`, `backfill`, `sync_config`, `ota/offer` and `ota/relay/*` inner message arrives inside a Deployment-signed wrapper. Replay is per issuer in four independent job-id sequences (`cmd`, `settings`, `backfill`, `ota`) with a 24-wide window. A device with no Deployment record accepts only reads, the first bare provisioning and bare contact protection.
- **Result states.** `result`: `accepted`, `sent`, `confirmed`, `failed`, `ok`. OTA: `accepted`, `downloading`, `verifying`, `installing`, `rebooting`, `pending_validation`, `success`, `failed`, `refused`, `reverted`, `dfu_entering`.
- **`cmd` verbs** include `send_dm`, `relay_dm`, `send_channel`, `provision_channel`, `provision_contacts`, `protect_contacts`, `advert`, `reboot`, `set_clock`, `send_status_beacon`, `log_subscribe`, `log_unsubscribe`, `get_oled_framebuffer`, `remote_login`, `remote_cli`, `remote_req`, `remote_telemetry`, `repeater_cli`, `deployment_provision`, `deployment_reset`, `continuity_store|clear|status|read`, `ble_bonds`, `ble_passkey`, `identity_rotate`, `identity_export`, `radio_temp`, `lora_ota_selftest`, `pw` and `apply_settings` (broker profiles). Which are allowed in a restricted posture varies by verb; a client reads `capabilities`.
- **Capacity.** The Companion gateways enforce 50 contacts on V3 and V4.

