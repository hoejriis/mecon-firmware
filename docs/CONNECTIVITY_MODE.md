# Connectivity mode

The standalone Companion builds (`Heltec_v3_companion_radio_standalone`, `heltec_v4_companion_radio_standalone`) carry BLE, the [Wi-Fi controller](WIFI.md) and the [MQTT controller](MQTT.md), and run **one of BLE or Wi-Fi at a time**. USB management works in both modes. Stock MeshCore has separate `_ble` and `_wifi` builds and no switching.

What is verified, and how: the decision logic (`src/helpers/connmode/`) by native unit tests; the mode handling, the button gesture, the screens and the audit by the simulated-board suite on the real application and the real `ui-new` screens, with a recording display ([Simulated-board test harness](SIMULATION.md)). **Not verified:** the simulator has no BLE stack (a stub logs when BLE would start), so nothing here proves BLE pairing or a BLE link; the USB-plus-BLE and USB-plus-Wi-Fi coexistence on the boards, the real OLED rendering and the real button timing are compiled but **not run on hardware**.

## The mode

- `ble` (default) or `wifi`, one byte in the prefs (`conn_mode`, saved atomically with the rest). Any other stored value starts BLE.
- Applied at boot. A switch saves the new mode, writes the audit entry and restarts about 1.5 s later, so the BLE and Wi-Fi stacks are never both initialised. In BLE mode the Wi-Fi controller and the MQTT controller do not start; in Wi-Fi mode BLE does not start.
- In Wi-Fi mode the [postures](STANDALONE_POSTURES.md) apply as before (Hardened starts Wi-Fi only for MQTT over TLS, never the TCP port). A switch to Wi-Fi on a device with no profile leaves USB as the only transport; the button switches back.

| Command | Effect |
|---|---|
| `get connectivity.mode` | `> ble` or `> wifi`; anyone who may run `get` commands |
| `set connectivity.mode ble\|wifi` | trusted local transports only: USB / UART, BLE paired with a non-default PIN, the device button. Saves and restarts |

A remote caller (a contact flagged for remote CLI, a TCP client) is refused with `ERR: not permitted` and the refusal is audited (`refused <caller> connectivity-mode`); a switch is audited as `mode <caller> ble|wifi` and the audit is written to flash before the restart.

## The button

On the connectivity status screen (home screen page after Bluetooth; reached with single clicks) a **long press** arms the switch and shows `Switch to <mode>?`. A second **long press** within 10 s confirms and restarts. A click, a double click or the timeout cancels and changes nothing. The gesture adds no new button handling: it uses the page-action key (`KEY_ENTER`, a long press on one-button boards) exactly as the Advert and Hibernate pages do, so click, double click and triple click keep their stock meaning. The stock rule that a long press in the first 8 s after start-up opens the rescue console still applies. It works in Hardened: it needs physical possession. In Wi-Fi mode the Bluetooth page is skipped.

## The screens

Home screen: the line that showed the Wi-Fi address now shows the mode: `BLE` (or `BLE (default PIN!)`), or `Wi-Fi <ip>` / `Wi-Fi: <state>` / `Wi-Fi: no profile`. The stock BLE pairing PIN on the device's own display is kept in BLE mode (it is the pairing mechanism); it is not drawn in Wi-Fi mode.

Connectivity status screen (four lines):

| BLE mode | Wi-Fi mode |
|---|---|
| `Mode: BLE, Normal` / `Hardened` | `Mode: Wi-Fi, Normal` / `Hardened` |
| `Pin: per-device` or `Pin: DEFAULT (unsafe)` | `Wi-Fi p<n> <ssid> <rssi>dBm`, or the controller state, `no profile`, `off` |
| `Phone: connected` / `not connected` | `IP: <address>` or `IP: -` |
| `Wi-Fi, MQTT: off` | `MQTT<n> plain` / `TLS verified` / `TLS UNVERIFIED`, or the state, `no profile`, `off` |

A default PIN (0 or the published 123456) and an unverified TLS session are drawn as warnings. The status text is built from a struct with no field for a password, key or PIN digit, so the screen cannot draw one; the simulated suite also scans every frame for the Wi-Fi and MQTT credentials.

## Memory

Compiled sizes of the standalone builds are in the CI memory table of each pull request. Measured when the feature was written: Heltec V3 RAM 201,988 B, flash 1,850,577 B (55.4 % of the app partition); Heltec V4 RAM 201,976 B, flash 1,848,793 B (28.2 %). Against the `_wifi` builds with MQTT this is about 22.6 KB more static RAM and 566 KB more flash, almost all the BLE stack.

## Known limits

- USB coexists with BLE and with Wi-Fi in the multiplexed serial interface; this is **assumed, not verified on hardware**, for both boards. The V3 reaches USB through a UART bridge; the V4 uses the chip's native USB, whose coexistence with BLE is the one to check first. A board where it fails is recorded here as an exception.
- The TCP companion port of the Wi-Fi mode is inert in the simulator; its untrusted classification is covered by the existing transport tests.
