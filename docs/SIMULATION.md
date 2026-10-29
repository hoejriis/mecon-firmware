# Simulated-board test harness

> **A pass in this harness is software verification on simulated boards. It is not a
> hardware claim.** No radio, flash, USB or BLE stack of a real board is exercised.

The harness builds the real application code (`examples/companion_radio`, `examples/simple_repeater` and `examples/simple_room_server`, with no simulator-specific edits) into Linux executables and drives them the way a
client would. It is the regression rig for work that has no hardware to run on.

## Running it

Everything runs inside the dev container (never on the host). The image carries `mosquitto`, `mosquitto-clients`, `openssl` and `libssl-dev` for the MQTT tests (CI installs the same packages):

```sh
sim/build.sh sim_repeater          # pio run -e sim_repeater, log in .sim-logs/
sim/build.sh sim_companion
sim/run.sh python3 -m pytest sim/tests -q
```

`sim/run.sh <cmd>` is `docker run` with the repo mounted and the PlatformIO cache volume
attached. For long jobs run it detached with output redirected to a file under
`.sim-logs/` and poll the file.

## Architecture

| Piece | Where | What it does |
|---|---|---|
| `sim` variant | `variants/sim/` | `platform = native` envs `sim_companion`, `sim_repeater` (and `sim_repeater_eth`, `sim_room_server_eth`, which add the RAK4631 Ethernet TCP console over a loopback listener); `target.h/.cpp` replace the board layer |
| Arduino/FS shims | `variants/sim/shims/` | just enough `Arduino.h`, `FS.h`, `SPIFFS.h`, `RTClib.h` to compile the examples; no upstream file is edited |
| SimRadio | `variants/sim/sim_radio.cpp` | `mesh::Radio` that sends/receives raw packets through the ether over a unix socket |
| Ether broker | `sim/ether.py` | decides who hears whom, per-link RSSI/SNR/loss, holds the channel busy for the packet's time on air, delivers at end of airtime, resolves overlapping receptions (capture effect, below), tells every node that can hear a transmission that the channel is busy |
| Time on air | `variants/sim/sim_toa.h` | Semtech formula (explicit header, CRC on, low-data-rate optimisation when Tsym > 16 ms) |
| Clock | `variants/sim/sim_clock.h` | `SimClock` interface behind `millis()`/`micros()`/`delay()` and the RTC: the scaled wall clock by default, or with `--lockstep` a clock the ether broker advances (see [Lock-step virtual time](#lock-step-virtual-time)) |
| Wi-Fi | `variants/sim/sim_wifi.cpp`, `shims/WiFi.h` | the ESP32 `WiFi` API subset the Wi-Fi controller uses, over access points the test defines (`WifiEnv` in `sim/harness.py`: SSID, password, RSSI, up/down at run time, in a file the node re-reads); scans take 0.3 s and joins 0.4 s of node time; the ESP-IDF scan calls the controller drives (`shims/esp_wifi.h`, `shims/esp_event.h`) sit over a simulated driver that a test can make wedge (the scan never ends and the driver refuses another until it is stopped) or make deaf (Arduino's tables never fill; `WifiEnv.scan(mode)`); failures use the ESP32 reasons; every transition is logged with the SSID only |
| MQTT network | `variants/sim/sim_mqtt_net.cpp`, `shims/Client.h`, `shims/IPAddress.h` | the `MqttNet` interface of the MQTT controller ([MQTT broker profiles](MQTT.md)) over real host sockets, so a real Mosquitto started by the test is the broker; TLS is OpenSSL (certificate checked against the pinned fingerprint, as on the device); DNS names and mDNS responders come from a file the test rewrites at run time (`MqttEnv` in `sim/harness.py`: name to address; responder with service type, site ID, address and port; removing a line withdraws it); a browse takes 0.2 s of node time and never blocks; every transport event is logged as `SIMMQTT ...` with addresses only. Built into `sim_companion_wifi_usb` and `sim_companion_wifi`. The connect runs on a worker thread (`SimWorker`: created on first use, reused, one job at a time) as the ESP32 adapter runs it on a FreeRTOS task, so the asynchrony is testable; the name table is read under a mutex because the worker and the main loop both read it |
| Loop-gap probe | `variants/sim/sim_main.cpp` | logs `SIMLOOP gap_ms=<n>` for each new longest gap (at least 5 ms) between two main-loop iterations, in wall-clock time whatever `--time-scale` is; `sim.harness.loop_gaps(node)` reads it. The last value is the longest stall of the run |
| Display and button | `variants/sim/sim_display.*` | the real `ui-new` screens run on a `DisplayDriver` that draws nothing and appends each distinct frame's text to `--ui-frames`; the user button replays gestures (`click`, `long`, `double`, `triple`) appended to `--ui-input`. Built into `sim_companion_standalone` |
| Simulated BLE | `variants/sim/sim_ble.h` | stands in for `SerialBLEInterface` (`SIM_BLE_STUB`). A second loopback TCP listener (port in `<--port-file>.ble`, read by `Node.connect_ble(pin)`) carrying the same frames as the USB port, behind a pairing: the central's first line must be `PAIR <pin>`; the right PIN answers `PAIRED` and the link is bonded for that connection, a wrong PIN answers `REJECT` and is closed, and anything else before pairing is closed. It reports its trust exactly like the real class (trusted only with a PIN that is neither 0 nor 123456, never a cable). The listener exists only after `begin()` (not in Wi-Fi mode, not in Hardened with a default PIN) and takes one central at a time, only while enabled; the Bluetooth page and `disable()` drop the link. Logs `SIMBLE begin`, `paired`, `pairing rejected`, `central left` (never the PIN). `--ble-env FILE` saying `connected` still makes the status screen show a phone. No radio, advertising, passkey UX, encryption, MTU or bond storage: BLE hardware stays unverified |
| SimFS | `variants/sim/sim_fs.cpp` | the ESP32 SPIFFS API (`fs::FS`, `File`) over a per-node host directory, with a power-cut fault hook |
| Serial | `variants/sim/sim_runtime.cpp` | the USB serial port is a loopback TCP listener; Companion frames and the repeater CLI pass through unchanged |
| Runner | `sim/harness.py`, `sim/tests/` | starts the broker and N nodes (each in its own temp dir) and drives them with a small Companion client and a CLI client |

Node command line: `--dir`, `--ether`, `--wifi-env`, `--mqtt-env`, `--name`, `--port-file`, `--seed`, `--time-scale`, `--lockstep` and `--quantum-us N` (lock-step virtual time, below),
`--rtc`, `--heap-budget BYTES` (the free heap the node reports at boot; the contact cache is sized from it; default 300000), `--heap-file FILE` (a file `FREE [LARGEST]` the test rewrites while the node runs, `Node.heap(free, largest)`: what `get mecon.mem` and the heap-floor check read from then on; the contact store keeps the boot budget), `--fs-size BYTES` (size of the simulated file system; default 1.5 MB), `--fs-calls FILE` (rewrites FILE with the running counts of file system calls, `exists=N open=N remove=N rename=N`, so a test can bound the work of a boot; a real SPIFFS scans its file table on every call, which the host directory hides), `--fault /path:BYTES`, and `--toa SF BW CR PREAMBLE LEN` (print time on air in
microseconds and exit). A node exits with status 75 on `reboot` and 0 on `poweroff`.

### Design notes and limits

* **Platform macro.** The examples pick their filesystem code by platform macro. The sim
  defines `ESP32` for project sources only (`build_src_flags`) so they take the SPIFFS path;
  libraries are built without it. `variants/sim/sim_prelude.h` is force-included to keep
  the Crypto library's portable software AES (its header otherwise selects an ESP32
  hardware class).
* **Time is real time, scaled, unless a test asks for lock-step.** `SimClock` is an interface
  and `--time-scale` multiplies the wall clock (the broker takes the same scale), so long
  windows can run faster, but such runs are not repeatable: a node and the broker stamp
  time independently and drift apart whenever a process is starved. LoRa-only tests can
  opt into [lock-step virtual time](#lock-step-virtual-time) instead.
* **Static-init order.** The examples construct their mesh object before `main()`, so the
  clock and the config are function-local statics.
* **Channel model.** Receivers that are tuned to the transmission's frequency, bandwidth and
  SF and have a link hear it; a node that transmits during a reception loses the packet. There
  is no noise floor model; RSSI/SNR are whatever the link says.
* **Capture effect.** When two receptions overlap at one node, strength is the link SNR
  (`set_link(..., snr=)`) and the threshold is `Ether(capture_db=6.0)` (the `capture_db`
  attribute can be changed on a running ether):
  * neither is the threshold stronger than the other (equal strength included): both are lost
    (`collision`);
  * the earlier one is at least the threshold stronger: it survives, the later one is lost
    (`captured`);
  * the later one is at least the threshold stronger: it survives only if it starts before the
    last 5 symbols of the earlier packet's preamble (the preamble the firmware programs: 32
    symbols at SF ≤ 8, 16 above), and the earlier one is lost (`captured`). Starting later, it
    finds the receiver locked onto the earlier packet, which it destroys: both are lost
    (`collision`).

  Sources: the 6 dB threshold and the 5-symbol critical section at the end of the preamble are
  the model of Bor, Roedig, Voigt and Alonso, "Do LoRa Low-Power Wide-Area Networks Scale?"
  (MSWiM 2016) and its LoRaSim simulator. The timing rule for a later, stronger packet follows
  the SX1276 measurement in Gu, Jiang and Tan, "Attack-Aware Data Timestamping in Low-Power
  Synchronization-Free LoRaWAN" (ICDCS 2020, arXiv:1905.01679): with an 8-symbol preamble a
  stronger frame that starts before the victim's 5th chirp is received instead, and one that
  starts later is not, because the receiver locks onto the victim's preamble from the 6th chirp.
  The two differ by one symbol on an 8-symbol preamble; the simulator uses Bor's 5 symbols,
  which on MeshCore's 16- and 32-symbol preambles leaves 11 and 27 symbols in which the receiver
  can still be taken over. The rule is pairwise: a packet survives only if it wins against every
  reception it overlaps. One node's own back-to-back transmissions never collide (broker/node
  clock jitter, not an overlap on air).
* **Channel activity (listen-before-talk).** At the start of every transmission the broker
  sends each node that can hear it (tuned, with a link, whether or not loss or a collision will
  later spoil the packet) a busy notice with the airtime. SimRadio's `isReceiving()` reports
  busy until that airtime is over, and that is all the firmware asks before transmitting:
  `Dispatcher::checkSend()` calls `isReceiving()` and, while it is true, retries every
  `getCADFailRetryDelay()` (200 ms) for up to `getCADFailMaxDuration()` (4 s). On a real board
  `RadioLibWrapper::isReceiving()` is the radio's preamble/header detection plus
  `isChannelActive()` (the RSSI interference threshold and, when enabled, hardware CAD); the
  simulator models the outcome (the channel is busy while a transmission is audible), not the
  SX126x CAD procedure itself. Under lock-step the notice is read at the next barrier, so the
  busy span may end up to one quantum late.
* Not simulated: the BLE radio and stack (the transport and its PIN pairing are, see above), the Wi-Fi radio itself (association timing, roaming, DHCP; only the access-point model above), displays, GPS/sensors, the real SX126x state machine and its CAD procedure (only its result, above),
  power saving, flash wear/latency.

## Storage fault injection

`--fault /path:BYTES` kills the process (status 99, nothing closed or flushed beyond what was written) after BYTES
of the next write to that file. `--fault-before rename:/path` or `remove:/path` kills it just before that operation
(the source path for a rename). SimFS refuses rename over an existing file, as ESP32 SPIFFS does.

On stock 1.18 the prefs save rewrites `/prefs.json` in place: cut after 40 bytes it leaves a truncated file that the
loader accepts, the early field (`name`) takes the new value and a field in the lost tail (`tx`) silently reverts to its
default, with no error. With atomic saves the same cut lands in `/prefs.json.tmp`, and the previous complete prefs,
identity and channel table survive a cut at any byte or between write and rename (`test_atomic_storage.py`).

Tests that wait on a node's first transmission after a boot are marked `@pytest.mark.timescale(10)`: the duty-cycle window is seeded at the limit on boot, so the first packet can wait for up to one 30 s bin of airtime to age out, and these tests run ten times faster than real time to keep that wait short.

## Lock-step virtual time

`--lockstep` replaces a node's clock with one the ether broker owns (`LockstepClock` in `variants/sim/sim_radio.cpp`;
`Ether(..., lockstep=True)` in `sim/ether.py`). It is opt-in per test with `@pytest.mark.lockstep(quantum_us=1000,
start_after=0)`, which puts both the `ether` fixture and every node made by `nodes` on it.

