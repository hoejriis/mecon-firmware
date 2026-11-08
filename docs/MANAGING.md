# Manage a device

This page describes the finished product. Management over LoRa and standing instructions first shipped in v0.2.0, a pre-release, and have run on few boards: see [availability](AVAILABILITY.md).

A device can be managed in two ways, and both are decided by the same privilege table ([command-line engine](CLI.md)):

- **CLI commands**, typed on a console or sent as a CLI line. Every setting is a CLI setting.
- **The device interface**, a versioned JSON request/result interface that a manager application uses. It reads and writes the same settings as the CLI, and adds capability discovery, jobs, telemetry and updates ([device interface](contract/DEVICE_INTERFACE.md)).

## Ways in

| Path | Roles | Trusted, on a standalone device? | Notes |
|---|---|---|---|
| USB | all | always | The recovery path. Companion: app frames. Repeater and Room server: text console |
| BLE | Companion | only when paired with a PIN other than 0 or 123456 | A default-PIN link can message, not manage |
| Wi-Fi TCP companion port | Heltec Companion | never | Messaging and reads only. Not started in Hardened |
| MQTT | Heltec boards, every role | only a TLS-verified broker session | What a broker may change depends on the posture and the profile's `manage` flag ([postures](STANDALONE_POSTURES.md)) |
| LoRa, point to point | all | by authority, not by the link | Authenticated requests from a manager. Repeaters and Room servers also keep stock remote admin login |
| LoRa, management channel | all | by authority, not by the link | Channel membership grants nothing |

