# Upstream delta register

This repository is a fork of [MeshCore](https://github.com/meshcore-dev/MeshCore), pinned at **v1.18.0** (upstream commit `81dbae592ffad40b4212bc0b37e82b38da08f4bc`, the `companion-v1.18.0`, `repeater-v1.18.0` and `room-server-v1.18.0` tags). Upstream history is kept, so a later upstream release merges in the ordinary way.

This register lists **every change this repository makes to a file that exists upstream**. Files this project adds are not listed. An entry stays here for as long as the change exists; when an upstream release makes it unnecessary, the entry is removed in the same change that removes the patch.

| Path | Change | Why |
|---|---|---|
| `README.md`, `CONTRIBUTING.md`, `SECURITY.md` | This project's own versions | The project describes and governs itself; upstream's versions remain in upstream history. |
| `.devcontainer/devcontainer.json` | Replaced | Upstream's container runs privileged on the host network, installs an unpinned PlatformIO and pipes a third-party installer through `curl … \| bash`. This project builds in an unprivileged container from the root `Dockerfile` with a pinned PlatformIO, matching CI. |
| `.github/workflows/github-pages.yml` | Removed | Deploys upstream's documentation site under upstream's domain on every push to `main`. |
| `.github/workflows/build-companion-firmwares.yml`, `build-repeater-firmwares.yml`, `build-room-server-firmwares.yml` | Removed | Release builders triggered by upstream's tag names. This project's releases have their own workflow. |
| `.github/FUNDING.yml`, `CNAME` | Removed | Upstream's funding link and documentation domain. |
| `.gitignore` | `.sim-logs/` added | Local logs from simulated-board runs. |
| `src/helpers/BaseSerialInterface.h`, `MultiSerialInterface.h`, `wifi/SerialWifiInterface.h`, `ethernet/SerialEthernetInterface.h` | `isTrustedTransport()` added (default true; false on the TCP interfaces; the multiplexer reports the transport of the last frame) | Lets the Companion tell an authenticated transport (USB, paired BLE) from the unauthenticated TCP server. |
| `examples/companion_radio/MyMesh.cpp` | `CMD_RUN_CLI_COMMAND`, export/import private key, set device PIN and factory reset are refused with `ERR_CODE_NOT_PERMITTED` (7) on an untrusted transport | Stock serves these to any client of the `_wifi` build's open TCP port. |
| `src/helpers/CommonCLI.cpp` | `set prv.key` is refused unless `sender_timestamp == 0` (local console) | Stock lets a remote admin replace the repeater's or room server's identity; `get prv.key` and `erase` are already local-only. |
| `examples/companion_radio/MyMesh.cpp` | `get wifi.pwd` answers "write-only" instead of the password | Stock returns the Wi-Fi password in clear text to any CLI caller. |
| `examples/companion_radio/MyMesh.cpp` | A remote CLI caller (flagged contact, `sender_timestamp != 0`) may run `get ...`, `ver`, `board` and `reboot` only; a remote timestamp of 0 is raised to 1 so a peer cannot present as local | Stock runs any command, including `set pin`, `set wifi.*`, radio settings and `poweroff`, for a flagged contact. |

Upstream's `stale-bot.yml` is kept unchanged; it only runs in the upstream repository.

## Offered upstream

Generic fixes made here that would also benefit stock MeshCore, with whether they have been offered upstream.

| Change | Offered upstream |
|---|---|
| Transport trust for Companion management commands (`isTrustedTransport()`, `ERR_CODE_NOT_PERMITTED`) | not yet offered |
| `set prv.key` local console only (repeater, room server, sensor) | not yet offered |
| Remote Companion CLI limited to reads and `reboot` | not yet offered |
| `get wifi.pwd` no longer reveals the Wi-Fi password | not yet offered |
