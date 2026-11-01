# Manage a device

A device can be managed in two ways, and both are decided by the same privilege table ([command-line engine](CLI.md)):

- **CLI commands**, typed on a console or sent as a CLI line. Every setting is a CLI setting.
- **The device interface**, a versioned JSON request/result interface that a manager application uses. It reads and writes the same settings as the CLI, and adds capability discovery, jobs, telemetry and updates ([device interface](contract/DEVICE_INTERFACE.md)).

## Ways in

| Path | Roles | Trusted? | Notes |
|---|---|---|---|
| USB | all | always | The recovery path. Companion: app frames. Repeater and Room server: text console |
| BLE | Companion | only when paired with a PIN other than 0 or 123456 | A default-PIN link can message, not manage |
| Wi-Fi TCP companion port | Heltec Companion | never | Messaging and reads only. Not started in Hardened |
| MQTT | Heltec boards, every role | only a TLS-verified broker session | What a broker may change depends on the posture and the profile's `manage` flag ([postures](STANDALONE_POSTURES.md)) |
| LoRa, point to point | all | by authority, not by the link | Authenticated requests from a manager. Repeaters and Room servers also keep stock remote admin login |
| LoRa, management channel | all | by authority, not by the link | Channel membership grants nothing |

A refusal is an explicit reply (for an app frame, error 7, `ERR_CODE_NOT_PERMITTED`) and an audit entry, never a silent drop.

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

A node with no IP link can still be managed over LoRa, point to point or on its management channel. It takes the same operations under the same authorization as any other path. LoRa adds no operation and no permission. Airtime is limited by the duty cycle, so LoRa carries operations and compact telemetry, not bulk transfers ([management over LoRa](contract/LORA_MANAGEMENT.md)).
