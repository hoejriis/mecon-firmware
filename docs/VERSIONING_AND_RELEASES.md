# Versioning and releases

mecon-firmware is built on upstream MeshCore 1.18.0 (commit `81dbae5`). Each release pins its upstream revision. A build on upstream's moving `dev` branch, or on an unpinned revision, is a development build.

## What a device reports

Clients gate on the interface version and capabilities, never on firmware release numbers. `hello` reports `interface` (the interface version), `upstream` (the MeshCore base), `firmware`, `hardware` and `role`, next to `device_id`, `node_key`, `epoch` and `capabilities` ([device interface](contract/DEVICE_INTERFACE.md)).

## The version string a build reports

One source: the build script `bin/mecon_version.py` (a PlatformIO `pre:` script on every platform base and the simulator base) passes `git describe --tags --match 'v[0-9]*' --dirty` as `-D MECON_VERSION`. `src/helpers/MeconVersion.h` turns it into two strings. The upstream base stays first, so an app that reads the leading `v1.18.0` still does.

| `git describe` | Full (`ver`, `get mecon.version`) | Short (device-info frame, display) |
|---|---|---|
| `v0.1.2` (clean tag build) | `v1.18.0-mecon.0.1.2` | `v1.18.0-mecon.0.1.2` |
| `v0.1.2-5-gabc1234` (5 commits after the tag) | `v1.18.0-mecon.0.1.2+5.gabc1234` | `v1.18.0-mecon.0.1.2` |
| `v0.1.2-5-gabc1234-dirty` | `v1.18.0-mecon.0.1.2+5.gabc1234.dirty` | `v1.18.0-mecon.0.1.2` |
| `v0.1.2-dirty` | `v1.18.0-mecon.0.1.2+dirty` | `v1.18.0-mecon.0.1.2` |
| `v0.3.14` | `v1.18.0-mecon.0.3.14` | `v1.18.0-m.0.3.14` |
| `v0.10.0-2-gabc1234` | `v1.18.0-mecon.0.10.0+2.gabc1234` | `v1.18.0-m.0.10.0` |
| no git or no `v*` tag (source tarball) | `v1.18.0-mecon.unknown` | `v1.18.0-mecon.unk` |

- The Companion device-info frame has a 20-byte version field (19 characters and the terminator), so it carries the short form. A release tag of the form `v0.1.2` fills it exactly as `v1.18.0-mecon.<tag>`. A longer tag (any two-digit component, from `v0.3.10` on) is written as `v1.18.0-m.<tag>`, so the whole tag still shows. Only a tag over 9 characters is cut at 19. The frame does not grow.
- `ver` prints the full form on the Companion, Repeater, Room server and Sensor. `get mecon.version` answers `> <full> (describe <git describe>, upstream <base>)`.
- `FIRMWARE_VER_CODE` (the Companion protocol level) is a separate number and does not change with the version string.
- A build with `-D FIRMWARE_VERSION=...` overrides only the short form that the Companion app and the screen show. `hello`, the runtime stream and the console still report the described version. Release artifacts take their version from the tag they are built from.
- A development or dirty build carries `+N.g<hash>` or `+dirty` in the full form, so it cannot be mistaken for a release artifact.

## Release artifacts

A release states, per board and role, what it supports. Its notes say what was verified and how, what was not, the behaviour changes and the measured memory cost. An update-capable release publishes a release manifest signed by a release key, with the image digest, the board, layout and role, the upstream base and the minimum version it may be installed over ([signed updates](contract/UPDATES.md)). USB stays the recovery path on every board.

Every nRF52 variant in a release (the Wio Tracker L1 and the T1000-E) ships two images of one application: the signed serial-DFU package `<variant>-<version>.zip`, and `<variant>-<version>.uf2` for the bootloader's USB drive. The release's build makes the UF2 from the same build as the package, and the release is refused unless the UF2 decodes to the package's `firmware.bin` (same bytes, one contiguous run, family `0xADA52840`). `update-set.json` lists it as `recovery_uf2`, with `recovery_uf2_sha256` and `recovery_uf2_base`, the application's start address (`0x27000` for the S140 7.3.0 bootloader these boards ship). The UF2 is not signed and no manager installs it: it is for a person at a cable (recovery below). ESP32 variants have no UF2.

### Recovering an nRF52 board from the bootloader's UF2 drive

A board whose bootloader came up after an interrupted update, with no valid application, can sit in the bootloader's UF2 mode, where a serial DFU may report success and activate nothing. On the bench, a bootloader DFU cut during the erase wait (about 15 s for these images) was recovered by one more full serial DFU; a cut after data writing began was not, and only the UF2 recovered it. `Device programmed.` is no evidence: confirm the version in `hello`. The drive (named like TRACKER L1 or T1000-E boot) is up in that mode.

1. Open `INFO_UF2.TXT` on the drive. It must name the SoftDevice S140 7.3.0 (`SoftDevice: S140 7.3.0`). A board with another SoftDevice must not take this file: its application base differs.
2. Copy the release's `<variant>-<version>.uf2` for that board and role onto the drive and wait for the copy to finish (sync or eject).
3. The first copy can return to the bootloader. If the drive reappears, copy the same file again and the application starts.

The node key, the device id and the Deployment record are not touched, because the UF2 writes only the application region. A manager confirms the new version in `hello` after the start.

## Release gate

A release is published as a pre-release first. It is promoted, or advertises a board and role, only after that board and role pass their hardware checks with realistic stored state ([hardware evidence](HARDWARE_VERIFIED.md)).

## Separate version lines

A private reference firmware for MeshContinuum's hosted beta uses its own 0.9x numbers. They do not order against this repository's: neither is an upgrade or a downgrade of the other, and tooling must not compare them. A device compares versions only within the line it runs.
