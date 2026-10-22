# NimBLE evaluation (ESP32 Companion)

The ESP32 BLE Companion builds use the Bluedroid stack through `src/helpers/esp32/SerialBLEInterface`. A second implementation, `src/helpers/esp32/SerialNimBLEInterface`, does the same job on NimBLE-Arduino 2.3.9. It is selected at build time with `-D USE_NIMBLE`; without that flag its source compiles to nothing and the default builds are unchanged.

This page records the build-level comparison and what is known and not known about behaviour. **Nothing here is verified on hardware yet**: pairing from a phone and from the Pi, the default-PIN refusal audit, and the boot heap are still to be measured.

## Builds

Each `_nimble` environment extends its Bluedroid twin with the `[esp32_nimble]` section of `platformio.ini`:

- `-D USE_NIMBLE`, and NimBLE-Arduino pinned at 2.3.9;
- the central and observer roles left out (`CONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED`, `CONFIG_BT_NIMBLE_ROLE_OBSERVER_DISABLED`): the Companion is only ever a peripheral;
- `CONFIG_BT_NIMBLE_MAX_BONDS=15`, the same bond limit as Bluedroid in arduino-esp32 2.0.17;
- the Bluedroid source (`SerialBLEInterface.cpp`) and the Bluedroid Arduino library (`ESP32 BLE Arduino`) kept out of the build.

| Bluedroid build | NimBLE twin |
|---|---|
| `Heltec_v3_companion_radio_ble` | `Heltec_v3_companion_radio_ble_nimble` |
| `Heltec_v3_companion_radio_standalone` | `Heltec_v3_companion_radio_standalone_nimble` |
| `heltec_v4_companion_radio_standalone` | `heltec_v4_companion_radio_standalone_nimble` |
| `LilyGo_TLora_V2_1_1_6_companion_radio_ble` | `LilyGo_TLora_V2_1_1_6_companion_radio_ble_nimble` |

All eight are built by the target-boards workflow, which reports flash and static RAM per build and checks them against `ci/size-budget.txt`.

## Size

From the target-boards workflow on the pull request that added the option (run 39721524160). Percentages are of each board's app partition. In the same run every Bluedroid build that was already on main came out at exactly the size of the latest main run: 0 B difference in flash and in static RAM.

| Board, build | Flash, Bluedroid | Flash, NimBLE | Flash difference | Static RAM, Bluedroid | Static RAM, NimBLE | Static RAM difference | Boot heap |
|---|---|---|---|---|---|---|---|
| Heltec V3, BLE Companion | 1,702,457 B (50.9 %) | 1,289,657 B (38.6 %) | -412,800 B | 131,028 B | 119,028 B | -12,000 B | to measure on hardware |
| Heltec V3, standalone Companion | 1,878,537 B (56.2 %) | 1,465,961 B (43.9 %) | -412,576 B | 136,980 B | 124,980 B | -12,000 B | to measure on hardware |
| Heltec V4, standalone Companion | 1,876,669 B (28.6 %) | 1,464,137 B (22.3 %) | -412,532 B | 136,960 B | 124,968 B | -11,992 B | to measure on hardware |
| LilyGo T-LoRa V2.1-1.6, BLE Companion | 1,935,653 B (98.5 %) | 1,448,389 B (73.7 %) | -487,264 B | 75,608 B | 77,080 B | +1,472 B | to measure on hardware |

On the ESP32-S3 boards (V3, V4) NimBLE saves about 403 KB of flash and 12 KB of static RAM. On the classic ESP32 (LilyGo) it saves about 476 KB of flash and costs 1.4 KB more static RAM. Why the static RAM goes the other way on the classic ESP32 is not established; NimBLE keeps its buffer pools in static memory, Bluedroid allocates most of its state from the heap at start-up, so the boot-heap figure is what decides the RAM comparison.

Static RAM is what the linker places in internal RAM (`.data` + `.bss`). The heap the stack allocates when it starts is not in it, and is the larger part of Bluedroid's cost; it is still **to measure on hardware** (`get mecon.mem` on v3test, before and after the BLE stack is up).

## Behaviour: how the two classes map

The public interface is the same, so `examples/companion_radio/main.cpp` uses either class unchanged (a `typedef` under `USE_NIMBLE`). The posture rules are not in the BLE class at all: `Posture::bleMayStart` decides before `begin()` whether the stack starts, and the refusal is audited there, so a Hardened device with a default PIN never starts NimBLE either. Trust (`isTrustedTransport`: a PIN other than 0 or 123456) and `isBleTransport` are the same code in both classes.

