# mecon-firmware documentation

## Start with availability

- [Shared capability and maturity matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md)
- [Hosted beta expectations](https://github.com/hoejriis/MeshContinuum/blob/main/docs/GETTING_STARTED.md)
- [Staged release roadmap](https://github.com/hoejriis/MeshContinuum/blob/main/docs/ROADMAP.md)
- [Detailed product target](PRODUCT_TARGET.md)

There is no public firmware download or source build yet. Public releases start at **0.1.0**, a standalone enhanced MeshCore on 1.18.0 (see [Clean refounding on MeshCore 1.18](CLEAN_REFOUNDING_1_18.md)); the first version to serve a MECON fleet is planned as 0.8.0 (see [Versioning and releases](VERSIONING_AND_RELEASES.md)). Documents below specify the target rather than certifying implementation or hardware readiness.

These documents describe the **complete MECON 1.0 public target**, not merely differences from stock MeshCore and not the temporary state of the private development repositories. The public line is built on **upstream MeshCore 1.18.0** (released 2026-10-15) from `main`; a specific revision is pinned for each public release, and `dev` is investigation/validation only. The target vocabulary (`device_id`, `{namespace}/…` topics) differs in places from what the private 0.9x reference firmware speaks today; [Current-to-target adjustments](CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line) states the shipped behaviour.

"The private MVP" in these documents is the earlier name for the private reference firmware, now the 0.9x line.

The documentation has two jobs:

1. A developer must be able to implement the firmware anew from released MeshCore 1.18 without reading the private MVP source.
2. A backend developer must be able to identify every change required to move the current MeshContinuum backend/Reader from the private MVP to MECON 1.0.

Firmware requirements belong here and in `contract/`. **Backend migration work caused by differences from the private firmware belongs in `BACKEND_MIGRATION.md`.** Do not preserve legacy firmware architecture in the normative contract merely to avoid backend changes.

## Start here

- [Complete MECON 1.0 firmware implementation specification](FIRMWARE_IMPLEMENTATION_SPEC.md) — functional checklist for rebuilding the firmware.
- [Private MVP → MECON 1.0 contract audit](LEGACY_CONTRACT_AUDIT.md) — exhaustive disposition of functionality from the five historical firmware contracts: keep, reuse MeshCore 1.18, change, retire or verify after 1.18 release.
- [Backend migration from the private MVP](BACKEND_MIGRATION.md) — MeshContinuum backend/Reader changes required by the clean public firmware.
- [Public device contract](contract/README.md) — normative device-facing protocol and behaviour.

## Product and architecture

- [Why mecon-firmware](WHY_MECON_FIRMWARE.md)
- [Architecture](ARCHITECTURE.md)
- [Supported hardware and roles](SUPPORTED_HARDWARE.md)
- [Beta boards](BETA_BOARDS.md): what the hosted beta offers, tested on real hardware
- [Connectivity and operating modes](CONNECTIVITY.md)
- [Integration guide](INTEGRATION.md)
- [Security model](SECURITY_MODEL.md)
- [Standalone security postures](STANDALONE_POSTURES.md): Normal and Hardened without a Deployment
- [Wi-Fi profiles](WIFI.md): up to ten prioritised networks, failover and return without flapping
- [Connectivity mode](CONNECTIVITY_MODE.md): BLE or Wi-Fi one at a time, switched by command or by the device button, with a status screen
- [Adaptive contact memory](CONTACTS.md): contacts in a persistent store on the file system with a RAM cache sized from the free heap, favourites resident, eviction of the least meaningful non-favourite, atomic saves
- [MQTT broker profiles](MQTT.md): up to six brokers (static or discovered on the LAN), TLS with a pinned certificate, failover by priority
- [Upstream and porting](UPSTREAM_AND_PORTING.md)
- [Versioning and releases](VERSIONING_AND_RELEASES.md)

## Public integration contract

- [Contract index](contract/README.md)
- [Core device and health contract](contract/CORE_DEVICE_CONTRACT.md)
- [MQTT/network contract](contract/MQTT_PROTOCOL.md)
- [Direct USB/BLE contract](contract/DIRECT_READER_PROTOCOL.md)
- [Settings contract](contract/DEVICE_SETTINGS.md)
- [Security and authority contract](contract/SECURITY_AND_AUTHORITY.md)

The contract documents are normative for MECON 1.0. They describe **all device-visible functionality required by the implementation specification**, including inherited/reused MeshCore functionality where a MECON backend must know how to invoke or interpret it. Where MeshCore 1.18 already supplies a suitable native operation, MECON reuses it rather than defining a duplicate operation.

## Migration/reference material

- [Backend migration](BACKEND_MIGRATION.md) is the actionable backend-side delta from deployed private firmware to MECON 1.0.
- [Legacy contract audit](LEGACY_CONTRACT_AUDIT.md) is the traceability matrix proving historical private requirements have a public disposition.
- [Current-to-target contract adjustments](CONTRACT_MIGRATION_NOTES.md) is non-normative engineering/reference material explaining legacy inconsistencies and why the public target differs.

The private `deimos-mesh-firmware` implementation and historical contracts were used as evidence for the audit, but every required product behaviour is now required to live here. **A requirement that exists only in private source or private documentation is considered undocumented for MECON 1.0.** Shipped behaviour of the reference firmware that these contracts do not yet specify (the signed management wrapper, the USB-unlock secret, signed uplink, the compact RF line, repeater posture, LoRa-relayed OTA, duty-cycle limits) is summarised in the adjustments note and is not yet normative here.

The remaining `VERIFY 1.18` entries are not permission to consult private code during implementation. They mean the product requirement is known but its exact native-vs-MECON implementation mapping must wait for the mapping inventory against the released MeshCore 1.18.0, which has not been completed.