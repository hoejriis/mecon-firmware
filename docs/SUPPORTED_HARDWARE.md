# Supported hardware and roles

Which board to choose, which role to run on it, and what each combination can do when finished. Some of it is not built yet, management over LoRa included: see [availability](AVAILABILITY.md). A device also reports its own capabilities (`hello` lists every operation and telemetry stream the build and board support), so a manager should read those rather than infer them from the board name.

## Choosing a board

| You want | Choose |
|---|---|
| A Companion, Repeater or Room server that you also manage over Wi-Fi and MQTT | **Heltec V3** or **Heltec V4** |
| More memory headroom on an ESP32 board (PSRAM, larger flash) | **Heltec V4** |
| A small, low-power device with GNSS, used over USB and Bluetooth | **SenseCAP T1000-E** (no screen) or **Seeed Wio Tracker L1** (with a screen) |
| Management of a node with no IP link | Any of the four, over LoRa (point to point or a private management channel) |

## Boards and roles

The four hardware test targets, with the role builds that carry the managed-device interface. The other environments for these boards (for example the separate `_ble`, `_usb` and `_wifi` Companion builds) stay stock-derived builds without it.

| Board | Roles (environment) | Local management | IP and MQTT | LoRa management | Screen | Firmware update |
|---|---|---|---|---|---|---|
| Heltec V3 (ESP32-S3) | Companion (`Heltec_v3_companion_radio_standalone`), Repeater (`Heltec_v3_repeater`), Room server (`Heltec_v3_room_server`) | Companion: USB and BLE. Repeater, Room server: USB console | Wi-Fi, MQTT | Point to point, and over a private management channel | OLED | Signed, over USB, BLE or IP where the role has that link. Boots on trial and rolls back if not confirmed |
| Heltec V4 (ESP32-S3, 2 MB PSRAM) | Companion (`heltec_v4_companion_radio_standalone`), Repeater (`heltec_v4_repeater`), Room server (`heltec_v4_room_server`) | As the V3 | As the V3 | As the V3 | OLED | As the V3 |
| SenseCAP T1000-E (nRF52840) | Companion (`t1000e_companion_radio_usb`, `t1000e_companion_radio_ble`), Repeater (`t1000e_repeater`), Room server (`t1000e_room_server`, [not available yet](AVAILABILITY.md)) | USB, and BLE on the BLE Companion | None | As the V3 | None | Signed offer, then the bootloader's serial DFU over USB. No previous image to return to |
| Seeed Wio Tracker L1 (nRF52840) | Companion (`WioTrackerL1_companion_radio_usb`, `WioTrackerL1_companion_radio_ble`), Repeater (`WioTrackerL1_repeater`), Room server (`WioTrackerL1_room_server`) | USB, and BLE on the BLE Companion | None | As the V3 | OLED | As the T1000-E |

## What differs, and why

One management model covers every path: an operation means the same wherever it is carried, and is authorized the same way. This table is the canonical list of what is not carried everywhere. `hello` remains the answer for a given device.

| Difference | What applies | Why |
|---|---|---|
| Wi-Fi, MQTT and IP updates on the T1000-E and Wio Tracker L1 | None | Hardware: no Wi-Fi |
| Screen snapshots on the T1000-E | None | Hardware: no screen |
| T1000-E Companion contacts | The stock fixed table | Hardware: its small file system cannot hold the store beside the stock data |
| BLE on a Repeater or Room server | None. Local management is the USB console | Owner decision |
| BLE and Wi-Fi together on the Heltec Companion | One per boot | Owner decision, to keep memory headroom |
| A Hardened Companion in Wi-Fi mode | No app transport. Use USB | Owner decision |
| Firmware updates on nRF52 boards | Over USB only, written by the bootloader, with no previous image to return to | Hardware: one image slot and a bootloader that checks no signature. Owner decision: no BLE update path |
| The sensor role | No device interface | Owner decision |
| The Heltec `_usb`, `_ble` and `_wifi` Companion builds | Stock-derived, without the device interface. The `standalone` build carries all of it | Build choice |
| Local access on each role | Companion: app frames over USB and BLE. Repeater, Room server: a console line over USB | Role: the stock roles differ |
| Messages, received-message collection, contact operations and `rf.request` | Companion only | Role: a Repeater or Room server has no stock chat client |
| Channels on a Repeater or Room server | One management slot, with no channel messaging | Role: no native channel table |
| Secrets and sensitive operations over LoRa | Never, on either path | Security rule |
| Results that list or reveal state, over LoRa | Point to point only | Privacy rule: every channel member can read a channel reply |
| Subscriptions, full logs and screen snapshots over LoRa | Not carried. Compact telemetry instead | Airtime rule |
| Unverified traffic over LoRa | Dropped and counted on the channel. Within a small budget on point to point | Airtime rule: nothing unverified may make a node transmit at will |
| Messaging only, on the TCP port and default-PIN BLE | No device-interface operation | Security rule: untrusted links |

