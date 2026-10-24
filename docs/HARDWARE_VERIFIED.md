# Hardware-verified matrix (standalone 0.1.x line)

What has been checked on real boards, on which board and build, and what is only compiled. A row says *verified* only
for a check made on hardware with the stated build; everything else is *compiled* (CI builds it) or *simulated* (the
simulated-board suite covers it). Compiling is not support. The intended product targets are in
[SUPPORTED_HARDWARE](SUPPORTED_HARDWARE.md); this page is the evidence against them.

Rows built on 2026-10-24 or later from 114feab6 onwards ran on Arduino-ESP32 3.1.3 / ESP-IDF 5.3 with NimBLE (see
[PLATFORM](PLATFORM.md)); earlier rows ran on Arduino-ESP32 2.0.17 / ESP-IDF 4.4.

Bench boards (on the MECON Pi): **v3test** (Heltec V3, MAC 44:1b:f6:fd:9c:f0), **v4test** (Heltec V4, MAC
f8:5b:1b:a1:aa:30, 2 MB PSRAM), **wiotest** (Seeed Wio Tracker L1). The Pi acts as BLE central (BlueZ, bleak), MQTT
broker host (throwaway TLS brokers) and USB host. Dates are UTC; builds are the `main` commit flashed.

## Heltec V3 (v3test)

| Feature | Result | Date, build |
|---|---|---|
| Boot to `the_mesh.begin()` | 1.58 s empty store (steady), 2.87 s first boot after erase, 2.14 s with 352 contacts (`get mecon.boot`) | 2026-10-21, 93fc3200 |
| Adaptive contacts | 350 contacts added over USB, all loaded at every boot, `damaged 0 werr 0`; no add holds the loop over 1 s | 2026-10-22, adf36c80 |
| Heap, BLE pairing (internal RAM) | low-water 97,072 B, largest block 90,100 B | 2026-10-22, adf36c80 |
| Heap, Wi-Fi + MQTT pinned-TLS session | free 63,700 B, largest 51,188 B, low-water 53,276 B; session stable over 120 s | 2026-10-22, adf36c80 |
| MQTT: connect off the main loop, failover | through repeated failovers from a black-holing broker: loop gap ≤ 1 ms, longest loop component 61 ms; the connect worker's stack peaks at about 4.2 KB of 10 KB | 2026-10-22, adf36c80 / 09c044f2 |
| MQTT CLI | `get` answered, `poweroff` refused over MQTT | 2026-10-17, 4fbc85b8 |
| CLI over LoRa (Repeater build) | remote-admin login; `ver`, `get name`, `get mecon.mem`, `get dutycycle` answer; `get mecon.cli.who` → `transport=lora … principal=remote-admin`; `erase` refused; `get prv.key` refused and audited | 2026-10-22, adf36c80 |
| Duty-cycle floor (Repeater) | `get dutycycle` reports the band's floor; boot seed holds the full hour | 2026-10-17, 4fbc85b8 |
| BLE: NimBLE build (then the opt-in `_nimble` build, ESP-IDF 4.4) | GATT service, PIN and default-PIN pairing, re-pairing after a forgotten bond, CLI over BLE, refusals audited as `ble`; +28 KB internal heap against Bluedroid | 2026-10-22, 09c044f2 (GATT fix included) |
| Hardened posture (default PIN) | persists across a reboot; key export over USB refused and audited (`refused cable key-export`); lowering without `confirm` refused, with `confirm` it lowers; BLE never starts with the default PIN (no advertisement, audited `refused ble ble-start`) | 2026-10-23, ed890932 |
| Device screen and button (standalone build) | the home screen reads `BLE`; on the connectivity page a long press asks `Switch to Wi-Fi?` and a second long press restarts into Wi-Fi mode, the home screen then reading `Wi-Fi: no profile`; the mode persists across a reset and the switch is audited as `mode button wifi` | 2026-10-24, 69755258 |
| Phone pairing with the MeshCore app (standalone build) | the home screen shows the per-session PIN with USB attached; a phone pairs with it over Bluetooth, and the app's info page shows a version starting `v1.18.0-mecon` | 2026-10-24, a0f67328 |
| Rescue console (standalone build) | entered by a long press once the logo screen shows (the banner prints about 9 s after reset); `rebuild` answers `rebuilt 2 contacts, 0 damaged` after about 88 s; `erase` formats the store (new identity, default name, audit entry `reset cable`) | 2026-10-24, 69755258 |
| Companion-to-Companion messaging over LoRa (with v4test) | direct messages both ways, each delivered with the ACK confirmed at the sender: 4 of 5 over two runs, the miss being the first transmission after the first boot (the firmware does not resend; the app does) | 2026-10-23, ed890932 |
| ESP-IDF 5.3: upgrade without erase (standalone) | the ESP-IDF 5.3 app written alone over a 2.0.17 bootloader and store boots; identity, name, PIN and all 50 contacts kept; BLE bonds are not kept, and the BLE address moves from base MAC + 2 to base MAC + 1 | 2026-10-24, 114feab6 |
| ESP-IDF 5.3: boot (standalone, 50 contacts) | `mesh_ms` 1,156 against 1,868 on 2.0.17, `setup_ms` 6,256 against 7,191 (same board and store) | 2026-10-24, 114feab6 |
| ESP-IDF 5.3: heap, BLE (standalone) | idle: free 107,116 B, largest 45,044 B (2.0.17 with Bluedroid: 105,056 B / 94,196 B); during a PIN pairing: low-water 102,524 B (105,056 B) | 2026-10-24, 114feab6 |
| ESP-IDF 5.3: BLE (NimBLE) | PIN pairing, re-pairing after the central forgot its bond, CLI over BLE (`auth=paired-pin principal=paired`); with the default PIN the app connects and a CLI command is refused and audited `refused ble cli` | 2026-10-24, 114feab6 |
| ESP-IDF 5.3: Wi-Fi + TLS MQTT (standalone) | joins in about 9 s from reset, skipping an absent higher-priority profile; MQTT `tls=verified` with a CA anchor in about 12 s, stable over 60 s; heap free 45,092 B, largest 31,732 B, low-water 32,588 B (2.0.17, same board and config: 61,956 B / 49,140 B / 51,664 B) | 2026-10-24, bd138cf9 |
| ESP-IDF 5.3: LoRa DM with v4test | 9 of 9 direct messages delivered over three runs, ACK confirmed at the sender for 7 (the two misses in the first run after boot, as on 2.0.17) | 2026-10-24, bd138cf9 |
| ESP-IDF 5.3: battery reading | 4,226 mV (4,244 mV on 2.0.17 the same hour) | 2026-10-24, 114feab6 |
| ESP-IDF 5.3: loop task stack (standalone) | least free since boot (`loop_stack_free`): BLE idle 5,472 B, after a PIN pairing and CLI over BLE 5,424 B, Wi-Fi + TLS MQTT 5,496 B (2.0.17 with the same reporting: 5,492 / 5,476 / 5,540 B) | 2026-10-24, 8c192a4a |

