# Three journeys, from the device's side

This page maps three things a manager does with a Companion or a managed node onto the generic operations the firmware provides: connected messaging, backfill after a reconnect, and RF health monitoring. It gives the device-visible states and the bounds a manager must plan for, and it separates what the firmware provides from what the manager decides. The contract pages stay normative. Where this page and a contract page differ, the contract wins. Nothing here is a second protocol: every operation is one of the [managed operations](contract/OPERATIONS.md). For the general split, see the [integration guide](INTEGRATION.md).

The firmware holds no routing policy, no message archive and no health policy. Each journey needs a manager to orchestrate it.

## 1. Connected messaging

A manager asks a Companion to send one native direct or channel message, follows what became of it, and reads the messages the Companion received. The stock app on the same device keeps working. A stock receiver reads a sent message as it reads any other, and a stock app on the sender is not told of a message it did not send.

### Device operations

| Operation | What it does | Defined in |
|---|---|---|
| `message.send` | Sends one native direct message (to a stored contact, by its whole key) or channel message (to a slot). Takes an optional `scope` for that send only | [OPERATIONS](contract/OPERATIONS.md) section 6, and section 5.3 for `scope` |
| `message.get`, `message.list`, `message.ack` | Read what became of a send; `message.ack` releases an entry | [OPERATIONS](contract/OPERATIONS.md) section 6.1, 6.2 |
| `inbox.list`, `inbox.ack` | Read the messages the device received, and release them | [OPERATIONS](contract/OPERATIONS.md) section 6.4 |
| `contact.list`, `contact.get`, `contact.put`, `contact.remove`, `contact.protect`, `contact.capacity` | Work on the native contact table | [OPERATIONS](contract/OPERATIONS.md) section 5.1 |
| `channel.*` | Work on the native channel table | [OPERATIONS](contract/OPERATIONS.md) section 1 |
| `message` and `inbox` events | A state change of a send, `{"message","hash","state"}`; a reception, `{"received","unread","lost"}` | [OPERATIONS](contract/OPERATIONS.md) sections 6.1 and 6.4 |

`status.get` carries `messages.open` and `messages.owed`, and the `inbox` counts, so a manager that reconnects learns there is something to collect.

### State machine: one send

| State | What moves it on | Next state |
|---|---|---|
| (request accepted) | handed to the radio queue | `queued` |
| (request refused) | all 8 outcome entries open: `busy` with `outcomes-full`. Radio queue full: `busy` with `queue-full`. Nothing is sent | none |
| `queued` | the dispatcher has no airtime | `duty-held` (`wait_ms` is the wait last given) |
| `queued`, `duty-held` | the packet goes on the air | `transmitted` |
| `transmitted` (channel message) | nothing more can be known | final: no ACK exists for a channel message |
| `transmitted` (direct message) | the recipient's native ACK arrives | `acked` (final) |
| `transmitted` (direct message) | no ACK within `est_ms` of the transmission | `timed-out` (final) |
| `timed-out` | a late ACK arrives | `acked` |
| `queued`, `duty-held` | the radio dropped it or could not start it | `failed` (final) |
| any | not in the table, or left the queue with no report for 2 hours | `unknown` (final) |

A text that does not fit is failed with `text-too-long`, not cut. A repeat past the result cache answers `failed` with `result-expired`. Whether `failed` can follow `transmitted` is not defined by the device.

### State machine: one received message

| State | What moves it on | Next state |
|---|---|---|
| (no copy kept) | the first `inbox.list`, or a Deployment on the device | slots exist; earlier receptions are counted as `lost` |
| unread | `inbox.ack` with the current `epoch` and an `upto` | released (below the mark) |
| unread | all 8 unread and a new reception | oldest channel message, or with none the oldest direct one, is pushed out and counted in `lost` |
| unread | reboot | gone; `seq` restarts at 1 under a new `epoch` |
| unread | `inbox.ack` under another `epoch` | failed with `wrong-epoch`, nothing released |

### Bounds a manager must plan for

