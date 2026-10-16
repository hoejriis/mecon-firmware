# mecon-firmware

## Keep your MeshCore radio independent. Add connected management.

**mecon-firmware** is the planned open firmware distribution for MeshContinuum (MECON). It extends MeshCore Companion and Repeater roles with observations, configuration and messaging through the transports each device supports.

> **Status — 15 October 2026:** this public repository contains specifications and target contracts, **not firmware source or downloadable builds**. The first public firmware will be **mecon-firmware 0.8.0**, built on upstream MeshCore 1.18.0; no release date is announced. A separate private beta (the 0.9x reference firmware) informs the design. [Current capabilities and hardware maturity](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md).

## Clean 1.18 refounding: standalone first

The public firmware is a **fresh build on MeshCore 1.18.0**, not a port of the private beta. Its first milestone improves stock MeshCore independently of MECON services: platform performance and portability; Normal/Hardened security and duty-cycle enforcement; prioritized Wi-Fi profiles (target ten); multi-broker MQTT with **priority failover, not parallel connections**; device status UI with Wi-Fi/BLE mode switching and USB management; adaptive contact capacity protecting favourites; and one CLI exposed through USB, BLE, MQTT and LoRa, with `mecon_*` extensions through the upstream hook.

The private MVP is inspiration, not the architectural baseline. Where a clean firmware design requires later Backend changes, the firmware design takes precedence. **MECON-specific enrollment, Deployment, signed management, connected observations, OTA and Backend/Reader integrations are later stages**, not prerequisites for the standalone foundation.

See the [ranked standalone foundation and acceptance plan](docs/CLEAN_REFOUNDING_1_18.md). The rest of this README and the linked product contracts retain the broader MECON vision.

## What it adds

- **Remote visibility:** feed radio observations and device health into a compatible Backend.
- **Manage supported devices:** configure and update through explicit, authorized device capabilities.
- **Keep local radio use:** normal MeshCore operation remains independent of MECON services.
- **Choose your integration:** public, versioned device contracts can be implemented by other Backends and Readers. mecon.cloud is not a required dependency.

These are product goals with differing beta maturity, not a promise that every board and transport supports every feature today.

## Who is it for?

MeshCore operators who want a connected Companion or a manageable Repeater, hardware testers willing to record real results, and developers building compatible Readers or Backends.

For the web Reader and overall product, start with **[MeshContinuum](https://github.com/hoejriis/MeshContinuum)**. First external testing is expected on invitation-only **[mecon.cloud](https://mecon.cloud)**; no opening date is announced.

## Hardware and release path

The first public release, **mecon-firmware 0.8.0**, is planned to ship four roles on upstream MeshCore 1.18.0 (released 15 October 2026): the Heltec V3 and V4 Companion Gateway, the SenseCAP T1000-E BLE Companion, the Seeed Wio Tracker L1 RF repeater and the ESP32 Repeater Observer (Heltec V3 and V4; the V4 build is new in 0.8.0). Support is granted per board/revision, role and build after hardware acceptance, and the T1000-E and Wio roles are limited in scope. Broader portability remains experimental.

The public implementation is gated on a pinned revision of MeshCore 1.18.0 and the existing firmware-first migration programme. [Roadmap](https://github.com/hoejriis/MeshContinuum/blob/main/docs/ROADMAP.md) · [Hardware target](docs/SUPPORTED_HARDWARE.md).

The public 0.8.x line and the private 0.9x line are **separate lines**: version numbers do not order across them, so a lower public number is not a downgrade from a private build.

**What the private beta does today (0.90 series):** Deployment-signed management on every transport; Normal and Hardened security posture; signed over-the-air updates for the Heltec gateways, including relay over a LoRa hop, and a staged LoRa image on the Wio repeater; signed uplink from the gateways; 50-contact Heltec gateways; a signed status beacon; and duty-cycle limits on transmit airtime. It speaks the legacy wire vocabulary (`gateway_id`, `deimos/gw/…`); the vocabulary in this repository's contracts is the target. See [Current-to-target adjustments](docs/CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line).

No public flash command is provided before accepted artifacts exist. Compilation alone is not a hardware-support claim.

**Hosted beta:** the boards and roles that have passed real-hardware tests for the first hosted beta are listed in [Beta boards](docs/BETA_BOARDS.md): today the Heltec V3 and V4 Companion gateways (beta firmware 0.90.5), with the T1000-E and the Wio repeater held back. To see the Reader in use, the [MeshContinuum demonstration](https://github.com/hoejriis/MeshContinuum#one-reader-more-reception) shows two Heltec gateways with synthetic data; it is not a claim about any other board or role.

## Get involved

- [Express beta interest](https://github.com/hoejriis/MeshContinuum/issues/new?template=beta-interest.yml) with your use case and board; this is not a guaranteed invitation.
- [Report a documentation or contract problem](https://github.com/hoejriis/mecon-firmware/issues/new?template=feedback.yml).
- [Offer hardware evidence](https://github.com/hoejriis/mecon-firmware/issues/new?template=hardware-report.yml) for an authorized test build.
- Star or Watch the repository to follow development.

Do not post private keys, provisioning exports or private message contents.

## Documentation

- [Documentation index](docs/README.md)
- [Detailed product target](docs/PRODUCT_TARGET.md) — preserved runtime, UI and upstream requirements
- [Implementation specification](docs/FIRMWARE_IMPLEMENTATION_SPEC.md)
- [Canonical device contracts](docs/contract/README.md)
- [Supported hardware target](docs/SUPPORTED_HARDWARE.md)
- [Beta boards](docs/BETA_BOARDS.md) — what the hosted beta offers, tested on real hardware
- [Versioning and release gates](docs/VERSIONING_AND_RELEASES.md)
- [Security model](docs/SECURITY_MODEL.md)
- [Contributing](CONTRIBUTING.md) · [Security reporting](SECURITY.md)

## License and upstream

This repository uses the [MIT licence](LICENSE), continuing the established firmware licensing. MeshContinuum's Backend/Reader uses Apache-2.0 separately. [Provenance](NOTICE.md).

MeshCore supplies the radio protocol and native Companion/Repeater behavior. MECON is an independent extension, not an official MeshCore service.
