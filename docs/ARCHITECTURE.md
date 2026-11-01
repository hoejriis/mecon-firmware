# Architecture

How mecon-firmware is built on MeshCore, where its code lives, where it may extend upstream, and how it is built and tested. What the device does for a user is in the [documentation index](README.md). The wire contracts are in the [contract index](contract/README.md).

## On top of MeshCore

mecon-firmware is MeshCore 1.18.0 (upstream commit `81dbae5`) with upstream history kept. It is not a separate radio stack. The Companion, Repeater and Room server keep their upstream roles, native frames and CLI. Their behaviour changes only where documented: authorization, secret read-back and some command paths, listed in the [README comparison](../README.md#stock-meshcore-1180-and-mecon-firmware-compared) and the [upstream delta register](UPSTREAM_DELTA.md). Everything this project adds sits beside them:

```text
                  managers (any implementation of the contracts)
                         │
          USB ─── BLE ─── MQTT ─── LoRa (point to point, management channel)
                         │
              ┌──────────┴──────────┐
              │   device interface   │  operations, jobs, telemetry, updates
              ├─────────────────────┤
              │  privilege engine    │  one table: caller × posture × class
              ├─────────────────────┤
              │  authority, records  │  Deployment, replay, signatures
              └──────────┬──────────┘
                         │
   MeshCore 1.18 roles: Companion · Repeater · Room server   (native frames and CLI kept, with the documented restrictions)
                         │
   platform services: Wi-Fi, MQTT, connectivity mode, contact store,
   atomic files, duty-cycle window, audit log, nRF52 worker
```

Four rules hold everywhere:

- **The same everywhere.** The firmware works the same way in every mode, on every role and over every transport: the same operations, the same results and the same security rules. A difference is either a limit of the hardware, stated as such, or an exception the project owner has decided. It is never left by omission.

- **Native state is authoritative.** Settings live in the role's own preferences, contacts in the native contact table, channels in the native channel table. An interface operation is implemented by the native function or CLI line it stands for, and nothing keeps a second copy.
- **The CLI is the one management plane.** Every setting is a CLI setting. The device interface reads and writes through it, and each request is decided by the same privilege cell as the CLI line it maps to. The interface is an allowlist of named operations, never a raw shell.
- **A transport is never authority.** USB, BLE, MQTT, a TLS session, a LoRa route and a channel key are ways in. What a caller may do is decided by the privilege table, the posture and, on an enrolled device, a manager's signature.

## Where the code lives

Directories and files this project adds under `src/helpers/`:

| Path | Responsibility |
|---|---|
| `iface/` | The device interface: the operation table and its authorization step, envelope, framing and JSON, settings (`config.*`), jobs and confirm-or-revert, telemetry streams, channel and management-channel operations, the Repeater and Room server console front end, signed results and events |
| `deployment/` | Device-side authority: the Deployment record, authority sets and their rotation, signed-request verification, replay watermarks, provisioning, local unlock, and the provisioned marker |
| `updates/` | Signed updates: manifest and preflight, the security floor, the job service, inactive-slot writing on ESP32, and the generated release anchors |
| `cli/` | The privilege engine: one gate in front of every command, whatever carried it, with the command registry, the privilege table, the `mecon.*` commands and the command line over MQTT |
| `wifi/` | Wi-Fi profiles: decision logic, platform controller and profile store |
| `mqtt/` | MQTT broker profiles: decision logic, controller over PubSubClient, network layer, WebSocket transport, profile store |
| `connmode/` | BLE-or-Wi-Fi connectivity mode on the standalone Companion |
| `AtomicFile.h` | Crash-safe file replacement: temporary copy, read-back check, swap, recovery at boot |
| `ContactStore.*` | The adaptive contact store: bucket files and a RAM cache |
| `RegulatoryDutyCycle.*`, `DutyWindow.h` | Band floors and the rolling one-hour airtime window |
| `Posture.h`, `Audit*.h` | Normal and Hardened postures, and the persistent audit log |
| `NrfWorker.*` | An 8 KB worker task on nRF52 for interface requests and received packets, whose loop stack is too small for them |
| `esp32/SerialNimBLEInterface.*` | The NimBLE BLE transport for ESP32 Companions |

The Companion's own interface glue is in `examples/companion_radio/` (`MeconIface.*`, `MeconDeploy.cpp`, `MeconMgmt.cpp`). The Repeater and Room server use `iface/ConsoleIface` from the shared code.

## Extension boundaries

Changes to upstream files are kept small and each one is recorded in the [upstream delta register](UPSTREAM_DELTA.md), which is the starting point for every upstream merge. The main points where this project hooks into upstream:

- **Commands.** `CommonCLI` passes every command through the privilege gate first, then the radio preferences, then the `mecon.*` commands, and only then upstream's variant hook. The Companion's command handler takes the posture and audit commands first.
- **Transports.** Each serial interface (USB, BLE, Wi-Fi TCP, Ethernet) declares whether it is a trusted transport. The default is untrusted, so a new transport fails closed.
- **Radio.** `Dispatcher` takes its airtime limit from the duty-cycle window, and asks each role for its channel to find the band floor. On nRF52 it runs packet handling on the worker task.
- **Contacts.** `BaseChatMesh` uses the contact store in place of the static array. `CONTACTS_STOCK_ARRAY` opts back into the stock array.
- **Storage.** Preferences, identity, regions, access lists and the Companion's data store save through `AtomicFile`.

New behaviour belongs in the shared code, with role and board adapters kept thin. Do not add a second implementation of something upstream already provides.

## Builds

Each board and role is a PlatformIO environment. The project's build flags:

| Flag | Effect |
|---|---|
| `ENABLE_MECON_IFACE` | Compiles the device interface into a Companion, Repeater or Room server. Also turns off BLE DFU and stock `start ota` |
| `ENABLE_MQTT` | The MQTT controller, and on ESP32 the device-side download for updates |
| `ENABLE_CONNMODE` | The BLE-or-Wi-Fi standalone Companion, used together with `ENABLE_WIFI_INTERFACE`, `BLE_PIN_CODE` and `ENABLE_USB_INTERFACE` |
| `CONTACTS_STOCK_ARRAY` | Keeps stock's fixed contact array instead of the adaptive store |
| `MECON_MARKER_PAGE`, `MECON_FLOOR_PAGE` | nRF52 flash pages outside the file system for the provisioned marker and the security floor |
| `ENABLE_WIFI_OTA` | Opts a build without the interface into stock's unsigned Wi-Fi updater. Not set in any shipped environment |
| `MECON_RELEASE_TEST_ANCHORS` | Builds the public test release key in. Simulated and native builds only: a firmware build with it fails to compile |

The Heltec V3 and V4 build on Arduino-ESP32 3.1.3 / ESP-IDF 5.3 with libraries rebuilt for more heap. `bin/idf5_libs.sh <env>` builds that board's library set into the PlatformIO core directory, and `pio run -e <env>` fails until it has. Any other ESP32 build swaps the set out, so the script runs again after one. It does nothing for an environment without a `custom_sdkconfig`. A separate `PLATFORMIO_CORE_DIR` per board avoids the swap ([runtime platform](PLATFORM.md#building)). The nRF52 boards keep the upstream Adafruit runtime. The version string comes from `git describe` at build time, with the upstream base first ([versioning and releases](VERSIONING_AND_RELEASES.md)).

Release keys are listed in `docs/contract/release-keys.json`, and `tools/gen_release_anchors.py` generates `src/helpers/updates/ReleaseAnchors.h` from it. CI checks that the two agree. A build trusts only the production keys in that list.

## Testing

| What | Where | Run locally |
|---|---|---|
| Native unit tests | `test/` | `sim/run.sh pio test -e native` |
| Simulated boards: the real role code on a simulated LoRa channel, with fault-injecting storage, real MQTT brokers and simulated access points | `sim/` | `sim/build.sh`, then `sim/run.sh python3 -m pytest sim/tests -q` |
| Mutation check: every mutant must make a test fail | `sim/mutants/` | `sim/mutate.sh` (needs a clean tree) |
| Stack gate for the nRF52 loop task and worker | `tools/stack_callgraph.py` | see the `stack-gate` workflow |
| Size budgets per environment | `ci/size-budget.txt` | the `target-boards` workflow |
| Locked library set | `ci/lib-lock.json`, `tools/lib_lock.py` | `tools/lib_lock.py check <env>` |
| Tool tests | `tools/tests/` | `pytest tools/tests` |

CI runs the unit tests, simulated boards and target-board builds with size budgets on pull requests that change code or the docs the tests read, and the stack gate on pull requests that touch the code it measures. Every pull request and merge runs a mutation spot check of the mutants its change touches. The full mutation set runs nightly on `main`, and on a pull request labelled `full-ci` ([mutation check](SIMULATION.md#mutation-check)). Every board environment is compiled nightly, and passes only when its failures match the recorded upstream ones. A failed or cancelled run on `main` opens or updates a tracking issue. The [test harness](SIMULATION.md) says what the simulator can and cannot show. Real-board results are recorded in [hardware evidence](HARDWARE_VERIFIED.md).

## Management over LoRa

LoRa is a transport for the same device interface, not a second command model. A request travels point to point as an encrypted native datagram, or as a group datagram on the device's management channel. It is dispatched through the same operation table and privilege cell, deduplicated by the same id cache and replay watermark, and limited by the duty-cycle window. Compact, low-rate telemetry travels the same paths. The wire form is defined in [management over LoRa](contract/LORA_MANAGEMENT.md).
