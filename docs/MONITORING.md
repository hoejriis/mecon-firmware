# Monitor a device

## On the device

- `get dutycycle`: the band, its regulatory floor, your limit and the airtime used in the last hour.
- `get wifi.status`, `get mqtt.status`: link state, and for MQTT whether TLS was verified.
- `get audit`: the security audit log (posture changes, refusals, failed logins, resets), over a trusted link ([postures](STANDALONE_POSTURES.md#audit-log)).
- The connectivity page on boards with a screen: mode, posture, PIN state, Wi-Fi and MQTT ([screens and buttons](UI.md)).

## Telemetry streams

A manager subscribes to a stream with `sub.start` and ends it with `sub.stop`:

```json
{"v":1,"id":"s-1","op":"sub.start","args":{"stream":"health","period_s":60,"lifetime_s":300}}
```

The device then sends a snapshot every `period_s` until `lifetime_s` runs out, so a manager that goes away costs nothing. Renew a subscription to keep it. Over MQTT, events arrive on `mecon/<device_id>/ev/<stream>`.

| Stream | What it carries | Default / minimum period |
|---|---|---|
| `health` | uptime, heap and stack, battery, contacts | 60 s / 5 s |
| `runtime` | reset reason, boot times, longest loop pass, heap-low state | 300 s / 60 s |
| `radio` | packet and error counters, noise floor, RSSI and SNR, airtime, duty cycle | 60 s / 5 s |
| `connectivity` | Wi-Fi, MQTT, BLE and USB link state | 60 s / 10 s, plus an event at each change |
| `power` | battery voltage, external power, charger, USB and solar | 300 s / 30 s |
| `environment` | attached sensors | 300 s / 30 s |
| `location` | GNSS position | 300 s / 30 s |
| `observations` | received packets | an event per packet, at most 10 per second |
| `logs` | audit log entries | an event per entry, at most 5 per second |
| `inventory` | contacts and channels | 3600 s / 300 s |
| `screen` | a snapshot of the device screen | on request, at most one per 5 s |

A board without a sensor, GNSS receiver, battery reading or screen does not offer that stream, and `hello` lists only the streams it can produce. Private streams (`location`, inventory entries, `screen`) and `logs` need more than a read permission: `logs`, for example, goes to the cable, to a BLE link paired with your own PIN, or over MQTT to a subscriber whose request a Deployment manager signed. A broker login by itself never gets it. The [telemetry catalogue](contract/TELEMETRY.md) gives every field and the access rule for each stream.

A screen snapshot is a diagnostic copy of what the device shows, sent to the manager. Nothing is ever drawn on the device's screen by a manager.

## Over LoRa

A node with no IP link sends compact telemetry over LoRa at a low rate, point to point or on its management channel. It is a small, versioned subset of the streams above. Logs and screen snapshots are too large for LoRa and are not part of it.