| | Bluedroid | NimBLE |
|---|---|---|
| Service | Nordic UART, `6E400001-…`, RX `…0002` write, TX `…0003` read + notify | the same UUIDs and properties |
| Characteristic security | `ESP_GATT_PERM_READ_ENC_MITM`, `WRITE_ENC_MITM` | `READ_ENC \| READ_AUTHEN`, `WRITE_ENC \| WRITE_AUTHEN` |
| Pairing | `setStaticPIN(pin)`: static passkey, IO capability display-only; `ESP_LE_AUTH_REQ_SC_MITM_BOND` | `setSecurityPasskey(pin)`, `BLE_HS_IO_DISPLAY_ONLY`, `setSecurityAuth(bond, mitm, sc)`; `onPassKeyDisplay()` returns the PIN too, because NimBLE asks it instead of using the static passkey when the passkey is 123456 |
| Key distribution | ENC \| ID (set by `setStaticPIN`) | ENC \| ID both ways, set explicitly |
| Bonds | NVS, up to 15 | NVS, up to 15; when full, NimBLE deletes the oldest |
| `onPassKeyRequest` / `onPassKeyNotify` | return / log the PIN | folded into `onPassKeyDisplay()` |
| `onConfirmPIN` (numeric comparison) | accept | `onConfirmPassKey`: accept, injected. A display-only device never reaches it |
| `onSecurityRequest` | allow | no peripheral-side equivalent; nothing to map |
| Link up | `onAuthenticationComplete`, `cmpl.success` | `onAuthenticationComplete`, link **encrypted and authenticated** (read from the connection, NimBLE passes no status) |
| Failed authentication | disconnect, advertise again after 1 s | the same |
| Disconnect | advertise again after 1 s while enabled | the same; NimBLE's own advertise-on-disconnect stays off |
| `enable()` | start the service, start advertising | start advertising (NimBLE registers the GATT table on the first start; it has no service start/stop) |
| `disable()` | stop advertising, disconnect the last connection, stop the service | stop advertising, disconnect **every** connection |
| Advertising | name and service UUID, repeated in the scan response | service UUID in the advertisement, name in the scan response (a 128-bit UUID and a long name do not fit one packet) |
| Send pacing, queues, frame size | 60 ms between notifications, 4-frame queues, 176-byte frames | the same code |

### What differs

- **Unauthenticated links are dropped.** Bluedroid marks the link up on any successful pairing; its characteristics already refuse reads and writes without MITM, so a Just Works link could do nothing but stay connected. The NimBLE class requires an encrypted and authenticated link before it is up, and drops anything else like a failed pairing. Stricter, never looser.
- **`disable()` drops every connection**, not only the last one, so a central that connected but never paired is not left behind.
- **A phone that forgot its bond.** NimBLE's host, by default, ignores a new pairing request from a peer it already holds a bond for (`BLE_GAP_EVENT_REPEAT_PAIRING` is answered "ignore" because NimBLE-Arduino's server does not handle it, and the event cannot be hooked from outside). Bluedroid is expected to accept the new pairing and replace the old bond (not checked in its source or on a board). So after "forget device" on the phone, a NimBLE device may refuse to pair again until its own bond is removed. To verify on hardware; if it holds, it needs a fix in the class (or a bond-clear command) before NimBLE could be adopted.
- **Advertising layout.** The name moves to the scan response. Apps that filter on the service UUID or that read the scan response see the device as before; a scanner that reads only the advertisement would show it without a name.

## Risks

- None of the behaviour above has run on hardware. The mapping is read from the two libraries' sources, not observed.
- The heap saving, which is the main reason to switch on the ESP32 boards, is not measured.
- A second BLE implementation is a second thing to keep in step: any change to the Bluedroid class (pacing, queues, posture hooks) has to be made in both while both exist.
- NimBLE-Arduino 2.x on arduino-esp32 2.0.17 is supported by the library, but this project has no run time on it yet.

## Recommendation per board

- **LilyGo T-LoRa V2.1-1.6 (4 MB flash, `min_spiffs` partition).** The case for NimBLE. The Bluedroid build is at 98.5 % of its partition and already leaves the VL53L0X driver out to fit; NimBLE brings it to 73.7 %. Adopt once the hardware checks below pass on this board and the forgotten-bond case is settled.
- **Heltec V3 (8 MB flash, 3.3 MB app partition).** No flash pressure (50.9 % with Bluedroid), and the measured headroom on this board does not call for a change. Stay on Bluedroid unless the boot-heap measurement shows a gain that matters for the standalone build (BLE and Wi-Fi with TLS share internal RAM; see [Heap floor](CONNECTIVITY_MODE.md#heap-floor)).
- **Heltec V4 (16 MB flash, 2 MB PSRAM).** No flash pressure (28.6 %). Stay on Bluedroid, with the same heap condition as the V3.

The decision per board is made after the hardware checks: boot heap on v3test, PIN pairing from the Pi as central and from a phone app, a default-PIN refusal still audited as `ble`, and the forgotten-bond case above.
