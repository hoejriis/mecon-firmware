# Runtime platform per target

Which framework, ESP-IDF and BLE stack each supported target builds on, and what changed from the earlier baseline. The supported targets are in [SUPPORTED_HARDWARE](SUPPORTED_HARDWARE.md); hardware evidence is in [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md).

## Per target

| Target family | PlatformIO platform | Framework | ESP-IDF | BLE stack (Companion) | Section in `platformio.ini` |
|---|---|---|---|---|---|
| Heltec V3 (ESP32-S3) | pioarduino `53.03.13-1` (Espressif 32 53.3.13) | Arduino-ESP32 3.1.3 | 5.3.0 | NimBLE-Arduino 2.3.9 | `esp32_idf5_base` |
| Heltec V4 (ESP32-S3) | pioarduino `53.03.13-1` | Arduino-ESP32 3.1.3 | 5.3.0 | NimBLE-Arduino 2.3.9 | `esp32_idf5_base` |
| LilyGo T-LoRa V2.1-1.6 (ESP32) | `platformio/espressif32@6.11.0` | Arduino-ESP32 2.0.17 | 4.4 | NimBLE-Arduino 2.3.9 | `esp32_base` (board exception below) |
| Seeed Wio Tracker L1, SenseCAP T1000-E (nRF52840) | `nordicnrf52` with the pinned Adafruit core | Adafruit nRF52 Arduino | none | Bluefruit (SoftDevice) | `nrf52_base` |
| Every other ESP32 board in the tree | `platformio/espressif32@6.11.0` | Arduino-ESP32 2.0.17 | 4.4 | Bluedroid | `esp32_base` |

The nRF52 targets keep their native runtime: nothing in `esp32_idf5_base` or `[esp32_nimble]` reaches them. Their Companion and Repeater images (Wio Tracker L1, T1000-E) come out at the same flash and static RAM, to the byte, as before this change.

Other ESP32 boards stay on the upstream runtime. Each has its own pin map, board library and sometimes partition, and none has been built, measured or run here on ESP-IDF 5. They are unknown on ESP-IDF 5, not unsupported. Moving one is a matter of pointing its base section at `esp32_idf5_base`, then building and measuring it.

The ESP32-C6 boards were already on pioarduino upstream; their section (`esp32c6_base`) now extends `esp32_idf5_base`, with the same pin.

## What changed from the baseline

The baseline is main at 69755258: every ESP32 target on Arduino-ESP32 2.0.17 / ESP-IDF 4.4, and Bluedroid for the Heltec BLE Companions. On the new platform:

- **Heltec V3 and V4 build on Arduino-ESP32 3.1.3 / ESP-IDF 5.3.** Source changes for it are version-guarded, so the same code still builds on 2.0.17 for the boards that stay there:
  - `ESPNOWRadio.cpp`: the ESP-NOW receive callback takes an `esp_now_recv_info_t`, and `esp_efuse_mac_get_default()` comes from `esp_mac.h`.
  - `MqttNetEsp32.cpp`: `mdns_query_async_get_results()` also reports the number of results.
  - `ESP32Board.h`: `adcAttachPin()` is gone; `analogRead()` attaches the pin itself.
- **NimBLE is the BLE stack of every Heltec V3 and V4 Companion build** (`_ble` and `_standalone`). The opt-in `_nimble` environments are gone, as they are now the same builds. The NimBLE class and its behaviour are described in [NIMBLE_EVALUATION](NIMBLE_EVALUATION.md).
- **Bonds and the BLE address do not carry over.** Bluedroid and NimBLE keep their bond records apart, and Arduino-ESP32 3.x derives the Bluetooth address as base MAC + 1 instead of + 2 on the ESP32-S3. A phone paired with an earlier build sees a new device after the upgrade: forget the old one and pair again.
- **Wi-Fi: a drop reported as a join starts is held for 1 s.** On ESP-IDF 5.3 the first join after boot is preceded by a disconnect event (reason 203) from an attempt the driver started itself, and the link comes up about 100 ms later. Counting that drop sent the profile into its cooldown and delayed the first link to about 25 s; the Wi-Fi state machine now counts a drop while joining only if no link follows within `join_drop_settle_ms` (1 s).

## Size

Flash and static RAM (`.data` + `.bss`) per Heltec target, from local builds in the project container. Before: main at 69755258 (ESP-IDF 4.4, Bluedroid). After: this platform (ESP-IDF 5.3, NimBLE for the BLE Companions). Percentages are of each board's app partition: 3,342,336 B on the V3, 6,553,600 B on the V4.

