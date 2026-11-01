# Install and set up a device

Pick a board and role first ([supported hardware](SUPPORTED_HARDWARE.md)). Before you install, check [availability](../README.md#getting-it): it says which releases exist and what each one supports.

## Build and flash

Build inside the project container rather than on your computer:

```sh
docker build -t mecon-firmware-dev .
sim/run.sh pio run -e Heltec_v3_companion_radio_standalone
```

Replace the environment name with the one for your board and role, from the table in [supported hardware](SUPPORTED_HARDWARE.md#boards-and-roles). On the Heltec boards the first build also rebuilds the ESP-IDF libraries, which takes a few minutes ([runtime platform](PLATFORM.md#building)).

Flash with `pio run -e <env> -t upload`, or write the built image with your usual tool. Use a spare board the first time, and keep a USB cable to hand: USB is the recovery path on every board.

## Reach the device over USB

- **Repeater and Room server.** Open the USB serial console and type commands.
- **Companion.** Its USB port speaks the app protocol, so use the MeshCore app over USB, or a tool that sends CLI lines in an app frame. For a plain text console, use the **rescue console**: long-press the user button within 8 seconds of the logo screen appearing ([CLI reference](CLI_REFERENCE.md#rescue-console)).

The cable is always a trusted link, so everything below works over it.

## First settings

| What | Command | Notes |
|---|---|---|
| Name | `set name <name>` | |
| Radio | `set radio <freq>,<bw>,<sf>,<cr>` | Applied at the next reboot. Choose a frequency and bandwidth that are legal where you are |
| BLE PIN (Companion) | `set pin <6 digits>` | BLE manages the device only when paired with a PIN other than 0 or 123456. Boards with a screen show a per-session PIN; boards without one need this |
| Admin password (Repeater, Room server) | `password <new>` | Remote administration stays off while the password is the default or empty |
| Duty cycle | `get dutycycle` | Shows the band, the regulatory floor for your channel, your own limit and the airtime used |
| Posture | `get posture`, `set posture hardened` | Optional. Hardened closes more paths: see [postures](STANDALONE_POSTURES.md) |

The full command list, with who may run each command and from where, is the [CLI reference](CLI_REFERENCE.md).

## Next

- [Connect to Wi-Fi and MQTT](CONNECTING.md) (Heltec Companions)
- [Manage a device](MANAGING.md)
