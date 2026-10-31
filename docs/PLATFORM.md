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
| `Heltec_v3_companion_radio_ble` | 1,512,148 B | 1,448,788 B | −63,360 B | 119,180 B | 117,852 B | −1,328 B |
| `Heltec_v3_companion_radio_standalone` | 1,772,828 B | 1,715,176 B | −57,652 B | 132,700 B | 131,124 B | −1,576 B |
| `Heltec_v3_companion_radio_usb` | 747,068 B | 707,688 B | −39,380 B | 39,096 B | 37,772 B | −1,324 B |
| `Heltec_v3_companion_radio_wifi` | 1,485,752 B | 1,428,968 B | −56,784 B | 115,524 B | 113,956 B | −1,568 B |
| `Heltec_v3_repeater` | 815,756 B | 776,232 B | −39,524 B | 43,552 B | 42,212 B | −1,340 B |
| `Heltec_v3_room_server` | 810,880 B | 771,348 B | −39,532 B | 47,240 B | 45,900 B | −1,340 B |
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

## Heap: rebuilt Arduino IDF libraries (hybrid compile)

The Heltec V3 envs (`esp32s3_idf5_base` in `platformio.ini`) carry a `custom_sdkconfig` and a `custom_component_remove` list. On the first build pioarduino's "hybrid compile" rebuilds the Arduino ESP-IDF libraries for the ESP32-S3 with them, instead of using the prebuilt set.

What is set:

