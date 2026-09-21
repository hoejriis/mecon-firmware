# Private MVP → MECON 1.0 contract audit

This is the completeness audit required before the public firmware can be considered rebuildable without consulting the private `deimos-mesh-firmware` repository.

The source set audited is the five firmware-owned private contracts:

- `MQTT_GATEWAY_PROTOCOL.md`
- `DEVICE_SETTINGS_PROTOCOL.md`
- `DEVICE_HEALTH.md`
- `OUTAGE_SYNC_PROTOCOL.md`
- `PROVISIONING_SERIAL_PROTOCOL.md`

Every material private surface is classified as one of:

- **KEEP / MECON v1** — required public behaviour; specify in `docs/contract/`.
- **NATIVE 1.18** — use the released MeshCore 1.18 primitive and do not invent a parallel MECON implementation.
- **CHANGE** — product behaviour remains but MECON 1.0 deliberately changes the wire/runtime model; `BACKEND_MIGRATION.md` must describe the backend change.
- **RETIRE** — private implementation detail or obsolete behaviour not carried into MECON 1.0.
- **VERIFY 1.18** — final mapping cannot be frozen until released MeshCore 1.18 is on `main`; the product requirement is nevertheless documented now.

## MQTT gateway protocol

| Private surface | MECON 1.0 disposition |
|---|---|
| stable gateway identity + node public key | **CHANGE:** canonical `device_id` plus native node key; backend enrollment ID remains backend-local |
| `deimos/gw/{gateway_id}` topic root | **CHANGE:** provisioned backend-neutral namespace and canonical device subtree |
| envelope token, protocol version, timestamp | **KEEP:** canonical MECON envelope; secrets/authority are provisioned and validated |
| `status` retained snapshot | **KEEP** |
| `contacts` / chunked contact inventory | **KEEP/NATIVE 1.18:** inventory remains required; source it from native Companion state |
| `channels` inventory | **KEEP/NATIVE 1.18** |
| `raw` packet observations | **KEEP:** canonical observation object |
| `rx` parsed receive stream | **KEEP where semantically useful:** canonical event/message receive surface; do not duplicate native Companion events |
| `result` command result | **KEEP:** typed job result with stable job identity |
| `config_schema`, `config_result`, `get_config`, `apply_config` | **KEEP**, canonical configuration profile |
| `backfill`, `backfill_result` | **CHANGE:** preserve delivery/backfill requirement in canonical delivery/resilience profile; exact legacy topics retire |
| `sync_config`, `sync_status` | **KEEP/CHANGE:** outage-sync product behaviour remains; canonical names/envelopes |
| `cmd` | **KEEP/NATIVE 1.18:** native 1.18 operations where adequate; MECON typed jobs only for extra semantics |
| QoS 1 for consequential messages | **KEEP** |
| retained last-known status/inventory/sync state | **KEEP where state is a snapshot** |
| multiple simultaneous broker profiles/sessions | **RETIRE:** one active session, local preferred, cloud fallback |
| per-broker independent publish/cmd capability model | **CHANGE:** one logical authority across alternate paths; path discovery grants no authority |
| shared replay guard / at-most-once state-changing jobs | **KEEP**, extended across local/cloud/direct transports |
| result returns only on ingress broker | **RETIRE as broker-specific rule:** result belongs to logical job; route over available authorized path |
| broker CA selection/bundle | **KEEP conceptually:** TLS trust is firmware-owned/provisioned policy; exact root bundle belongs to release implementation, not backend authority |
| WSS-only historical transport | **CHANGE:** secure MQTT transport is required; exact supported MQTT transport(s) are capability/configuration-defined rather than hard-coding a private broker deployment |
| application-layer enrollment token | **KEEP conceptually:** public security contract requires device-specific authority credential; exact v1 envelope field is canonical MECON vocabulary |
| `deimos_*` aliases | **RETIRE from new device output; backend legacy adapter only** |
| raw observations dropped rather than unbounded offline queue | **KEEP** |
| command replay/idempotency | **KEEP** |
| on-demand contact/channel provisioning used for sends | **NATIVE 1.18 / VERIFY 1.18:** retain outcome, map to native operation where possible |

## Device settings protocol

