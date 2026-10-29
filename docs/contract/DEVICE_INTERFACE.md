# Device interface v1

One logical device model reached over MQTT, USB and BLE. A Backend talks to the device directly over MQTT, or through a Reader that bridges USB or BLE; the device answers the same requests with the same results either way. Native MeshCore 1.18 Companion frames and the Repeater console stay exactly as they are beside it.

> **Status: v1.0 implemented for the Companion, the Repeater and the Room server.** The design was recorded here before the code, and the code follows it. Built: every operation of section 4 on the Companion, over USB and BLE (`0x7F` frames) and MQTT, on the Heltec V3 and V4 standalone builds (`ENABLE_MECON_IFACE`) and the simulated standalone Companion; and on the Repeater and Room server over the USB console (section 7), on the Heltec V3 and V4 Repeater and Room server builds and the simulated ones. Verified on the simulated boards, on real Heltec V3 and V4 Companions over all three transports, and on a real Heltec V3 Repeater over its console ([HARDWARE_VERIFIED](../HARDWARE_VERIFIED.md)); the V3 Room server build on the simulated board only; the V4 Repeater and Room server builds over their console on a real Heltec V4. The broader target contract is in the [contract set](README.md); where it and this page differ on wire details, this page is the newer decision and the contract set will be aligned to it as the implementation lands.

## Starting point (history)

Before v1.0, an inventory of the target contract against the fork (77 device-visible items) found 25 met by native MeshCore 1.18 operations, 15 by extensions already in this fork, and 37 missing. That inventory is not repeated. At that point, everything MECON-specific was reached as one line of text CLI: over the Companion's `CMD_RUN_CLI_COMMAND`, the Repeater console and the MQTT `cli/req` topic. There was no device identifier separate from the node key, no capability discovery, no structured settings read or write, no correlated job results and no event streams. This interface added those, and keeps the CLI as the single place where settings live and where authorization is decided.

## Decisions

### 1. Identity

