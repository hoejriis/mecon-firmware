# Architecture

> **Target documentation (planned and MECON-specific work; not implemented here unless a section says so).** The tagged releases v0.1.0 to v0.1.2 are source-only pre-releases without the device interface. Current `main` implements the generic [device interface v1](contract/DEVICE_INTERFACE.md) and the standalone features in the [README](../README.md), with real-board evidence in [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md). The generic work still planned (Issues #95, #96, #109, #110, #114 to #117) and the MECON-specific work (Issues #118 to #123) described here are not implemented. Release timing is a separate decision. This page describes the target for the public line, not the private 0.9x reference firmware, whose shipped behaviour differs in places (see [Current-to-target adjustments](CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line)). See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

> **Generic authority and the MECON profile.** The generic authority layer (device identity, authorization, replay protection and recovery) is Issue #95, and any conforming manager can use it. MECON's Deployment mapping, membership and ownership workflow is Issue #118. Where this page describes Deployment-specific workflow, it describes the MECON profile. Unsettled security design is decided in #95's review, not on this page.

## Upstream foundation

The public firmware is an extension of **released MeshCore 1.18 from upstream `main`**, not an independent RF stack. MeshCore 1.18.0 was released on 2026-10-15 and the repository is pinned at upstream v1.18.0 (commit `81dbae5`).

MECON is intentionally **not defined as MeshCore + Wi-Fi/MQTT**. Portable Core behaviour is separated from optional transports/platform services.

```text
MeshCore 1.18 (upstream main; pinned)
        │
        ├── Companion ──┐
        │               ├── MECON Core
        └── Repeater ───┘       │
                                ├── identity / capabilities / versions
                                ├── CLI/configuration extensions
                                ├── health / status / diagnostics
                                ├── messaging / favourites semantics
                                ├── resilience / outage-sync behaviour
                                └── common management operations
                                      │
                 ┌────────────────────┼────────────────────┐
                 │                    │                    │
             MECON USB            MECON BLE            MECON IP
                                      │
                               MeshCore RF/LoRa
                               where supported
```

A target advertises capabilities. Reader/backend must not infer Wi-Fi, MQTT, display, OTA mechanism, BLE or recovery support solely from MECON identity.

## One management plane: extend the MeshCore CLI

**MeshCore CLI/configuration semantics are the canonical device-management semantics.** MECON does not define separate configuration products for USB, BLE, MQTT and LoRa.

Where released MeshCore 1.18 already exposes a suitable CLI/configuration operation, MECON reuses it and its authoritative persistence/validation. MECON-specific behaviour extends the same logical plane under a `mecon` namespace. The reference firmware's shipped plane is `get_config` / `apply_config` plus the native CLI and a set of `cmd` verbs, with settings under `mesh.*` (native keys) and `mecon.*` (and `deimos.*` accepted permanently as an alias); on repeaters the same operations are available as `mecon info|get|list|status|set|reboot|sync|pw|dep|ota|writes|posture|clock|radiotemp` lines, compact (replies of at most 159 characters) and, when written over RF, Deployment-signed. USB, BLE, MQTT and MeshCore RF/LoRa are transports to that plane; they do not redefine setting names or side effects. On `main`, the built plane is the device interface: `config.schema`, `config.get` and `config.apply` over USB, BLE and MQTT, plus the unified CLI ([DEVICE_INTERFACE](contract/DEVICE_INTERFACE.md)); the reference-firmware names above are legacy wire.

This does **not** mean exposing an unrestricted raw shell. MECON transports carry an allowlisted, versioned operation envelope with request/job correlation, authorization, results/errors and replay/idempotency protection. A transport may use a compact binary encoding, especially over LoRa, while mapping to the same canonical operation.

For native configuration, upstream storage such as `NodePrefs`/`ConfigSerializer` (or the released 1.18 equivalent) remains authoritative. MECON must not maintain a second authoritative copy of radio/name/location/routing/GPS/etc. Schema and atomic `get_config`/`apply_config` facilities are UI/API orchestration over this command/configuration plane, not a competing settings architecture.

Remote RF management should use MeshCore's released authenticated CLI/command mechanism where available: Backend/Reader -> local transport -> Companion -> MeshCore RF -> target CLI/configuration. MECON may add correlation, auditing, policy and compact framing around the operation without inventing a proprietary RF configuration protocol.

The consolidated pre-release target definition is [`contract/TARGET_FIRMWARE_CONTRACT_0.9.md`](contract/TARGET_FIRMWARE_CONTRACT_0.9.md). Exact MECON extension names/encodings are frozen only after the released 1.18 API is pinned.

## Roles in the reference firmware

The private 0.9x firmware builds four roles, which the contracts abbreviate to Companion and Repeater: the **Companion Gateway** (ESP32, Heltec V3 and V4, Wi-Fi/MQTT plus USB/BLE), the **Repeater Observer** (ESP32, Heltec V3, MQTT plus RF administration), the **T1000-E BLE Companion** (nRF52, no Wi-Fi) and the **Wio RF repeater** (nRF52, managed over RF and USB). Capabilities, not these names, decide what a client may request.

## Portable MECON Core

Core functionality includes, where the target permits it: stable identity/version/capability discovery, common CLI/configuration dispatch, health/status, native contact/channel inventory, DM/channel/favourites semantics, private-channel outage/resilience behaviour, autonomous health reports, common authorization/replay/idempotency and transport-independent management operations.

## Deployment and device identities

A MECON Deployment has a human-readable name and permanent cryptographic identity. The Deployment name is also the default name of its private offline sync channel.

Credentials remain separated by purpose:

- Deployment trust-root/recovery authority belongs to Backend/recovery architecture and is not an ordinary Companion credential;
- each Backend has its own instance identity;
- enrolled Companions hold the current private offline sync-channel key/generation;
- each IP-capable Companion has an individual MQTT/device transport credential that can be revoked without rotating every other device;
- optionally designated Recovery Companions hold an encrypted Recovery Package, not a plaintext master key.

Possession of the offline sync-channel key must never be sufficient to mint an MQTT credential or impersonate the Deployment trust root.

## Offline sync key lifecycle

The private Deployment sync channel supports offline MECON coordination/enrollment behaviour. Because every enrolled Companion may hold its shared private key, loss/revocation of a Companion requires:

1. revoke the device's individual online/MQTT authority;
2. increment sync-key generation;
3. generate a new sync-channel private key;
4. distribute the new generation to remaining authorized devices through trusted paths.

Firmware must represent sync-key generation explicitly enough to reject/diagnose stale state.

## Recovery Companion capability

Recovery is optional and explicitly provisioned. A normal enrolled Companion is not automatically a Recovery Companion.

*Reference firmware today:* the private 0.9x firmware ships the underlying slot as an opaque continuity slot on the Companion gateways and the T1000-E (`continuity_store`, `continuity_clear`, `continuity_status` and `continuity_read`, the last USB only and bare). It is bounded to 1,953 bytes on the T1000-E, refused for storing in a restricted posture, and destroyed on every posture change. The terms "Recovery Companion" and "designated" are target terminology and do not appear in the shipped specification.

A Recovery Companion stores a small opaque encrypted **Recovery Package** in persistent storage. It must not keep a directly usable plaintext Deployment master/recovery key. Package encryption uses a key derived from a user-selected passphrase; MECON does not impose passphrase complexity, though clients may provide strength guidance.

### Access boundary

Recovery Package access is **USB only** and requires an explicit recovery operation. It is not readable/exportable through RF, MQTT or BLE and must not be exposed in ordinary status/diagnostics. Firmware should not remotely advertise that a device is a Recovery Companion.

The intended cold-start flow is a browser attached by USB to a Recovery Companion while connected to a fresh Backend/Reader. The browser reads the opaque package, accepts the user's passphrase and performs recovery processing/provisioning according to the public recovery contract.

### Recovery Package v1 scope

Store only high-value continuity material that cannot reliably be reconstructed:

- package format/version;
- monotonic recovery generation;
- Deployment ID and name;
- encrypted material required to prove/re-establish Deployment trust-root continuity and its key generation;
- offline sync-channel identity/private key/generation;
- KDF salt/parameters and authenticated-encryption metadata.

Do not use the package as configuration backup. Exclude ordinary Wi-Fi profiles, broker endpoints/passwords, retention settings, UI preferences, observations/history, templates, routine device configuration, favourites/users that can be reconstructed from surviving Companion state, and similar replaceable information.

### Recovery generation and revocation

Fundamental recovery/trust changes advance the Recovery Package generation and update designated Recovery Companions. Revoking a Recovery Companion advances/replaces recovery authority so a stale package cannot remain indefinitely current. Cold recovery generates fresh operational Backend/broker credentials and rotates the offline sync key before normal operation resumes.

### Resource model

Recovery storage must be implemented as a bounded persistent blob rather than permanently resident heap state. Normal runtime RAM cost should be negligible: read/stream the encrypted blob in bounded chunks only during the explicit USB recovery operation. The v1 package should remain small (expected order of kilobytes, not a general backup archive), making the model suitable for constrained and non-ESP targets that expose the capability.

## ESP32 runtime baseline

Heltec V3/V4 build on **pioarduino / Arduino-ESP32 3.1.3 / ESP-IDF 5.3** with **NimBLE** on `main` ([PLATFORM](PLATFORM.md); real-board rows in [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md)). These are ESP32 implementation choices, not universal MECON requirements. Network work must be non-blocking with respect to radio servicing.

## Native role is authoritative

With every MECON transport unavailable, Companion and Repeater retain normal MeshCore 1.18 behaviour. Companion contact capacity is determined by measured target-specific memory budget and published as a machine-readable resource limit.

## Connectivity and transport profiles

MQTT, USB, BLE and applicable MeshCore RF/LoRa are transports for one logical capability/command model; a device need not implement all.

### MECON IP

The standalone firmware on `main` stores up to ten ordered Wi-Fi profiles and uses one MQTT session at a time, with up to six broker profiles and failover by priority ([MQTT](MQTT.md)). The target broker selection is compatible authenticated local broker first, then configured primary cloud broker, then secondary cloud broker, with backoff/anti-flapping; the private 0.9x reference firmware behaves this way (see [Connectivity](CONNECTIVITY.md#mqtt-and-broker-selection)). Boards without IP omit MECON IP and may use Reader BLE/USB bridging.

### MECON BLE

BLE provides direct Reader/configuration/messaging where supported. ESP32 uses NimBLE; non-ESP targets use the appropriate native stack. Recovery-package export is explicitly excluded from BLE.

### MECON USB

USB/direct management provides configuration, recovery and Reader access according to capability. Recovery Package export is a distinct privileged USB-only operation and is available only on explicitly designated Recovery Companions.

### MeshCore RF/LoRa

Where released MeshCore provides remote CLI/command delivery, MECON uses it as the remote transport for the same canonical operations. Remote management remains bounded, authenticated and airtime-aware; LoRa does not acquire a separate MECON settings protocol. Today, RF management on `main` is the stock remote-admin CLI under the unified privilege table ([CLI](CLI.md)). The planned generic additions are in the next section.

### Management over LoRa (planned)

Nothing in this section is built. Three planned, generic capabilities, usable by any conforming manager, are owned by their own Issues:

- [#114](https://github.com/hoejriis/mecon-firmware/issues/114): a private management channel. Channel membership never grants command authority.
- [#115](https://github.com/hoejriis/mecon-firmware/issues/115): authorized operations over direct (peer-to-peer) and channel LoRa, with results. The request carries its own signature.
- [#116](https://github.com/hoejriis/mecon-firmware/issues/116): compact telemetry, a smaller and less frequent selection of the same telemetry catalogue ([#96](https://github.com/hoejriis/mecon-firmware/issues/96), not yet merged).

Encodings are not yet designed, so this page gives none. MECON's forwarding, backfill and reporting policy, and LoRa image delivery, are later MECON-specific work ([#121](https://github.com/hoejriis/mecon-firmware/issues/121), [#122](https://github.com/hoejriis/mecon-firmware/issues/122), [#123](https://github.com/hoejriis/mecon-firmware/issues/123)).

## Non-display targets

Display/UI is capability-dependent. Targets without screens omit framebuffer requirements. LEDs/buzzer/haptics may map the attention policy: DMs/favourited-channel traffic may alert; ordinary observed/public traffic remains silent.

## OTA

OTA is a logical management capability with target-specific implementation. Clients discover update capability/mechanism rather than assuming ESP OTA.

## Device UI

MECON target Heltec builds preserve MeshCore 1.18 Companion/Repeater UI/button semantics where possible. Deliberate additions in the original target were the version at boot, compact Wi-Fi/MQTT/BLE status, BLE PIN while unconnected and DM/favourite wake filtering. *Built differently:* the standalone Companion builds add a connectivity page ([CONNECTIVITY_MODE](CONNECTIVITY_MODE.md)). Recovery mode has no normal on-device UI requirement because initiation is USB/browser driven.

## Contract ownership

`docs/contract/` holds the device-facing contracts. [`DEVICE_INTERFACE.md`](contract/DEVICE_INTERFACE.md) is the current, implemented wire contract and wins where other pages disagree; `TARGET_FIRMWARE_CONTRACT_0.9.md` is a target document (see the [contract index](contract/README.md)). Contracts distinguish Core semantics from optional capability profiles, including recovery capability. MeshContinuum is the reference backend/Reader, not a prerequisite.

## Upstream boundary

MECON-owned code remains isolated for future MeshCore merges. Platform/board adapters must not leak ESP32 assumptions into portable Core behaviour. No third-party MeshCore fork is part of the update chain.