| Private surface | MECON 1.0 disposition |
|---|---|
| schema-driven generic settings | **KEEP** |
| atomic `apply_config` transaction | **KEEP** |
| device is authoritative for persisted device settings | **KEEP** |
| read and write capabilities independent | **KEEP** |
| role-scoped schema | **KEEP** |
| dotted setting paths | **KEEP for MECON settings; native settings follow 1.18 canonical model** |
| node name / radio frequency / BW / SF / CR / location | **NATIVE 1.18 / VERIFY 1.18** |
| channel visibility | **NATIVE 1.18; avoid second authoritative channel database** |
| 3 Wi-Fi slots | **KEEP:** ordered profiles 0–2 |
| Wi-Fi passwords write-only | **KEEP** |
| MQTT URI/user/password | **KEEP**, adjusted to local-first single-session architecture |
| broker profile arrays / concurrent broker capabilities | **RETIRE/CHANGE** |
| `mecon.companion_tcp.enabled` | **VERIFY 1.18/product need:** do not automatically port an unauthenticated TCP listener; only expose if retained deliberately and document security consequences |
| `deimos.*` path alias | **RETIRE from public contract; backend/legacy tooling adapter only** |
| schema metadata: type, writable, secret, constraints, effect | **KEEP** |
| `live`, reconnect/restart effects | **KEEP** |
| `success` means applied and in force | **KEEP** |
| atomic rollback on failed/disruptive connectivity change | **KEEP** |
| secrets never returned | **KEEP** |
| one canonical storage with multiple transports/writers | **KEEP** |
| settings replay/idempotency | **KEEP** |
| config protocol independently versioned | **KEEP as configuration profile version** |

## Device health/status

All of these remain required unless the target hardware cannot measure them; unavailable measurements use explicit unknown/null semantics rather than false zeroes.

| Private surface | MECON 1.0 disposition |
|---|---|
| uptime | **KEEP** |
| reset reason/code + abnormal flag | **KEEP** |
| battery mV | **KEEP where measurable** |
| free heap + minimum free heap | **KEEP** |
| loop stack size + minimum free | **KEEP** |
| crash record from previous panic | **KEEP** |
| RF rx/tx flood/direct counters | **KEEP/NATIVE 1.18 source** |
| raw RX / RX error counters | **KEEP** |
| RF counter epoch | **KEEP** |
| RX age | **KEEP** |
| software RX armed + chip mode | **KEEP where target exposes them** |
| RX liveness rearm counter/guard | **KEEP outcome; revalidate need/implementation on 1.18 + IDF5** |
| raw observation tried/accepted/refused/no-target/dropped | **KEEP**, adapted to one active MQTT session |
| event-status coalescing + heartbeat | **KEEP** |
| status-events-suppressed counter | **KEEP** |
| Wi-Fi RSSI nullable | **KEEP** |
| active Wi-Fi slot + actual SSID | **KEEP** |
| contact count + runtime capacity | **KEEP** |
| protected contact count + eviction count | **KEEP for Companion if eviction remains** |
| BLE state | **KEEP**, NimBLE implementation and new startup semantics |
| per-broker counters | **CHANGE:** active MQTT/path counters, not concurrent broker array |
| null != zero; absent != null | **KEEP as normative consumer rule** |
| boot/counter epochs | **KEEP** |

## Outage sync

| Private surface | MECON 1.0 disposition |
|---|---|
| optional configured private channel | **KEEP** |
| monotonic config version + acknowledgement | **KEEP, but v1 should return explicit job success/failure as well** |
| channel key + managed member roster | **KEEP** |
| bounded roster/capacity | **KEEP; capability reports limit** |
| DM forwarding during MQTT/backend outage | **KEEP** |
| MeshCore `GRP_DATA` envelope | **KEEP unless 1.18 provides a superior native primitive; wire format must be frozen before release** |
| envelope version, origin hash, kind, original timestamp/src/dest hints, forwarder hint, seq, text, MAC | **KEEP requirement; exact binary v1 format specified in MQTT/resilience contract before implementation** |
| channel-key-derived HMAC and replay window | **KEEP** |
| 30-second forward rate limit / bounded airtime | **KEEP as initial safety limit unless hardware tests justify a documented replacement** |
| overlength DM drop rather than silent truncation | **KEEP** |
| receiver injects authenticated envelope once | **KEEP** |
| attribution hints are not authenticated identity | **KEEP security rule** |
| channel members can read forwarded DM | **KEEP explicit privacy warning** |
| stock-readable duplicate copy | **KEEP as optional capability**; default off; operator warning required |
| stock-copy `[DM xx > yy]` representation | **KEEP for compatibility unless 1.18 constraints require revision** |
| stock-copy airtime roughly doubles | **KEEP warning/test requirement** |
| outage-sync configured/current status in normal status | **KEEP** |
| daily private-channel health report | **ADD:** planned MECON 1.0 behaviour not fully represented by old outage-sync contract; public resilience contract must specify it |
| MQTT alternate DM/channel path when RF mesh unavailable | **ADD:** planned MECON 1.0 behaviour; stable message identity/dedup required |