| Target | Flash before | Flash after | Difference | Static RAM before | Static RAM after | Difference |
|---|---|---|---|---|---|---|
| `Heltec_v3_companion_radio_ble` | 1,702,821 B (50.9 %) | 1,507,828 B (45.1 %) | −194,993 B | 131,028 B | 118,956 B | −12,072 B |
| `Heltec_v3_companion_radio_standalone` | 1,878,817 B (56.2 %) | 1,652,036 B (49.4 %) | −226,781 B | 136,980 B | 125,084 B | −11,896 B |
| `Heltec_v3_companion_radio_wifi` | 1,312,361 B (39.3 %) | 1,474,908 B (44.1 %) | +162,547 B | 114,336 B | 114,276 B | −60 B |
| `Heltec_v3_companion_radio_usb` | 682,665 B (20.4 %) | 741,880 B (22.2 %) | +59,215 B | 38,460 B | 38,880 B | +420 B |
| `Heltec_v3_repeater` | 1,191,817 B (35.7 %) | 1,390,696 B (41.6 %) | +198,879 B | 63,216 B | 63,132 B | −84 B |
| `Heltec_v3_room_server` | 1,187,221 B (35.5 %) | 1,385,672 B (41.5 %) | +198,451 B | 66,904 B | 66,820 B | −84 B |
| `heltec_v4_companion_radio_ble` | 1,701,017 B (26.0 %) | 1,494,780 B (22.8 %) | −206,237 B | 131,008 B | 118,932 B | −12,076 B |
| `heltec_v4_companion_radio_standalone` | 1,877,005 B (28.6 %) | 1,639,088 B (25.0 %) | −237,917 B | 136,960 B | 125,036 B | −11,924 B |
| `heltec_v4_companion_radio_wifi` | 1,310,717 B (20.0 %) | 1,461,916 B (22.3 %) | +151,199 B | 114,324 B | 114,236 B | −88 B |
| `heltec_v4_companion_radio_usb` | 681,069 B (10.4 %) | 727,340 B (11.1 %) | +46,271 B | 38,440 B | 38,832 B | +392 B |
| `heltec_v4_repeater` | 1,190,041 B (18.2 %) | 1,377,532 B (21.0 %) | +187,491 B | 63,180 B | 63,084 B | −96 B |
| `heltec_v4_room_server` | 1,185,421 B (18.1 %) | 1,372,540 B (20.9 %) | +187,119 B | 66,868 B | 66,772 B | −96 B |

ESP-IDF 5.3 adds about 150–200 KB of flash to a build with Wi-Fi or a role firmware, and about 50 KB to a USB-only Companion; static RAM is within 0.5 KB. NimBLE takes about 400 KB of flash and 12 KB of static RAM off a BLE Companion, more than ESP-IDF 5 adds, so the BLE Companions come out smaller than before.

On ESP-IDF 5.3, Bluedroid against NimBLE for the same build (before the switch):

| Target | Flash, Bluedroid | Flash, NimBLE | Static RAM, Bluedroid | Static RAM, NimBLE |
|---|---|---|---|---|
| `Heltec_v3_companion_radio_ble` | 1,949,024 B | 1,506,932 B | 132,512 B | 118,956 B |
| `Heltec_v3_companion_radio_standalone` | 2,092,836 B | 1,651,028 B | 138,632 B | 125,084 B |

The size budgets in `ci/size-budget.txt` are seeded from the "after" column (use + 4 KB).

## On hardware

Measured on v3test and v4test on 2026-10-24, each board first on main (2.0.17) and then with the ESP-IDF 5.3 app written over it without an erase, same store and configuration. Every row, with its build, is in [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md).

| | Heltec V3, 2.0.17 | Heltec V3, ESP-IDF 5.3 | Heltec V4, 2.0.17 | Heltec V4, ESP-IDF 5.3 |
|---|---|---|---|---|
| Setup done after reset (`setup_ms`, 50 contacts) | 7,191 ms | 6,256 ms | 4,832 ms | 2,914 ms |
| BLE idle: free / largest block | 105,056 / 94,196 B | 107,116 / 45,044 B | 145,876 / 131,060 B | 152,208 / 90,100 B |
| Wi-Fi + TLS MQTT: free / largest / low-water | 61,956 / 49,140 / 51,664 B | 45,092 / 31,732 / 32,588 B | 118,200 / 102,388 / 110,504 B | 108,228 / 42,996 / 101,380 B |
| Wi-Fi joined after reset | not timed | about 9 s | about 7–9 s | about 6 s |
| Loop task stack free: BLE idle / after BLE pairing + CLI / Wi-Fi + TLS MQTT | 5,492 / 5,476 / 5,540 B | 5,472 / 5,424 / 5,496 B | 5,520 / 5,488 / not captured | 5,516 / 5,500 / 5,516 B |

The stack figures are the least free stack of the Arduino loop task (8 KB, where every CLI command and the radio and app paths run) since boot, from `get mecon.mem` (`loop_stack_free`); both runtimes leave about 5.4 KB of it unused in every state measured. On 2.0.17 the BLE pairing step's CLI over BLE was not answered, so that baseline reading covers pairing and USB CLI only; the V4's 2.0.17 Wi-Fi reading was lost to an MQTT reconnect on that build and is not captured.

What carried over unchanged: identity, name, PIN, contacts (50 of 50) and settings through the upgrade; PIN and default-PIN BLE pairing, re-pairing after a forgotten bond, CLI over BLE and its refusals; Wi-Fi profile priority; MQTT with a CA anchor (`tls=verified`); LoRa direct messages between the two boards; the battery reading (within 20 mV).

What is worse, and stays recorded until it is won back:

- **Largest free block.** On ESP-IDF 5.3 the largest allocatable block of internal RAM is about 43–45 KB on both boards whatever the free total, where 2.0.17 had 90–130 KB. Every allocation the firmware makes today fits.
- **Wi-Fi + TLS heap on the Heltec V3.** About 17 KB less free and 19 KB less low-water than 2.0.17, which leaves about 12 KB above the 20,480 B heap floor during a TLS session. The V4 is far from its floor.

## Board exceptions

- **LilyGo T-LoRa V2.1-1.6 stays on Arduino-ESP32 2.0.17 / ESP-IDF 4.4.** Its 4 MB flash uses the `min_spiffs` partition (1,966,080 B app, with OTA). On ESP-IDF 5.3 its NimBLE BLE Companion image is 2,116,296 B, over that partition; on ESP-IDF 4.4 it is 1,492,577 B (75.9 %). It uses NimBLE on 2.0.17.
