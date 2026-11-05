# Hardware-verified matrix (`main` commits)

## Verification policy — owner decision, 25 October 2026

Only four test boards are available: **Heltec V3, Heltec V4, Seeed Wio Tracker L1 and SenseCAP T1000-E**. Public firmware must be hardware-verified on these boards for their applicable roles, transports and advertised capabilities. Availability does not mean a check has passed; the evidence matrix records actual board, role, commit, test and result.

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
| Logs over MQTT, signed (standalone, provisioned with the public test keys) | through a TLS broker with a CA anchor (`tls=verified`): a plain `sub.start` for `logs` is refused; signed with no channel nonce is refused (`bad_channel_nonce`); signed with the nonce from a `hello` on that link is allowed, and a `logs` event arrives on `mecon/<device_id>/ev/logs`. 9 of 9 checks | 2026-11-04, ad4bfe77 (the pull request's branch) |
| `message.send` over USB (standalone), to a Heltec V4 on stock MeshCore companion-v1.18.0 | a direct message read by the stock app frames, its native ACK reported as `acked` with one hash and ACK code throughout; a private-channel message read in its slot, ending `transmitted`; a repeat gave the kept answer and no second message; a contact with no radio behind it reads `timed-out`; a reboot reads `unknown` under a new epoch. 21 of 21 checks, on a radio setting of their own | 2026-11-04, 0115549e (the pull request's branch) |
| `inbox.list` and `inbox.ack` over USB (standalone), from a Wio Tracker L1 sending on a private setting | before any read a reception is counted and lost, and the stock queue has it; a direct message read with the whole sender key, the sender's time, type and native hash; a stock app frame and the copy each still have it after the other reads; `inbox.ack` releases it; a private-channel message names its slot and no sender; 7 unread read back in order over pages; an ack under another epoch refused; events carry counts only; a reboot empties the copy under a new epoch | 2026-11-05, 8445e62c |
| `clock back` over USB | a clock a day ahead goes back to the real time and says what it costs; forward, below the floor and nonsense refused and the clock unchanged; audited as `clock-back cable`. Peers ignoring adverts was not run on a board | 2026-11-05, 8445e62c |
| The reserved mark (bench image with a counter) | one mark save for the first signed id; none for 30 more, for the id at the top of the reservation, or for 10 signed private `sub.start`; one more for the id above it; a reboot lands on the reservation (258 + 256 = 514) and the next id is refused with the mark, then continues above it. SPIFFS showed 0 erases across these saves on a near-empty file system | 2026-11-05, 8445e62c |
| Device interface (Repeater build, USB console) | every request fixture for the Repeater role plus the health stream, `mecon> ` lines through `tools/mecon_iface_hw.py console`: 24 of 24 | 2026-10-25, 6b3f33fc |
| Rebuilt IDF libraries, 2 KB IPC stack (standalone) | 300 resets, 0 IPC canary panics (stock libraries: 4 in 304). Heap with a pinned-TLS MQTT session, against main in one session: free 50,764 B (36,956 B), low-water 38,228 B (24,860 B) | 2026-10-31, 558dfd09 |
| Signed update over BLE (standalone) | offer and confirm over the cable (a device with no record); all 1,719,040 bytes of the image pushed over BLE from a Linux host (BlueZ) in 1,524 s at the default 23-byte MTU, 13 transport retries; the new image attested its job over BLE and stayed after confirmation | 2026-10-31, bce8ef8c with the bench test key |
| MQTT broker roles | two brokers; with no record the priority-90 profile is used; after a provisioning that names the priority-10 profile `primary`, that profile is used after a reboot; an unsigned priority edit is refused while enrolled | 2026-11-01, 28bc7d7d |
| Sensor role (`Heltec_v3_sensor`) | boots after a flash erase; `get role` reads `sensor`; `get dutycycle` reports the band and the 10.00 % floor; `get posture` answers `??: posture` | 2026-11-04, this PR's branch |
| TLS broker by address: no SNI | a static profile with host `10.0.0.69` connects (`tls=unverified`) and the broker sees the client; the TLS front recorded `SNI None` (the same front recorded a name from an `openssl s_client` control) | 2026-11-04, this PR's branch |
| First boot after a flash erase | 0 `task_wdt` lines and no reboot in 60 s (3,268 lines before the change) | 2026-11-04, this PR's branch |
| BLE MTU | ATT exchange: server RX MTU 179 (a 176-byte frame fits a notification), `status.get` `ble_started` true | 2026-11-04, this PR's branch |
| Multi-frame requests over the UART under mesh traffic (3.2 KB, each sent after the previous answer) | off the mesh channel: 0 of 1,500 dropped. On it, core default FIFO threshold (120 bytes): 4 to 13 of 1,500 dropped across runs, and in the run that counted them 77 FIFO overflows. With a threshold of 32 bytes: 2 of 1,500 dropped, 34 overflows. A doubled receive buffer changed nothing, and the longest loop pass stayed near 50 ms. A dropped request answers `invalid` / `incomplete` and is resent | 2026-11-04, this PR's branch |

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
| `config.apply` of a nine-key broker profile | answered in 1.25 to 1.30 s (9.45 s before the change, measured 2026-10-31); every key read back after a reboot | 2026-11-04, this PR's branch |
| First boot after a flash erase | 0 `task_wdt` lines and no reboot in 100 s (3,516 lines before the change) | 2026-11-04, this PR's branch |
| Longest interface line on the Repeater's native USB | lines of 200, 1,000, 1,590 and 1,600 B each answered, with the 2,048-byte receive buffer | 2026-11-04, this PR's branch |

## Seeed Wio Tracker L1 (wiotest)

| Feature | Result | Date, build |
|---|---|---|
| Adaptive contacts capacity | 1000 (the store ceiling): the Companion keeps contacts on the 2 MB QSPI flash | 2026-10-21, dd5c9c44 |
| nRF52 interface worker (#158) | A test-only Repeater build with the device interface, the #157 marker and this worker, over the console with the public test keys: a first `deployment.provision` answered and signed with no reset (it reset the board before the worker); a signed `config.apply` and a signed `deployment.reset` verified on the CC310 and ran. `loop_stack_free` 1,176 B both before and after (the boot's peak); the worker's least free stack 4,864 B of 8 KB. The board was restored to stock 1.17.1 afterwards | 2026-10-29, test build of fix/158 + feat/nrf52-marker-117 |
| nRF52 receive on the worker (#162) | On the Wio Tracker L1. A Repeater build of this branch received 10 packets on the worker, all handled and with no reset. A test-only Repeater build with the device interface answered requests through the same hand-off; `loop_stack_free` 1,176 B, worker 5,720 B free (a first provisioning is refused on these builds until the nRF52 marker store, so its signed steps did not run). A test-only Companion build filled the 2 MB QSPI to 96.7 % and timed every receive: the first advert-blob save after each of 3 cold boots took 5,423, 5,398 and 5,408 ms (LittleFS's free-block scan); later saves about 850 ms. The filler files were removed and the board restored to stock 1.17.1, same identity and settings | 2026-10-29, test builds of fix/162 |
| The reserved mark (bench image with a counter and the interface added) | as on the Heltec V3, with erases: a mark save costs 5 page erases; 30 further signed ids, the id at the reservation's top and 10 signed private `sub.start` cost none; the id above it costs 5 again | 2026-11-05, 8445e62c |

## SenseCAP T1000-E

| Feature | Result | Date, build |
|---|---|---|
| Companion on a board carrying the private firmware's data | boots and stays up; 350 of 350 contacts and 40 of 40 channels identical to a prior backup (the stock contact table); a change to the full table survives a reboot; the private firmware, flashed back, reads every contact and channel unchanged | 2026-10-31, bce8ef8c |
| Companion heap and loop stack (USB build) | idle free 54,160 B; loop stack free 816 B; 5 of 5 log events signed | 2026-11-01, 90d46a9f |
| Management writes with the private firmware's Deployment record | refused `deployment_record_corrupt` (fail closed); the record itself left unchanged | 2026-10-31, bce8ef8c |
| `get dutycycle` per band (Heltec V3 standalone Companion) | 869.525 MHz: the 10% floor; 915.0 and 922.4 MHz: `band none`, `no time rule enforced`; back at 869.525 the floor returns; nothing sent | 2026-11-05, b6d0a50e |
| `message.deliver` (Heltec V3 standalone Companion, USB) | a stock frame reads `[via manager] <text>` in the sender's thread with the time supplied; `inbox.list` says `source: manager`; a sender not stored and a text too long are answers; airtime used did not change | 2026-11-05, b6d0a50e |
| Clock unset until a real source sets it (Heltec V3) | never set: an event carries no time; set: it carries the time; a soft reset keeps it | 2026-11-05, b6d0a50e |
| Clock unset until a real source sets it (Wio Tracker L1) | never set: no time; set: the time; after a reset unset again, and it says so | 2026-11-05, b6d0a50e |
| Public Wio Tracker L1 Companion USB build | hello, first provisioning, a signed `config.apply`, an unsigned one refused, a signed reset; 10 of 10; restored to stock with 0 of 27 settings changed | 2026-11-05, b6d0a50e |
| `online` means requests are accepted (Heltec V3 standalone Companion, verified broker) | a request sent the moment `online` is seen is answered about 0.2 s later, 3 of 3 boots, no resend | 2026-11-05, d23437d5 |
| A tentative change over a broker keeps its answer (Heltec V3 standalone Companion, verified broker) | the answer arrives, the change is in effect, unconfirmed it reverts and the profile is as it was | 2026-11-05, d23437d5 |
| HTTPS download beside a verified broker session (Heltec V4 standalone Companion, test key, budget lowered) | 1.72 MB in about 16 s; session up throughout; lowest free 39,728 B, lowest largest block 31,732 B; attested and confirmed | 2026-11-05, 082fa4b6 |
| Management-channel slot and `identity.rotate` (Heltec V3 and V4, Repeater and Room server, USB console) | bind, rotate and remove over the cable; a signed change that reverts unconfirmed; a confirmed one that stays; a reboot while pending that reverts; rotation of the key with the record kept; 34 of 34 on each of the four builds | 2026-11-05, adaf9fed and f0067d70 |
| Device interface over a TLS-verified broker (Heltec V3 and V4, Repeater and Room server) | hello; a plain session changes and reads nothing; hardened keeps a verified broker across a reboot; signed logs (plain refused, signed with no nonce refused, signed with the nonce allowed, an entry arrives); a health event; a tentative change answers and reverts; the session returns after a reboot; 22 of 22 on each of the four builds | 2026-11-05, adaf9fed and a76e33d7 |
| Repeater and Room server heap with a verified session (Heltec V3, V4) | free about 137 and 133 KB on V3, about 164 and 161 KB on V4; lowest loop stack free about 1,300 B, at a revert | 2026-11-05, adaf9fed and a76e33d7 |

Builds b6d0a50e, d23437d5, adaf9fed, f0067d70 and a76e33d7 were bench merges of branches then open; their features are on `main` by these rows' merge.

## Not yet verified on hardware

- Wi-Fi failover when the access point itself goes off the air (the verified run breaks the joined profile instead).
- MQTT broker discovery by mDNS on a real ESP32 (verified only in the simulator).
- The Sensor role beyond boot, its console and `get dutycycle`.
- Any board not listed above.