| Bound | Source |
|---|---|
| Outcome table: 8 entries, in RAM, no message text. A collected entry goes first, then the oldest final one | [OPERATIONS](contract/OPERATIONS.md) section 6.2 |
| A reboot empties the table. A send then read as `unknown` may or may not have gone out | [OPERATIONS](contract/OPERATIONS.md) section 6.3 |
| Direct text: 160 bytes. Channel text: 160 bytes including the sender's name, a colon and a space | [OPERATIONS](contract/OPERATIONS.md) section 6.2 |
| A repeat with the same identity answers the kept result and is never a second send | [OPERATIONS](contract/OPERATIONS.md) section 6.3 |
| A signed repeat after a reboot is refused. An unsigned repeat after a reboot is a new send | [OPERATIONS](contract/OPERATIONS.md) section 6.3 |
| Inbox: 8 entries in RAM, about 1.8 KB, no expiry. A page is 8 entries at most, with `more` and `after` | [OPERATIONS](contract/OPERATIONS.md) section 6.4 |
| Inbox text over 160 bytes is kept cut and carries `cut` | [OPERATIONS](contract/OPERATIONS.md) section 6.4 |
| One packet heard twice is one entry. A sender's own retry is a new entry with another `hash` | [OPERATIONS](contract/OPERATIONS.md) section 6.4 |
| An ACK proves the recipient's radio node had the message, not that anyone read it. A timeout does not prove non-receipt | [OPERATIONS](contract/OPERATIONS.md) section 6.1 |
| Binary channel data and anything but a text message are not kept | [OPERATIONS](contract/OPERATIONS.md) section 6.4 |

### Who provides what

| Firmware provides | Manager (Backend) provides |
|---|---|
| The native send, and an honest outcome for each send | Which authorized device and gateway to send through |
| A bounded outcome table and inbox, each with an epoch | Correlation, and duplicate suppression across reboots and across connected and radio paths |
| The `hash`, `source`, `rx_ts` and `age_s` fields, and counts of what was lost | Retry decisions after an ambiguous outcome (`timed-out`, `unknown`) |
| Contact and channel administration on the native tables | Which contacts and channels exist, and favourites and protected-entry policy |
| Events and `status.get` counts that tell a reconnecting manager to collect | Reading before the 8-entry bounds overflow, and what to show a user |

## 2. Backfill and reconnect catch-up

A manager supplies messages a Companion missed while it was away, so the stock app shows them. The device puts each one on its native receive queue and transmits nothing. It chooses nothing: which history, which recipients, in what order and how fast are the manager's.

### Device operations