* **The barrier.** A node runs one `loop()`, then tells the broker how far it wants to go (the end of the current
  quantum, on a grid shared by all nodes) and blocks. `delay()` and `delayMicroseconds()` ask for now + the delay
  instead, so a sleeping node does not hold the barrier quantum by quantum. The broker advances its time, a plain
  microsecond counter, to the smallest request only once every connected lock-step node has reported, sends every
  reception due by then, and then sends the grant. A node's `millis()` is the granted time minus the broker time at
  which it joined, so it starts at 0 at every boot as on a board, and a transmission started at node time t is
  stamped join + t by the broker: the two clocks agree exactly.
* **Frames are applied only at a barrier.** Under lock-step the node reads the ether socket only while it waits for
  a grant, and stops after the grant, so a busy or receive frame becomes visible to the firmware at the same virtual
  time in every run, whatever the wall-clock moment it arrived. Receptions due at the same time are delivered in
  the order of sender and receiver names, not in the order their frames reached the broker. With the same seeds,
  stored prefs and inputs, the broker's `tx_log` is identical from run to run (`test_lockstep.py`).
* **Joining.** A node connects to the broker before `setup()` (after publishing its serial port) with an `L` frame
  and gets the broker's current time in a `G` frame. `start_after=N` holds time at 0 until N lock-step nodes have
  joined, so nodes started one after another still boot at the same virtual instant. A node that leaves (a reboot)
  is dropped from the barrier; while no lock-step node is connected, time stands still. Protocol additions:
  node to broker `L` (join) and `A` (u64 microseconds wanted); broker to node `G` (u64 microseconds granted).
