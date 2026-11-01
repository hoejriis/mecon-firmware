# Connectivity mode

The standalone Companion builds (`Heltec_v3_companion_radio_standalone`, `heltec_v4_companion_radio_standalone`) carry BLE, the [Wi-Fi controller](WIFI.md) and the [MQTT controller](MQTT.md), and run **one of BLE or Wi-Fi at a time**. USB management works in both modes. Stock MeshCore has separate `_ble` and `_wifi` builds and no switching.

What has been checked on real boards is in [hardware evidence](HARDWARE_VERIFIED.md), and what the simulated-board suite covers is in the [test harness](SIMULATION.md).

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

On the connectivity status screen (home screen page after Bluetooth; reached with single clicks) a **long press** arms the switch and shows `Switch to <mode>?`. A second **long press** within 10 s confirms and restarts. A click, a double click or the timeout cancels and changes nothing. The gesture adds no new button handling: it uses the page-action key (`KEY_ENTER`, a long press on one-button boards) exactly as the Advert and Hibernate pages do, so click, double click and triple click keep their stock meaning. The stock rule that a long press in the first 8 s after the UI starts (when the logo screen replaces "Loading...") opens the rescue console still applies. It works in Hardened: it needs physical possession. In Wi-Fi mode the Bluetooth page is skipped.

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

## Heap floor

A run-time check that the largest free heap block stays above a per-board floor (`src/helpers/HeapFloor.h`). CI cannot measure heap and the hardware sessions only sample it, while a Wi-Fi + TLS session, a BLE pairing or a long uptime can leave no block big enough for the next allocation.

- **When.** Once after setup, then every 60 s. The figure is the **largest allocatable block of internal RAM** (PSRAM, where a board has it, is excluded: the BLE, Wi-Fi and TLS stacks need internal RAM), not the total free heap (fragmentation is what makes an allocation fail). On the ESP32 builds the floor is 20,480 B (the threshold proposed from the first hardware session); `-D HEAP_FLOOR_BYTES=<n>` changes it at build time, and `0` turns the check off. Platforms whose heap figure is not measurable never fire, and the Repeater and Room server run no check.
- **Episode.** The device is *low* once the largest block is below the floor, and the episode ends when it is back at floor + 2,048 B (so a block hovering at the floor is one episode, not one per look).
- **What it does while low.** `get mecon.mem` reports `heap_low=1` and `floor=<bytes>`; the home status line carries `LOW MEM` (after the mode text where both fit the 21-column line, else alone, drawn as a warning); and **one** audit entry is written per episode: event `low-mem`, caller `system`, command class `heap-floor`. The entry folds like any other, so its count is the number of episodes since the log was last cleared.
- **What it does not do.** It does not stop anything, shed load or restart: it makes a shrinking margin visible and gives hardware sessions a threshold to test against. The sizes are the hardware session's proposal, not a promise of how much headroom a feature needs.

The decision logic is checked by native unit tests; the state change, the single audit entry per episode, the CLI fields and the screen marker are checked end to end on the simulated board (the test lowers the simulated heap while the node runs). **Not verified:** the real allocator and the figure on a board.

## Known limits

- USB coexists with BLE and with Wi-Fi in the multiplexed serial interface; this is **assumed, not verified on hardware**, for both boards. The V3 reaches USB through a UART bridge; the V4 uses the chip's native USB, whose coexistence with BLE is the one to check first. A board where it fails is recorded here as an exception.
- The TCP companion port of the Wi-Fi mode is inert in the simulator; its untrusted classification is covered by the existing transport tests.
