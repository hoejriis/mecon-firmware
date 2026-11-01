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
| no git or no `v*` tag (source tarball) | `v1.18.0-mecon.unknown` | `v1.18.0-mecon.unk` |

- The Companion device-info frame has a 20-byte version field (19 characters and the terminator), so it carries the short form, cut at 19 characters. A release tag of the form `v0.1.2` fills it exactly; a tag with two-digit components is cut. The frame does not grow.
- `ver` prints the full form on the Companion, Repeater, Room server and Sensor. `get mecon.version` answers `> <full> (describe <git describe>, upstream <base>)`.
- `FIRMWARE_VER_CODE` (the Companion protocol level) is a separate number and does not change with the version string.
- A build with `-D FIRMWARE_VERSION=...` still overrides the string. Release artifacts take their version from the tag they are built from.
- A development or dirty build carries `+N.g<hash>` or `+dirty` in the full form, so it cannot be mistaken for a release artifact.

## Release artifacts

A release states, per board and role, what it supports. Its notes say what was verified and how, what was not, the behaviour changes and the measured memory cost. An update-capable release publishes a release manifest signed by a release key, with the image digest, the board, layout and role, the upstream base and the minimum version it may be installed over ([signed updates](contract/UPDATES.md)). USB stays the recovery path on every board.

## Release gate

A release is published as a pre-release first. It is promoted, or advertises a board and role, only after that board and role pass their hardware checks with realistic stored state ([hardware evidence](HARDWARE_VERIFIED.md)).

## Separate version lines

A private reference firmware for MeshContinuum's hosted beta uses its own 0.9x numbers. They do not order against this repository's: neither is an upgrade or a downgrade of the other, and tooling must not compare them. A device compares versions only within the line it runs.
