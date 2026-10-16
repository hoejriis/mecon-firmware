# Command-line engine

Stock MeshCore 1.18 lets commands reach a device four ways, each with its own parser and its own idea of who may do what: the Companion's `CMD_RUN_CLI_COMMAND` frame, the serial rescue console, a remote `TXT_TYPE_CLI_COMMAND` from a flagged contact, and the repeater and room server console and remote admin. This firmware routes all of them, and the new MQTT transport, through **one engine** in `src/helpers/cli/`.

What the simulated-board suite checks here is software behaviour. BLE needs a radio stack the simulator does not have: its rows of the table are covered by unit tests of the table, not by a simulated pairing ([Simulated-board test harness](SIMULATION.md)). **Nothing here has run on a board.**

## Shape

```
adapter (USB, BLE, LoRa, MQTT)  ->  Context   transport, authentication, remote, posture, broker-management flag
gate(command line, context)     ->  the registry names the command's class, the table says allow or refuse
refused                         ->  an explicit reply, and an audit entry (class and caller)
allowed                         ->  the role's own handler runs the command, unchanged
```

- **The adapters** only frame bytes and build the context. They never decide who may run what.
- **The registry** (`CliRegistry.h`) gives every command a class. A command nobody registered is `write`, which fails closed for every caller limited to reads.
- **The table** (`CliPolicy.h`) maps (caller, posture, class) to allow or to the reason for the refusal. It is the only place a rule about *who* lives. The older entry points (`Posture::keyTransferAllowed`, `Posture::factoryResetAllowed`, `Posture::isRemoteReadOnly`, `ConnMode::callerMaySwitch`, the posture commands) are thin functions that ask it.
- **The handlers** (the Companion's `handleCommand`, the repeater's, the room server's, `CommonCLI`) run only what the gate let through. Upstream commands keep their names, syntax and replies.

A remote caller never inherits a local caller's privileges: a LoRa or MQTT caller is a different row from the cable, whatever it sent. A context that disagrees with itself (a remote flag that does not match the transport, an authentication that does not belong to the transport) resolves to the *untrusted* row, which refuses everything.

## Classes

| Class | Commands |
|---|---|
| `read` | `get ...`, `ver`, `board`; on a repeater or room server also `clock`, `neighbors`, `power`, `powersaving`, `region`, `region get`, `sensor get`, `sensor list`, `gps` |
| `write` | everything else that changes a setting or transmits, including `set ...`, `password`, `advert`, `poweroff`; **also the class of any command nobody registered** |
| `secret` | private-key material: `get prv.key`, `set prv.key`; the Companion frames `CMD_EXPORT_PRIVATE_KEY` and `CMD_IMPORT_PRIVATE_KEY` |
| `posture` | `set posture ...`, `get audit`, `set connectivity.mode`, `set mqtt.profile <n> manage ...` |
| `destructive` | `erase`; the Companion frame `CMD_FACTORY_RESET` |
| `reboot` | `reboot` (exactly; `reboot now` is a `write`) |

The Companion frames that are not command lines are gated by the same table: `CMD_RUN_CLI_COMMAND` counts as `read` (may this caller reach the CLI at all), `CMD_SET_DEVICE_PIN` as `write`. Wi-Fi and MQTT passwords are `write` commands whose *read* is answered `> wifi.pwd is write-only` by the engine, or shows only `set` / `unset`: no reply on any transport carries a secret of this kind.

Lowering protection (`set posture normal`, and any `set posture` argument that is not `hardened`) carries a *cable only* flag: whatever the class cell says, no caller but the cable gets through.

## The table

Rows are the callers; per caller the left cell is Normal and the right is Hardened (a posture byte other than Normal counts as Hardened). A dash is allowed.

| Caller (transport, authentication) | read | write | secret | posture | destructive | reboot |
|---|---|---|---|---|---|---|
| **cable** (USB / UART) | - / - | - / - | - / refused | - / - | - / - | - / - |
| **paired phone** (BLE, a PIN of the device's own) | - / - | - / - | - / refused | - / - | - / refused | - / - |
| **untrusted** (the TCP port, BLE with a default PIN, an unset or inconsistent context) | refused | refused | refused | refused | refused | refused |
| **remote admin** (LoRa login, password set) | - / - | - / refused | local only / refused | refused | local only / refused | - / - |
| **remote admin, default password** | refused | refused | refused | refused | refused | refused |
| **guest** (LoRa) | refused | refused | refused | refused | refused | refused |
| **flagged contact** (Companion remote CLI) | - / off | limited / off | limited / off | refused / off | limited / off | - / off |
| **MQTT, TLS verified** | - / - | - / flag | refused | refused | refused | - / flag |
| **MQTT, plain or unverified TLS** | refused | refused | refused | refused | refused | refused |
| **device button** | refused | refused | refused | allowed | refused | refused |

- *limited*: a flagged contact reads and reboots only. *off*: Hardened turns the remote CLI off entirely, reads included.
- *flag*: allowed only when the broker profile the session runs on is flagged for management (`set mqtt.profile <n> manage on`); otherwise the request is answered `refused` with `ERR: management is off for this broker profile`.
- *local only* and *refused* are explicit replies and audit entries; `set prv.key` from the air is explained but, as it always was, not logged.
- The button reaches the connectivity-mode switch and nothing else; it is not a command line.
- "A paired phone" is only a BLE session paired with a PIN other than 0 or `123456`. The firmware cannot tell BLE with a default PIN from the TCP port at this layer and treats both as untrusted, which is what it always did.

A refusal's reply text (the strings are those the individual checks always gave):

| Reason | Reply |
|---|---|
| not permitted | `ERR: not permitted` |
| flagged contact, limited | `ERR: not permitted remotely` |
| Hardened remote admin | `ERR: not permitted in hardened posture` |
| Hardened remote CLI | `ERR: remote CLI is off in hardened posture` |
| default admin password | `ERR: remote admin is off until the admin password is set on the local console` |
| key in Hardened | `ERR: key export is refused in hardened posture` / `ERR: key import is refused in hardened posture` |
| `set prv.key` from the air | `ERR: prv.key can only be set from the local console` |
| MQTT, session not verified | `ERR: management needs a TLS-verified broker` |
| MQTT, Hardened, profile not flagged | `ERR: management is off for this broker profile` |
| a Companion frame on an untrusted transport | `ERR_CODE_NOT_PERMITTED` (7) |

Every refusal is an audit entry ([Standalone security postures](STANDALONE_POSTURES.md)): the class (`remote-write`, `posture`, `audit`, `key-export`, ...) and who asked (`cable`, `ble`, `net`, `remote-admin`, `remote-cli`, `mqtt`). Two new kinds: `mqtt-session` (a request on a session that is not TLS-verified) and `mgmt-flag` (an attempt to set the management flag from a caller that may not). Repeats fold into one entry with a count.

## MECON commands

New commands live under `mecon.` and are tried at the place where upstream calls `MainBoard::handleCommand`, ahead of a board's own override, so a board with vendor commands keeps them and no board class changes.

| Command | Class | Reply |
|---|---|---|
| `get mecon.cli.who` | read | `> transport=usb auth=cable remote=0 posture=normal principal=cable`, how the engine sees the caller (`mgmt=on` is appended for a flagged broker session) |
| `get mecon.cli.check <line>` | read | `> class=write remote-limited`: the class of `<line>` and what the table says for this caller, without running it and without an audit entry |

## The command line over MQTT

The Companion `_wifi` builds with MQTT ([MQTT broker profiles](MQTT.md)) subscribe to a request topic once a broker session is up. `<node id>` is the first four bytes of the public key as 8 hex digits, the same id as in the status topic.

| Topic | Direction | Payload |
|---|---|---|
| `mecon/<node id>/cli/req` | broker to device | `{"id":"<correlation id>","cmd":"<command line>"}` |
| `mecon/<node id>/cli/resp` | device to broker | `{"id":"<same id>","status":"ok\|refused\|unknown\|bad-request","ok":<bool>,"reply":"<text>"}` |

- `id` is 1 to 32 characters of letters, digits, `.` `_` `:` `-`; it is echoed unchanged. `cmd` is 1 to 159 characters, with no control characters (a command line has no line breaks). Other keys are ignored; a duplicated `id` or `cmd`, a nested value, or anything that is not one JSON object is a `bad-request`, answered with an empty `id` and run as nothing.
- `status` is `ok` (the command ran), `refused` (the gate said no; `reply` is the refusal text of the table above), `unknown` (allowed, but not a command) or `bad-request`. `reply` is the command's own reply, JSON-escaped; a reply that does not fit the 512-byte MQTT buffer is cut at a character boundary and `"truncated":true` is added.
- QoS 0, not retained. **`reboot` and `poweroff` are never answered**: the device is gone before it could reply.
- A request is answered in every case, including a refusal. The device subscribes on any session; a request is **run only on a session that is TLS-verified** (the broker certificate matched the profile's pinned anchor). On a plain session, or TLS without an anchor, every request is answered `refused` with `ERR: management needs a TLS-verified broker` and audited.
- **Authority.** Mosquitto (or whatever broker) decides who may publish to a device's request topic; the device cannot tell publishers apart. Treat publish rights on `mecon/+/cli/req` as management rights over every device, and grant them per device with the broker's ACLs. The device's own check is the transport: the session must be TLS-verified, so the broker is the one the owner pinned.
- **Hardened.** A verified session reads. Writes and `reboot` need the profile flagged for management: `set mqtt.profile <n> manage on` (`off` clears it). The flag can be set only from a caller that may change the posture (the cable or a paired phone); a broker, however trusted, cannot flag itself. `get mqtt.profile` shows `manage=on` only when on, so a profile that does not manage lists as it always did. In Normal a verified session may manage without the flag. No session, flagged or not, runs a `secret`, `posture` or `destructive` command.
- **Retained requests.** A broker redelivers a retained message to every new subscriber, and the MQTT library does not say which messages are retained, so requests arriving in the first 1.5 s after the subscription are dropped: a retained `reboot` cannot become a reboot loop. A live request in that window gets no answer and is sent again. Publishers should not retain requests.
- **Cost.** The MQTT packet buffer grows from 256 to 512 bytes (one request or response). A request runs on the main loop like any other command.

Not verified on hardware: the ESP32 path of all of this. The TLS verification and the exchange are verified on the simulated board against real Mosquitto brokers.

## Memory

Measured by compiling the Heltec V3 environments at this change and at its base (the adaptive contact memory); RAM is the static figure PlatformIO reports (data and bss):

| Environment | RAM before | RAM after | Flash before | Flash after |
|---|---|---|---|---|
| `Heltec_v3_companion_radio_standalone` | 136,300 B | 136,396 B (+96 B) | 1,857,901 B | 1,865,069 B (+7,168 B) |
| `Heltec_v3_repeater` | 63,048 B | 63,048 B (+0 B) | 1,184,085 B | 1,187,481 B (+3,396 B) |

The registry and the table are `const` data in flash. Not in the static figure: the MQTT packet buffer is 256 B larger on the heap (512 B instead of 256 B), and a request is handled on the main loop's stack (about 1 KB for the request, the reply and the response). Neither has been measured on a board.

## Adding a command

1. Add it to the handler of its role as before.
2. If its privilege is not that of a `write` (the default), add a line to the registry in `CliRegistry.h` with its class. A command that is only read-only, secret, posture, destructive or reboot needs the line; a plain setting does not.
3. If it needs a cell the table does not have, that is a change to `CliPolicy.h` and to the matrix test, nowhere else.

## Known limits

- `poweroff` and `shutdown` are `write`: a caller that may write may power the device off, as before. A broker that may manage a device (Normal, or Hardened with the flag) therefore can. Power the device off only where someone can press the button.
- BLE with a default PIN and the TCP port are one *untrusted* row; the audit records both as `net`.
- A remote `erase` or `get prv.key` on a repeater or room server used to fall through to an unknown-command reply and now gets `ERR: not permitted` and an audit entry. The only behavioural difference from before the engine; no test relied on the old reply.
- The sensor example reaches the same gate through the common CLI. It had no front door before, so Hardened now applies its remote read-only rule there too. The sensor role is not verified on hardware or in the simulator.
- No rate limit on requests. A flood of refused requests folds into one audit entry but each is answered.
