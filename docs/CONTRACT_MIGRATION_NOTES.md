# Current-to-target contract adjustments

This file is **non-normative**. It records known differences between the current private development firmware/backend contracts and the public mecon-firmware target. The files under `docs/contract/` describe the target and should not preserve legacy inconsistencies merely for compatibility.

No issues are created in the private `deimos-mesh*` repositories from this document.

## Naming and identity

| Current development behavior | Public target | Adjustment required |
|---|---|---|
| Legacy `deimos_mqtt_protocol_version` / `deimos_firmware_version` existed and mixed fleets may still emit them | `mecon_*` only | Public firmware emits canonical MECON names; migration adapters may accept legacy input outside the public contract |
| `deimos.*` setting paths are still advertised/accepted in parts of the current implementation | `mecon.*` | Migrate schema and implementation to canonical `mecon.*`; legacy aliases belong only in migration compatibility code |
| Default MQTT namespace historically `deimos/gw` | no service-specific compiled default | Provision namespace explicitly; examples may use `mecon/devices` but must not imply MeshContinuum ownership |
| `gateway_id` describes a backend-assigned identity | `device_id` is the backend-neutral device vocabulary; backend record IDs remain backend-local | Separate stable device identity from a particular backend's gateway record/enrollment identity |
| role names include `Companion Gateway` and `Repeater Observer` | native roles are `companion` and `repeater`; `observe` is a capability | Normalize public role vocabulary and keep product/UI labels outside the wire contract |

## Topic and message shape

Current MQTT uses a broad historical topic set including `raw`, `rx`, `contacts`, `channels`, `result`, `config_schema`, `config_result`, `backfill_result`, `sync_status`, `cmd`, `get_config`, `apply_config`, `sync_config` and `backfill`.

The target groups these into stable conceptual surfaces (`observations`, `events`, `inventory`, `results`, `config/*`, `commands`, `ota`) and relies on typed envelopes. Before source migration, define the exact v1 payload schemas and map every existing operation to the target without losing functionality.

The current common envelope includes `gateway_id`, `enrollment_token`, protocol version and `ts`. The target requires backend-neutral `device_id`, stable `message_id`, explicit protocol identity/version and an unambiguous timestamp representation. Decide whether enrollment authentication remains in every payload or moves entirely into the broker-profile authority model; do not carry the current shared token forward by inertia.

## Settings transport

Current Companion and Repeater local configuration use different mechanisms: Companion binary commands/tunnel paths versus Repeater text CLI (`mecon_set` and related commands). MQTT uses generic `get_config`/`apply_config`.

Target: **one logical settings contract** across MQTT, Companion USB/BLE and Repeater USB. Physical framing may differ, but names, validation, effects and result semantics must not.

A versioned MECON JSON tunnel over Companion USB/BLE is required to remove the current need for transport-specific implementations of new MECON operations. Repeater USB must map its CLI/framing to the same logical envelopes.

## Direct BLE/USB behavior

Current BLE historically depended on Wi-Fi/MQTT startup state and has had shared/default passkey behavior during development. Target Companion BLE starts independently of Wi-Fi/MQTT and uses a device-specific credential.

Current direct browser support is incomplete and split between existing MeshCore Companion commands and newer MECON-specific operations. Target exposes full supported Companion functionality through USB or BLE to Chrome/Edge, including configuration and observations, without requiring Wi-Fi.

## Wi-Fi and brokers

Current code supports multiple Wi-Fi and broker profiles, but public limits and semantics have not always been stated consistently. Target fixes the supported baseline at **3 Wi-Fi profiles and 2 broker profiles**.

Broker profiles must carry independent grants. Review current multi-broker replay-watermark and job-ID behavior so the same logical operation arriving through two brokers cannot execute twice.

## Capability/profile model

Current development firmware has both individual `capabilities` and grouped `profiles`, with older devices lacking profiles. Target keeps profiles as the compatibility/version boundary and capabilities as feature discovery. Publish one canonical mapping and remove backend assumptions based on firmware version or role labels.

Add explicit public profiles for `ota` and `direct_reader` rather than leaving these as implied behavior.

## Configuration effects

Current contract text historically included `reboot_required` although firmware could not honor it consistently; current builds guard against reintroducing it accidentally. Target keeps `reboot_required` as a valid schema effect **only when the implementation can actually complete and report the reboot lifecycle correctly**. Until then, no setting may advertise that effect.

## Observations

Current MQTT calls the packet stream `raw`; Repeater direct USB work uses separate raw-log concepts. Target calls the logical object an `observation` regardless of transport. Preserve raw MeshCore packet bytes and receiver metadata, and define one schema reused by MQTT and direct Reader paths.

## Messaging and inventories

Current Companion messaging, contact/channel inventories, provisioning and backfill are spread across native Companion commands and MQTT-specific command types. Target should inventory these operations and decide which are:

- native MeshCore operations reused unchanged;
- MECON typed operations shared across transports;
- backend-only concepts that do not belong in firmware.

Do not create duplicate MECON commands where the native Companion protocol already supplies the required semantics reliably.

## Contact capacity

Current releases evolved from a larger contact table to a 64-contact BLE-on default. Target makes **64** an explicit supported Companion resource limit. Complete deterministic protection/eviction semantics so managed/enrolled identities cannot be displaced by ordinary learned contacts merely because the table is full.

## OTA

Current development firmware already has signed manifests, hashes and dual-slot OTA machinery, while older documentation still describes OTA as absent/backlog. Target treats signed managed OTA as part of the supported contract. Align all public documentation, status capabilities, manifest fields and backend UX around the actual target trust model.

## Hardware naming/support

Current documentation has disagreed about whether V4 is supported while release artifacts already include V4 variants. Target matrix is explicit: **Heltec V3 and V4, Companion and Repeater**. A target becomes supported only after its hardware gate. Experimental revisions such as V4 R8 remain separately identified until promoted.

## MeshContinuum alignment

The current private backend contains firmware contract copies and historical assumptions. Before public release:

1. make `mecon-firmware/docs/contract/` canonical;
2. update MeshContinuum to reference/consume the public contract rather than own a competing copy;
3. remove Deimos/private-instance vocabulary from public protocol surfaces;
4. keep compatibility adapters for deployed legacy firmware inside migration code, clearly outside the public v1 contract;
5. run contract tests against both projects so future changes cannot diverge silently.