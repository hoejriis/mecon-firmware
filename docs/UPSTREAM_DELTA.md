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
| `src/helpers/BaseSerialInterface.h`, `MultiSerialInterface.h`, `ArduinoSerialInterface.h`, `wifi/SerialWifiInterface.h`, `ethernet/SerialEthernetInterface.h`, `esp32/`, `nrf52/` and `rp2040/SerialBLEInterface.*` | `isTrustedTransport()` added. It **defaults to false (fail closed)**. USB/UART serial is trusted. BLE is trusted only when paired with a PIN other than 0 or the default 123456. The TCP interfaces are untrusted. The multiplexer reports the transport of the last frame. | Lets the Companion tell an authenticated transport from an unauthenticated one. A PIN anyone can look up does not authenticate, so on default-PIN boards management stays on USB. |
| `examples/companion_radio/MyMesh.cpp` | `CMD_RUN_CLI_COMMAND`, export/import private key, set device PIN and factory reset are refused with `ERR_CODE_NOT_PERMITTED` (7) on an untrusted transport | Stock serves these to any client of the `_wifi` build's open TCP port. |
| `src/helpers/CommonCLI.cpp` (`loadPrefs`, `savePrefs`), `examples/companion_radio/DataStore.cpp` (prefs, channel table), `src/helpers/IdentityStore.cpp`/`.h` | Saves go through `src/helpers/AtomicFile.h` (new): temp file, flush, read-back check of length and CRC, then swap; boot completes or drops an interrupted swap. A stored identity that cannot be read is renamed to `<file>.damaged` instead of being overwritten by a fresh one | Stock rewrites these files in place: a power cut leaves a truncated file that the loader accepts (settings silently revert), or loses the identity. |
| `examples/simple_repeater/main.cpp` | Prints a warning when the identity file was damaged and kept aside | The repeater console is the only place that can report it. |
| `src/Mesh.cpp` | TRACE handling: payloads shorter than the 9-byte header are dropped, the hash-list length is no longer a wrapping `uint8_t`, and a hop entry must lie fully inside the payload before it is compared | Stock computes `payload_len - 9` as a `uint8_t`, so a short TRACE yields a length near 255 and the next-hop comparison reads past the packet buffer (found with AddressSanitizer in the simulator). |
| `src/helpers/CommonRadioPrefs.cpp`, `src/helpers/CommonCLI.cpp` | `strcpy` of the command tail into fixed 132-byte buffers (`set radio`, `tempradio`, `sensor set`, `extra.sf`) replaced by a bounded copy; `??:` and `unknown config:` replies are bounded | Stock overflows the buffer for a long argument; a remote caller sends up to about 160 characters. |
| `src/helpers/CommonCLI.cpp` | `set prv.key` is refused unless `sender_timestamp == 0` (local console) | Stock lets a remote admin replace the repeater's or room server's identity; `get prv.key` and `erase` are already local-only. |
| `examples/companion_radio/MyMesh.cpp` | `get wifi.pwd` answers "write-only" instead of the password | Stock returns the Wi-Fi password in clear text to any CLI caller. |
| `examples/companion_radio/MyMesh.cpp` | A remote CLI caller (flagged contact, `sender_timestamp != 0`) may run `get ...`, `ver`, `board` and `reboot` only; a remote timestamp of 0 is raised to 1 so a peer cannot present as local | Stock runs any command, including `set pin`, `set wifi.*`, radio settings and `poweroff`, for a flagged contact. |
| `src/Dispatcher.h`, `src/Dispatcher.cpp` | The token bucket (full at boot, refilled continuously) is replaced by a **sliding one-hour airtime window**: airtime per completed transmission goes into 121 bins of 30 s (242 bytes of RAM), and a packet is sent only if the trailing hour plus its own time on air stays within the limit. The limit is the stricter of the user's airtime factor and the regulatory floor of `src/helpers/RegulatoryDutyCycle.*` for the channel reported by the new virtual `getRadioChannel()`. The check runs after the packet is dequeued (its length is only then known): a packet that does not fit is put back as the most urgent, scheduled for when enough airtime has aged out; a packet longer than the whole limit is dropped. `updateTxBudget()` and the virtual `getDutyCycleWindowMs()` are gone; `getRemainingTxBudget()` now reports limit minus trailing airtime. `formatDutyCycleReply()` added for the CLI. | Stock's bucket lets a sliding hour carry almost twice the limit, the default limit is 100 %, and the strictest reachable limit is 10 %, so a 1 % or 0.1 % sub-band cannot be honoured. |
| `examples/companion_radio/MyMesh.h`/`.cpp`, `examples/simple_repeater/MyMesh.h`/`.cpp` | `getRadioChannel()` reports the frequency and bandwidth last applied to the radio (updated at each `setParams()`: boot, the Companion's set-radio command, the repeater's `tempradio` and its revert), so a frequency change is enforced at once. `get dutycycle` answers from the Dispatcher: band, limit, floor, user setting and trailing-hour airtime. | The floor must follow the radio, not the stored prefs (`set radio` only applies at the next boot). |
| `platformio.ini` | `[env:native]` also builds `src/helpers/RegulatoryDutyCycle.cpp` | Unit tests of the sub-band table. |