Which operations each path carries differs ([what differs, and why](SUPPORTED_HARDWARE.md#what-differs-and-why)), and `hello` lists what this device offers on it.

An operation refused to a caller gets an explicit reply (for an app frame, error 7, `ERR_CODE_NOT_PERMITTED`) and an audit entry. Traffic that fails before its signature is verified over LoRa is different. On the management channel it is dropped without an answer and counted, and on a point-to-point path it gets an answer only within a small shared budget. Otherwise anyone could make the device transmit by sending it junk ([airtime](contract/LORA_MANAGEMENT.md#7-airtime)).

## Standalone and enrolled

| | Standalone (no Deployment) | Enrolled |
|---|---|---|
| Who may change settings | A trusted link, by the table above and the [posture](STANDALONE_POSTURES.md) | A manager's signature, on any path |
| The unsigned USB cable | Anything its posture allows | Reads, reboot and the recovery operations, and no configuration writes. In Hardened it needs a local unlock for reads, and lowering the posture takes a signed `posture.set`. A factory reset always stays with the cable |
| A TLS-verified broker session | Reads, and writes as the posture and profile allow | Reads only |
| A paired BLE link | Management | Messaging, reads and reboot, with no factory reset |

Being trusted describes a link on a standalone device. On an enrolled device, no link grants a change by itself.

## Using the device interface

A request names an operation and carries an id. The result repeats the id:

```json
{"v":1,"id":"r-1","op":"hello"}
{"v":1,"id":"r-2","op":"config.get","args":{"keys":["name","radio"]}}
```

`hello` lists every operation and telemetry stream this build and board support. Read it rather than assume. The interface travels as `0x7F` app frames over USB and BLE, as `mecon> ` lines on the Repeater and Room server console, and on the topics `mecon/<device_id>/req` and `resp` over MQTT. The [device interface](contract/DEVICE_INTERFACE.md) gives the framing and limits, and the [managed operations](contract/OPERATIONS.md) list what exists.

`tools/mecon_iface_hw.py` runs the published request fixtures against a board over `serial`, `ble`, `console` or `mqtt`, and is a quick way to see the interface answer. Some fixtures change settings, so use it on a spare board.

## Changes that could cut you off

A change to the radio, transmit power, connectivity mode, Wi-Fi or broker settings could lose the link it was sent over. From anything except the cable, such a change must ask for confirmation:

```json
{"v":1,"id":"r-3","op":"config.apply","args":{"set":{"tx":20},"confirm":{"within_s":300}}}
```

The change takes effect as a job in `awaiting-confirm`. Send `job.confirm` with that job once you can still reach the device. If you do not confirm within the time, if you cancel, or if the device boots twice first, the old values come back.

## Managed by a manager: Deployments

On its own, a device follows its posture rules. To have a manager command it, enrol it in a **Deployment**: a management domain whose authority key signs the list of managers. Enrolment is done once, over the USB cable. After that:

- each manager signs its requests, and the device refuses a replayed request, or an unsigned one that changes state. The unsigned cable keeps reads and the recovery operations, not configuration writes;
- results and events are signed by the device;
- a broker login, a BLE pairing, a TLS session or a channel key never grants authority by itself.

The details, including manager rotation, revocation and local unlock, are in [management authority](contract/AUTHORITY.md). Leaving a Deployment is `deployment.reset` ([update and recover](UPDATING_AND_RECOVERY.md#resets)).

## A private management channel

A device can have a **management channel**: the private channel a manager reaches it on over LoRa. A Companion marks one of its own channels, which stays an ordinary channel and still carries messages. A Repeater or Room server holds a single management channel for this purpose only, with the same bind, rotate and remove steps and the same confirmation rule. On a Companion:

1. Create the channel in a free slot: `channel.put` with `slot`, `name` and a 128-bit `secret` (32 hex digits), or with the app.
2. Bind it: `mgmt.channel.bind` with that `slot`.
3. Rotate its key later with `mgmt.channel.rotate` and a new `secret`.
4. Remove the binding with `mgmt.channel.unbind`. Add `"remove": true` to delete the channel too.

From anything but the cable, each of these steps takes a `confirm` and is reverted if not confirmed, so a wrong key cannot strand the device. The public channel cannot be bound, and binding starts no traffic of its own ([management channel](contract/MANAGEMENT_CHANNEL.md)).

## Over LoRa

A node with no IP link can still be managed over LoRa, point to point or on its management channel. An operation keeps its meaning and authorization there, and LoRa adds no operation and no permission. It carries less than the other paths:

- **Enrolled devices only.** Only a signed request runs, so a device with no Deployment runs nothing on this path. A native remote-admin login to a Repeater or Room server is a separate path, and stays as described in the [CLI reference](CLI_REFERENCE.md).
- **No secrets.** Key material and sensitive operations never go on the air, on either path.
- **What the channel may disclose.** Every member of a management channel can read its replies. So the channel carries only status, job state and the results of changes. Anything that lists or reveals state goes point to point, under a pairwise key.
- **What airtime allows.** The duty cycle limits every frame, so LoRa carries operations and compact telemetry, not subscriptions, full logs, screen snapshots or firmware images.

The [management over LoRa](contract/LORA_MANAGEMENT.md) contract gives the rules for each operation.

## Messages, contacts and channels through a manager

On a Companion, a manager can do what the stock app does, and the two share the device:

- **Send** one native direct or channel message with `message.send`, and follow it with `message.get`. An acknowledgement shows the recipient's node had the message. A timeout does not prove it did not.
- **Collect** received messages with `inbox.list`. This is a copy beside the native queue: neither reader takes messages from the other.
- **Hand a missed message to the stock app** with `message.deliver`. It is marked `[via manager]` and never transmitted.
- **Administer contacts and channels** with `contact.*` and `channel.*`, on the native tables.

The device keeps a few of these in RAM and a reboot loses them, so this is not delivery or history. Bounds and what each field proves are in the [messages contract](contract/OPERATIONS.md#6-messages).

## Standing instructions

A manager can ask the device for a [compact report](contract/TELEMETRY.md) over LoRa now (`report.now`), or on a schedule that the device keeps while the manager is away (`standing.put`). A standing instruction sends a report and does nothing else.

- **What a manager sets:**
  - an `id`;
  - the report's `fields`;
  - a period, `every_s`, from 900 s to 30 days;
  - a destination, either `to` (a node key) or `slot` (a channel). `location` never goes on a channel, where every member could read it.
  - Optionally, `runs`, to stop after that many tries;
  - optionally, `durable: false`, to end it at the next boot.
  A device keeps at most 4, and a put with an existing `id` replaces that instruction.
- **When reports come.** The first comes one period after the put. Each run adds a jitter of up to a tenth of the period, at most 5 minutes. After a boot, the next run comes within that jitter. A run missed while the device was off is not made up.
- **Counting runs.** `runs` counts tries, sent or not. What is left of it is kept across a boot, so a device that reboots does not get its runs back. `standing.list` shows it as `runs_left`.
- **Airtime.** A report goes through the duty window. With no room, it looks again once a minute for 5 minutes. Then it counts as `skipped`, which does not use up a run, and waits for the next period.
- **Counters and removal.** `standing.list` counts `runs`, `failed` and `skipped` since the last boot. `standing.remove` ends an instruction. One set by a member who leaves the authority set is removed the next time it comes due.

**In v0.2.0 and v0.2.1,** a new instruction sends its first report at once, and a count set with `runs` starts again at each boot. Both are fixed on `main` for the next release: see [availability](AVAILABILITY.md).

The full rules are in the [operations contract](contract/OPERATIONS.md#standing-instructions).
