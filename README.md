# mecon-firmware

**mecon-firmware** is an open MeshCore firmware distribution for Heltec V3 and V4 devices. It preserves normal MeshCore Companion and Repeater operation while adding Wi-Fi, MQTT, USB and BLE connectivity, remote management, observations and direct web-reader access.

It is the sister project of [MeshContinuum](https://github.com/hoejriis/MeshContinuum), but it is **not coupled to MeshContinuum**. Any backend, broker or Reader may integrate with mecon-firmware by implementing the public, versioned contracts in this repository.

> This repository currently defines the public release target. Source will be migrated here only after the implementation conforms to these contracts and passes the public-release gates.

## Release target

A supported mecon-firmware device:

1. **Remains a MeshCore device.** With no Wi-Fi, MQTT, backend or Internet, it behaves as an ordinary MeshCore Companion or Repeater. The standard Companion build deliberately uses a **64-contact limit** to retain memory headroom for concurrent connectivity.
2. **Enables Wi-Fi, BLE and USB by default.** Loss of one transport does not disable the underlying MeshCore role.
3. **Stores up to three Wi-Fi profiles** and reconnects automatically to known networks.
4. **Connects to up to two MQTT brokers** concurrently, with independently scoped authority for remote management, packet observations and Companion messaging.
5. **Supports direct browser operation.** A compatible Reader in desktop Chrome or Edge can attach to a Companion over USB or BLE and provide messaging, observations and configuration when Wi-Fi is unavailable. Repeaters support direct USB access; Repeater BLE is not part of the initial target.
6. **Publishes an open integration contract.** Device identity, capabilities, configuration, health, observations, messaging, authentication and authorization are backend-neutral and versioned.
7. **Tracks upstream MeshCore cleanly.** MECON functionality is a shared integration layer around upstream roles, with thin role/hardware adapters and a documented forward-port process.

> **Loss of Wi-Fi, MQTT, a MECON-compatible backend, or Internet connectivity must never prevent the device from performing its underlying MeshCore role.**

## Target architecture

```text
meshcore-dev/MeshCore
        │
        ├── Companion role ──┐
        │                    ├── shared MECON runtime
        └── Repeater role ───┘       │
                                     ├── Wi-Fi ×3 profiles
                                     ├── MQTT ×2 brokers
                                     ├── USB
                                     └── BLE (Companion)
                                           │
                                  versioned open contracts
                                           │
                           ┌───────────────┴───────────────┐
                           │                               │
                    MeshContinuum                    Other projects
```

## Supported release targets

- Heltec V3 Companion
- Heltec V3 Repeater
- Heltec V4 Companion
- Heltec V4 Repeater

All advertised release targets must pass their hardware release gate before being labelled supported. Experimental board revisions may be published separately but are not part of the supported baseline until promoted.

## Documentation

- [Documentation index](docs/README.md)
- [Why mecon-firmware](docs/WHY_MECON_FIRMWARE.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Supported hardware and roles](docs/SUPPORTED_HARDWARE.md)
- [Connectivity and operating modes](docs/CONNECTIVITY.md)
- [Integration guide](docs/INTEGRATION.md)
- [Security model](docs/SECURITY_MODEL.md)
- [Upstream and porting model](docs/UPSTREAM_AND_PORTING.md)
- [Versioning and releases](docs/VERSIONING_AND_RELEASES.md)
- [Public contract](docs/contract/README.md)
- [Current-to-target contract adjustments](docs/CONTRACT_MIGRATION_NOTES.md)
- [Contributing](CONTRIBUTING.md)
- [Security policy](SECURITY.md)

## Independence

MeshContinuum is the reference backend and Reader for mecon-firmware, not a prerequisite. A conforming third-party implementation receives the same protocol surface and must not require private MeshContinuum knowledge.

MeshCore remains the RF protocol and source of native Companion/Repeater behavior. mecon-firmware does not define a competing RF protocol.