## Heltec V4 (v4test)

| Feature | Result | Date, build |
|---|---|---|
| Heap (internal RAM; PSRAM reported apart) | BLE idle largest 131,060 B; BLE pairing low-water 137,592 B; Wi-Fi joined largest 155,636 B; Wi-Fi + pinned-TLS MQTT largest 106,484 B | 2026-10-21, ce77034d |
| Wi-Fi standalone mode | joins in about 14 s from reset; reception continues in Wi-Fi mode (18 packets in 10 min, 0 receive errors) | 2026-10-21, ba722b41 |
| MQTT TLS | pinned fingerprint `tls=verified`; CA anchor: a renewed certificate under the same CA is accepted, another CA's is refused | 2026-10-21, ce77034d |
| KISS modem duty floor | at the 1 % floor: first frame held 28.5 s after boot, then 0.92 % of airtime over 185 s | 2026-10-21, dd5c9c44 |
| Reply routing | 22 USB replies on USB, 3 BLE replies on BLE, interleaved, none crossed | 2026-10-21, ce77034d |
| Default-PIN BLE refusals | key export and CLI refused, audited `refused ble …` | 2026-10-21, ce77034d |
| Connectivity mode | `set connectivity.mode wifi|ble` switches and restarts; the USB console answers in both modes | 2026-10-21, ce77034d |
| Wi-Fi profile priority | with a higher-priority profile whose network does not exist, the scan skips it and the board joins the lower-priority one, about 8 s from boot | 2026-10-23, ed890932 |
| Wi-Fi failover between two access points | joins the priority-90 network about 8 s from boot; with that profile's passphrase broken on the running board, fails over to the priority-10 network in about 9 s; with the passphrase restored, returns to the priority-90 network in about 3 s | 2026-10-23, ed890932 |
| ESP-IDF 5.3: upgrade without erase (standalone) | app written alone over a 2.0.17 bootloader and store: identity, name, PIN and 50 contacts kept | 2026-10-24, 114feab6 |
| ESP-IDF 5.3: boot (standalone, 50 contacts) | `setup_ms` 2,914 against 4,832 on 2.0.17 | 2026-10-24, bd138cf9 |
| ESP-IDF 5.3: heap, BLE (standalone) | idle: free 152,208 B, largest 90,100 B (2.0.17 with Bluedroid: 145,876 B / 131,060 B); with a paired central: low-water 151,980 B | 2026-10-24, bd138cf9 |
| ESP-IDF 5.3: BLE (NimBLE) | PIN pairing and CLI over BLE (`auth=paired-pin`) | 2026-10-24, bd138cf9 |
| ESP-IDF 5.3: Wi-Fi + TLS MQTT (standalone) | joins in about 6 s from reset (2.0.17: about 7 to 9 s), skipping an absent higher-priority profile; MQTT `tls=verified` in about 9 s (2.0.17: 11 to 13 s); heap free 108,228 B, largest 42,996 B, low-water 101,380 B (2.0.17: 118,200 B / 102,388 B / 110,504 B) | 2026-10-24, bd138cf9 |
| ESP-IDF 5.3: battery reading | 4,122 mV (4,139 mV on 2.0.17 the same hour) | 2026-10-24, bd138cf9 |
| ESP-IDF 5.3: loop task stack (standalone) | least free since boot (`loop_stack_free`): BLE idle 5,516 B, after a PIN pairing and CLI over BLE 5,500 B, Wi-Fi + TLS MQTT 5,516 B (2.0.17: 5,520 / 5,488 B in BLE; its Wi-Fi reading was not captured) | 2026-10-24, 8c192a4a |

## Seeed Wio Tracker L1 (wiotest)

| Feature | Result | Date, build |
|---|---|---|
| Adaptive contacts capacity | 1000 (the store ceiling): the Companion keeps contacts on the 2 MB QSPI flash | 2026-10-21, dd5c9c44 |

## SenseCAP T1000-E

Compiled only. Contacts capacity is derived, not measured: the Companion keeps contacts on the 100 KB ExtraFS, which
gives about 450 at the capacity rule, above stock's 350.

## Not yet verified on hardware

- Wi-Fi failover when the access point itself goes off the air (the verified run breaks the joined profile instead).
- Any board not listed above.
