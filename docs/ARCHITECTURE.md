# Architecture

## Upstream foundation

MECON 1.0 is an extension of **released MeshCore 1.18 from upstream `main`**, not an independent RF stack. The 1.18 merge/release is a dependency for the production refactor. A specific 1.18 revision is pinned before implementation proceeds.

```text
MeshCore 1.18 (upstream main; pinned)
        │
        ├── Companion ──┐
        │               ├── shared MECON runtime
        └── Repeater ───┘       │
                                ├── Wi-Fi ×3
                                ├── one local-first MQTT session
                                ├── USB
                                └── NimBLE where supported
```

MECON extends the 1.18 Wi-Fi/configuration, command, board-preference, UI and native Companion/Repeater facilities wherever suitable. It does not recreate their 1.17/private-MVP equivalents.

## Runtime baseline

After pinning and proving stock MeshCore 1.18 on Heltec V3/V4, the fork migrates those targets to **pioarduino / Arduino-ESP32 3.x / ESP-IDF 5.x** and establishes **NimBLE**. Stock-derived Companion and Repeater behaviour is proven again before the MECON feature layer is added.

Network work must be non-blocking with respect to radio servicing. V3 and V4 are first-class targets with shared behaviour and explicit hardware adapters.

## Native role is authoritative

With every MECON transport unavailable, Companion and Repeater retain their normal MeshCore 1.18 behaviour. MECON never makes RF operation dependent on Wi-Fi, MQTT, a backend or Internet connectivity.

Companion contact capacity is deliberately reduced only as required by the **measured post-IDF5/NimBLE memory budget**. The private MVP's historical 32/64-contact values are not a normative 1.0 constant. The final supported capacity is published as a machine-readable resource limit.

## Connectivity

The device stores up to three ordered Wi-Fi profiles. It uses **one MQTT client/session at a time**: a compatible locally discovered broker is preferred, otherwise the configured cloud broker is used. The old private-MVP concurrent two-broker architecture is not part of MECON 1.0.

MQTT, USB and BLE are transports for one logical capability model. Native MeshCore 1.18 operations are reused where appropriate; MECON-specific operations use the public versioned contract.

## Device UI

MECON 1.0 preserves the MeshCore 1.18 Companion/Repeater UI and button semantics as far as possible. The deliberate UI delta is limited to MECON version at boot, compact Wi-Fi/MQTT/BLE status on the existing front screen, BLE PIN while unconnected, and the established display-wake filtering for DMs/favourited channels. There is no MECON secondary screen in 1.0.

## Contract ownership

`docs/contract/` is canonical for MECON-specific device-facing behaviour. MeshContinuum is the reference backend/Reader, not a prerequisite.

## Upstream boundary

MECON-owned code should remain isolated so later MeshCore releases can be merged with a small documented patch surface. No third-party MeshCore fork is part of the update chain.