* **No mixing.** A node without `--lockstep` on a lock-step ether, or the reverse, is refused: the broker records
  it in `Ether.errors` and closes the connection (a lock-step node then exits with status 3), and the `ether`
  fixture fails a test whose broker recorded any error.
* **What stays on the wall clock.** The test itself (its waits are wall-clock timeouts; use `Ether.wait_until_us()`
  to wait for virtual time) and everything that talks to a real peer: the serial and BLE TCP ports, Wi-Fi, MQTT and
  TLS. Commands sent over the serial port reach the node at whatever virtual time it has reached, so a run that
  depends on them is exact but not repeatable. The Wi-Fi, MQTT, TLS and BLE tests therefore stay on scaled real
  time; nothing in the firmware may busy-wait on `millis()` under lock-step, since time does not move inside a
  quantum.
* **Speed and the default quantum.** Virtual time runs as fast as the barriers allow. Measured with
  `sim/lockstep_speed.py` (idle Repeaters in range of each other, in the dev container on an Apple-silicon laptop):

  | nodes | quantum 1 ms | quantum 10 ms |
  |---|---|---|
  | 1 | 199 virtual s per wall s | 1,917 |
  | 3 | 26 to 29 | 275 to 306 |
  | 7 | 14 | 165 |

  The cost is per barrier (about 29,000 a second with three nodes), so the quantum buys speed almost linearly. The
  default is **1 ms**: it is the resolution of `millis()`, so the firmware never sees its clock jump, a timer fires
  in the millisecond it is due, a send is charged its time on air rounded up by less than 1 ms, and a node sees
  another's transmission begin within 1 ms (10 ms would delay each of these by up to 10 ms). Three nodes at 1 ms
  already run faster than the 10x scaled real time most tests use. A test that needs hours of virtual time with
  little traffic can ask for `quantum_us=10000`.
