# Versioning and releases

Public mecon-firmware releases start at **0.1.0** and are built on upstream **MeshCore 1.18.0**, released on 2026-10-15 (the `companion`, `repeater` and `room-server` v1.18.0 tags, upstream commit 81dbae5). A specific upstream revision is pinned for each public release. Development against the moving `dev` branch does not satisfy the release gate. "MECON target" in these documents names the complete target contract that the public line works towards. It is not a release, a version number or a date.

## Two version lines

The public mecon-firmware line (from 0.1.0) and the private reference line (0.9x, currently the 0.90 series) are **separate lines**. Their numbers do not order across each other: a public 0.x.y is not an upgrade or a downgrade relative to a private 0.90.x build, and tooling must not compare them. A device compares versions only within the line it runs, and a managed update refuses to start unless the running version is at least the manifest's minimum-from-version.

The target contract reports four identities independently:

- `mecon_firmware_version` (target name; not emitted by any build in this repository);
- `meshcore_base_version` — exact pinned upstream 1.18-derived revision (target name; not emitted);
- MECON contract/profile versions;
- hardware target, role and build variant (`hardware_target` and `build_variant` are target names; not emitted).

The private 0.9x firmware reports the same four identities today: `mecon_firmware_version`, `meshcore_base_version` (currently a 1.17.1-derived value), `hardware_target` / `build_variant` / `role`, and `mecon_mqtt_protocol_version` / `cmd_protocol_version` / `config_protocol_version` / `profiles`. Its firmware version is the `git describe` of a `mecon-v*` tag, so a development build describes as a count after the base tag and sorts after that tag. The device interface version on `main` is `1.0`, reported by `hello` (see below).

Clients gate protocol behaviour on contract/profile versions and capabilities, not firmware release numbers.

*Built today, on `main`:* the firmware reports one string that carries the firmware tag and the upstream base (next section). The device interface `hello` result also reports `interface` (the interface version, `1.0`), `upstream` (the upstream base), `hardware` and `role`, next to `device_id`, `node_key`, `firmware`, `epoch` and `capabilities` ([DEVICE_INTERFACE](contract/DEVICE_INTERFACE.md)). The names `mecon_firmware_version`, `meshcore_base_version`, `hardware_target` and `build_variant` above are **not emitted** by any build in this repository, and neither are separate contract/profile version fields beyond `interface`. The tagged releases v0.1.0 to v0.1.2 have no `hello`.

## The version string a build reports

One source: the build script `bin/mecon_version.py` (a PlatformIO `pre:` script on every platform base and the simulator base) passes `git describe --tags --match 'v[0-9]*' --dirty` as `-D MECON_VERSION`. `src/helpers/MeconVersion.h` turns it into two strings. The upstream base stays first, so an app that reads the leading `v1.18.0` still does.

| `git describe` | Full (`ver`, `get mecon.version`) | Short (device-info frame, display) |
|---|---|---|
| `v0.1.2` (clean tag build) | `v1.18.0-mecon.0.1.2` | `v1.18.0-mecon.0.1.2` |
| `v0.1.2-5-gabc1234` (5 commits after the tag) | `v1.18.0-mecon.0.1.2+5.gabc1234` | `v1.18.0-mecon.0.1.2` |
| `v0.1.2-5-gabc1234-dirty` | `v1.18.0-mecon.0.1.2+5.gabc1234.dirty` | `v1.18.0-mecon.0.1.2` |
| `v0.1.2-dirty` | `v1.18.0-mecon.0.1.2+dirty` | `v1.18.0-mecon.0.1.2` |
| no git or no `v*` tag (source tarball) | `v1.18.0-mecon.unknown` | `v1.18.0-mecon.unk` |

- The Companion device-info frame has a 20-byte version field (19 characters and the terminator), so it carries the short form, cut at 19 characters. A release tag of the form `v0.1.2` fills it exactly; a tag with two-digit components is cut. The frame does not grow.
- `ver` prints the full form on the Companion, Repeater, Room server and Sensor. `get mecon.version` answers `> <full> (describe <git describe>, upstream <base>)`.
- `FIRMWARE_VER_CODE` (the Companion protocol level, currently 14) is a separate number and does not change with the version string.
- A build with `-D FIRMWARE_VERSION=...` still overrides the string; `build.sh` no longer does, so release artifacts take their version from the tag they are built from.
- A development or dirty build carries `+N.g<hash>` or `+dirty` in the full form, so it cannot be mistaken for a release artifact.

## Release artifacts

*Target.* The releases published so far (0.1.0, 0.1.1, 0.1.2) are source-only pre-releases with release notes and no binaries, manifest or signatures. Each supported board/role publishes flash/OTA artifacts, machine-readable manifest, digest/signature metadata, hardware/role/variant identifiers, exact MeshCore base, MECON contract versions and measured resource limits such as contact capacity.

## Release gate

Before a release is promoted from pre-release or advertises a board (target; no release has passed it): the pinned MeshCore 1.18.0 baseline must be proven on V3/V4; the IDF5/NimBLE migration must preserve stock-derived behaviour (done on `main` for V3 and V4, with the rows in [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md)); and each advertised board/role must pass the hardware gate with realistic persisted state.

## OTA

*Planned (Issue #110, signed updates over IP, USB and BLE; LoRa image delivery is Issue #123).* Managed OTA validates signed metadata and hardware/role/variant compatibility. USB remains the recovery path. No managed update is built in this repository yet.

The private 0.9x firmware validates an Ed25519-signed manifest (with a key identifier and a minimum-from-version), reverts an update that is not confirmed, and confirms with a signed `ota/ack` (capability `ota_ack_signed`). ESP32 gateways can take a full image relayed over one LoRa hop (1,228-byte chunks, a 10 % hourly airtime budget, a window capped at one day); the Wio repeater stages an image without activating it. A public build supplies its own trust anchors.

## Development builds

Builds based on upstream `dev`, dirty trees or unpinned revisions identify themselves as development builds and must not be mistaken for release artifacts.

## Public availability

The public releases so far are source-only pre-releases (0.1.0, 0.1.1, 0.1.2) with no binaries. They are on ESP-IDF 4.4 and have no device interface; `main` is ahead of them. Release timing is a separate decision and no later version number is planned here.

1. MeshCore 1.18.0 pinned (upstream commit 81dbae5) as this repository's baseline.
2. **0.1.0**, the first public release: the pinned baseline plus closed gaps in stock 1.18 (unauthenticated management paths, secret read-back, storage integrity, packet bounds), verified on simulated boards. 0.1.1 added duty-cycle enforcement and postures, and 0.1.2 the Wi-Fi and MQTT controllers. Work merged since (ESP-IDF 5.3 and NimBLE, the device interface, the unified CLI) is on `main` and not yet tagged. The approved programme is in [Clean refounding on MeshCore 1.18](CLEAN_REFOUNDING_1_18.md).
3. Backend support for the public firmware's interfaces follows the firmware; it is not a precondition for it.

The public firmware speaks the **v1 vocabulary** in `contract/` only; it does not emit the legacy vocabulary of the private 0.9x firmware. A private release number is not a public download or evidence that its hardware gates passed.

In the private line a release is published as a pre-release first and promoted only after a hardware bench; promotion, not publication, marks it ready for use.

The [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) is the public maturity reference.