| Operation | What it does | Defined in |
|---|---|---|
| `message.deliver` | Puts a missed message on the native receive queue. Args `from` (a stored contact's whole key) or `channel` (a slot), `text`, `timestamp`. Result `{"state","epoch"}` | [OPERATIONS](contract/OPERATIONS.md) section 6.5 |
| `inbox.list` | Reports `source`: `radio`, or `manager` for a delivered message. The device sets it | [OPERATIONS](contract/OPERATIONS.md) section 6.4 |
| `contact.put` | Stores the contact a delivery needs. `message.deliver` creates none | [OPERATIONS](contract/OPERATIONS.md) sections 5.1 and 6.5 |
| `message.send` | Sends on the radio, if the manager asks for that separately | [OPERATIONS](contract/OPERATIONS.md) section 6 |

### State machine: one delivery

| State | What moves it on | Next state |
|---|---|---|
| (request) | the sender or slot, time and text match none of the last 16 heard or delivered | `delivered` |
| (request) | they match one of the last 16 | `duplicate`; nothing is queued |
| (request) | text does not fit with the marker | failed with `text-too-long` |
| (request) | native queue full of direct messages | `busy` with `queue-full` |
| (request) | contact or slot not stored | failed with `no-contact` or `no-channel`; no contact is created |
| `delivered` | a radio copy arrives | native ACK is sent; the copy is not queued a second time |
| `delivered` | the 16-entry memory moves past it, or the device reboots | a repeat reads as `delivered` again, under a new `epoch` after a reboot |
| (request, repeated) | the same request is sent again | the kept result |

When the native queue holds a channel message and is full, the oldest channel message goes, as on a reception. Whether a delivery is signed is decided by enrolment: once enrolled, a delivery must be signed.

### Bounds a manager must plan for

| Bound | Source |
|---|---|
| Duplicate memory: the last 16 messages delivered or heard, in RAM. Past 16, or after a reboot, a repeat reads as new | [OPERATIONS](contract/OPERATIONS.md) section 6.5 |
| Text with the marker: 146 bytes direct, 151 on a channel | [OPERATIONS](contract/OPERATIONS.md) section 6.5 |
| The marker is `[via manager] ` at the start of the text. Path reads as none, SNR as 0. It proves nothing; `source` in `inbox.list` does | [OPERATIONS](contract/OPERATIONS.md) sections 6.4 and 6.5 |
| Sender and time are what the manager supplied, and are not checked | [OPERATIONS](contract/OPERATIONS.md) section 6.5 |
| Inbox: 8 entries in RAM. `seq` restarts at 1 under a new `epoch` at reboot. Overflow shows as `lost` | [OPERATIONS](contract/OPERATIONS.md) section 6.4 |
| A reboot empties the native queue, under a new `epoch` | [OPERATIONS](contract/OPERATIONS.md) section 6.5 |
| A radio copy of a delivered message is not queued twice, but is still ACKed | [OPERATIONS](contract/OPERATIONS.md) section 6.5 |

A device does not keep a duplicate record across a reboot. A manager that must show one logical message after a reboot works from its own record, plus `hash`, `epoch` and `seq`.

### Who provides what

| Firmware provides | Manager (Backend) provides |
|---|---|
| Delivery to the local queue, with no transmission and no ACK | Which history is eligible, for which recipients, and how long it is kept |
| `delivered` or `duplicate` for the last 16 messages | Ordering, checkpoints and the catch-up rate |
| `source` set by the device, so the provenance is trustworthy | Duplicate suppression across reboots, and across overlapping radio and connected copies |
| Visible overflow: `lost`, `busy`, `queue-full` | Bounded retries, and keeping each batch inside the queue and memory sizes |
| No contact created by a delivery | Storing the contact or slot first, and signing the delivery once enrolled |

## 3. RF health monitoring

A node with no IP link sends a small signed report over LoRa. The manager can ask for one, or leave a standing instruction that sends reports on a schedule while it is away. The device runs only what the instruction names: a compact report to a named destination. It has no other action, script or rule, and no health policy of its own.

### Device operations

| Operation | What it does | Defined in |
|---|---|---|
| `report.now` | Sends one compact report to `to` (a node key) or to `slot`, and returns its `seq`. At most once in 30 s | [OPERATIONS](contract/OPERATIONS.md) section 5.1; [TELEMETRY](contract/TELEMETRY.md), compact reports |
| `standing.put` | Sends that report every `every_s` while the manager is away, with `runs` and `durable`. The same `id` replaces | [OPERATIONS](contract/OPERATIONS.md) section 5.1 |
| `standing.list` | Each instruction, with `runs_left`, and since boot `runs`, `failed` and `skipped` | [OPERATIONS](contract/OPERATIONS.md) section 5.1 |
| `standing.remove` | Removes one by `id` | [OPERATIONS](contract/OPERATIONS.md) section 5.1 |
| `status.get` | `lora`, and on a Companion `rf` and `lora_tx` | [LORA_MANAGEMENT](contract/LORA_MANAGEMENT.md) section 7 |
| `rf` stream | On a Companion, the replies and compact reports it hears, as `{"from","report"}` (base64) for the manager to verify | [TELEMETRY](contract/TELEMETRY.md), compact reports |

All of these go over LoRa point to point, and `report.now` goes on the management channel too. Standing instructions are defined under [Standing instructions](contract/OPERATIONS.md#standing-instructions). *(`lora_tx` and `rf.last_incomplete`: On `main`, in the release after v0.3.5.)*

### State machine: one standing instruction

The contract names no states for it. These are the events it defines.

| Condition | What moves it on | Next |
|---|---|---|
| (none) | `standing.put` | one period later, with jitter, the first run |
| waiting | the period comes due, one run per pass | a report is sent, and the schedule carries on |
| waiting | the duty window has no room, after looking once a minute for 5 minutes | `skipped`; waits for the next period, and the run does not count towards `runs` |
| waiting | the report cannot be built or sent | counted in `failed`; the schedule carries on |
| waiting | `runs` tries used, sent or not | ends; what is left is kept across a boot as `runs_left` |
| waiting | `standing.remove` | removed |
| waiting | the member who set it leaves the authority set, or the Deployment is gone | removed the next time it comes due, unsent |
| waiting | reboot | `durable: false`: ends. Durable: stays. A missed tick is gone, and the first run after boot comes within the jitter, with no catch-up burst |

An instruction set by an unsigned caller on a standalone device has no member and stays until removed.

### The compact report

Layout 0x1E, version 1, little-endian: fields, boot u32, seq u32, time u32 (0 when the device does not know the time), the fields in bit order, then a 64-byte signature. The fields are `health` (8 bytes), `radio` (12 bytes) and `location` (14 bytes). A full report is at most 113 bytes. 0xFFFF (u16) and 0x7F (i8) mean unknown, never zero. A board that cannot produce a field answers `unsupported-field`. `location` needs the right to read it and never goes on a channel. All of this is in [TELEMETRY](contract/TELEMETRY.md), compact reports.

### Bounds a manager must plan for

| Bound | Source |
|---|---|
| At most 4 standing instructions | [OPERATIONS](contract/OPERATIONS.md) section 5.1 |
| `every_s` from 900 s to 30 days, jitter up to a tenth of the period and at most 5 minutes | [OPERATIONS](contract/OPERATIONS.md) section 5.1 |
| `report.now` at most once in 30 s, else `busy` / `rate-limited` | [TELEMETRY](contract/TELEMETRY.md), compact reports |
| A full report is at most 113 bytes, signed with the node key. A boot that cannot sign sends none | [TELEMETRY](contract/TELEMETRY.md), compact reports |
| Every report goes through the duty window like any transmission. The window is seeded at the limit after boot and drains over an hour, so a reboot cannot exceed the limit | [TELEMETRY](contract/TELEMETRY.md), `radio` |
| A management message over LoRa is at most 1,024 bytes (8 frames). A `standing.put` request is several frames | [LORA_MANAGEMENT](contract/LORA_MANAGEMENT.md) section 4 |
| A message missing frames is discarded after 60 s. A reply held by the duty window is dropped after 10 min and stays owed or cached | [LORA_MANAGEMENT](contract/LORA_MANAGEMENT.md) section 8 |
| `runs_left`, `runs`, `failed` and `skipped` are the device's own counts. `runs` counts since boot; `runs_left` is kept across a boot | [OPERATIONS](contract/OPERATIONS.md) section 5.1 |
| A reply lost in transit is seen in `status.get` `rf` and `lora_tx` on a Companion | [LORA_MANAGEMENT](contract/LORA_MANAGEMENT.md) section 7 |

Cadence and airtime over a whole run have not been measured and published yet. Counts of a report heard are the receiver's, not a guarantee of delivery.

### Who provides what

| Firmware provides | Manager (Backend) provides |
|---|---|
| Signed compact reports with `boot` and `seq`, so a repeat or reorder shows | Choosing the fields, destinations and schedule, and putting the instruction |
| The standing instruction and its counts | Verifying each report, and keeping one record per device, boot and `seq` with its receivers |
| The duty window, rate limit and `unsupported-field` | Freshness rules and alert policy. Silence alone does not prove a device has failed |
| `rf` and `lora_tx` counters | Reading the counters, and the cadence and airtime measurements |
| A `location` field kept off channels | Whatever display it wants, and sizing any relay route it runs for a multi-frame request |

## Related issues

- Connected messaging: [#119](https://github.com/hoejriis/mecon-firmware/issues/119)
- Backfill and reconnect catch-up: [#120](https://github.com/hoejriis/mecon-firmware/issues/120)
- RF health monitoring: [#122](https://github.com/hoejriis/mecon-firmware/issues/122)
