# Monitor a device

## On the device

- `get dutycycle`: the band, its regulatory floor, your limit and the airtime used in the last hour.
- `get wifi.status`, `get mqtt.status`: link state, and for MQTT whether TLS was verified.
- `get audit`: the security audit log (posture changes, refusals, failed logins, resets), over a trusted link ([postures](STANDALONE_POSTURES.md#audit-log)). A manager reads its history with `audit.list`.
- `get mecon.mem`: free heap, the largest free block, the lowest free heap since boot, and whether the heap is below its floor ([heap floor](CONNECTIVITY_MODE.md#heap-floor)).
- `get mecon.heaplow`: what was running when the largest block, and the free heap, were at their lowest since boot.
- `get mecon.slowpass` and `get mecon.slowsections`, on a Companion: the longest main-loop pass and which part of the loop took it, and each part's own longest run. A long pass keeps the cable and the radio waiting ([CLI reference](CLI_REFERENCE.md)).
- `status.get` over the device interface: the status snapshot, with the LoRa management counts (`lora`), and on a Companion that has relayed a request, the counts of its relay replies (`rf`), which show where a missing reply was lost ([LoRa management](contract/LORA_MANAGEMENT.md)). Within it:
  - `rf.last_incomplete` names the frames that came of the last reply still missing frames after 60 s;
  - on a Companion that has sent a LoRa management frame, `lora_tx` says what became of each frame, with `lora_tx.recent`, `rf.heard` and `now_ms` giving when the last frames went on the air and were heard;
  - `events_dropped` counts signed events dropped for want of room (a completed `rf` reply or a `job` event first waits up to 10 s), and on the nRF52 boards `cable_dropped` counts frames dropped because the cable host stopped reading.

  `rf`, `lora_tx`, `events_dropped` and `cable_dropped` are left out of a `status.get` answered over LoRa.
- The connectivity page on boards with a screen: mode, posture, PIN state, Wi-Fi and MQTT ([screens and buttons](UI.md)).

## Telemetry streams

A manager subscribes to a stream with `sub.start` and ends it with `sub.stop`:

```json
{"v":1,"id":"s-1","op":"sub.start","args":{"stream":"health","period_s":60,"lifetime_s":300}}
```

The device then sends a snapshot every `period_s` until `lifetime_s` runs out, so a manager that goes away costs nothing. `lifetime_s` is 10 to 600 s (300 by default) and `period_s` at most 3600 s. Renew a subscription to keep it. Over MQTT, events arrive on `mecon/<device_id>/ev/<stream>`.

| Stream | What it carries | Default / minimum period |
|---|---|---|
| `health` | uptime, heap and stack, battery, contacts | 60 s / 5 s |
| `runtime` | reset reason, boot times, longest loop pass, heap-low state, and what was running at the heap's lowest (`heap_lows`) | 300 s / 60 s |
| `radio` | packet and error counters, noise floor, RSSI and SNR, airtime, duty cycle | 60 s / 5 s |
| `connectivity` | Wi-Fi, MQTT, BLE and USB link state | 60 s / 10 s, plus an event at each change |
| `power` | battery voltage, external power, charger, USB and solar | 300 s / 30 s |
| `environment` | attached sensors | 300 s / 30 s |
| `location` | GNSS position | 300 s / 30 s |
| `observations` | received packets | an event per packet, at most 10 per second |
| `logs` | audit log entries | an event per entry, at most 5 per second |
| `inventory` | contacts and channels on a Companion; counts only on a Repeater or Room server | 3600 s / 300 s |
| `screen` | a snapshot of the device screen | 60 s / 5 s |
| `rf` | Companion only: the replies the managed devices send to this Companion's `rf.request`, and their compact reports | an event per reply |

A board without a sensor, GNSS receiver, battery reading or screen does not offer that stream, and `hello` lists only the streams it can produce. Private streams (`location`, inventory entries, `screen`) and `logs` need more than a read permission: `logs`, for example, goes to the cable, to a BLE link paired with your own PIN, or over MQTT to a subscriber whose request a Deployment manager signed. A broker login by itself never gets it. The [telemetry catalogue](contract/TELEMETRY.md) gives every field and the access rule for each stream.

A screen snapshot is a diagnostic copy of what the device shows, sent to the manager. Nothing is ever drawn on the device's screen by a manager.

## Over LoRa

A node with no IP link sends compact telemetry over LoRa at a low rate, point to point or on its management channel. It is a small, versioned subset of the streams above: health, radio figures and, point to point only, location. A manager asks for one report at a time, or leaves a [standing instruction](contract/OPERATIONS.md#standing-instructions) that sends them on a schedule while it is away. Logs and screen snapshots are too large for LoRa and are not part of it.
