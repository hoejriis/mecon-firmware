# Architecture

## Upstream foundation

MECON 1.0 is an extension of **released MeshCore 1.18 from upstream `main`**, not an independent RF stack. The 1.18 merge/release is a dependency for the production refactor. A specific 1.18 revision is pinned before implementation proceeds.

MECON is intentionally **not defined as MeshCore + Wi-Fi/MQTT**. Portable Core behaviour is separated from optional transports/platform services.

```text
MeshCore 1.18 (upstream main; pinned)
        │
        ├── Companion ──┐
        │               ├── MECON Core
        └── Repeater ───┘       │
                                ├── identity / capabilities / versions
                                ├── common configuration model
                                ├── health / status / diagnostics
                                ├── messaging / favourites semantics
                                ├── resilience / outage-sync behaviour
                                └── common management operations
                                      │
                 ┌────────────────────┼────────────────────┐
                 │                    │                    │
             MECON USB            MECON BLE            MECON IP
```

A target advertises capabilities. Reader/backend must not infer Wi-Fi, MQTT, display, OTA mechanism, BLE or recovery support solely from MECON identity.

## Portable MECON Core

Core functionality includes, where the target permits it: stable identity/version/capability discovery, common configuration, health/status, native contact/channel inventory, DM/channel/favourites semantics, private-channel outage/resilience behaviour, autonomous health reports, common authorization/replay/idempotency and transport-independent management operations.

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

For Heltec V3/V4, after pinning/proving stock MeshCore 1.18, migrate to **pioarduino / Arduino-ESP32 3.x / ESP-IDF 5.x** and establish **NimBLE**. These are ESP32 implementation choices, not universal MECON requirements. Network work must be non-blocking with respect to radio servicing.

## Native role is authoritative

With every MECON transport unavailable, Companion and Repeater retain normal MeshCore 1.18 behaviour. Companion contact capacity is determined by measured target-specific memory budget and published as a machine-readable resource limit.

## Connectivity and transport profiles

MQTT, USB and BLE are transports for one logical capability model; a device need not implement all.

### MECON IP

Initial Heltec targets store up to three ordered Wi-Fi profiles and use one MQTT session at a time. Post-P3 broker selection is compatible authenticated local broker first, then configured primary cloud broker, then secondary cloud broker, with backoff/anti-flapping. Boards without IP omit MECON IP and may use Reader BLE/USB bridging.

### MECON BLE

BLE provides direct Reader/configuration/messaging where supported. ESP32 uses NimBLE; non-ESP targets use the appropriate native stack. Recovery-package export is explicitly excluded from BLE.

### MECON USB

USB/direct management provides configuration, recovery and Reader access according to capability. Recovery Package export is a distinct privileged USB-only operation and is available only on explicitly designated Recovery Companions.

## Non-display targets

Display/UI is capability-dependent. Targets without screens omit framebuffer requirements. LEDs/buzzer/haptics may map the attention policy: DMs/favourited-channel traffic may alert; ordinary observed/public traffic remains silent.

## OTA

OTA is a logical management capability with target-specific implementation. Clients discover update capability/mechanism rather than assuming ESP OTA.

## Device UI

MECON 1.0 Heltec builds preserve MeshCore 1.18 Companion/Repeater UI/button semantics where possible. Deliberate additions are MECON version at boot, compact Wi-Fi/MQTT/BLE status, BLE PIN while unconnected and DM/favourite wake filtering. No secondary MECON screen in 1.0. Recovery mode has no normal on-device UI requirement because initiation is USB/browser driven.

## Contract ownership

`docs/contract/` is canonical for MECON-specific device-facing behaviour. Contracts distinguish Core semantics from optional capability profiles, including recovery capability. MeshContinuum is the reference backend/Reader, not a prerequisite.

## Upstream boundary

MECON-owned code remains isolated for future MeshCore merges. Platform/board adapters must not leak ESP32 assumptions into portable Core behaviour. No third-party MeshCore fork is part of the update chain.