- `device_id`: 16 random bytes generated on first boot, stored in their own file, written as 32 lowercase hex digits. Distinct from the native node public key, which a key rotation changes, and from any Backend row. Whether a reset scope keeps or regenerates it is decided with enrollment ([#95](https://github.com/hoejriis/mecon-firmware/issues/95)); until then the native factory reset and the rescue `erase` regenerate it.
- Every `hello` and every status snapshot reports both `device_id` and `node_key` (the native public key, hex). `hello` also carries `device_id_sig`, the node key's signature binding the two, and, for `args.nonce`, `nonce` and `device_id_sig_nonce` ([authority contract](AUTHORITY.md) section 3.5).
- **How a Backend learns it before MQTT.** The first `hello` travels over a direct path at enrolment: USB, or BLE through a Reader bridge. MQTT credentials reach a device only after that, so the Backend knows `device_id`, and can add `mecon/<device_id>/#` to the device's broker ACL, before the device ever publishes there. The broker username is the Backend's choice (it may rotate logins by generation): the device accepts any username and publishes under its `device_id` whatever it is.
- **A device already on MQTT when it gets this interface** (an in-place upgrade) never makes that direct-path `hello`, and from its first boot on the new build it publishes under `mecon/<device_id>/`, which its broker ACL does not yet cover. The Backend asks it for its `device_id` with the CLI line `get device.id` (or `get device.id <nonce>`) on the device's legacy `mecon/<node id>/cli/req` topic, which its existing login already covers. The answer is `> <device_id> <sig>` (with `<nonce>` appended for the nonce form). The Backend verifies `<sig>` against the node key of that login ([authority contract](AUTHORITY.md) section 3.5), then adds `mecon/<device_id>/#` to that same login. Nothing is granted up front.

### 2. Versions

- One interface version, `MAJOR.MINOR`, starting at `1.0`. A request carries the major it speaks (`"v":1`); a device that does not speak that major answers `unsupported-version` and executes nothing. A minor adds operations, fields or event types and never changes the meaning of an existing one.
- Each operation family that can evolve on its own (settings schema, each telemetry stream) carries its own profile version in its result, so a stream can change without a new interface major.
- Unknown fields are ignored. An unknown operation is refused (`unknown-op`). Authority that is not understood is never granted.

### 3. Envelope

UTF-8 JSON objects. Three shapes:

```json
{"v":1,"id":"r-17","op":"config.get","args":{"keys":["name","radio"]}}
{"v":1,"id":"r-17","status":"ok","result":{ ... }}
{"v":1,"ev":"health","seq":42,"epoch":7,"ts":1792843200,"data":{ ... }}
```

- `id`: chosen by the requester, 1 to 32 characters of `[0-9A-Za-z._:-]`, unique per request. The device echoes it on the result. For operations that change state the device keeps, in one cache shared by every transport:
  - the last 8 ids with their results (each up to 384 bytes, enough for an apply of about eight keys): a duplicate (a reconnect, a retried write, the same request over another transport) gets the stored result instead of a second execution;
  - an 8-byte hash of each of the last 64 such ids: a duplicate whose result is no longer kept gets `failed` with `"error":"result-expired"`, never a second execution.
  A stored id belongs to the operation that produced it: the same id under another operation gets `invalid` with `"error":"id-in-use"`, neither the stored result nor a run, so an open read cannot fetch a write's result by naming its id. A duplicate is checked against the caller's authority as it is now (refused if that has been withdrawn) and nothing else: changes of roster or authority do not clear the cache. A reboot does; every result carries `epoch`, so a requester that sees `epoch` change must not retry a state-changing request blindly but read the state first. A read is not kept: running it again is harmless.
- `status`: `ok`, `refused` (not authorized; nothing executed), `invalid` (malformed or bad arguments), `unknown-op`, `unsupported-version`, `busy` (a bound was reached; retry later), `failed` (executed and failed; `error` says why). `refused`, `invalid`, `unknown-op`, `unsupported-version` and `busy` guarantee that nothing ran.
- `ts`: Unix seconds when the device clock is set, `null` when it is not. `epoch`: a counter kept in flash and incremented on every boot, carried on every result and event, so `(epoch, seq)` orders events across reboots even while `ts` is `null`.
- `seq`: one sequence per stream per device, not per subscription. An event is produced once and the same `(epoch, seq)` goes to every transport subscribed to its stream, so a Backend receiving it over MQTT and over a Reader bridge can drop the second copy. An event dropped on one transport (its queue full) leaves a gap on that transport only.
- Absent, `null` and `0` mean different things: absent = not supported by this build; `null` = supported but unknown now; `0` = measured zero.
- Size bound: 4,096 bytes per message after reassembly. A result that would be larger is paged by the operation (`cursor` in, `next` out).
- `auth`: reserved for signed authority ([#95](https://github.com/hoejriis/mecon-firmware/issues/95)). A v1.0 device implements none, so a request that carries `auth` is `refused` rather than run as if it were unsigned.

### 4. Operations v1.0

| Operation | Does | Authorized as |
|---|---|---|
| `hello` | interface versions, `device_id`, `node_key`, role, hardware, firmware and upstream versions, capability list (paged) | read |
| `status.get` | the current status snapshot (the same document MQTT publishes as `status`) | read |
| `config.schema` | settings this build has: key, type, range or enum, writable, write-only, whether a reboot is needed (paged) | read |
| `config.get` | values of the requested keys (write-only keys report `"set"` or `"unset"`) | read; secret keys as the CLI decides |
| `config.apply` | a list of key/value writes, checked as a whole before any is applied; one result per key. With `"confirm":{"within_s":N}` (N 30 to 900, default 300) it is applied tentatively and reverted unless confirmed; any caller but the unsigned cable must send it for a key the schema marks `confirm` ([operations contract](OPERATIONS.md) section 3.4) | write, per key |
| `sub.start` / `sub.stop` | start or stop a telemetry stream on this transport, with a requested rate and lifetime | read, per stream |
| `deployment.get` | the stored authority set and its signature, the epochs and revisions, the fallback flag and each member's sequence mark ([authority contract](AUTHORITY.md) section 6.3). Companion only | open read |
| `deployment.provision` | first provisioning into a Deployment, or a signed re-enrolment ([authority contract](AUTHORITY.md) section 6.1). Companion only | write; unsigned only from the cable or BLE with a PIN of the device's own, and only for a first provisioning |
| `deployment.authority_update` | a newer DAK-signed authority set with any rotations before it ([authority contract](AUTHORITY.md) section 6.2). Companion only | any caller (it carries its own signature); rate limited |
| `identity.rotation` | the rotation statement the old node key signed, `{"rotation":{"statement":"<signed text>","sig":"<base64>"}}`, or an empty result when the key was never rotated; `deployment.get` carries the same member, only for a caller that may read ([authority contract](AUTHORITY.md) sections 3.5 and 11). Companion only | read: the statement names `device_id`, so untrusted links are refused |
| `identity.rotate` | a new node key for a new owner; drops contacts and the message queue, keeps configuration, channels, the record and `device_id` ([authority contract](AUTHORITY.md) section 11). Companion only | signed and sensitive; refused without a readable record |
| `job.get`, `job.list` | one job, `{"job","op","state","owed","result"}` (and `confirm_left_s` while awaiting confirmation), or every job with the table's `open` and `owed` counts ([operations contract](OPERATIONS.md) section 3) | read; plain reads when signed |
| `job.ack` | releases an owed terminal result, `{"acked":true}` | read; once a device holds a Deployment record, signed with a `job_id` |
| `job.cancel`, `job.confirm` | cancel a job, or revert it if it has applied; confirm a change awaiting confirmation | every CLI class the job's change needed |
| `cancel` | stop a paged or long operation by its `id`. **Not implemented** in v1.0 on any build; it is not in the capability list | as the operation |

Native operations stay native: messaging, contacts, channels, adverts, clock, reboot and key import/export keep their Companion commands and CLI lines. Telemetry families and their streams are specified with [#96](https://github.com/hoejriis/mecon-firmware/issues/96); v1.0 ships the stream mechanism with the `health` stream.

### 5. Settings: one store, the CLI's

`config.get` and `config.apply` are built on the existing CLI settings, not beside them. Each schema key maps to the CLI `get`/`set` it stands for (`name` → `get name`/`set name`, `wifi.profile.1.ssid` → `set wifi.profile 1 ssid`), so there is one store, one validation and one audit trail. A key the CLI has no line for does not exist in the schema.

Indexes in keys are **0-based**, as in the CLI: `wifi.profile.0.ssid` is the first profile, `wifi.profile.1.ssid` the second, `mqtt.profile.0.host` the first broker profile.

### 6. Authorization

Every operation and every key is checked by the CLI privilege engine (`src/helpers/cli/`) with the caller's real context: transport, how it authenticated, and the posture. The same table decides for a CLI line and for an interface request, so a transport change never upgrades a permission and a bridge cannot add any. A request refused by the table never executes and is audited like a refused CLI line. #95 adds signed authority as one more caller kind in that table.

### 7. Framing per transport

**A Reader bridge** carries reassembled envelopes verbatim between the device and the Backend, tagged with the transport; it never builds or alters `id`, `op`, `args` or `auth`. A Backend may stamp its own receive time beside an event whose `ts` is `null`.

**USB and BLE (Companion builds).** One Companion command code, `0x7F` (`CMD_MECON`), carries interface traffic in both directions; native codes are untouched and a stock app never sees `0x7F` unless it sends it. Each frame is:

```
0x7F | xid (1 byte) | index (1 byte) | count (1 byte) | up to 168 bytes of the message
```

- `xid` numbers messages per direction; `count` is the number of frames in the message (1 to 25, so at most 4,096 bytes), `index` counts from 0.
- One inbound message is reassembled at a time per transport, in a buffer allocated when it starts and freed when it is taken or dropped. A frame for a new `xid` while one is incomplete discards the incomplete one; a message incomplete after 5 s is discarded. Either answers `invalid` when the `id` is known. Every frame but the last carries exactly 168 bytes, so frames may arrive in any order; a repeated frame is ignored.
- Outbound results and events are queued per transport in a bounded queue of 8 messages. Events may fill at most 7 of them and must leave 512 bytes free; past that an event is dropped and counted in its stream. A result that does not fit is replaced by a short `busy` result, which always fits in what events had to leave. Frames of different messages are never interleaved.
- Interleaving with native pushes is normal: a client must skip native frames while it waits for its result, as it already must for native requests.

**MQTT.** Under `mecon/<device_id>/`:

| Topic | Direction | Content |
|---|---|---|
| `req` | to device | request envelopes |
| `resp` | from device | result envelopes, QoS 0, not retained |
| `ev/<stream>` | from device | event envelopes, QoS 0, not retained |
| `status` | from device | the status snapshot, retained, with a last will `{"online":false}` |

One status topic: a build that speaks this interface publishes `mecon/<device_id>/status` (with its last will) and no longer publishes the earlier `mecon/<node id>/status`, so presence never comes from two last wills. Which topic a device uses follows from its build; `hello` reports the interface version.

No fragmentation. On a build with the interface the MQTT client's buffer is 1,600 bytes (512 without it), which bounds a message over MQTT at about 1.5 KB rather than the 4,096 bytes of the frame transports: every v1.0 result fits (paged results are paged to fit), and a larger request is dropped by the client library. The client publishes at QoS 0 (PubSubClient has no QoS 1 publish), so a lost result is recovered by retrying the request with the same `id`, which returns the stored result for a state-changing operation. The CLI topic's guard applies here too: requests arriving in the first 1.5 s after the subscription, where a retained message lands, are dropped, so a retained request never runs again at a reconnect. The existing `mecon/<node id>/cli/req` and `cli/resp` topics stay for the CLI line until the interface covers what they are used for, then they are withdrawn.

**Repeater and Room server.** They have no Companion framing. Over USB they take the same envelope as one line on the console, prefixed `mecon> `; over MQTT, where a build has it, as above. Over LoRa the remote-admin path keeps the CLI.

- A request is one line, `mecon> <envelope>`, ended by CR or LF. The console echoes the seven characters of the prefix as it echoes any typed text; from there on the line is the interface's and is not echoed, so the console's 160-character limit does not apply. An interface line may be up to 1,600 bytes (the bound MQTT already sets). A longer line is refused with `busy`, error `line-too-long`, and the id `""` (it is never read), and nothing of it runs.
- The result comes back as one line `mecon> <envelope>`, after the CR LF that ends the echoed prefix. Events of a subscription (`stream.health`) come back the same way, between requests. A client reads lines, takes those that start with `mecon> {`, and skips everything else: the echo, logs and stock CLI replies.
- The caller is the console, that is the cable: every operation and key is decided by the CLI privilege table exactly as the same console line would be.
- `hello` reports `role` `repeater` or `room`. `status.get` and the health stream carry no `contacts`, which these roles do not have.
- Settings use the role's own CLI. The Repeater and Room server share one settings CLI, and so one key table: `name`, `tx`, `radio`, `lat` and `lon` (signed decimals within 90 and 180), `repeat`, `advert.interval` (minutes, kept in 2-minute steps, so an odd value reads back one lower), `flood.advert.interval` (hours), `flood.max`, `loop.detect`, `allow.read.only` and `owner.info`. `guest.password` is not offered: its CLI line reads it back in clear, so it cannot be write-only here.

### 8. Resource bounds and the radio loop

- Requests are parsed and answered in the main loop in bounded steps; no operation blocks on a network or BLE write. A paged result is produced page by page.
- Each subscription has a bounded rate, a lifetime after which it ends unless renewed, and a drop counter reported in its events. A slow or vanished consumer costs dropped events, never loop time.
- Memory: the id cache, 3.4 KB for the device, and 512 bytes of hashes of older ids; per transport, the outbound queue and, only while a message is arriving, its reassembly buffer (at most about 4.1 KB). The implementation records the measured total against the heap figures in [PLATFORM](../PLATFORM.md).

### 9. Conformance

Published with the implementation, in this repository:

- a JSON Schema for each envelope shape and each v1.0 operation;
- example requests and results, used as fixtures by the tests;
- a reference client (Python, `tools/`) that speaks USB, BLE and MQTT and runs the conformance suite against a simulated board and a real one.

A third party should be able to implement a client from this page, the schemas and the fixtures alone.

Published so far (v1.0 first cut):

- [`schema/`](schema/): `request.json`, `result.json`, `event.json`; the results of `hello`, `status.get`, `config.schema`, `config.get`, `config.apply`, `sub.start` and `sub.stop`; and the data of the `health` event (`event.health.json`);
- [`fixtures/requests.json`](fixtures/requests.json): requests with the status (and result schema) a v1.0 device must answer, plus a transport case. A case that only applies to some roles lists them in `roles`; [`fixtures/framing.json`](fixtures/framing.json): one message and its exact `0x7F` frames;
- [`tools/mecon_iface.py`](../../tools/mecon_iface.py): the reference client (framing, reassembly, envelope; transport-neutral), and [`tools/mecon_schema.py`](../../tools/mecon_schema.py), a dependency-free checker for the keywords these schemas use.

- [`fixtures/config_schema.recorded.json`](fixtures/config_schema.recorded.json) and [`fixtures/config_radio.recorded.json`](fixtures/config_radio.recorded.json): the whole settings schema and real radio writes and reads, recorded from one build.

The simulated-board suite (`sim/tests/test_device_interface.py`) runs every fixture against the firmware over USB, BLE and MQTT and checks each answer against the schemas; it fails if a recorded fixture drifts from the firmware.

## Left to the implementation

- The JSON parser (a bounded in-tree parser like the MQTT CLI's, or a library) is chosen on measured flash and heap.
- The exact capability names and the first schema key list come from the CLI registry when the code is written.
- The status snapshot's field list is the one in [Core device contract](CORE_DEVICE_CONTRACT.md) restricted to what the build can measure; #96 extends it.

## Out of scope for v1.0

Signed authority, enrollment and reset scopes ([#95](https://github.com/hoejriis/mecon-firmware/issues/95)); the full telemetry catalogue, logs and screen frames ([#96](https://github.com/hoejriis/mecon-firmware/issues/96)); managed operations ([#109](https://github.com/hoejriis/mecon-firmware/issues/109)); signed updates over IP, USB and BLE ([#110](https://github.com/hoejriis/mecon-firmware/issues/110)); private management channels, authorized LoRa operations and compact LoRa telemetry ([#114](https://github.com/hoejriis/mecon-firmware/issues/114), [#115](https://github.com/hoejriis/mecon-firmware/issues/115), [#116](https://github.com/hoejriis/mecon-firmware/issues/116)); and the MECON-specific messaging, backfill, outage sync and LoRa image delivery ([#119](https://github.com/hoejriis/mecon-firmware/issues/119) to [#121](https://github.com/hoejriis/mecon-firmware/issues/121), [#123](https://github.com/hoejriis/mecon-firmware/issues/123)).
