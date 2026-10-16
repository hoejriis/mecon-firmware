# Simulated-board test harness

> **A pass in this harness is software verification on simulated boards. It is not a
> hardware claim.** No radio, flash, USB or BLE stack of a real board is exercised.

The harness builds the real upstream application code (`examples/companion_radio` and
`examples/simple_repeater`, unmodified) into Linux executables and drives them the way a
client would. It is the regression rig for work that has no hardware to run on.

## Running it

Everything runs inside the dev container (never on the host):

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
| `sim` variant | `variants/sim/` | `platform = native` envs `sim_companion`, `sim_repeater`; `target.h/.cpp` replace the board layer |
| Arduino/FS shims | `variants/sim/shims/` | just enough `Arduino.h`, `FS.h`, `SPIFFS.h`, `RTClib.h` to compile the examples; no upstream file is edited |
| SimRadio | `variants/sim/sim_radio.cpp` | `mesh::Radio` that sends/receives raw packets through the ether over a unix socket |
| Ether broker | `sim/ether.py` | decides who hears whom, per-link RSSI/SNR/loss, holds the channel busy for the packet's time on air, delivers at end of airtime, destroys overlapping receptions |
| Time on air | `variants/sim/sim_toa.h` | Semtech formula (explicit header, CRC on, low-data-rate optimisation when Tsym > 16 ms) |
| Clock | `variants/sim/sim_clock.h` | `SimClock` interface behind `millis()`/`micros()`/`delay()` and the RTC |
| SimFS | `variants/sim/sim_fs.cpp` | the ESP32 SPIFFS API (`fs::FS`, `File`) over a per-node host directory, with a power-cut fault hook |
| Serial | `variants/sim/sim_runtime.cpp` | the USB serial port is a loopback TCP listener; Companion frames and the repeater CLI pass through unchanged |
| Runner | `sim/harness.py`, `sim/tests/` | starts the broker and N nodes (each in its own temp dir) and drives them with a small Companion client and a CLI client |

Node command line: `--dir`, `--ether`, `--name`, `--port-file`, `--seed`, `--time-scale`,
`--rtc`, `--fault /path:BYTES`, and `--toa SF BW CR PREAMBLE LEN` (print time on air in
microseconds and exit). A node exits with status 75 on `reboot` and 0 on `poweroff`.

### Design notes and limits

* **Platform macro.** The examples pick their filesystem code by platform macro. The sim
  defines `ESP32` for project sources only (`build_src_flags`) so they take the SPIFFS path;
  libraries are built without it. `variants/sim/sim_prelude.h` is force-included to keep
  the Crypto library's portable software AES (its header otherwise selects an ESP32
  hardware class).
* **Time is real time, scaled.** `SimClock` is an interface and `--time-scale` multiplies
  the wall clock (the broker takes the same scale), so long windows can run faster, but
  runs are *not* lock-step deterministic. A runner-driven virtual clock can replace the
  real one through `sim_set_clock()` without touching the shims.
* **Static-init order.** The examples construct their mesh object before `main()`, so the
  clock and the config are function-local statics.
* **Channel model.** Receivers that are tuned to the transmission's frequency, bandwidth and
  SF and have a link hear it; two overlapping receptions at one node, or a node that
  transmits during a reception, lose the packet. There is no capture effect and no noise
  floor model; RSSI/SNR are whatever the link says.
* Not simulated: BLE, WiFi, displays, GPS/sensors, the real SX126x state machine, CAD,
  power saving, flash wear/latency.

## Storage fault injection

`--fault /path:BYTES` kills the process (status 99, nothing closed or flushed beyond what was written) after BYTES
of the next write to that file. `--fault-before rename:/path` or `remove:/path` kills it just before that operation
(the source path for a rename). SimFS refuses rename over an existing file, as ESP32 SPIFFS does.

On stock 1.18 the prefs save rewrites `/prefs.json` in place: cut after 40 bytes it leaves a truncated file that the
loader accepts, the early field (`name`) takes the new value and a field in the lost tail (`tx`) silently reverts to its
default, with no error. With atomic saves the same cut lands in `/prefs.json.tmp`, and the previous complete prefs,
identity and channel table survive a cut at any byte or between write and rename (`test_atomic_storage.py`).

## Test inventory

* `test_toa.py` – node's time on air against an independent restatement of the Semtech
  formula for five SF/BW/CR/length combinations (within 1 ms) and two published values.
* `test_repeater_cli.py` – `ver`, `get name`, `set name` and persistence across a restart.
* `test_mesh.py` – two Companions that cannot hear each other exchange a direct message
  and a channel message through a Repeater; and nothing is delivered with no links.
* `test_atomic_storage.py` – power cuts in the temp file and between write and rename for repeater prefs, Companion identity and channel table; a damaged identity is kept aside.
* `test_companion_transport.py` – management commands refused on the TCP-class transport, allowed on USB; messaging works on both.
* `test_companion_cli_secrets.py` – `get wifi.pwd` does not reveal the password.
* `test_companion_remote_cli.py` – remote Companion CLI limited to reads and `reboot`.
* `test_malformed_input.py` – runs the `sim_*_asan` sanitizer builds: hand-built truncated TRACE packets injected through the ether broker (`Ether.inject`) and over-long CLI arguments must not crash the node; a well-formed TRACE is still forwarded / delivered.
* `test_duty_cycle.py` – regulatory duty cycle, measured by the ether broker (`Ether.max_duty_in_any_hour`: the largest share of any sliding hour a node spent on the air, from the transmissions the broker saw, not from node counters). Seven nodes under sustained load for 1.25 virtual hours at time scale 60 (75 s): 869.525 MHz BW 250 (10 %), with a looser and with a stricter user setting, 868.1 MHz BW 125 (1 %, also with the CLI's strictest factor 9), a channel straddling two bands (0.1 %), 915 MHz (no floor; the user's 10 % rules) and a Repeater at 868.1 MHz; a Room Server at 868.1 MHz; a live frequency change on a Companion; two Companions power-cycled every 5 virtual minutes (15 times) that must stay within their limit across boots; the delay before the first advert after a boot; `get dutycycle` on both roles; and a self-test of the sliding-window instrument. Time scale 60 is coarse enough that the broker stamps a transmission up to a few real milliseconds (a fraction of a virtual second) late; the node's gate leaves more margin than that, but a limit needing sub-second exactness would need the virtual clock `sim_set_clock()` allows.
* `test_repeater_prvkey.py` – `set prv.key` refused for a remote admin, allowed on the local console.

## Mutation check

`sim/mutate.sh` breaks one thing per test group (a time-on-air constant, SimRadio dropping
every packet, SimFS discarding writes, the fault hook disabled), rebuilds, expects that
group to go red, restores the file with `git checkout` and finishes with a clean full run.
Run it on a clean tree. It also breaks each duty-cycle rule (sliding window turned into a bucket, floor ignored, floor clamped at the CLI's 10 %, a straddling channel judged by its centre, a frequency change not applied) and expects the load tests and the unit tests to go red; those mutants take about 2.5 minutes each.
