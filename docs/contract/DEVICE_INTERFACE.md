# Device interface v1 (design)

One logical device model reached over MQTT, USB and BLE. A Backend talks to the device directly over MQTT, or through a Reader that bridges USB or BLE; the device answers the same requests with the same results either way. Native MeshCore 1.18 Companion frames and the Repeater console stay exactly as they are beside it.

> **Status: design, recorded before implementation.** This page holds the decisions the implementation follows; each section names what is decided and what is left to the implementation. Nothing here is built yet. The broader target contract is in the [contract set](README.md); where it and this page differ on wire details, this page is the newer decision and the contract set will be aligned to it as the implementation lands.

## Starting point

An inventory of the target contract against the fork (77 device-visible items) found 25 met by native MeshCore 1.18 operations, 15 by extensions already in this fork, and 37 missing. Everything MECON-specific is reached today as one line of text CLI: over the Companion's `CMD_RUN_CLI_COMMAND`, the Repeater console and the MQTT `cli/req` topic. There is no device identifier separate from the node key, no capability discovery, no structured settings read or write, no correlated job results and no event streams. This interface adds those, and keeps the CLI as the single place where settings live and where authorization is decided.

## Decisions

### 1. Identity

- `device_id`: 16 random bytes generated on first boot, stored in their own file, written as 32 lowercase hex digits. Distinct from the native node public key, which a key rotation changes, and from any Backend row. Whether a reset scope keeps or regenerates it is decided with enrollment ([#95](https://github.com/hoejriis/mecon-firmware/issues/95)); until then the native factory reset and the rescue `erase` regenerate it.
- Every `hello` and every status snapshot reports both `device_id` and `node_key` (the native public key, hex).

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

- `id`: chosen by the requester, 1 to 32 characters of `[0-9A-Za-z._:-]`. The device echoes it on the result and keeps the last 16 ids per transport, so a duplicate delivered again (a reconnect, a retried write) gets the stored result instead of a second execution.
- `status`: `ok`, `refused` (not authorized; nothing executed), `invalid` (malformed or bad arguments), `unknown-op`, `unsupported-version`, `busy` (a bound was reached; retry later), `failed` (executed and failed; `error` says why). `refused`, `invalid`, `unknown-op`, `unsupported-version` and `busy` guarantee that nothing ran.
- `ts`: Unix seconds when the device clock is set, `null` when it is not. `epoch`: a boot counter kept in flash, so `(epoch, seq)` orders events across reboots and makes a gap visible.
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
| `config.apply` | a list of key/value writes, checked as a whole before any is applied; one result per key | write, per key |
| `sub.start` / `sub.stop` | start or stop a telemetry stream on this transport, with a requested rate and lifetime | read, per stream |
| `cancel` | stop a paged or long operation by its `id` | as the operation |

Native operations stay native: messaging, contacts, channels, adverts, clock, reboot and key import/export keep their Companion commands and CLI lines. Telemetry families and their streams are specified with [#96](https://github.com/hoejriis/mecon-firmware/issues/96); v1.0 ships the stream mechanism with the `health` stream.

### 5. Settings: one store, the CLI's

`config.get` and `config.apply` are built on the existing CLI settings, not beside them. Each schema key maps to the CLI `get`/`set` it stands for (`name` → `get name`/`set name`, `wifi.profile.1.ssid` → `set wifi.profile 1 ssid`), so there is one store, one validation and one audit trail. A key the CLI has no line for does not exist in the schema.

### 6. Authorization

Every operation and every key is checked by the CLI privilege engine (`src/helpers/cli/`) with the caller's real context: transport, how it authenticated, and the posture. The same table decides for a CLI line and for an interface request, so a transport change never upgrades a permission and a bridge cannot add any. A request refused by the table never executes and is audited like a refused CLI line. #95 adds Deployment-signed authority as one more caller kind in that table.

### 7. Framing per transport

**USB and BLE (Companion builds).** One Companion command code, `0x7F` (`CMD_MECON`), carries interface traffic in both directions; native codes are untouched and a stock app never sees `0x7F` unless it sends it. Each frame is:

```
0x7F | xid (1 byte) | index (1 byte) | count (1 byte) | up to 168 bytes of the message
```

- `xid` numbers messages per direction; `count` is the number of frames in the message (1 to 25, so at most 4,096 bytes), `index` counts from 0.
- One inbound message is reassembled at a time per transport. A frame for a new `xid` while one is incomplete discards the incomplete one; a message incomplete after 5 s is discarded. Either answers `invalid` when the `id` is known.
- Outbound results and events are queued per transport in a bounded queue (8 messages). When it is full, an event is dropped and counted in its stream; a result is never dropped but the request that would produce it gets `busy` first. Frames of different messages are never interleaved.
- Interleaving with native pushes is normal: a client must skip native frames while it waits for its result, as it already must for native requests.

**MQTT.** Under `mecon/<device_id>/`:

| Topic | Direction | Content |
|---|---|---|
| `req` | to device | request envelopes |
| `resp` | from device | result envelopes, QoS 1, not retained |
| `ev/<stream>` | from device | event envelopes, QoS 0, not retained |
| `status` | from device | the status snapshot, retained, with a last will `{"online":false}` |

No fragmentation. The MQTT client buffer (512 bytes today) has to grow to carry a full message; its size is set on measured heap, since the Heltec V3 has the least headroom during a TLS session, and a result that would not fit is paged. The existing `mecon/<node id>/cli/req` and `cli/resp` topics stay for the CLI line until the interface covers what they are used for, then they are withdrawn.

**Repeater and Room server.** They have no Companion framing. Over USB they take the same envelope as one line on the console, prefixed `mecon> ` (a line too long for the console is refused with `busy` and never truncated); over MQTT, where a build has it, as above. Over LoRa the remote-admin path keeps the CLI.

### 8. Resource bounds and the radio loop

- Requests are parsed and answered in the main loop in bounded steps; no operation blocks on a network or BLE write. A paged result is produced page by page.
- Each subscription has a bounded rate, a lifetime after which it ends unless renewed, and a drop counter reported in its events. A slow or vanished consumer costs dropped events, never loop time.
- Memory: one reassembly buffer per transport while a message is in flight, the outbound queue, and the last-16 id cache. The implementation records the measured total against the heap figures in [PLATFORM](../PLATFORM.md).

### 9. Conformance

Published with the implementation, in this repository:

- a JSON Schema for each envelope shape and each v1.0 operation;
- example requests and results, used as fixtures by the tests;
- a reference client (Python, `tools/`) that speaks USB, BLE and MQTT and runs the conformance suite against a simulated board and a real one.

A third party should be able to implement a client from this page, the schemas and the fixtures alone.

## Left to the implementation

- The JSON parser (a bounded in-tree parser like the MQTT CLI's, or a library) is chosen on measured flash and heap.
- The exact capability names and the first schema key list come from the CLI registry when the code is written.
- The status snapshot's field list is the one in [Core device contract](CORE_DEVICE_CONTRACT.md) restricted to what the build can measure; #96 extends it.

## Out of scope for v1.0

Signed authority, enrollment and reset scopes ([#95](https://github.com/hoejriis/mecon-firmware/issues/95)); the full telemetry catalogue, logs and screen frames ([#96](https://github.com/hoejriis/mecon-firmware/issues/96)); messaging over MQTT, outage sync, backfill and OTA jobs (later milestones).
