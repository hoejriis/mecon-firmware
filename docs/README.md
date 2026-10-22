# mecon-firmware documentation

mecon-firmware is MeshCore 1.18.0 with standalone improvements; it needs no backend. Start with the [project README](../README.md), which lists the differences from stock MeshCore and what is verified how. Everything built so far is verified on simulated boards and by unit tests only; no hardware test has been run. Documents are labelled either **built** (describes code in this repository) or **target** (a design for the later MECON-integrated stage, not implemented).

## Using the firmware (built)

- [CLI reference](CLI_REFERENCE.md): every stock and MECON command, who may run it, and from where
- [Screens and buttons](UI.md)
- [Getting started](../README.md#getting-started) and [status and hardware](../README.md#status-and-hardware)

## Reference (built)

- [Command-line engine](CLI.md): one privilege table for every way a command reaches the device (USB, BLE, LoRa, MQTT), and the command line over MQTT
- [Standalone security postures](STANDALONE_POSTURES.md): Normal and Hardened, the audit log, recovery
- [Wi-Fi profiles](WIFI.md): up to ten prioritised networks, failover and return without flapping
- [MQTT broker profiles](MQTT.md): up to six brokers (static or discovered on the LAN), TLS with a pinned certificate or a CA, failover by priority
- [Connectivity mode](CONNECTIVITY_MODE.md): BLE or Wi-Fi one at a time, switched by command or by the device button, with a status screen
- [Adaptive contact memory](CONTACTS.md): a persistent contact store with a RAM cache sized from the free heap, favourites resident, atomic saves
- [Supported hardware and roles](SUPPORTED_HARDWARE.md): the target role matrix (what is intended, not what is verified)
- [Hardware-verified matrix](HARDWARE_VERIFIED.md): what has been checked on real boards, on which board and build, and what is only compiled

Upstream MeshCore's own protocol and CLI documents ([companion protocol](companion_protocol.md), [CLI commands](cli_commands.md), [packet format](packet_format.md), the FAQ and others in this folder) are unchanged except where this firmware changes behaviour.

## Developing (built)

- [Simulated-board test harness](SIMULATION.md): how to run the suite, what it can and cannot show, the mutation check
- [Upstream delta register](UPSTREAM_DELTA.md): every change to a file that exists upstream
- [Versioning and releases](VERSIONING_AND_RELEASES.md): the version string a build reports, release gates
- [Upstream and porting](UPSTREAM_AND_PORTING.md)
- [NimBLE evaluation](NIMBLE_EVALUATION.md): a NimBLE build option for the ESP32 BLE Companion, its size beside Bluedroid, and what is not yet verified on hardware
- [Clean refounding on MeshCore 1.18](CLEAN_REFOUNDING_1_18.md): the staged plan. Stage A (standalone) is what the sections above document; Stage B (MECON) is later

## MECON integration (target; none of it is implemented here)

These documents specify the optional, later stage in which a device is enrolled in a MECON deployment and managed by a backend. The first version planned to serve a MECON fleet is 0.8.0, with no date. The vocabulary may differ from what the private 0.9x reference firmware speaks today; [Current-to-target adjustments](CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line) states the shipped behaviour of that private firmware. The public 0.1.x line and the private 0.9x line are separate lines whose version numbers do not order against each other.

- [Product target](PRODUCT_TARGET.md) and [why mecon-firmware](WHY_MECON_FIRMWARE.md)
- [Architecture](ARCHITECTURE.md), [connectivity and operating modes](CONNECTIVITY.md), [security model](SECURITY_MODEL.md), [non-ESP portability](NON_ESP_PORTABILITY.md)
- [Complete MECON 1.0 firmware implementation specification](FIRMWARE_IMPLEMENTATION_SPEC.md): functional checklist
- [Integration guide](INTEGRATION.md)
- [Shared capability and maturity matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md), [staged roadmap](https://github.com/hoejriis/MeshContinuum/blob/main/docs/ROADMAP.md)
- [Beta boards](BETA_BOARDS.md): what the separate private hosted beta offers, tested on real hardware (not this repository's builds)

### Public integration contract (target)

- [Contract index](contract/README.md)
- [Core device and health contract](contract/CORE_DEVICE_CONTRACT.md)
- [MQTT/network contract](contract/MQTT_PROTOCOL.md)
- [Direct USB/BLE contract](contract/DIRECT_READER_PROTOCOL.md)
- [Settings contract](contract/DEVICE_SETTINGS.md)
- [Security and authority contract](contract/SECURITY_AND_AUTHORITY.md)

The contract documents are normative for MECON 1.0. They describe **all device-visible functionality required by the implementation specification**, including inherited/reused MeshCore functionality where a MECON backend must know how to invoke or interpret it. Where MeshCore 1.18 already supplies a suitable native operation, MECON reuses it rather than defining a duplicate operation.

### Migration and traceability (target)

- [Backend migration from the private MVP](BACKEND_MIGRATION.md): MeshContinuum backend/Reader changes required by the clean public firmware. Backend migration work caused by differences from the private firmware belongs there.
- [Private MVP to MECON 1.0 contract audit](LEGACY_CONTRACT_AUDIT.md): disposition of the functionality of the historical private contracts.
- [Current-to-target contract adjustments](CONTRACT_MIGRATION_NOTES.md): non-normative engineering notes on why the public target differs.

The private `deimos-mesh-firmware` implementation and historical contracts were used as evidence for the audit, but every required product behaviour is now required to live here. **A requirement that exists only in private source or private documentation is considered undocumented for MECON 1.0.** Shipped behaviour of the reference firmware that these contracts do not yet specify (the signed management wrapper, the USB-unlock secret, signed uplink, the compact RF line, repeater posture, LoRa-relayed OTA, duty-cycle limits) is summarised in the adjustments note and is not yet normative here.

The remaining `VERIFY 1.18` entries are not permission to consult private code during implementation. They mean the product requirement is known but its exact native-vs-MECON implementation mapping must wait for the mapping inventory against the released MeshCore 1.18.0, which has not been completed.