Upstream's `stale-bot.yml` is kept unchanged; it only runs in the upstream repository.

## Offered upstream

Generic fixes made here that would also benefit stock MeshCore, with whether they have been offered upstream.

| Change | Offered upstream |
|---|---|
| Transport trust for Companion management commands (`isTrustedTransport()`, `ERR_CODE_NOT_PERMITTED`) | not yet offered |
| Atomic prefs/identity/channel saves (`AtomicFile.h`) | not yet offered |
| Bounded TRACE parsing; bounded copies in CLI parsing | not yet offered |
| `set prv.key` local console only (repeater, room server, sensor) | not yet offered |
| Remote Companion CLI limited to reads and `reboot` | not yet offered |
| `get wifi.pwd` no longer reveals the Wi-Fi password | not yet offered |
| Sliding one-hour airtime window replacing the token bucket, with a regulatory sub-band floor (EU 863-870 MHz, 433 MHz) | not yet offered |

## Duty-cycle enforcement: transmit paths

Every LoRa transmission of the Companion and Repeater roles goes through `Dispatcher::checkSend()`, the only caller of `startSendRaw()` outside the KISS modem. Audit of every `startSendRaw`/`startTransmit` call site in `src/`, `examples/` and `variants/`:

| Path | Gated by the window? | Handling |
|---|---|---|
| `Dispatcher::checkSend()`: every Mesh packet (adverts, messages, acks, forwards, discovery, bridge-injected packets, CLI replies) | yes | enforced for Companion and Repeater with the regulatory floor |
| Room server, sensor, secure-chat examples | window yes, floor **no** | they do not report their channel, so only the user's airtime factor applies. Out of scope here (not Companion or Repeater); they need the same `getRadioChannel()` override |
| `examples/kiss_modem/KissModem.cpp` (`_radio.startSendRaw`) | **no** | the host controls timing and the modem is a different role. Out of scope; the host application owns the duty cycle |
| `src/helpers/esp32/ESPNOWRadio.cpp` | no | 2.4 GHz ESP-NOW, not subject to the 863-870/433 MHz limits |

Limits of the enforcement: the window lives in RAM, so a reboot starts with an empty hour (a persisted window is not implemented). A packet put back because it does not fit keeps its place only as "most urgent": the packet manager does not expose a packet's original priority. Airtime is charged as the larger of the estimated time on air and the measured send duration (capped at 1.5 times the estimate).

## Atomic saves: rename behaviour per file system

`AtomicFile` swaps a verified temporary file over the live file. What the platforms are expected to do, from their library sources and documentation, **not yet verified on hardware**:

| Family | File system | Rename over an existing file | What `AtomicFile` does |
|---|---|---|---|
| ESP32 | SPIFFS | Not assumed to replace the destination (SPIFFS reports a name conflict) | Two-step swap: temp file to `<path>.new`, remove `<path>`, `<path>.new` to `<path>`. There is a window with no `<path>`; boot completes the swap from the verified `<path>.new`. The simulated board refuses rename-over to hold the code to this. |
| nRF52, STM32 | Adafruit LittleFS (littlefs) | Atomic replace (`lfs_rename`) | Direct rename over the live file. |
| RP2040 | LittleFS (littlefs) | Atomic replace (`lfs_rename`) | Direct rename over the live file. |

Not covered: contacts, blobs and the client ACL are still written in place.

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