## Provisioning and direct USB/BLE

The old first-boot line-JSON provisioning console is explicitly superseded even in the private firmware. MECON 1.0 must not resurrect it.

| Private/current MVP surface | MECON 1.0 disposition |
|---|---|
| first-boot 5-minute JSON serial console | **RETIRE** |
| `set_wifi`/`set_mqtt`/`set_label` JSON commands | **RETIRE wire format; outcomes remain in common settings/direct management** |
| Companion `CMD_SET_MECON_GW_VAR` | **CHANGE:** replace private key:value command with versioned MECON direct extension where native 1.18 cannot do the operation |
| Repeater `mecon_set` / `mecon_status` | **CHANGE:** preserve recovery/manageability, expose common logical operations over Repeater USB rather than a divergent product model |
| Repeater `mecon_rawlog` | **KEEP capability:** bounded local raw observation stream for diagnostics; public direct contract must specify framing/backpressure/drop counter |
| standard Companion USB framing | **NATIVE 1.18** |
| unsolicited Companion push frames interleaved with replies | **NATIVE 1.18 client rule; direct MECON tooling must tolerate it** |
| USB recovery after MQTT failure | **KEEP** |
| reset/reprovision capability | **KEEP**, but define public reset scopes and identity/credential effects |
| BLE Companion interoperability | **KEEP/NATIVE 1.18 semantics, implemented with NimBLE** |
| device-specific BLE PIN | **KEEP** |
| PIN shown on front display while unconnected | **KEEP** |
| BLE starts independently of successful MQTT | **KEEP** |
| framebuffer export | **KEEP diagnostic/test capability** |

## UI and runtime requirements learned outside the five wire contracts

These are required for rebuild completeness even though they are not primarily backend contracts:

- preserve stock MeshCore 1.18 Companion/Repeater screen and button behaviour;
- show MECON firmware version at boot;
- add Wi-Fi, MQTT and BLE status to existing front screens;
- show BLE PIN while BLE is available and unconnected;
- no secondary MECON screen in 1.0;
- wake/show display only for DMs and favourited-channel messages;
- V3 and V4 Companion/Repeater are first-class release targets;
- pioarduino / Arduino-ESP32 3.x / IDF 5.x baseline;
- NimBLE from the initial public fork;
- non-blocking Wi-Fi/discovery/MQTT/OTA work;
- three ordered Wi-Fi profiles;
- one active MQTT session, local broker preferred and cloud fallback;
- framebuffer export for automated display verification;
- managed signed OTA;
- daily private-channel status;
- MQTT/RF alternate-path messaging and deduplication.

## Release-blocking items that remain dependent on MeshCore 1.18

The audit is complete at the product-contract level. These mappings intentionally remain marked **VERIFY 1.18** until 1.18 is released on upstream `main`:

1. exact native settings/CLI primitives for node/radio/location/channel management;
2. exact native Companion messaging/contact/channel operations to reuse;
3. whether any 1.18 native transport supersedes parts of the private Companion TCP feature;
4. final UI patch points on the released 1.18 screens;
5. final sensor/capability exposure inherited from 1.18;
6. final memory/resource baseline before fixing contact/roster capacities.

Those are implementation mappings, not undocumented requirements. A coding agent can now identify what behaviour must exist without consulting the private repository; after 1.18 lands, the remaining work is deciding which released native primitive implements each marked requirement.