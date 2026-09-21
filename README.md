# mecon-firmware

**mecon-firmware** is an open MeshCore firmware distribution for Heltec V3 and V4 devices. It preserves normal MeshCore Companion and Repeater operation while adding resilient IP connectivity, remote management, observations and messaging through Wi-Fi, MQTT, USB and BLE.

It is the sister project of [MeshContinuum](https://github.com/hoejriis/MeshContinuum), but it is **not coupled to MeshContinuum**. Any backend, broker or Reader may integrate with mecon-firmware by implementing the public, versioned contracts in this repository.

> This repository currently defines the public release target. The existing private MVP is an implementation reference, not source to be copied wholesale. The public firmware will be refactored here against the documented behaviour and contracts.

## What MECON changes compared with stock MeshCore

MECON is deliberately an extension of MeshCore, not a replacement for it. A device should remain useful as an ordinary MeshCore node even when every MECON service is unavailable.

1. **It remains a normal MeshCore Companion or Repeater.** RF operation, mesh participation and the native role continue without Wi-Fi, MQTT, a backend or Internet connection. MECON uses a deliberately smaller Companion contact table than stock configurations may allow, trading contact capacity for enough RAM to run the additional connectivity and management services reliably. Managed/protected contacts must survive normal eviction.

2. **It can store three ordered Wi-Fi profiles.** The device automatically tries configured networks in their entered priority order and reconnects as availability changes. Losing Wi-Fi must not interrupt the underlying MeshCore role.

3. **It adds local-first MQTT connectivity.** A device may be provisioned with MQTT credentials and authority. When a compatible broker is advertised on the local network, the device prefers that local path; otherwise it uses its configured cloud broker. Local broker discovery is automatic, while credentials and permitted capabilities remain provisioned configuration. Only one MQTT session needs to be active at a time, reducing RAM pressure on constrained hardware.

4. **It exposes one management and observation model over MQTT, USB and BLE.** Packet observations, device events, logs, status, inventory, configuration and supported actions are available through the applicable transports using the same logical, versioned MECON contracts. Companion devices support direct USB/BLE operation; Repeaters support direct USB, with BLE support dependent on the advertised hardware/role capability. Native MeshCore operations should be reused rather than duplicated where they already provide the required behaviour.

5. **It supports managed OTA firmware updates.** Firmware can be updated remotely through the MECON management path, including MQTT-triggered OTA. Releases use manifests, integrity checks and the device's supported OTA trust model rather than treating an arbitrary URL as permission to install firmware.

6. **It can use the mesh itself as an outage path.** When MQTT/IP connectivity is unavailable, selected direct-message traffic can be relayed into a configured private MeshCore channel so loss of the Internet path does not necessarily mean loss of communication. The exact forwarding rules must prevent loops and duplicate delivery when IP connectivity returns.

7. **It can publish autonomous health reports over MeshCore.** A configured private channel can receive a daily status report from the device, providing basic health and connectivity visibility even when no MQTT backend is reachable.

8. **It can use MQTT as an alternate messaging path when the RF mesh is unavailable.** Direct messages and channel messages can be submitted through the authorized MQTT interface for transmission/delivery through the connected MECON infrastructure. MQTT augments MeshCore messaging; it does not change the MeshCore RF protocol or make normal mesh operation dependent on a backend.

These behaviours are the public product target. The private development firmware demonstrates much of the machinery and informs the implementation, but the public repository's contracts and tests are authoritative for the refactor.

## Design principles

- **MeshCore first:** loss of Wi-Fi, MQTT, a MECON-compatible backend or Internet connectivity must never prevent the device from performing its underlying MeshCore role.
- **Local first, cloud second:** use nearby infrastructure when available and fall back to cloud connectivity when it is not.
- **Transport-independent operations:** MQTT, USB and BLE are transports for the same logical capabilities, not separate product implementations.
- **Backend-neutral:** MeshContinuum is the reference implementation, not a required service.
- **Explicit capabilities:** software must discover what a device/role supports rather than infer it from a product name or firmware version.
- **Resource-aware:** Heltec-class devices have constrained RAM. Connectivity features must be designed around measured resource limits rather than assuming desktop/server behaviour.
- **Upstream-friendly:** MECON functionality should live in a shared integration layer around upstream MeshCore roles, minimizing the patch surface required when MeshCore advances.

## Target architecture

```text
meshcore-dev/MeshCore
        │
        ├── Companion role ──┐
        │                    ├── shared MECON runtime
        └── Repeater role ───┘       │
                                     ├── Wi-Fi ×3 profiles
                                     ├── local-first MQTT
                                     ├── USB
                                     └── BLE (where supported)
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