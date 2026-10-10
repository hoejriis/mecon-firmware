# mecon-firmware

## Keep your MeshCore radio independent. Add connected management.

**mecon-firmware** is the planned open firmware distribution for MeshContinuum (MECON). It extends MeshCore Companion and Repeater roles with observations, configuration and messaging through the transports each device supports.

> **Status — 9 October 2026:** this public repository contains specifications and target contracts, **not firmware source or downloadable builds**. A separate private beta informs the design. [Current capabilities and hardware maturity](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md).

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

Heltec **V3 and V4**, each in Companion and Repeater roles, are the intended public release baseline. Support is granted per board/revision, role and build after hardware acceptance. SenseCAP T1000-E and broader portability remain experimental.

The public implementation is gated on a pinned released MeshCore 1.18 baseline and the existing firmware-first migration programme. [Roadmap](https://github.com/hoejriis/MeshContinuum/blob/main/docs/ROADMAP.md) · [Hardware target](docs/SUPPORTED_HARDWARE.md).

No public flash command is provided before accepted artifacts exist. Compilation alone is not a hardware-support claim.

**Hosted beta:** the boards and roles that have passed real-hardware tests for the first hosted beta are listed in [Beta boards](docs/BETA_BOARDS.md): today the Heltec V3 and V4 Companion gateways, with the T1000-E experimental. To see the Reader in use, the [MeshContinuum demonstration](https://github.com/hoejriis/MeshContinuum#one-reader-more-reception) shows two Heltec gateways with synthetic data; it is not a claim about any other board or role.

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
