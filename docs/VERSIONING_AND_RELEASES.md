# Versioning and releases

The first public firmware, **mecon-firmware 0.8.0**, is built on upstream **MeshCore 1.18.0**, released on 2026-10-15 (the `companion`, `repeater` and `room-server` v1.18.0 tags, upstream commit 81dbae5). A specific upstream revision is pinned for each public release. Development against the moving `dev` branch does not satisfy the release gate. "MECON 1.0" in these documents names the complete target contract that the public line works towards.

## Two version lines

The public mecon-firmware line (0.8.x) and the private reference line (0.9x, currently the 0.90 series) are **separate lines**. Their numbers do not order across each other: a public 0.8.0 is not an upgrade or a downgrade relative to a private 0.90.x build, and tooling must not compare them. A device compares versions only within the line it runs, and a managed update refuses to start unless the running version is at least the manifest's minimum-from-version.

Four identities are reported independently:

- `mecon_firmware_version`;
- `meshcore_base_version` — exact pinned upstream 1.18-derived revision;
- MECON contract/profile versions;
- hardware target, role and build variant.

The private 0.9x firmware reports the same four identities today: `mecon_firmware_version`, `meshcore_base_version` (currently a 1.17.1-derived value), `hardware_target` / `build_variant` / `role`, and `mecon_mqtt_protocol_version` / `cmd_protocol_version` / `config_protocol_version` / `profiles`. Its firmware version is the `git describe` of a `mecon-v*` tag, so a development build describes as a count after the base tag and sorts after that tag. A proposed device-operations version is emitted by no build.

Clients gate protocol behaviour on contract/profile versions and capabilities, not firmware release numbers.

## Release artifacts

Each supported board/role publishes flash/OTA artifacts, machine-readable manifest, digest/signature metadata, hardware/role/variant identifiers, exact MeshCore base, MECON contract versions and measured resource limits such as contact capacity.

## Release gate

Before a public release: the pinned MeshCore 1.18.0 baseline must be proven on V3/V4; the IDF5/NimBLE migration must preserve stock-derived behaviour; and each advertised board/role must pass the hardware gate with realistic persisted state.

## OTA

Managed OTA validates signed metadata and hardware/role/variant compatibility. USB remains the recovery path.

The private 0.9x firmware validates an Ed25519-signed manifest (with a key identifier and a minimum-from-version), reverts an update that is not confirmed, and confirms with a signed `ota/ack` (capability `ota_ack_signed`). ESP32 gateways can take a full image relayed over one LoRa hop (1,228-byte chunks, a 10 % hourly airtime budget, a window capped at one day); the Wio repeater stages an image without activating it. A public build supplies its own trust anchors.

## Development builds

Builds based on upstream `dev`, dirty trees or unpinned revisions identify themselves as development builds and must not be mistaken for MECON 1.0 release artifacts.

## Staged migration and public availability

No public release artifacts exist in this repository yet. Follow the [shared roadmap](https://github.com/hoejriis/MeshContinuum/blob/main/docs/ROADMAP.md):

1. Released MeshCore 1.18.0, a revision pinned, stock behaviour proven. MeshCore 1.18.0 was released on 2026-10-15, so the "wait for 1.18" gate is open; the pin and the stock-behaviour proof are still to do.
2. **mecon-firmware 0.8.0**, the first public release, shipping four roles: Heltec V3/V4 Companion Gateway, SenseCAP T1000-E BLE Companion, Seeed Wio Tracker L1 RF repeater and ESP32 Repeater Observer. No date is announced.
3. Backend compatibility and fleet migration, public Backend refounding, and validation against that Backend, in the order the shared roadmap sets.

Which wire vocabulary the first public release speaks (the legacy one the private 0.9x firmware speaks today, or the v1 target vocabulary in `contract/`) is a roadmap decision; until it is published, treat the v1 vocabulary as the target and the legacy vocabulary as shipped reference behaviour. A private release number is not a public download or evidence that its hardware gates passed.

In the private line a release is published as a pre-release first and promoted only after a hardware bench; promotion, not publication, marks it ready for use.

The [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) is the public maturity reference.