- **Wi-Fi TX buffers are dynamic** (`CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER=y`, 16 buffers), not a static pool of 8 buffers of about 1.6 KB. `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=n` goes with it: Arduino's base config puts Wi-Fi and LWIP buffers in PSRAM, which locks out dynamic TX. The V3 has no PSRAM, so nothing else moves there. On a board with PSRAM Wi-Fi and LWIP buffers would come from internal RAM instead, which is why the V4 does not take this (below).
- **The IPC tasks get a 2,048-byte stack** (`CONFIG_ESP_IPC_TASK_STACK_SIZE=2048`) instead of the stock 1,024. Installing the GPIO ISR service at boot runs on the IPC task, and with heap poisoning on it overran the 1,024-byte stack in 4 of 304 resets on v3test (`Stack canary watchpoint triggered (ipc0/ipc1)`, Issue #216). Upstream's library builder made the same change. It costs 2 KB of internal heap (two tasks).
- **13 unused managed components are removed** from the library build: esp_insights, esp_rainmaker, rmaker_common, esp_diagnostics, esp_diag_data_store, esp_schedule, esp-sr, esp-zboss-lib, esp-zigbee-lib, esp_modem, esp-modbus, esp_rcp_update and esp-libhelix-mp3.

**The mbedTLS buffers stay static.** Dynamic mbedTLS buffers (`CONFIG_MBEDTLS_DYNAMIC_BUFFER`) were tried and break Wi-Fi: the radio never leaves `scanning` once a TLS session starts. Do not add them to `custom_sdkconfig`.

Measured on v3test (V3 standalone Companion, Wi-Fi + TLS MQTT verified, about 4 minutes after start, Issue #100): free heap 44,440 B on main against 59,536 B with both changes (about 15 KB more), low-water 31,796 B against 48,216 B (about 16 KB more); the largest free block is unchanged at 31,732 B. Of that, removing the components gives about 8 KB free and 11 KB low-water, and the dynamic TX buffers about 7 KB and 5 KB more. Both gains need the rebuilt libraries; there is no runtime-only route, because the prebuilt libraries reserve the static pool from their own sdkconfig.

Flash and static RAM of the V3 envs, main (bafb88d5) against this build (local builds in the project container, same toolchain and locked libraries, 2026-10-31):

| Target | Flash before | Flash after | Difference | Static RAM before | Static RAM after | Difference |
|---|---|---|---|---|---|---|
| `Heltec_v3_companion_radio_ble` | 1,509,044 B | 1,444,828 B | −64,216 B | 119,156 B | 117,828 B | −1,328 B |
| `Heltec_v3_companion_radio_standalone` | 1,713,020 B | 1,654,564 B | −58,456 B | 126,988 B | 125,412 B | −1,576 B |
| `Heltec_v3_companion_radio_usb` | 743,912 B | 704,056 B | −39,856 B | 39,064 B | 37,740 B | −1,324 B |
| `Heltec_v3_companion_radio_wifi` | 1,477,396 B | 1,419,524 B | −57,872 B | 114,636 B | 113,068 B | −1,568 B |
| `Heltec_v3_repeater` | 1,445,804 B | 1,383,208 B | −62,596 B | 63,940 B | 62,612 B | −1,328 B |
| `Heltec_v3_room_server` | 1,440,816 B | 1,378,152 B | −62,664 B | 67,628 B | 66,300 B | −1,328 B |

Flash drops by about 57-63 KB on the BLE, standalone and Wi-Fi Companions and by about 39-40 KB on the USB Companion, the repeater and the room server; static RAM by about 1.3-1.6 KB. The V3 budgets in `ci/size-budget.txt` are set from these builds (use + 4 KB).

### The Heltec V4 stays on the stock libraries

The V4 envs extend `esp32_idf5_base`, without the section. Measured on v4test with Wi-Fi + TLS MQTT, the hybrid build cost internal heap: free 101,472 B against 102,568 B on main, largest block 34,804 B against 38,900 B, low-water 91,816 B against 97,832 B. The cause is `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=n`, which the dynamic TX buffers need and which moves Wi-Fi and LWIP out of the V4's PSRAM. The rule set before the run was that a build which costs the V4 does not go to the V4. The V4 builds and budgets are unchanged from main.

V3 and V4 are both ESP32-S3, so one PlatformIO core directory cannot hold both library sets: building a V3 then a V4 (or the reverse) in one directory reinstalls the stock package and rebuilds the hybrid one again (about 6 minutes). Use separate volumes locally, or separate core directories (`all-boards.yml` gives the V4 and C6 envs their own). `target-boards` and `pr-build-check` build one env per runner, so they cannot mix.

### Building

The libraries are rebuilt once into the shared `framework-arduinoespressif32-libs` package of the PlatformIO core directory (about 6 minutes cold). pioarduino keys the rebuilt set by the `custom_sdkconfig` text and the chip, so **every ESP32-S3 env that takes the hybrid build must carry the same section** (the V3 envs do, through `esp32s3_idf5_base`): an env without it, or for another chip such as the ESP32-C6 boards, makes the next build reinstall the stock libraries and rebuild again. A CI job that mixes them uses a separate `PLATFORMIO_CORE_DIR` for the others (`all-boards.yml` does this for the C6 envs).

It is a two-step build, because the hybrid pass matches `build_src_filter` against a placeholder directory and our `../examples` entries escape it:

1. `bin/idf5_libs.sh` builds the stub env `idf5_hybrid_libs`, which only exists to trigger the library rebuild. It is idempotent (a second run takes seconds) and installs PyYAML into PlatformIO's Python if it is missing, which the component removal needs. Given an env name it does nothing unless that env has a `custom_sdkconfig`, so a workflow can call it before every build.
2. `pio run -e <env>` as usual.

Locally: `sim/run.sh bin/idf5_libs.sh`, then `sim/run.sh pio run -e Heltec_v3_repeater`. `build.sh` and the three build workflows (`target-boards`, `pr-build-check`, `all-boards`) call the script. The native sim envs are not affected. To check the result, read `packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig` in the PlatformIO core directory (the options above) and the "Removing component" lines in the first build's log.

Not done: a hardware run of the V4 and of the other roles with this build.

## On hardware

Measured on v3test and v4test on 2026-10-24, each board first on main (2.0.17) and then with the ESP-IDF 5.3 app written over it without an erase, same store and configuration. Every row, with its build, is in [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md).

| | Heltec V3, 2.0.17 | Heltec V3, ESP-IDF 5.3 | Heltec V4, 2.0.17 | Heltec V4, ESP-IDF 5.3 |
|---|---|---|---|---|
| Setup done after reset (`setup_ms`, 50 contacts) | 7,191 ms | 6,256 ms | 4,832 ms | 2,914 ms |
| BLE idle: free / largest block | 105,056 / 94,196 B | 107,116 / 45,044 B | 145,876 / 131,060 B | 152,208 / 90,100 B |
| Wi-Fi + TLS MQTT: free / largest / low-water | 61,956 / 49,140 / 51,664 B | 45,092 / 31,732 / 32,588 B | 118,200 / 102,388 / 110,504 B | 108,228 / 42,996 / 101,380 B |
| Wi-Fi joined after reset | not timed | about 9 s | about 7–9 s | about 6 s |
| Loop task stack free: BLE idle / after BLE pairing + CLI / Wi-Fi + TLS MQTT | 5,492 / 5,476 / 5,540 B | 5,472 / 5,424 / 5,496 B | 5,520 / 5,488 / not captured | 5,516 / 5,500 / 5,516 B |

### nRF52 worker: interface requests and received packets

The Adafruit nRF52 core runs `loop()` on a fixed 4 KB stack (`LOOP_STACK_SZ`). A first `deployment.provision` overflowed it on hardware (#158): the parse, the authority-set verify and the signing of the result came to about 4.8 KB of frames. Every device-interface request on nRF52 now runs on an 8 KB worker task (`src/helpers/NrfWorker.h`).
- **The hand-off.** The loop task hands one request to the worker and waits until it is answered, up to 15 s, so the request sees the same single-threaded world.
- **A job that overruns** resets the device: it is still running on the caller's buffers, so no reply is safe. If the worker task cannot be created (no memory), the request is answered `busy`, error `worker`, unsigned, and nothing runs on the small stack.
- **Strict encodings.** A signature whose S is not below the group order, or whose R is not a canonical point, is refused before any verifier runs, so every build accepts the same signatures.
- **The verify.** The authority layer verifies Ed25519 on the nRF52840's CC310 (`src/helpers/deployment/DepVerify.h`), as `mesh::Identity::verify` does; other firmware uses the Crypto library's verifier.
- **Received packets** run on the same worker (#162). A remote CLI `set` on a Repeater or Room server goes down to a prefs save, and a received advert on a Companion rewrites the advert-blob table, each through LittleFS. Measured on the T1000-E and Wio builds, those chains came to 3,730–3,780 B and about 3,600 B of the loop task's 3,840 B budget, before LittleFS's bad-block relocation, which recurses (184 B per level, 376 B through `lfs_deorphan`). `Dispatcher::processRecvPacket` hands each packet's `onRecvPacket` to the worker and waits up to 30 s, so mesh state still has one runner at a time.
  - If `run()` ever returned with the job unfinished, the device resets rather than carry on.
  - The worker is created when the Dispatcher starts, so a failure is known at boot. Packets are then handled on the loop task as before, and `ERR_EVENT_WORKER` is set in the Dispatcher's error flags.
  - The longest receive measured is 5.4 s, on a Wio Companion with its 2 MB QSPI 96.7 % full. It is the first advert-blob save after a cold boot, which pays LittleFS's free-block scan of the whole flash; later saves took about 850 ms. That leaves the 30 s wait about 5.5 times the worst case seen.
  - There is no hardware watchdog on these builds. One added later needs a timeout longer than this wait.
- **The boot's identity-rotation recovery** on a Companion with the device interface runs on the worker too (#172). It reads and rewrites the rotation statement (two 512 B buffers) and saves it through LittleFS; on the loop task it had been the deepest path, 3,568 of 3,840 B. Without a worker it runs on the loop task as before.
- **Measured.** `status.get` reports the loop task's `loop_stack_free` and the worker's `worker_stack_free`.
- **The gate.** The `stack-gate` workflow (`tools/stack_callgraph.py`) builds the T1000-E and Wio Tracker L1 Repeater, Room server and Companion envs with GCC's call-graph files. It fails a change whose worst static path is over 3,840 B on the loop task or 7,936 B on the worker, or where a hand-off no longer reaches `NrfWorker::run`: received packets, a Companion's contact-import loopback, and the boot's rotation recovery. It follows the receive chain into the file system, and counts a job nested in another on the worker together with it.

The stack figures are the least free stack of the Arduino loop task (8 KB, where every CLI command and the radio and app paths run) since boot, from `get mecon.mem` (`loop_stack_free`); both runtimes leave about 5.4 KB of it unused in every state measured. On 2.0.17 the BLE pairing step's CLI over BLE was not answered, so that baseline reading covers pairing and USB CLI only; the V4's 2.0.17 Wi-Fi reading was lost to an MQTT reconnect on that build and is not captured.

What carried over unchanged: identity, name, PIN, contacts (50 of 50) and settings through the upgrade; PIN and default-PIN BLE pairing, re-pairing after a forgotten bond, CLI over BLE and its refusals; Wi-Fi profile priority; MQTT with a CA anchor (`tls=verified`); LoRa direct messages between the two boards; the battery reading (within 20 mV).

What is worse, and stays recorded until it is won back:

- **Largest free block.** On ESP-IDF 5.3 the largest allocatable block of internal RAM is about 43–45 KB on both boards whatever the free total, where 2.0.17 had 90–130 KB. Every allocation the firmware makes today fits.
- **Wi-Fi + TLS heap on the Heltec V3.** About 17 KB less free and 19 KB less low-water than 2.0.17, which leaves about 12 KB above the 20,480 B heap floor during a TLS session. The V4 is far from its floor.

## Board exceptions

- **LilyGo T-LoRa V2.1-1.6 stays on Arduino-ESP32 2.0.17 / ESP-IDF 4.4.** Its 4 MB flash uses the `min_spiffs` partition (1,966,080 B app, with OTA). On ESP-IDF 5.3 its NimBLE BLE Companion image is 2,116,296 B, over that partition; on ESP-IDF 4.4 it is 1,492,577 B (75.9 %). It uses NimBLE on 2.0.17.
