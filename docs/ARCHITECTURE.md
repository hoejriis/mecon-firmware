# Architecture

## Upstream foundation

MECON 1.0 is an extension of **released MeshCore 1.18 from upstream `main`**, not an independent RF stack. The 1.18 merge/release is a dependency for the production refactor. A specific 1.18 revision is pinned before implementation proceeds.

MECON is intentionally **not defined as MeshCore + Wi-Fi/MQTT**. The product architecture separates portable MECON behaviour from optional transports/platform services so non-ESP MeshCore boards can participate without pretending to have IP connectivity.

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
                 │                    │                    │
        native USB/direct     native BLE/direct     Wi-Fi ×3 + MQTT
        management/Reader     management/Reader     discovery + cloud
```

A target advertises capabilities. A Reader/backend must not infer Wi-Fi, MQTT, display, OTA mechanism or BLE solely from `MECON` firmware identity.

MECON extends the 1.18 configuration, command, board-preference, UI and native Companion/Repeater facilities wherever suitable. It does not recreate their 1.17/private-MVP equivalents.

## Portable MECON Core

The Core is intended to be portable to supported MeshCore hardware beyond ESP32. Where the underlying MeshCore target permits it, Core functionality includes:

- stable MECON/native device identity and version/capability discovery;
- one logical configuration model;
- health/status and applicable RF/runtime diagnostics;
- native contact/channel inventory for Companion roles;
- DM/channel operations and favourites semantics;
- private-channel outage/resilience behaviour;
- autonomous private-channel health reports;
- common authorization and replay/idempotency semantics;
- transport-independent management operations.

Hardware-specific functionality is optional. For example, a tracker without Wi-Fi can implement Core + BLE + USB and rely on a Reader/gateway to bridge its operations to MQTT/backend infrastructure.

## ESP32 runtime baseline

For Heltec V3/V4, after pinning and proving stock MeshCore 1.18, the fork migrates those targets to **pioarduino / Arduino-ESP32 3.x / ESP-IDF 5.x** and establishes **NimBLE**. These are ESP32-target implementation choices, not requirements for every MECON-capable board.

Network work must be non-blocking with respect to radio servicing. V3 and V4 are first-class 1.0 targets with shared behaviour and explicit hardware adapters.

## Native role is authoritative

With every MECON transport unavailable, Companion and Repeater retain their normal MeshCore 1.18 behaviour. MECON never makes RF operation dependent on Wi-Fi, MQTT, a backend or Internet connectivity.

Companion contact capacity is reduced only as required by the **measured target-specific memory budget**. The private MVP's historical 32/64-contact values are not normative. Supported capacity is published as a machine-readable resource limit per build/target.

## Connectivity and transport profiles

MQTT, USB and BLE are transports for one logical capability model, but a device need not implement all of them.

### MECON IP

On the initial Heltec targets, the device stores up to three ordered Wi-Fi profiles and uses **one MQTT client/session at a time**: a compatible locally discovered broker is preferred, otherwise the configured cloud broker is used. The old private-MVP concurrent two-broker architecture is not part of MECON 1.0.

Boards without an IP stack simply do not advertise MECON IP. They can still be full MECON-managed devices over direct BLE/USB, with a Reader optionally providing the IP/backend bridge.

### MECON BLE

BLE provides direct Reader/configuration/messaging access where the target supports it. The ESP32 implementation uses NimBLE; non-ESP targets use the appropriate native MeshCore/platform BLE implementation while preserving the same logical MECON operations where possible.

### MECON USB

USB/direct management provides configuration, recovery and Reader access according to target capability. Framing may follow the target's native MeshCore interface plus the versioned MECON extension; logical operations remain transport-independent.

## Non-display targets

Display/UI behaviour is capability-dependent. Targets without a screen do not implement framebuffer/display requirements. Where hardware has LEDs, a buzzer or haptic motor, future target profiles may map the same attention policy used by the display builds: DMs and favourited-channel traffic may alert the user, while ordinary observed/public traffic remains silent. Exact physical indications are target-specific and must be documented before a target is promoted to supported.

## OTA

OTA is a logical MECON management capability, not one ESP-specific implementation. ESP32 IP targets may support MQTT-triggered managed OTA. Other targets may use their platform's secure DFU/update mechanism through BLE/USB or another supported transport. Every target advertises its update capability/mechanism; clients do not assume ESP OTA.

## Device UI

MECON 1.0 Heltec builds preserve the MeshCore 1.18 Companion/Repeater UI and button semantics as far as possible. The deliberate Heltec UI delta is limited to MECON version at boot, compact Wi-Fi/MQTT/BLE status on the existing front screen, BLE PIN while unconnected, and the established display-wake filtering for DMs/favourited channels. There is no MECON secondary screen in 1.0.

## Contract ownership

`docs/contract/` is canonical for MECON-specific device-facing behaviour. Contracts distinguish required Core semantics from optional capability profiles. MeshContinuum is the reference backend/Reader, not a prerequisite.

## Upstream boundary

MECON-owned code should remain isolated so later MeshCore releases can be merged with a small documented patch surface. Platform/board adapters must not leak ESP32 assumptions into portable Core behaviour. No third-party MeshCore fork is part of the update chain.