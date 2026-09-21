# mecon-firmware

**mecon-firmware** is an open MeshCore firmware distribution for Heltec V3 and V4 devices. It preserves normal MeshCore Companion and Repeater operation while adding resilient IP connectivity, remote management, observations and messaging through Wi-Fi, MQTT, USB and BLE.

It is the sister project of [MeshContinuum](https://github.com/hoejriis/MeshContinuum), but it is **not coupled to MeshContinuum**. Any backend, broker or Reader may integrate with mecon-firmware by implementing the public, versioned contracts in this repository.

> This repository currently defines the public release target. The existing private MVP is an implementation reference, not source to be copied wholesale. The public firmware will be refactored here against the documented behaviour and contracts.

## Upstream dependency for MECON 1.0

**MECON 1.0 depends on MeshCore 1.18.** Development work may study and validate against the upstream MeshCore `dev` branch while 1.18 is being prepared, but the production MECON 1.0 implementation will be based on **MeshCore 1.18 only after that release has been merged into upstream `main`**. Until that happens, the upstream firmware baseline is considered unresolved and MECON 1.0 cannot complete its foundational fork/refactor.

This dependency is intentional. The 1.18 lineage contains substantial upstream work that overlaps areas MECON needs, including Wi-Fi infrastructure, runtime configuration, Companion/Repeater command handling, board-specific preferences, remote CLI/message primitives, scoping/subscription work, UI evolution and Heltec V3/V4 support. MECON should extend those upstream facilities rather than recreate their older 1.17-era equivalents from the private MVP.

Once MeshCore 1.18 is on `main`, MECON will pin its initial upstream baseline to a specific 1.18 commit/tag and perform the clean public refactor from there. The private MVP remains a behavioural and test reference, not the source-code baseline.

## What MECON changes compared with stock MeshCore

MECON is deliberately an extension of MeshCore, not a replacement for it. A device should remain useful as an ordinary MeshCore node even when every MECON service is unavailable.

1. **It remains a normal MeshCore Companion or Repeater.** RF operation, mesh participation and the native role continue without Wi-Fi, MQTT, a backend or Internet connection. MECON uses a deliberately smaller Companion contact table than stock configurations may allow, trading contact capacity for enough RAM to run the additional connectivity and management services reliably. Managed/protected contacts must survive normal eviction.

2. **It can store three ordered Wi-Fi profiles.** The device automatically tries configured networks in their entered priority order and reconnects as availability changes. Losing Wi-Fi must not interrupt the underlying MeshCore role. MECON should extend the MeshCore 1.18 Wi-Fi/configuration infrastructure rather than carry forward the private MVP's parallel Wi-Fi implementation.

3. **It adds local-first MQTT connectivity.** A device may be provisioned with MQTT credentials and authority. When a compatible broker is advertised on the local network, the device prefers that local path; otherwise it uses its configured cloud broker. Local broker discovery is automatic, while credentials and permitted capabilities remain provisioned configuration. Only one MQTT session is active at a time, reducing RAM pressure on constrained hardware.

4. **It exposes one management and observation model over MQTT, USB and BLE.** Packet observations, device events, logs, status, inventory, configuration and supported actions are available through the applicable transports using the same logical, versioned MECON contracts. Companion devices support direct USB/BLE operation; Repeaters support direct USB, with BLE support dependent on the advertised hardware/role capability. Native MeshCore 1.18 operations, CLI/configuration primitives and transport-independent behaviour should be reused rather than duplicated where they already provide the required behaviour.

5. **It supports managed OTA firmware updates.** Firmware can be updated remotely through the MECON management path, including MQTT-triggered OTA. Releases use manifests, integrity checks and the device's supported OTA trust model rather than treating an arbitrary URL as permission to install firmware.

6. **It can use the mesh itself as an outage path.** When MQTT/IP connectivity is unavailable, selected direct-message traffic can be relayed into a configured private MeshCore channel so loss of the Internet path does not necessarily mean loss of communication. The exact forwarding rules must prevent loops and duplicate delivery when IP connectivity returns.

7. **It can publish autonomous health reports over MeshCore.** A configured private channel can receive a daily status report from the device, providing basic health and connectivity visibility even when no MQTT backend is reachable.

8. **It can use MQTT as an alternate messaging path when the RF mesh is unavailable.** Direct messages and channel messages can be submitted through the authorized MQTT interface for transmission/delivery through the connected MECON infrastructure. MQTT augments MeshCore messaging; it does not change the MeshCore RF protocol or make normal mesh operation dependent on a backend.

These behaviours are the public product target. The private development firmware demonstrates much of the machinery and informs the implementation, but the public repository's contracts and tests are authoritative for the refactor.

## Device UI for 1.0

MECON 1.0 deliberately keeps the physical user interface as close to the corresponding stock MeshCore Companion or Repeater firmware as possible. MECON should add useful connectivity information without turning the device display into a separate management dashboard.

- **Boot identity:** show `MECON <version>` during boot as the firmware identity. Do not add a build date to the boot display.
- **Existing front screen:** retain the stock Companion/Repeater front-screen layout and behaviour as far as possible, adding compact connection status for **Wi-Fi**, **MQTT** and **BLE**.
- **BLE PIN:** when BLE is available but not connected, the front screen must expose the device's pairing PIN. Once BLE is connected, show connected state instead of the PIN.
- **No additional MECON screen in 1.0:** there is no secondary connectivity/diagnostic screen. Details such as IP address, RSSI, broker identity, uptime and extended diagnostics belong on the management transports rather than in the 1.0 device UI.
- **Message wake/display filtering:** preserve the private-MVP behaviour: direct messages and messages on favourited channels may wake/show on the display. Public and unfavourited-channel traffic continues to be processed normally but does not wake the display merely because MECON observed it.
- **Buttons:** MECON 1.0 introduces no new normal button semantics. Preserve the stock Companion/Repeater button behaviour.
- **Framebuffer export:** retain framebuffer/display export through the applicable management/debug interface for automated testing and diagnostics. This is a management/test capability, not an additional user-facing screen.

The UI rule for 1.0 is therefore: **preserve the stock MeshCore 1.18 interaction model; add only MECON firmware identity, connectivity state and BLE pairing information, plus the established DM/favourite display filtering.**

## Modern runtime baseline

The public refactor deliberately modernizes the underlying ESP32 runtime **after establishing the released MeshCore 1.18 baseline and before the MECON feature layer is ported**. These are foundational choices, not later optimizations:

- **MeshCore 1.18 from upstream `main` is the source baseline.** The current upstream `dev` branch may be used for investigation and early compatibility work, but MECON 1.0 does not fork a moving development branch as its release foundation. The implementation gate is MeshCore 1.18 merged to `main`, after which a specific upstream revision is pinned.
- **pioarduino / Arduino-ESP32 3.x / ESP-IDF 5.x for Heltec V3 and V4.** The public firmware will not reproduce the private MVP's Arduino-ESP32 2.x / ESP-IDF 4.4 baseline. V3 and V4 should be brought up on the modern platform first, and ordinary 1.18-derived Companion and Repeater behaviour proven there before MECON functionality is layered on top.
- **NimBLE for Bluetooth.** BLE support should be based on NimBLE from the beginning rather than porting a legacy Bluedroid implementation and replacing it later. Wi-Fi, MQTT/TLS and BLE must be designed to coexist within the measured V3/V4 memory budget.
- **One MQTT client/session architecture.** Do not port the private MVP's historical concurrent-broker implementation. The target is one active MQTT session with local discovery/preference and cloud fallback.
- **Non-blocking networking and discovery.** DNS, mDNS, broker discovery, MQTT management, OTA and similar IP work must not block the MeshCore/radio main loop for multi-second waits. Local broker discovery should be asynchronous or incrementally polled and should not retain unnecessary responder/task memory between searches.
- **V3 and V4 are first-class targets from the start.** Common behaviour belongs in shared code, while hardware-specific USB, display, button, radio and board behaviour remains behind explicit target abstractions. V4 must not be treated as a later port of a V3-only MECON implementation.
- **Public MECON naming and contracts are native.** The new implementation should use the public MECON contract and naming directly. Private-MVP `deimos_*` compatibility identifiers are migration concerns, not the internal architecture of the public firmware.

The order matters. The intended bring-up is:

1. wait for MeshCore 1.18 to be merged/released on upstream `main`;
2. pin MECON 1.0 to a specific MeshCore 1.18 upstream revision;
3. prove unmodified stock MeshCore 1.18 Companion and Repeater operation on the target Heltec V3/V4 hardware;
4. migrate the Heltec V3/V4 targets to pioarduino / Arduino-ESP32 3.x / ESP-IDF 5.x;
5. establish NimBLE as the BLE implementation;
6. prove the 1.18-derived Companion and Repeater behaviour again on real V3 and V4 hardware;
7. measure and document the new memory/runtime baseline;
8. add the shared MECON runtime and transport adapters, extending native 1.18 facilities where possible;
9. extend upstream Wi-Fi support to the MECON three-profile model and add single-session local-first MQTT;
10. add outage/resilience behaviour and managed OTA.

The private MVP is useful evidence for required behaviour, failure modes and hardware constraints. It is **not** the architectural baseline to preserve when the clean public implementation can avoid known technical debt.

## Design principles

- **MeshCore first:** loss of Wi-Fi, MQTT, a MECON-compatible backend or Internet connectivity must never prevent the device from performing its underlying MeshCore role.
- **1.18 first:** MECON 1.0 extends released MeshCore 1.18 behaviour rather than recreating features from an older upstream baseline.
- **Local first, cloud second:** use nearby infrastructure when available and fall back to cloud connectivity when it is not.
- **Transport-independent operations:** MQTT, USB and BLE are transports for the same logical capabilities, not separate product implementations.
- **Backend-neutral:** MeshContinuum is the reference implementation, not a required service.
- **Explicit capabilities:** software must discover what a device/role supports rather than infer it from a product name or firmware version.
- **Resource-aware:** Heltec-class devices have constrained RAM. Connectivity features must be designed around measured resource limits rather than assuming desktop/server behaviour. Contact limits and other capacity reductions should be based on the measured modern-runtime budget rather than copied blindly from the private MVP.
- **Non-blocking by default:** network management must not compromise radio servicing or ordinary MeshCore behaviour.
- **Upstream-friendly:** MECON functionality should live in a shared integration layer around upstream MeshCore roles, minimizing the patch surface required when MeshCore advances.

## Target architecture

```text
meshcore-dev/MeshCore 1.18 (released on main; pinned revision)
        │
        ├── Companion role ──┐
        │                    ├── shared MECON runtime
        └── Repeater role ───┘       │
                                     ├── Wi-Fi ×3 profiles
                                     ├── local-first MQTT
                                     ├── USB
                                     └── NimBLE (where supported)
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