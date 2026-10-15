# Current-to-target contract adjustments

This file is **non-normative engineering history**. It explains lessons from the private MVP and why MECON 1.0 differs. It is not the backend implementation checklist: all actionable changes required in MeshContinuum because of these differences belong in [`BACKEND_MIGRATION.md`](BACKEND_MIGRATION.md).

The public line is rebuilt from **MeshCore 1.18.0**, released on upstream `main` on 2026-10-15; its first release will be mecon-firmware 0.8.0. The private implementation (the 0.9x reference firmware, described [below](#reference-firmware-today-09x-line)) is inspiration/evidence and a behavioural test oracle, not a code migration baseline.

## Foundational reset

Before porting MECON features:

1. pin released MeshCore 1.18;
2. prove stock V3/V4 Companion/Repeater behaviour;
3. migrate to pioarduino / Arduino-ESP32 3.x / IDF 5.x;
4. adopt NimBLE (the reference firmware's ESP32 gateways already use NimBLE-Arduino 2.5.1, on the older platform);
5. prove behaviour again and measure memory;
6. set resource limits from those measurements;
7. implement MECON using 1.18 primitives where possible.

This intentionally avoids inheriting obsolete private-MVP architecture.

## Naming and identity

The **target** public vocabulary is `mecon_*`, `mecon.*`, backend-neutral `device_id`, and native `companion`/`repeater` roles; the legacy `deimos_*`, `deimos.*` and `gateway_id` names and the `Companion Gateway` / `Repeater Observer` role labels are migration concerns for that target.

The reference firmware does not speak the target vocabulary yet. Today it identifies a device by `gateway_id`, reports the roles `companion_gateway` and `repeater_observer`, emits `mecon_*` fields together with their `deimos_*` aliases, and uses the `deimos/gw/{gateway_id}/…` topics. Its `mecon.*` settings paths are shipped and canonical, and it accepts `deimos.*` permanently as an alias.

## MeshCore 1.18 reuse

The private MVP contains parallel Wi-Fi, configuration, CLI/command, UI and board-specific mechanisms created against older MeshCore. Review each against 1.18 before porting. If 1.18 provides the required primitive, extend/reuse it instead of migrating the private implementation.

## Wi-Fi and MQTT

The target is **three ordered Wi-Fi profiles and one active MQTT session**. The reference firmware already behaves this way (one session, three Wi-Fi slots); it still stores up to four broker profiles and opens one at a time. A compatible locally discovered broker is preferred; otherwise the device uses its configured cloud broker. The private MVP's historical concurrent-broker architecture and associated replay complexity are not public 1.0 requirements.

Discovery is non-blocking and conveys reachability only; trust/authority remains provisioned.

## Settings and transports

One logical settings/capability model spans MQTT, USB and BLE. Native MeshCore 1.18 operations are reused where adequate. MECON-specific operations use versioned envelopes/tunnels rather than transport-specific product implementations.

BLE is NimBLE-based (already true of the reference gateways). USB remains recovery-capable.

## Resource limits

Do not copy the private MVP's historical 32/64-contact limits into the public contract. Measure V3/V4 after IDF5/NimBLE and set/publish the supported contact capacity from evidence. For reference, the 0.9x firmware enforces 50 contacts on Heltec V3 and V4 and 350 on the T1000-E. Managed/protected-contact semantics remain required where contact eviction exists.

## Device UI

Do not port the private UI wholesale. Preserve the MeshCore 1.18 Companion/Repeater screen/button behaviour and add only: MECON version at boot; Wi-Fi/MQTT/BLE status on the existing front screen; BLE PIN while unconnected; and DM/favourite-only display wake behaviour. No secondary MECON screen in 1.0. Retain framebuffer export as test/diagnostic capability.

## Resilience features to preserve

The public refactor preserves the MVP-derived product behaviours: MQTT-offline DM forwarding to a configured private channel; a periodic private-channel health/status report (the reference firmware's counterpart is a signed status beacon, below); delivery/outage synchronization; and authorized MQTT delivery of DMs/channel messages when the RF mesh path is unavailable. Loop/duplicate prevention and stable delivery identity are required.

## OTA

Signed managed OTA remains a requirement of the target, implemented after the clean 1.18/IDF5/NimBLE base is established. The reference firmware already ships it, including relay over LoRa (below).

## Historical-contract audit

The private firmware has substantial historical contracts for device health, device settings, MQTT gateway behaviour, outage synchronization and serial provisioning. Before MECON 1.0 implementation is considered specification-complete, each operation/field/lifecycle in those documents and the deployed backend must be classified as:

- still required and represented in the public MECON 1.0 contract;
- supplied by a named native MeshCore 1.18 operation; or
- intentionally legacy/backend-only and covered by `BACKEND_MIGRATION.md`.

This audit is necessary because the public documentation must be sufficient to implement the firmware anew; merely summarizing the private MVP is not sufficient.

## Reference firmware today (0.9x line)

*Added 2026-10-15.* The private reference firmware is the 0.9x line. The 0.90 series is current: 0.90.5 is the latest promoted release at the time of writing, and later 0.90.x builds are still pre-releases. This section records what that firmware does today so the target documents can be read against it. It is a statement of shipped behaviour, not a requirement on the public line. The public 0.8.x line and the private 0.9x line are separate lines: version numbers do not order across them, and a lower public number is not a downgrade from a private build (or the reverse). The reference firmware is built on MeshCore 1.17.1; the public line starts on 1.18.0.

**Builds.** Four roles ship in every release: a Companion gateway (ESP32, Heltec V3 and V4), a Repeater Observer (ESP32, Heltec V3), a BLE Companion (nRF52, SenseCAP T1000-E) and an RF repeater (nRF52, Seeed Wio Tracker L1). There is no Heltec V4 repeater build. The ESP32 builds use Arduino-ESP32 2.x / ESP-IDF 4.4 with NimBLE-Arduino 2.5.1.

**Wire vocabulary (legacy, shipped).**

- Identity is `gateway_id` plus the node public key; `hardware_target` also takes `t1000_e` and `wio_tracker_l1`; `role` is `companion_gateway` or `repeater_observer` (an RF repeater reports `repeater` in its RF `mecon info`); `build_variant` is reported.
- Topics live under `deimos/gw/{gateway_id}/`. Device to backend: `status` (retained), `contacts`, `channels`, `rx`, `raw`, `result`, `config_schema`, `config_result`, `backfill_result`, `sync_status`, plus result topics for authority updates and OTA. Backend to device: `cmd`, `get_config`, `apply_config`, `sync_config`, `backfill`, `authority_update`, `beacon` and `ota/…` (with `ota/relay/…` for the LoRa hop). A retained last will carries `gateway_id` and `online: false`.
- Every envelope carries `gateway_id`, `enrollment_token`, `mecon_mqtt_protocol_version` (alias `deimos_mqtt_protocol_version`) and `ts`, and since 0.87.0 `usig_boot`, `usig_seq` and `usig`. `mecon_firmware_version` is emitted with a `deimos_firmware_version` alias. The direction is to stop relying on `enrollment_token` once every device is latched on signed uplink.
- The transport is MQTT over WSS on port 443, one session at a time, in the order local, primary, secondary broker.

**Authority and signing.**

- Every management message (`cmd`, `apply_config`, `get_config`, `backfill`, `sync_config`, `ota/offer` and the LoRa relay messages) is wrapped in a Deployment-signed envelope: issuer, authority revision and an Ed25519 signature over the canonical inner body, which names the device by `gateway_id` (a repeater by its target node public key). Replay is tracked per issuer in separate job-id sequences for commands, settings, backfill and OTA. A message over the 3,072-byte slot is refused.
- A device's Deployment record is `none`, `present` or `corrupt`. A corrupt record fails closed. With no record, a broker, BLE or TCP caller can only read, send the first bare Deployment provisioning, or send the bare contact-protection command; USB works under physical possession.
- Every MQTT publish from a gateway is signed (domain `MECON-UPLINK-v1`); the Backend latches per device.
- In a restricted posture an authenticated Reader unlocks USB with an HMAC challenge over a write-once USB-auth secret (single-use nonce valid for 60 s, five failures lock the unlock for five minutes, ten minutes idle ends the session).
- Repeaters take a compact signed RF line, `mecon !<issuer>.<revision>.<signature> <verb> id=N` (domain `MECON-RFCMD-v1`), with replies of at most 159 characters. Verbs include `info`, `get`, `list`, `status`, `set`, `reboot`, `sync`, `pw`, `dep`, `ota`, `writes`, `posture`, `clock` and `radiotemp`.
- Stock key import is refused on the companion protocol; the stock repeater console `set prv.key` works only while no valid Deployment record is held. Key export, identity rotation, BLE passkey set and bond clear are described in [Security and authority](contract/SECURITY_AND_AUTHORITY.md).

**Direct Reader.** USB and BLE carry the stock companion frames plus a JSON tunnel (`CMD_MECON_JSON`, 120) that carries the same signed requests as the broker. Device information is paged so a device can list more capabilities than fit in one frame. USB provisioning uses a key/value command (46) for Wi-Fi (three slots), broker, label, connectivity mode and the write-once identifier and secrets; a first bare Deployment provisioning follows, after which every write is signed.

**Over-the-air update.** Ed25519-signed manifest with a key identifier and a minimum-from-version check; an update that is not confirmed reverts by itself; a signed `ota/ack` confirms it (capability `ota_ack_signed`). ESP32 gateways can take a full image relayed over one LoRa hop (1,228-byte chunks, a 10 % hourly airtime budget). The Wio repeater stages an image received over LoRa and never activates it. The T1000-E updates through the USB bootloader after a signed offer. BLE DFU is compiled out of the nRF52 builds, because the bootloader checks no signature. A public build supplies its own trust anchors.

**Posture.** Normal and Hardened ship on all four roles (on the repeaters since 0.90.0). A device must hold a USB-auth secret before it can enter Hardened, so it can always be unlocked. The Wio and T1000-E each have a recovery-only image that removes the posture records and nothing else.

**Settings.** The settings namespace is `mesh.*` (native keys) and `mecon.*` (`wifi_profiles[N]`, `mqtt.*`, `connectivity.mode`, `posture`, `companion_ble.enabled`, `companion_tcp.enabled`, `ble_passkey`). Wi-Fi and MQTT changes are committed in two phases and reverted if the device does not come back. Connectivity mode is `mqtt`, `ble` or `both`; `both` is not offered on Heltec V3 (memory).

**Health additions** beyond [the core contract](contract/CORE_DEVICE_CONTRACT.md): `battery_level` (`ok|low|critical|unknown`), a `radio` object, duty-cycle fields (`duty_limit_permille`, `duty_legal_permille`, `tx_budget_ms`, `tx_shed`), uplink-signing counters (`usig_*`), `ble_events`, `crash_history`, a `power` object, `location`, `connection_path`, `deployment` (with `auth_rejections`), and the network fields `wifi_ip`, `wifi_gateway`, `wifi_netmask` and `wifi_bssid`. Status still carries a `brokers[]` array of per-profile entries, of which only one is connected.

**Resilience and clock.** The periodic private-channel report is a signed status beacon (group-data type 0x4402, schema 4), sent at a random time every 20 to 23 hours, plus one at boot if the last automatic one is 12 hours old. `set_clock` moves a clock forward to any time at or after 2026-01-01 and back by at most 600 s per command.

## MeshContinuum alignment

`mecon-firmware/docs/contract/` is canonical for public device behaviour. MeshContinuum should consume it and keep legacy compatibility adapters outside the public v1 contract. See [`BACKEND_MIGRATION.md`](BACKEND_MIGRATION.md) for the concrete backend work.

## Corrections of 2026-10-15

A review of the public documents against the private 0.9x reference firmware corrected these substantive points. Everything else in this change is added context.

- **Versions and plan.** The old milestone names (0.8.1, 0.9.1, 1.0.1) are replaced: the first public firmware is mecon-firmware 0.8.0, built on MeshCore 1.18.0 (released 2026-10-15), and the public 0.8.x and private 0.9x lines are separate, so numbers do not order across them. The "wait for 1.18" gate is open.
- **Hardware.** The T1000-E is built and hardware-proven in limited scope, not "planned"; the Wio Tracker L1 RF repeater and the ESP32 Repeater Observer are part of the set; no Heltec V4 repeater build exists; contact capacity is 50 on Heltec V3/V4 and 350 on the T1000-E; Beta boards now cites 0.90.5 and the Wio's tested state.
- **Over-the-air update.** LoRa-relayed update is shipped, not absent; BLE DFU is compiled out of the nRF52 builds, not a supported mechanism.
- **Connectivity.** Single-session broker selection (local, primary, secondary) has shipped since 0.51.0, not "post-P3"; `Both` mode is not available on the Heltec V3; the multi-broker audit row is already retired in firmware; security posture ships on the repeater roles since 0.90.0.
- **Key custody and passkey.** Key import is not "never available": the stock repeater console `set prv.key` works while no valid Deployment record is held, and the key can be read over USB without a signature while the record is corrupt. The BLE passkey is writable through a signed command and USB command 37, and readable as a read-only entry of `get_config`. The bond count is available on any `cmd` path; only the clear is USB-only.
- **Wire.** The shipped wire is still `gateway_id`, `deimos/gw/…` and `enrollment_token`, with signed uplink and a signed downlink wrapper; the v1 vocabulary is the target.
- **Settings.** `mecon.*` paths are shipped and canonical, and `deimos.*` is accepted permanently; the "daily report" has a shipped counterpart in the signed status beacon.
- **References.** Mentions of private-repository issue numbers are replaced by plain descriptions.

