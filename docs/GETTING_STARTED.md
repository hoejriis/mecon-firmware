# Install and set up a device

From choosing a board to one useful result. This page is for operators. Contributor checks, such as the simulator and unit tests, are in [contributing](../CONTRIBUTING.md#build-and-test), and you do not need them to use the firmware.

> **Availability.** There are no downloadable images yet, so installing means building from source. Some features on these pages are not built yet: see [availability](AVAILABILITY.md).

## 1. Choose a board and role

[Supported hardware](SUPPORTED_HARDWARE.md#boards-and-roles) lists each board, its roles and the environment name to build. Use a spare board the first time, and keep a USB cable to hand: USB is the recovery path on every board.

## 2. Keep what is on the device

Decide what you would lose before you install. What a backup can do depends on the board:

| Board | Backup | Restoring it | Installing over existing data |
|---|---|---|---|
| Heltec V3, V4 | A full flash dump with `esptool read-flash`, which reads every setting, key and record | Puts back the whole device, but also rolls back its counters. A manager that has seen a later boot rejects its signed messages until the device has booted past that point, and replay marks go back too | Writing the application alone over an earlier build of this firmware kept the identity, name, PIN and contacts, checked on both boards. BLE bonds do not carry over from a build on the older BLE stack: pair again |
| T1000-E, Wio Tracker L1 | No full dump: the bootloader's serial DFU cannot read flash. Save what the device will report: contacts and channels through an app, settings with `get`, and the private key if it is not enrolled | Set the saved values again. An enrolment cannot be saved: re-enrol the device | The Wio USB Companion build over stock, then stock again, kept all 27 settings checked. The identity survived too. A T1000-E Companion booted on the private firmware's data with all 350 contacts and 40 channels |

Some things hold on every board:

- **The private key.** `get prv.key` works on the USB console in Normal, before enrolment. On a Companion, use the rescue console or the app's export. Keep the key safe: it is the device's identity. Hardened refuses the export.
- **A change of role is not a clean swap.** A Companion and a Repeater share a settings file, so going from one to the other and back can change the radio and other settings. Compare with `get` afterwards.
- **A stock contact file** migrates at the first boot ([contacts](CONTACTS.md)), except on the T1000-E Companion, which keeps the stock table.
- **A board from the private reference firmware** reads that firmware's Deployment record as corrupt, and refuses changes until it is cleared ([steps](migration/BACKEND_MIGRATION.md#moving-a-board-from-the-private-090x-line)).

These are results from particular runs, not a guarantee for your device. If you cannot lose it, back it up first and compare afterwards. A factory reset erases everything, the node key included.

## 3. Install

Build inside the project container rather than on your computer:

```sh
docker build -t mecon-firmware-dev .
sim/run.sh bin/idf5_libs.sh Heltec_v3_companion_radio_standalone
sim/run.sh pio run -e Heltec_v3_companion_radio_standalone
```

Replace the environment name with the one for your board and role. On a Heltec V3 or V4, run `bin/idf5_libs.sh <env>` before the first build, and again after building another board in the same PlatformIO directory. It rebuilds that board's ESP-IDF libraries (about 6 minutes, and seconds when nothing changed), and without it the build stops at once and says so. It is idempotent and prints "nothing to do" for any environment that does not need it, so running it before every build is safe. Keeping each board in its own PlatformIO directory (`PLATFORMIO_CORE_DIR=<dir>`) avoids the rebuild when you switch ([runtime platform](PLATFORM.md#building)).

Flash with `pio run -e <env> -t upload`, or write the built image with your usual tool: `esptool` on the Heltec boards, `adafruit-nrfutil dfu serial` on the nRF52 boards. PlatformIO's upload on a Heltec board also writes the bootloader and partition table. The run in step 2 wrote the application alone, so take a dump first if the data matters.

## 4. Check the device

Reach it over USB:

- **Repeater and Room server.** Open the USB serial console and type commands.
- **Companion.** Its USB port speaks the app protocol, so use the MeshCore app over USB, or a tool that sends CLI lines in an app frame. For a plain text console, use the **rescue console**: long-press the user button within 8 seconds of the logo screen appearing ([CLI reference](CLI_REFERENCE.md#rescue-console)).

Before a device is enrolled, the cable can change anything below. Then check, and set what you need:

| What | Command | Check or set |
|---|---|---|
| Firmware | `ver` | The version you built, with the upstream base first |
| Identity | `get public.key` | The same key as before, if you kept the data |
| Name | `set name <name>` | |
| Radio | `get radio`, `set radio <freq>,<bw>,<sf>,<cr>` | Applied at the next reboot. Choose a frequency and bandwidth that are legal where you are |
| Duty cycle | `get dutycycle` | The band, the regulatory floor for your channel, your own limit and the airtime used |
| BLE PIN (Companion) | `set pin <6 digits>` | BLE manages the device only when paired with a PIN other than 0 or 123456. Boards with a screen show a per-session PIN; boards without one need this |
| Admin password (Repeater, Room server) | `password <new>` | Remote administration stays off while the password is the default or empty |
| Posture | `get posture`, `set posture hardened` | Optional. Hardened closes more paths: see [postures](STANDALONE_POSTURES.md) |

The full command list, with who may run each command and from where, is the [CLI reference](CLI_REFERENCE.md).

Then check ordinary MeshCore operation, which needs no manager: send a message to, or get an advert from, a node on the same radio settings. A stock app works on a Companion.

## 5. Choose how to run it, and see one result

**On its own.** Nothing more is needed. The device follows its posture rules, and you manage it over a trusted link. A first useful result is to ask it about its own state in the structured form a manager would use. On a Repeater or Room server, type this on the console:

```text
mecon> {"v":1,"id":"r-1","op":"status.get"}
```

The answer is one `mecon> {...}` line. A Companion takes the same request as a `0x7F` app frame, through a tool or a manager ([device interface](contract/DEVICE_INTERFACE.md)). To reach the device over the network, [connect it to Wi-Fi and MQTT](CONNECTING.md) (Companion, Repeater and Room server on the Heltec boards). To see what else it reports, read [monitor a device](MONITORING.md).

**With a manager.** Managed workflows need an application that implements the [public contracts](contract/README.md). [MeshContinuum](https://github.com/hoejriis/MeshContinuum) is one, and its Reader needs its Backend. Enrol the device once, over the USB cable, as that manager describes, then check one result from the manager:

1. `hello` reports the `device_id` and node key you recorded.
2. A read such as `status.get`, or a stream such as `health`, arrives with the device's signature.

After enrolment, a change needs a manager's signature, so the unsigned cable keeps reads and the recovery operations only ([manage a device](MANAGING.md#standalone-and-enrolled)).

## Next

- [Connect to Wi-Fi and MQTT](CONNECTING.md)
- [Manage a device](MANAGING.md)
- [Monitor a device](MONITORING.md)
- [Update and recover](UPDATING_AND_RECOVERY.md)
