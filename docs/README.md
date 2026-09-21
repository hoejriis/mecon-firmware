# mecon-firmware documentation

These documents describe the **complete MECON 1.0 public target**, not merely differences from stock MeshCore and not the temporary state of the private development repositories. MECON 1.0 depends on **MeshCore 1.18 after it is merged/released on upstream `main`**; `dev` is investigation/validation only.

The documentation has two jobs:

1. A developer must be able to implement the firmware anew from released MeshCore 1.18 without reading the private MVP source.
2. A backend developer must be able to identify every change required to move the current MeshContinuum backend/Reader from the private MVP to MECON 1.0.

Firmware requirements belong here and in `contract/`. **Backend migration work caused by differences from the private firmware belongs in `BACKEND_MIGRATION.md`.** Do not preserve legacy firmware architecture in the normative contract merely to avoid backend changes.

## Start here

- [Complete MECON 1.0 firmware implementation specification](FIRMWARE_IMPLEMENTATION_SPEC.md) — functional checklist for rebuilding the firmware.
- [Backend migration from the private MVP](BACKEND_MIGRATION.md) — MeshContinuum backend/Reader changes required by the clean public firmware.
- [Public device contract](contract/README.md) — normative device-facing protocol and behaviour.

## Product and architecture

- [Why mecon-firmware](WHY_MECON_FIRMWARE.md)
- [Architecture](ARCHITECTURE.md)
- [Supported hardware and roles](SUPPORTED_HARDWARE.md)
- [Connectivity and operating modes](CONNECTIVITY.md)
- [Integration guide](INTEGRATION.md)
- [Security model](SECURITY_MODEL.md)
- [Upstream and porting](UPSTREAM_AND_PORTING.md)
- [Versioning and releases](VERSIONING_AND_RELEASES.md)

## Public integration contract

- [Contract index](contract/README.md)
- [Core device contract](contract/CORE_DEVICE_CONTRACT.md)
- [MQTT contract](contract/MQTT_PROTOCOL.md)
- [Direct USB/BLE contract](contract/DIRECT_READER_PROTOCOL.md)
- [Settings contract](contract/DEVICE_SETTINGS.md)
- [Security and authority contract](contract/SECURITY_AND_AUTHORITY.md)

The contract documents are normative for MECON 1.0. They must describe **all device-visible functionality required by the implementation specification**, including inherited/reused MeshCore functionality where a MECON backend must know how to invoke or interpret it. Where MeshCore 1.18 already supplies a suitable native operation, MECON reuses it rather than defining a duplicate operation.

## Migration/reference material

- [Backend migration](BACKEND_MIGRATION.md) is the actionable backend-side delta from deployed private firmware to MECON 1.0.
- [Current-to-target contract adjustments](CONTRACT_MIGRATION_NOTES.md) is non-normative engineering/reference material explaining legacy inconsistencies and why the public target differs.

The private `deimos-mesh-firmware` implementation and historical contracts may be consulted as evidence when filling a missing detail, but every required behaviour must ultimately be captured here. **A requirement that exists only in private source or private documentation is considered undocumented for MECON 1.0.**
