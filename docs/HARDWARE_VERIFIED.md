# Hardware-verified matrix (`main` commits)

## Verification policy — owner decision, 25 October 2026

Only four test boards are available: **Heltec V3, Heltec V4, Seeed Wio Tracker L1 and SenseCAP T1000-E**. Public firmware must be hardware-verified on these boards for their applicable roles, transports and advertised capabilities. Availability does not mean a check has passed; the evidence matrix records actual board, role, commit, test and result. T1000-E remains compile-only until #1 supplies hardware evidence.

All other repository board environments receive **software verification only**: compilation, size checks where configured, and applicable unit/simulation/conformance tests. Publish the checks actually run and their results, including exclusions and known upstream failures. A build pass proves compilation, not runtime behaviour; a shared-stack test on another board is not hardware evidence for this board. No additional board purchase or unavailable-board UAT is required to close software-only work.

Public documentation must distinguish **hardware verified**, **software verified (with checks specified)**, **failed**, and **not verified**, at board/role/feature level. Never silently promote a board or all its features from partial evidence. The four-board hardware gate applies to applicable capabilities as they are implemented, not unsupported features. This policy supersedes older requests to wait for hardware on other boards. Release timing remains separate.

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
| Device interface (standalone) | every request fixture of `docs/contract/fixtures/requests.json` plus the health stream, through `tools/mecon_iface_hw.py`: 24 of 24 over USB, 24 of 24 over BLE in three runs with no reset between them (so the retried `config.apply` got its stored result) | 2026-10-24, 9f7b2bb7 |
| Device interface (Repeater build, USB console) | every request fixture for the Repeater role plus the health stream, `mecon> ` lines through `tools/mecon_iface_hw.py console`: 24 of 24 | 2026-10-25, 6b3f33fc |
| Rebuilt IDF libraries, 2 KB IPC stack (standalone) | 300 resets, 0 IPC canary panics (stock libraries: 4 in 304). Heap with a pinned-TLS MQTT session, against main in one session: free 50,764 B (36,956 B), low-water 38,228 B (24,860 B) | 2026-10-31, 558dfd09 |
| Signed update over BLE (standalone) | offer and confirm over the cable (a device with no record); all 1,719,040 bytes of the image pushed over BLE from a Linux host (BlueZ) in 1,524 s at the default 23-byte MTU, 13 transport retries; the new image attested its job over BLE and stayed after confirmation | 2026-10-31, bce8ef8c with the bench test key |
| MQTT broker roles | two brokers; with no record the priority-90 profile is used; after a provisioning that names the priority-10 profile `primary`, that profile is used after a reboot; an unsigned priority edit is refused while enrolled | 2026-11-01, 28bc7d7d |

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
| Device interface (standalone) | every request fixture plus the health stream, 24 of 24 over each transport: USB, BLE (two runs) and MQTT through a TLS broker with a CA anchor (`mecon/<device_id>/req`, `resp`, `ev/health`) | 2026-10-24, 6f82504c |
| Device interface (Repeater build, USB console) | every request fixture for the Repeater role plus the health stream, `mecon> ` lines through `tools/mecon_iface_hw.py console`: 24 of 24, after the receive-buffer fix (before it, lines over about 160 bytes were lost over the native USB) | 2026-10-25, 866ece59 |
| Device interface (Room server build, USB console) | the same suite for the Room server role: 24 of 24 | 2026-10-25, 866ece59 |
| Authority layer (standalone, USB) | `tools/mecon_auth_hw.py` with the public test keys: unsigned first provisioning over USB; a signed `config.apply` runs, the same bytes return the stored result, a newer job runs, the older one is refused as replay; an unsigned write is refused once enrolled; after a reset of the board the mark is kept and the job at the mark is a duplicate; `channel_nonce` in `hello`; signed `set_unlock`, a wrong HMAC answer refused and the right one unlocks; the secret in no reply; signed `deployment.reset`. 17 of 17 | 2026-10-28, 30ef4e4d |
| Authority layer: corrupt record over USB | flashed over the private firmware, the board read that firmware's Deployment files as a corrupt record and refused provisioning and signed requests (`deployment_record_corrupt`); an unsigned `deployment.reset` over the cable in Normal cleared it | 2026-10-28, 30ef4e4d |
| Rebuilt IDF libraries, 2 KB IPC stack (standalone) | 300 software resets, 0 IPC canary panics (main on stock libraries: 6 in 303). Heap against main in one session: idle free 147,612 B (140,916 B); with a pinned-TLS MQTT session free 103,484 B (96,792 B), low-water 98,692 B (92,020 B); PSRAM unchanged | 2026-11-01, 08c1072b |
| Connectivity-mode restart while a host polls | `config.apply` of `connectivity.mode` answered, then the restart 1.50 s after the reply with a `hello` every 0.33 s in between | 2026-10-31, 32cc0fb7 |
| MQTT over WebSocket to a public broker | `wss://` on port 443 with a CA anchor (ISRG Root YR) loaded by `set mqtt.profile 0 ca …`: `tls: verified`, enrolled and online in 43 s | 2026-10-31, 32cc0fb7 |

## Seeed Wio Tracker L1 (wiotest)

| Feature | Result | Date, build |
|---|---|---|
| Adaptive contacts capacity | 1000 (the store ceiling): the Companion keeps contacts on the 2 MB QSPI flash | 2026-10-21, dd5c9c44 |
| nRF52 interface worker (#158) | A test-only Repeater build with the device interface, the #157 marker and this worker, over the console with the public test keys: a first `deployment.provision` answered and signed with no reset (it reset the board before the worker); a signed `config.apply` and a signed `deployment.reset` verified on the CC310 and ran. `loop_stack_free` 1,176 B both before and after (the boot's peak); the worker's least free stack 4,864 B of 8 KB. The board was restored to stock 1.17.1 afterwards | 2026-10-29, test build of fix/158 + feat/nrf52-marker-117 |
| nRF52 receive on the worker (#162) | On the Wio Tracker L1. A Repeater build of this branch received 10 packets on the worker, all handled and with no reset. A test-only Repeater build with the device interface answered requests through the same hand-off; `loop_stack_free` 1,176 B, worker 5,720 B free (a first provisioning is refused on these builds until the nRF52 marker store, so its signed steps did not run). A test-only Companion build filled the 2 MB QSPI to 96.7 % and timed every receive: the first advert-blob save after each of 3 cold boots took 5,423, 5,398 and 5,408 ms (LittleFS's free-block scan); later saves about 850 ms. The filler files were removed and the board restored to stock 1.17.1, same identity and settings | 2026-10-29, test builds of fix/162 |

## SenseCAP T1000-E

| Feature | Result | Date, build |
|---|---|---|
| Companion on a board carrying the private firmware's data | boots and stays up; 350 of 350 contacts and 40 of 40 channels identical to a prior backup (the stock contact table); a change to the full table survives a reboot; the private firmware, flashed back, reads every contact and channel unchanged | 2026-10-31, bce8ef8c |
| Companion heap and loop stack (USB build) | idle free 54,160 B; loop stack free 816 B; 5 of 5 log events signed | 2026-11-01, 90d46a9f |
| Management writes with the private firmware's Deployment record | refused `deployment_record_corrupt` (fail closed); the record itself left unchanged | 2026-10-31, bce8ef8c |

## Not yet verified on hardware

- Wi-Fi failover when the access point itself goes off the air (the verified run breaks the joined profile instead).
- MQTT broker discovery by mDNS on a real ESP32 (verified only in the simulator).
- The Sensor role (neither on a board nor in the simulator).
- Any board not listed above.