* **Which tests run in lock-step.** Every test whose nodes talk only over the simulated ether:
  `test_mesh.py`, `test_mesh_protocol.py` and `test_lockstep.py` whole; in `test_duty_cycle.py` every test with
  nodes (the seven-node load test and the reboot test at `quantum_us=10000`, the rest at 1 ms); and the radio tests
  of `test_contacts.py` (favourites reachable, a contact that wrote to us), `test_postures_admin.py` (remote admin of
  a Repeater and a Room server), `test_repeater_prvkey.py`, `test_atomic_blobs_acl.py` (the client ACL power
  cuts), `test_malformed_input.py` (TRACE packets injected through the broker), `test_companion_remote_cli.py` and
  the remote-CLI tests of `test_postures_companion.py` (the `companion_wifi_usb` build there has no network
  configured, so its only peer is the other node). The tests of those files that use no ether are unchanged.
  Tests that use the ether but stay on scaled real time, deliberately, because a node in them also has a peer on a
  real socket with wall-clock timeouts: `test_cli_unified.py` and `test_mqtt_nonblocking.py` (Mosquitto),
  `test_connectivity_mode.py` (Wi-Fi, Mosquitto, BLE), `test_companion_ble.py` (the BLE link) and
  `test_companion_transport.py` (the app on the Wi-Fi TCP server). The Wi-Fi, MQTT, TLS, Ethernet and UI tests
  without an ether stay on real time too, and `test_ether_physics.py`
  drives the broker's physics without nodes (on real time, or on a lock-step clock the test sets
  itself for exact start times).
