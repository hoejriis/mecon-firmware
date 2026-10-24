# Hardware-verified matrix (standalone 0.1.x line)

What has been checked on real boards, on which board and build, and what is only compiled. A row says *verified* only
for a check made on hardware with the stated build; everything else is *compiled* (CI builds it) or *simulated* (the
simulated-board suite covers it). Compiling is not support. The intended product targets are in
[SUPPORTED_HARDWARE](SUPPORTED_HARDWARE.md); this page is the evidence against them.

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
| BLE: NimBLE build (`_nimble`) | GATT service, PIN and default-PIN pairing, re-pairing after a forgotten bond, CLI over BLE, refusals audited as `ble`; +28 KB internal heap against Bluedroid | 2026-10-22, 09c044f2 (GATT fix included) |
| Hardened posture (default PIN) | persists across a reboot; key export over USB refused and audited (`refused cable key-export`); lowering without `confirm` refused, with `confirm` it lowers; BLE never starts with the default PIN (no advertisement, audited `refused ble ble-start`) | 2026-10-23, ed890932 |
| Companion-to-Companion messaging over LoRa (with v4test) | direct messages both ways, each delivered with the ACK confirmed at the sender: 4 of 5 over two runs, the miss being the first transmission after the first boot (the firmware does not resend; the app does) | 2026-10-23, ed890932 |

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

## Seeed Wio Tracker L1 (wiotest)

| Feature | Result | Date, build |
|---|---|---|
| Adaptive contacts capacity | 1000 (the store ceiling): the Companion keeps contacts on the 2 MB QSPI flash | 2026-10-21, dd5c9c44 |

## SenseCAP T1000-E

Compiled only. Contacts capacity is derived, not measured: the Companion keeps contacts on the 100 KB ExtraFS, which
gives about 450 at the capacity rule, above stock's 350.

## Not yet verified on hardware

- The device screen and the button gesture for switching connectivity mode (the rendering is covered by the
  simulator's UI captures).
- A phone pairing with the MeshCore app, and the app reading the `v1.18.0-mecon` version string.
- Wi-Fi failover when the access point itself goes off the air (the verified run breaks the joined profile instead).
- USB recovery through the Companion's rescue console (`rebuild`, `erase`): it is entered by a long press of the user
  button within 8 s of power-up, so it needs a person at the board. Covered by the simulated suite.
- Any board not listed above.
