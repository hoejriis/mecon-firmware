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

Upstream's `stale-bot.yml` is kept unchanged; it only runs in the upstream repository.

## Offered upstream

Generic fixes made here that would also benefit stock MeshCore are listed in this section once they exist, with whether they have been offered upstream.

## Known stock compile failures

24 of the 618 PlatformIO environments in MeshCore 1.18.0 do not compile on stock upstream code. They are listed in `ci/known-upstream-failures.txt`. The all-boards workflow passes only when the failures equal that list, so a new failure, or a listed environment that starts compiling, turns it red. None of them is a board or role this project verifies on hardware.

| Environments | Stock error |
|---|---|
| `*_kiss_modem` on Heltec E213, E290, T190, Tracker v2, LilyGo T-Beam 1W, Station G3 ESP32 (6) | `USBCDC` has no `setTxBufferSize` |
| `Generic_E22_kiss_modem` | `LORA_TX_POWER` not declared |
| `Generic_ESPNOW_*` (4), `SenseCapIndicator-ESPNow_comp_radio_usb` | `P_LORA_DIO_1` not declared |
| `*_repeater_bridge_espnow` on Heltec CT62 and Tenstar C3 (3) | ESP-NOW receive callback signature mismatch |
| `ThinkNode_M8_kiss_modem`, `_repeater`, `_room_server` | link error |
| `Station_G2_companion_radio_ble`, `Station_G3_ESP32_companion_radio_ble` | image larger than the 1,310,720-byte app partition |
| `Heltec_mesh_solar_companion_radio_ble`, `_usb` | vendor logger macros collide with the board library |
| `MeshTracker_X1_kiss_modem` | `MicroNMEA.h` not found |
| `ThinkNode_M5_companion_radio_serial` | `D6`/`D7` not declared |