* **Writing a lock-step test.** Wait on broker time (`Ether.wait_until_us()`), not on `time.sleep()`: an absence
  check such as "no login success" then holds for a fixed span of virtual time however slowly the host runs. Two
  nodes started together join at the same broker instant, and each holds its first packet until the first 30 s
  duty-cycle bin has aged out, so their first packets start in the same quantum and collide, in every run rather
  than now and then: wait for a server's boot advert (`Ether.wait_tx()`) before sending it anything that needs an
  answer, and for a quiet channel (`Ether.wait_quiet()`) before a sender that cannot hear the previous exchange's
  follow-ups (a path return). `Ether.joined_us[name]` is the broker time of a node's latest join, the instant its
  `millis()` read 0; a time read before a restart is earlier by however many quanta the old process still ran.
  To make something happen at a known broker time, `Ether.hold_at(t)` lets time advance no further than `t` and
  waits until every node is blocked on its grant; a CLI line sent then is read by the node's first `loop()` after
  `Ether.release()` (`test_lockstep.py`'s deferral test queues two adverts 200 ms apart this way).
  When a test fails, the `ether` fixture prints the broker's last 40 transmissions
  (sender, start, type, who heard it, who lost it and why) under "Captured stdout teardown".

## Test inventory

* `test_boot_times.py` – `get mecon.boot` on every role: the stage times are present and in order (store ≤ mesh ≤ setup on the Companion; `n/a` for the store stage on the Repeater and Room server).
* `test_toa.py` – node's time on air against an independent restatement of the Semtech
  formula for five SF/BW/CR/length combinations (within 1 ms) and two published values.
* `test_repeater_cli.py` – `ver`, `get name`, `set name` and persistence across a restart.
* `test_mesh.py` – two Companions that cannot hear each other exchange a direct message
  and a channel message through a Repeater; and nothing is delivered with no links (the message is on the air, and
  120 virtual s later nobody has it). Lock-step.
* `test_mesh_protocol.py` – the Companion's mesh path: a direct message is acknowledged
  (flood and direct ACK, `PUSH_CODE_SEND_CONFIRMED`); a lost ACK leaves the sender unconfirmed, the
  firmware does not resend by itself and the app's retry (attempt 1) is a second transmission with its own
  ACK; the first flood teaches the sender an out-path and the next message goes direct (ether log); a heard
  advert becomes a contact with auto-add on and is only reported (`PUSH_CODE_NEW_ADVERT`) with it off. Lock-step.
* `test_atomic_storage.py` – power cuts in the temp file and between write and rename for repeater prefs, Companion identity and channel table; a damaged identity is kept aside.
* `test_atomic_blobs_acl.py` – power cuts for a Companion blob (the contact's last advert, byte-identical after the cut) and for the Repeater and Room server client ACL (a logged-in client is still recognised): at four byte offsets of the temp file and before the rename, the remove and the second rename of the SPIFFS-style swap, and a fault armed on the live file that must never fire.
* `test_contacts.py` – the adaptive contact memory ([Adaptive contact memory](CONTACTS.md)): migration of a stock `/contacts3` (fields unchanged, a cut before the old file is removed, a merge into a filled store), app sync returning every stored contact beyond the cache, capacity and cache following the reported heap, RAM against the stock array, favourites resident and reachable over the radio under a small heap, eviction of the least meaningful non-favourite (the overwrite push names it, favourites and contacts with message history either way survive), refusal when only favourites remain, power cuts at any byte of a bucket's temp file and between write and rename, a damaged bucket, the file system calls of a boot (empty store and 350 contacts, bounded by the files present, not the capacity) and a swap left in a high-numbered bucket recovered at boot, and the `CONTACTS_STOCK_ARRAY` build (RAM-only, atomic, stock eviction). `sim_companion_stockcontacts` is the opt-out build; `sim_companion_standalone_stockcontacts` is the same with the standalone screens and the device interface, for the rescue console's rebuild, which formats the file system on that build (the simulated format takes the Deployment's provisioned marker with it, as the ESP32 NVS erase does).
* `test_cli_unified.py` – the command-line engine ([Command-line engine](CLI.md)) on one Companion with three ways in, at time scale 10: the same line over USB, the LoRa remote CLI of a flagged contact and MQTT against a real Mosquitto gives the same reply where all three may run it (`get name`, `ver`, `board`, `get posture`, `get wifi.pwd`, `get radio`, `get mqtt.enabled`) and the defined refusal where they may not (a setting from the contact; the audit log, the posture, the connectivity mode and the management flag from the contact and the broker), each refusal audited; `get mecon.cli.who` and `get mecon.cli.check` per caller; MQTT refused and audited on plain TCP and on TLS without a pinned anchor (and no session at all with a wrong anchor); Hardened: a verified broker reads, a write is refused until the profile is flagged for management locally, and never the posture, the flag or the audit log; the flag off by default, persisted and cleared with the profile; malformed requests answered `bad-request` and run as nothing, the correlation id echoed; a retained request not replayed to a reconnecting node; no secret in any response or file; `reboot` over MQTT and the node back on its broker.
* `test_companion_transport.py` – management commands refused on the TCP-class transport, allowed on USB; messaging works on both.
* `test_companion_cli_secrets.py` – `get wifi.pwd` does not reveal the password.
* `test_companion_remote_cli.py` – remote Companion CLI limited to reads and `reboot`.
* `test_malformed_input.py` – runs the `sim_*_asan` sanitizer builds: hand-built truncated TRACE packets injected through the ether broker (`Ether.inject`) and over-long CLI arguments must not crash the node; a well-formed TRACE is still forwarded / delivered.
* `test_lockstep.py` – lock-step virtual time: two Repeaters (one given a 2 % duty cycle beforehand, so they do not always collide) booted together run ten virtual minutes three times; the same seeds give the identical `tx_log` (sender, start in microseconds, length and bytes) and the same receptions, and other seeds give other packets; every start lies on the 1 ms grid and no node starts a packet before its previous one ended; a real-time node on a lock-step ether and a lock-step node on a real-time ether are both refused; listen-before-talk: two Repeaters in range, the second's advert falls due 200 ms after the first's has started, and on the ether log it starts after the first one ends (within one 200 ms CAD retry period plus 100 ms) instead of overlapping it, and both are heard.
* `test_ether_physics.py` – the broker's channel model without nodes: overlapping transmissions of two nodes collide, one node's own back-to-back ones do not; capture (the lock window is the preamble less its last 5 symbols; equal strength collides; the earlier packet 6 dB stronger survives and the later is `captured`; a later packet 6 dB stronger survives if it starts at the same instant or one symbol before the lock, but both are lost when it starts at the lock or in the payload; 5.9 dB either way loses both; the threshold is configurable; packets that do not overlap are both delivered); and the busy notice goes, with the airtime, to every tuned node with a link, lossy included, and to nobody else (not the sender, a node out of range or on another SF), with nothing delivered before the airtime is over.
* `test_duty_cycle.py` – regulatory duty cycle, measured by the ether broker (`Ether.max_duty_in_any_hour`: the largest share of any sliding hour a node spent on the air, from the transmissions the broker saw, not from node counters). On lock-step virtual time, every run length and reboot interval in broker time. Seven nodes under sustained load for 1.25 virtual hours (10 ms quanta): 869.525 MHz BW 250 (10 %), with a looser and with a stricter user setting, 868.1 MHz BW 125 (1 %, also with the CLI's strictest factor 9), a channel straddling two bands (0.1 %), 915 MHz (no floor; the user's 10 % rules) and a Repeater at 868.1 MHz; a Room Server at 868.1 MHz; a live frequency change on a Companion; two Companions power-cycled every 5 virtual minutes (exactly 15 times) that must stay within their limit across boots; the delay before the first advert after a boot, exactly a whole number of 30 s bins (as many as the advert's charge needs from a hour seeded at the limit) plus 1.002 s; `get dutycycle` on both roles; and a self-test of the sliding-window instrument. The broker's stamps are exact, so every limit is asserted without slack. For the Room Server (1 ms quanta, 1.25 virtual hours) the busiest sliding hour must not exceed the limit by a microsecond, and must use at least the limit minus what the firmware's window is documented to leave unused (one 30 s bin, the largest packet, and under 1 ms of rounding per packet).
* `test_repeater_prvkey.py` – `set prv.key` refused for a remote admin, allowed on the local console.
* `test_postures_admin.py` – the standalone postures on a Repeater and a Room server ([Standalone security postures](STANDALONE_POSTURES.md)): the stock default password opens nothing in either posture (blank-password re-login of a stored admin entry included) and a set password opens remote admin (read-only in Hardened); Hardened remote admin is read-only plus `reboot` with every refusal an explicit reply; a remote caller can neither raise nor lower the posture or read the audit log, and each attempt is audited; key export and import are refused on the console in Hardened; an attempted password appears in no file of the node directory and not in its log; posture and audit survive a reboot; a power cut while lowering leaves the device Hardened and a cut while raising keeps the previous prefs (three cut points each); `erase` recovers a Hardened device and the audit log keeps its history through the reset; repeats fold into one audit entry with a count.
* `test_postures_companion.py` – the same on a Companion: key export and import refused over USB in Hardened and allowed again after `set posture normal confirm`; the remote CLI off in Hardened and back on in Normal; a remote CLI caller cannot change the posture or read the audit log; a Wi-Fi-class build with a stored Hardened posture does not serve its TCP port (the same build in Normal does); untrusted-transport refusals are audited in the file; the USB factory-reset frame recovers Hardened and keeps the audit history; posture survives a reboot and a power cut while lowering. BLE is covered by `test_companion_ble.py`.

* `test_companion_ble.py` – the simulated BLE transport on `companion_standalone` (time scale 10; BLE needs a PIN set over USB and a reboot): the right PIN pairs and carries frames (the PIN is not in the log), a wrong PIN, PIN 0 and the default PIN against a per-device PIN are each disconnected and counted, no lockout, a frame or a half pairing line before pairing closes the link; with a per-device PIN the CLI frame 66 (`get mecon.cli.who` reads `transport=ble auth=paired-pin ... principal=paired`), key export (equal to the cable's) and a configuration write work; with the default PIN the same link pairs but frames 66, export, import, set-PIN and factory-reset are each refused with `ERR_CODE_NOT_PERMITTED`, audited as untrusted-transport refusals, the node and its identity unchanged, while a direct message and a channel message are exchanged with a second node in both directions; Hardened with the default PIN brings up no BLE (no listener, no `SIMBLE begin` after the posture change, an audited `ble-start` refusal), Hardened with a per-device PIN does, and there key export, key import, factory reset and posture lowering over BLE are refused and the device stays Hardened; the same factory-reset frame over the same link resets a Normal device; a paired phone switching to Wi-Fi mode loses the link and the next boot starts no BLE; the Bluetooth page switches the link off (dropped, no pairing possible) and on.

* `test_companion_wifi.py` – the prioritised Wi-Fi controller ([Wi-Fi profiles](WIFI.md)) on `companion_wifi_usb` (profiles set over the USB console, the controller runs after a reboot; in that build the TCP side is inert) and `companion_wifi` on the same node directory, at time scale 20: the highest-priority visible profile is joined with no detour; a higher profile that is absent is skipped; failover when the network disappears and on a wrong password (`last=auth` in the listing); return to the better network only after the hold-down and exactly one switch (joins counted in the node log); a failed switch up is not hammered; backoff and joining when the network appears later; a live profile edit rejoins without a reboot; ten profiles persist across a reboot and a stop; a power cut at four points of the prefs save keeps all ten; each password appears in no reply and in no file or log but `prefs.json`; the stock `wifi.ssid` / `wifi.pwd` / `wifi.clear` commands and a stock-format prefs file; the TCP companion server answers only while the controller holds a link; Hardened starts no Wi-Fi; a scan that never ends being stopped in the driver and the best profile joined directly, the scan after it working, results read from the driver when Arduino's tables never fill, and the main loop never held up by a wedged scan (time scale 1).

* `test_companion_mqtt.py` – the MQTT controller ([MQTT broker profiles](MQTT.md)) against real Mosquitto brokers on their own loopback addresses (plain and TLS, certificates generated at test time, one anonymous and one with a password file), at time scale 10: the highest-priority broker wins (the better one has the higher profile number) and the lower one is never touched, failover when the broker goes away; the retained status message; return to the better broker only after the hold-down, counted at the brokers' own logs (one session each, no flapping); a host name resolved again on every attempt after the name moves, and an unresolvable name failing over; discovery accepting a matching site ID with a matching certificate (`tls=verified`, nothing sent to the fallback broker), rejecting the right site ID with the wrong certificate (the rogue broker's log shows no MQTT session) and not shutting out the real broker that advertises the same site ID, ignoring another site ID (not even a TCP connection) and a responder that advertises plain MQTT (never contacted); browsing only while not on the best profile (none at all on the best, repeated at the rescan interval otherwise); a responder that appears later taken after the hold-down with exactly one switch; credentials used and a wrong password failing over and an edit fixing it at once; the password and user name in no reply and in no file or log but `prefs.json`; a TLS profile without an anchor reported `tls=unverified` and a wrong anchor refused; Hardened refusing plain MQTT at the command, never connecting a stored plain profile, ending a plain session when the posture rises, and keeping the Wi-Fi link for MQTT while never serving the TCP port (on the TCP-class build, with a Normal positive control); profiles persisting across a reboot and `set mqtt.enabled`; and the CA anchors: a broker certificate renewed under the same CA keeps working (and a leaf fingerprint does not survive the same renewal), a certificate from another CA and a wrong host name are refused, an address literal and a discovery profile check the chain only, the server name reaches a TLS server that records it (a small Python TLS probe, since Mosquitto does not log SNI), an RSA-4096 CA fits, and a damaged CA is refused at `ca end` with the profile unchanged. The CA, the broker certificates and the console commands (from `tools/mqtt_ca_commands.py`) are all generated at test time.
* `test_mqtt_nonblocking.py` – the MQTT connect does not block the main loop ([MQTT broker profiles](MQTT.md)), at time scale 1 so wall-clock gaps are read directly: against a broker that accepts TCP and never answers (plain and TLS) and against an unreachable address, the longest main-loop gap stays under 200 ms (it was about 6 s per attempt when the connect ran inline) and `get mqtt.status` answers `connecting` at once; a direct message sent while an attempt is in flight reaches the Companion within seconds; and, on the `sim_companion_wifi_usb_asan` build, editing the profile being connected and raising the posture during a pending connect leave no sanitizer report, no transport open beyond the session, and the right final broker.

* `test_connectivity_mode.py` – connectivity-mode switching and screens on `companion_standalone` ([Connectivity mode](CONNECTIVITY_MODE.md)), time scale 10: the default mode starts only the BLE interface, even with a Wi-Fi profile; Wi-Fi mode starts only the Wi-Fi controller and survives a power cycle; BLE, Wi-Fi, BLE, Wi-Fi never brings up both stacks; a damaged mode byte starts BLE; a power cut in the temp file (two points) and before the rename keeps the previous prefs and a clean switch works afterwards; USB management (name, key export, Wi-Fi status) in both modes and across a switch; a remote switch refused and audited with the device not restarting; the cable switch audited; bad input changes nothing; the button switches only after a confirming long press (not before, audit entry `button`), a click or a double click cancels, an unconfirmed prompt times out, the button works in Hardened and in the other direction and the Bluetooth page is skipped in Wi-Fi mode; the screens in BLE mode (PIN class, phone connected), with a default PIN (warning), following the posture, in Wi-Fi mode (profile, RSSI, IP, link lost, no profile, Hardened without MQTT), and the MQTT line against real Mosquitto (plain, TLS verified, TLS unverified, no session); and no Wi-Fi or MQTT credential in any frame of any boot, nor in any file or log but `prefs.json`.
* `test_heap_floor.py` – the enforced heap floor ([Heap floor](CONNECTIVITY_MODE.md#heap-floor)) on `companion_standalone`, time scale 20 (a look about every 3 s), the simulated heap rewritten while the node runs: a healthy heap reports `heap_low=0 floor=20480` and no audit entry; the first look comes right after setup (time scale 1: low within seconds of a boot, not a minute); exactly the floor is not low and one byte below is; the episode holds at the floor and one byte short of floor + 2,048 and ends at floor + 2,048; one audit entry per episode however many looks it spans, and a second episode makes the same entry `x2`; the largest block, not the total free heap, is the figure; `LOW MEM` on the home status line (`BLE LOW MEM`, or alone when the default-PIN text would not fit) only while low; the state is not kept across a boot.

## Unit tests (`pio test -e native`)

`test_cli_engine` asserts every cell of the privilege table (every transport x authentication x remote flag, in Normal and in damaged-byte Hardened, with and without the broker-management flag and the cable-only flag, 154 parametrised contexts), the properties that must survive any edit of the table (a remote caller never gets a secret, posture or destructive command; untrusted and unverified callers get nothing; Hardened never loosens a row; an inconsistent context is untrusted), the registry's classification of both command sets, the gate's replies and audit entries for every refusal reason, the Companion frames, the facades in `Posture` and `ConnMode`, the MECON commands and the MQTT framing (request parsing, response escaping, truncation at every buffer size).

`test_posture` covers the pure decision functions (posture byte, BLE start with a default PIN, TCP server start, key transfer, who may factory-reset, default-password test, the Hardened read-only list, the posture / audit commands for every caller, the Repeater / Room server front door) and the audit ring (folding, eviction, file image and its CRC against any single flipped byte, the flush policy and its millisecond wrap, paging). The BLE start rule and the BLE factory-reset refusal are also checked end to end by `test_companion_ble.py`.

`test_contact_store` drives the contact store with an in-memory backend that can be told to fail: the record image against the stock layout and against every single flipped byte, lookups by prefix, by full key and by path hash (contacts sharing their first four key bytes), flush granularity (only changed buckets), the cache size rule (floor, ceiling, unknown heap), favourites resident from boot and under churn, a changed record written before its slot is reused and kept when that write fails, capacity from file-system space (our own files counted as free) and from heap, each eviction rule (store full and overwriting allowed, key in the overwrite event also for a record only on the file system, message recency window, known path, favourite never, only-favourites refusal, favourite set through a pointer, the stock policy), iteration including records outside the cache, bulk import (one write per bucket; fewer slots than contacts), failed and half-finished flushes, the RAM-only mode.

`test_wifi_core` drives the Wi-Fi decision core with a fake radio and a virtual clock (selection and tie-break, failover on wrong password, not-found, join timeout and quick drops, the direct join after a failed or silent scan, the escalating cooldown, hold-down return and its reset by a missed scan, no scans on the best profile, backoff growth and reset, profile edits, ten profiles, the 32-bit clock wrap). `test_wifi_prefs` covers the profile store in the real `NodePrefs` serializer (a stock-format file, ten full-length profiles round-trip, the prefs size) and the profile CLI (bad input changes nothing, paging through ten profiles, last failure shown, no reply ever carries a password).

`test_mqtt_core` drives the MQTT decision core with a fake network (selection and tie-break, every failure kind falling back, the escalating cooldowns and their caps, a session that drops or dies young, hold-down return and the bound of one interruption per hold-down, edits, link loss, discovery with site matching, a plain responder never a candidate, a rogue candidate cooling alone, browsing only off the best profile and abandoned when it never reports, a vanished responder, the Hardened rules and a session dropped when the posture rises, the 32-bit clock wrap). `test_mqtt_prefs` covers the profiles in the real `NodePrefs` serializer (a file without them, six full-length profiles round-trip) and their CLI (bad input changes nothing, the Hardened refusal, the anchor normalised, paging, the longest line fits the reply, no reply ever carries a credential).

## Mutation check

In CI the mutation workflow runs on pushes to `main` and on pull requests that are ready (not drafts) and labelled `full-ci`. Adding the label starts it, and every later push re-runs it. Use the label on authority and security changes, where the mutants should pass before the merge. Elsewhere they are checked on `main` straight after it, and a red run there is fixed at once.

`sim/mutate.sh` breaks one thing per test group (a time-on-air constant, SimRadio dropping
every packet, SimFS discarding writes, the fault hook disabled), rebuilds, expects that
group to go red, restores the file with `git checkout` and finishes with a clean full run.
Run it on a clean tree. It also breaks each duty-cycle rule (sliding window turned into a bucket, floor ignored, floor clamped at the CLI's 10 %, a straddling channel judged by its centre, a frequency change not applied) and expects the load tests and the unit tests to go red; those mutants take about 2.5 minutes each. A second group breaks each posture rule (a damaged posture byte read as Normal, BLE started with a default PIN, TCP servers started in Hardened, key transfer allowed, remote admin not read-only, default password accepted, an attempted password logged, remote lowering allowed, lowering without the confirm token, posture not persisted, a factory reset keeping the posture, refusals not audited, the audit readable remotely, repeats not folded, the flush limit off, the audit CRC unchecked, remote CLI left on in Hardened) and expects the posture tests or the unit tests to go red.

A third group breaks each Wi-Fi controller rule (priority ignored, tie to the higher number, a failed scan not joined directly or joined without regard to priority, a missed guess not cooled, a scan not stopped in the driver or not bounded or its failure taken for an empty result, Arduino's table or the driver's list not read, a scan that holds the main loop, a wrong password or a not-found retried at once, no escalation, no join timeout, drops not counted or never forgiven, a lost network not failed over, no hold-down, a missed scan not restarting it, scans on the best profile, backoff not growing or not reset, an edit that keeps the link or the cooldown, the radio's own leave counted as a failure, an unmapped auth reason, the stock keys or commands changed, priorities or further profiles not persisted, a password in a listing or in the log, the TCP server served without a link, Wi-Fi started in Hardened) and expects the Wi-Fi tests or the unit tests to go red.

A fourth group breaks each MQTT rule (priority ignored, a tie to the higher number, a failed connect or refused credentials not cooling down or not escalating, a session that drops not failed over, no hold-down for a static profile or for a discovery responder, browsing on the best profile, a plain responder or a wrong site ID accepted as a candidate, a certificate mismatch cooling the whole profile or not cooling at all, the pinned fingerprint not checked or not passed on, a lost responder not failed over, an edit that keeps the session or the cooldown or waits for the hold-down, a plain profile used or settable in Hardened, a plain session kept when the posture rises, Wi-Fi not kept for MQTT in Hardened, the TCP port served in Hardened, TLS dropped, a TLS profile without an anchor reported as verified, a name resolved only once, a password in a listing or in the log, the credentials not sent, profiles not persisted, the status not published) and, for CA anchors and SNI, the chain not verified, the host name not checked or not passed on, SNI not sent, a refusal not reported as `pin`, the CA not passed on or not copied, a CA not persisted or only its first piece, a truncated CA accepted, a CA saved before `ca end`, a CA and a fingerprint both kept, a CA that does not make a discovery profile usable) and expects the MQTT tests or the unit tests to go red.

`test_heap_floor` covers the heap-floor decision (the floor and the hysteresis at the byte, the largest block as the figure, one audit entry per episode and a count per further episode, a block hovering at the floor, disabled and unmeasurable platforms, the look period and its 32-bit wrap, the `get audit` line and the file image), the `get mecon.mem` reply with and without the check, and the home status line wording with the marker.

`test_connmode` covers the connectivity-mode decision functions (one stack per stored byte for all 256 values, a damaged byte, who may switch, the audit of changes and refusals, the immediate audit flush, the file image) and the status-screen wording (each mode and posture, the default-PIN and unverified-TLS warnings, line lengths with the longest SSID and address).

A fifth group breaks each connectivity-mode rule (a damaged byte read as Wi-Fi, BLE or Wi-Fi started in the wrong mode, a remote caller allowed to switch, a refusal or a change not audited, the mode not persisted, prefs written in place, no restart after a switch, a button switch with no confirmation, any key confirming, a confirmation that never times out, the Bluetooth page not skipped, a default PIN not flagged, the posture not shown, USB counted as a BLE phone, unverified TLS shown as verified, a Wi-Fi or MQTT password drawn) and expects the connectivity tests or the unit tests to go red.

A sixth group breaks each heap-floor rule (the total free heap used instead of the largest block, no hysteresis, no audit entry, the first look a period late, the period ignored, an unmeasurable platform firing, the fields or the marker dropped, the check not started or not run from the main loop) and expects the heap-floor tests to go red.

Two lock-step mutants must turn the determinism test in `test_lockstep.py` red. One makes the node read the wall clock instead of the broker's grants; the node then falls behind virtual time and stops transmitting, so the test's precondition catches it. The other adds up to 5 ms of the wall clock to the node's lock-step clock: the node keeps time and keeps transmitting, and only the comparison of the two runs' `tx_log` can see that its timers now fire in host-dependent quanta.

A seventh group breaks each BLE rule (a wrong PIN accepted, data before pairing accepted, the default PIN classed as trusted, a link surviving `disable()`, BLE started in Hardened with the default PIN or in Wi-Fi mode, the frame context trusting every interface, a paired link allowed key export or factory reset in Hardened) and expects `test_companion_ble.py` to go red.

An eighth group breaks the ether's capture and channel-activity rules (a later, stronger packet never winning; an earlier, stronger one never winning; equal strength surviving; 5.8 dB counted as enough; a later, stronger packet winning after the receiver has locked; the lock window the whole preamble; the broker's busy notice not sent, against both the physics tests and the deferral test; SimRadio never reporting busy) and expects `test_ether_physics.py` or the deferral test in `test_lockstep.py` to go red.

## Matching the target toolchains

The simulated boards are compiled with `-funsigned-char`, because every target toolchain (Xtensa for ESP32, ARM for nRF52 and RP2040) treats plain `char` as unsigned, and x86-64 Linux treats it as signed. Without the flag, a UI key code at or above 0x80 (`KEY_NEXT` is `0xF1`) arrives as a negative `char` on an x86-64 CI runner and never matches. An aarch64 host happens to behave like the targets, which is how the difference went unnoticed until CI ran the UI tests.