## What each mechanism means

- **USB.** The cable is always a trusted link. It is the recovery path on every board and role: a factory reset over USB always works. Keep USB access to a device you depend on.
- **BLE.** Companion builds only. The Repeater and Room server have no BLE, by owner decision: their local management is the USB console. A BLE link is trusted only when it is paired with a PIN other than 0 or the default 123456. On a default-PIN link the phone app can message, but management is refused. Heltec Companions show a per-session PIN on the screen. The T1000-E has no screen, so set its PIN over USB before you manage it over BLE.
- **BLE or Wi-Fi, not both.** The Heltec Companion runs one radio stack per boot, by owner decision, to keep memory headroom. Switch with `set connectivity.mode` or a two-step long press on the device button. USB works in both modes ([connectivity mode](CONNECTIVITY_MODE.md)).
- **IP and MQTT.** Every role on the Heltec boards: up to 10 Wi-Fi profiles and 6 MQTT broker profiles, with TLS ([Wi-Fi](WIFI.md), [MQTT](MQTT.md)). The nRF52 boards have no Wi-Fi.
- **LoRa management.** Authenticated device-interface operations point to point or over a private management channel, on every role. A Companion binds one of its own channels. A Repeater or Room server holds a single management channel for this, not general channel messaging. Compact, low-rate telemetry travels the same paths. It needs no IP link, and it is subject to the duty-cycle limits.
- **Screen.** Boards with a screen can send diagnostic screen snapshots to a manager. That is telemetry from the device. A manager never draws on the physical display. Boards without a screen do not offer the stream.
- **Firmware update.** An update installs only if it is signed by a release key and authorised for the device. Each board takes updates only over the paths in the table: a board without an IP link has no IP update, and BLE never stands in for a missing path. On ESP32 boards, a new image boots on trial and rolls back unless it is confirmed. On nRF52 boards, updates are over USB only, by owner decision. The bootloader writes the image: the device is checked by its reported version afterwards, and there is no previous image to roll back to ([updates](contract/UPDATES.md)). Stock MeshCore's unsigned Wi-Fi updater (`start ota`) is refused in every build with the managed-device interface and on nRF52. Only a build of your own without the interface can opt into it (`ENABLE_WIFI_OTA`), and that path is unsigned.

## Practical limits

- **Physical access bypasses signatures.** No board's bootloader checks signatures, and, by owner decision, no secure boot or flash encryption is configured. Someone with the device and a cable can enter the bootloader (BOOT button, double reset or a 1200-baud touch) and install any image. Signed updates protect the remote paths only.
- **Companion contact capacity varies by board.** On the Heltec boards and the Wio Tracker L1 it follows free flash and heap, and never drops below the stock count while space allows ([contacts](CONTACTS.md)). The T1000-E Companion keeps the stock fixed table, a hardware limit: its small file system cannot hold the store beside the stock data.
- **Memory headroom is tighter on the Heltec V3.** During a TLS MQTT session it has less free heap than the V4 ([runtime platform](PLATFORM.md)).
- **BLE bonds do not carry over from older builds.** On the Heltec boards, firmware built on the earlier Bluedroid stack keeps its bonds apart from this one, and the Bluetooth address changes. Forget the device on the phone and pair again ([runtime platform](PLATFORM.md)).

## Verification policy

Only the four boards above are hardware test targets. A capability is called supported on one of them only after it has been checked on that board, for that role and transport. What has been checked, and on which build, is recorded per board in [hardware evidence](HARDWARE_VERIFIED.md). A release states its own board and role support in its release notes.

The other board environments in the tree (other Heltec variants such as the V4 TFT, LilyGo, RAK and others) build from the same source and are **software-verified only**: compilation, size checks where configured, and the applicable unit, simulation and conformance tests. A passing build shows that it compiles, not that it runs correctly on the board, and a test on a board with the same stack is not evidence for another board. The CI runs say which environments build and which fail, including the ones that already fail on stock MeshCore.

Evidence is recorded as **hardware verified**, **software verified** (with the checks named), **failed** or **not verified**, per board, role and feature. One check on a board never promotes the board's other features.

## Screens and attention

Boards with a screen keep the stock MeshCore screen and button model with a few additions: the firmware version at boot, a connectivity page (mode, posture, PIN state, Wi-Fi and MQTT state), the BLE PIN while unconnected, and wake filtering for direct messages and favourites ([screens and buttons](UI.md)). On boards with LEDs, a buzzer or haptics, the same principle applies: direct messages and favourites may alert, and ordinary received traffic does not.
