# Managed operations (design and matrix)

Every management action a device offers, on which board, role and transport, under which authority, and with which result. This page extends the [device interface](DEVICE_INTERFACE.md) rather than adding a second command model: an operation is a device-interface `op`, its authority is the CLI privilege table ([Command-line engine](../CLI.md)) plus Deployment authority, and its settings are the CLI's settings.

> This page specifies every operation. Which of them a device offers depends on its build, board and role: `hello` lists `op.<name>` for each one it can run, and any other answers `unknown-op`.

## 1. One operation, every transport

- **One name, one meaning.** An operation is `<family>.<verb>` (`device.reboot`, `contact.put`, `job.confirm`). It has the same arguments, result, errors and authority over MQTT, over a Reader's USB or BLE bridge, on the Repeater and Room server console, and over LoRa where the operation opts in ([management over LoRa](LORA_MANAGEMENT.md)). A transport never adds a permission, and a bridge forwards envelopes verbatim (DEVICE_INTERFACE section 7).
- **Native state stays authoritative.** Each operation is implemented by the native MeshCore function or CLI line it stands for. `contact.put` writes the native contact table, `channel.put` the native channel table, and `config.apply` the CLI settings. Nothing keeps a second copy. A native Companion frame or CLI line and the operation that stands for it change the same state and are decided by the same table cell.
- **Allowlisted, not a shell.** Only named operations exist. There is no "run this CLI line" operation on the interface. The MQTT `cli/req` topic stays only until the interface covers what it is used for (DEVICE_INTERFACE section 7), and then it is withdrawn.
- **What a target can do, it says.** `hello` lists an operation in `capabilities` (as `op.<name>`) only when this build, board and role can run it. An operation absent from the list answers `unknown-op`. Permission is never inferred from the transport: a listed operation can still be `refused` for this caller.
- **Messages are generic, and policy is not.** Native sending and receiving keep their Companion frames. Section 6 adds what a manager may ask for: a native send with correlated outcomes, collecting received messages, and supplying one to the local app. Choosing a gateway, retrying and deciding what to catch up on belong to the manager.

## 2. Authority

Every operation carries one class of the CLI table: `read`, `write`, `secret`, `posture`, `destructive` or `reboot`. The caller's principal (cable, paired BLE, verified broker, remote admin, ...) and the posture select the table cell. The Deployment-signed principal, and what unsigned callers may do once a device holds a Deployment record, are in [AUTHORITY](AUTHORITY.md) section 9. This page does not restate those rules; each operation below names its class, and that table decides.

- An operation whose class the caller lacks answers `refused` with the table's reason as `error`. Nothing runs, and the refusal is audited like a refused CLI line.
- An operation that maps to a CLI line is decided on that line, with the caller's real context, as `config.apply` already is per key. Its class cannot be weaker than the line's.
- A secret is never in a result, an event or a log: Wi-Fi and broker passwords, the broker user, channel secrets, private keys and the BLE PIN. The exceptions are the ones in [Security and authority](SECURITY_AND_AUTHORITY.md) (the private key under its custody rule, the PIN over a trusted local link), and each is a `secret`-class operation of its own.

## 3. Jobs: results that outlive a request

**What the reference firmware has today.**
- The table of 3.2 and 3.3 is built on the Companion, Repeater and Room server. It is kept in `/mecon_jobs`, and saved again at every boot and every 30 running seconds while a job awaits confirmation, so neither the boot count nor the time left starts over.
- `job.get`, `job.list`, `job.ack`, `job.cancel`, `job.confirm` and `status.get`'s `jobs` member answer it.
- `config.apply` with `confirm` (3.4) is the operation that creates jobs. `job.cancel` and `job.confirm` need every CLI class the job's lines needed, so a `write` caller cannot confirm or revert a `posture` change such as `connectivity.mode`.
- A job's identity is the request `id`, or `#<issuer>:<job_id>` for a signed request, where `<issuer>` is the first 6 bytes of the issuer's instance id in hex.
- Each change of a job's state is a `job` event on every link whose caller may read, and over a TLS-verified broker session.
- `update.offer` creates the other kind of job: a firmware update, which spans the reboot into the new image and is confirmed or reverted under its own rules ([UPDATES](UPDATES.md) section 7).

### 3.1 States

A request that finishes at once answers its result as in v1.0. A request that cannot finish within its own exchange answers `status:"ok"` with `result.job` and `result.state:"accepted"`. That covers waiting for a confirmation, needing a reboot, or taking longer than the 5 s a transport waits. From then on it is a job, and it moves through these states:

| State | Means | Terminal |
|---|---|---|
| `accepted` | checked and authorized; nothing applied yet | no |
| `progress` | running; `progress` carries a percentage or a step name | no |
| `applied` | the change is in effect | yes, unless the operation needs confirmation |
| `awaiting-confirm` | applied, and reverted unless confirmed in time (3.4) | no |
| `confirmed` | confirmed by an authorized `job.confirm` | yes |
| `reverted` | not confirmed in time, or cancelled after it was applied; the previous state is back | yes |
| `failed` | it ran and failed; `error` says why, and `reverted` says whether the previous state was restored | yes |
| `cancelled` | stopped by `job.cancel` before anything was applied | yes |

`refused`, `invalid`, `unknown-op`, `unsupported-version` and `busy` stay synchronous statuses: a refused request never becomes a job.

Each change of state is an event on the `job` stream (`ev/job` on MQTT), `{"job", "op", "state", "progress", "error", "result"}`. It goes to every connected transport whose caller may read it, and `job.get` and `job.list` read it on request. A job's identity is the request `id`, or for a signed request its `(issuer, job_id)`, so the same job has the same identity on every transport.

### 3.2 Results owed across a disconnect or a reboot

- The device keeps its open jobs and its undelivered terminal results in an **owed-results table** in flash. The table holds 8 entries of up to 384 bytes each, the bound of the v1.0 id cache, written atomically. An entry is written when a job is accepted, if it can span a reboot, and at every terminal state.
- A terminal result stays owed until a caller who may read it acknowledges it with `job.ack`, or until the table needs the slot. The oldest acknowledged entry goes first, then the oldest terminal one. An evicted job's id stays in the 64-entry hash ring, so a later duplicate of it gets `failed` with `result-expired`, never a second run.
- The status snapshot carries `jobs.open` and `jobs.owed`, so a Backend that reconnects over any path learns there is something to collect. **Running work and a result waiting for delivery are different states** (`progress` against a terminal state with `owed:true`), and both are visible.
- A result goes out over whichever authenticated path is available when it is ready, not the path the request came in on. If the request's path is gone (the broker it arrived through, a Reader that unplugged), the result waits in the table.

### 3.3 Never twice

- A signed request is deduplicated by its persisted per-issuer watermark. That holds across reboots, transports and broker changes, so a state-changing signed request runs at most once.
- An unsigned request is deduplicated by the v1.0 id cache, which a reboot clears. A job that can span a reboot also has its id in the owed-results table, so a duplicate after the reboot gets the job's current state instead of a second run.
- One state-changing job runs at a time. A second state-changing request while one is open answers `busy` with `error` `job-open:<job>`, and nothing runs. Reads, `job.get`, `job.list`, `job.ack`, `job.confirm` and `job.cancel` are always served.
- **A failed or pending job never blocks recovery.** A reset scope (3.5) or `job.cancel` from a caller allowed to run it first reverts an `awaiting-confirm` job, then runs. A job that never ends is impossible by construction: every non-terminal state has a deadline (3.4) or belongs to an operation that ends by itself.

### 3.4 Commit or revert

A change that can cut the path its caller uses is applied **tentatively**, and reverted unless confirmed. These are:
- the Wi-Fi profile in use or its credentials;
- the broker profile in use, or its host, port, TLS, anchor or credentials;
- `connectivity.mode`;
- the radio (`radio`, `tx`, and any key that changes frequency, bandwidth, spreading factor or coding rate);
- over LoRa, the admin password and the management channel.

Rules:
1. **When.** A remote caller (MQTT, LoRa, a Reader bridge) must ask for confirmation: `config.apply` and the other operations above take `"confirm":{"within_s":N}`, N from 30 to 900, default 300. Without it, a remote caller is refused `confirm-required` for those keys. A local cable caller may omit it, because it does not lose its path.
2. **Snapshot.** Before applying, the device writes the previous values of exactly the keys being changed into the job's entry in flash. That includes write-only values; the entry is device-internal like the prefs and is never reported.
3. **Apply.** The job is now `awaiting-confirm`. If the change needs a reboot, the device reboots once.
4. **Confirm.** `job.confirm {"job"}` from a caller allowed the original operation's class, arriving after the change took effect, makes the job `confirmed`. Reachability alone never confirms: a device that simply reconnects is not confirmed until someone with authority says so. Typically the confirmation arrives over the new path, which proves the path works.
5. **Revert.** The device restores the snapshot (rebooting again if needed) and reports `reverted` when either of these happens first:
   - `within_s` passes in the boot where the change took effect;
   - the device boots a second time while the job is still pending.

   An operation that needed a reboot gets its one reboot, never two.
6. **Report.** The terminal result is owed (3.2) and goes out over whichever path works afterwards: the new one after a confirmation, the old one after a revert. When no path works, the cable can always read `job.list`, and a reset scope always recovers the device.

The bound on any pending change is therefore `within_s` plus at most one reboot. A narrow change keeps every other setting and the node identity.

**How the reference firmware does it.**
- **Which keys.** `config.schema` marks them `"confirm": true`:
  - `radio` and `tx` on every role;
  - on the Companion, also `connectivity.mode`; on every build with those links, `wifi.enabled`, `mqtt.enabled` and every `wifi.profile.*` and `mqtt.profile.*` key.

  This is stricter than rule 1 on one point. No controller reports which profile it is using, so a change to *any* Wi-Fi or broker profile counts, not only the one in use.
- **Who must ask.** Only the unsigned cable may apply these keys without `confirm`; it is the principal that counts, not the wire. A signed request on USB, a paired phone, a verified broker and the network console must ask.
  - Without it, they are refused `invalid` / `confirm-required`, with nothing written.
  - A `confirm` on a request with no such key is refused `confirm-not-needed`.
  - A second tentative change while one is open is `busy` / `job-open:<job>`.
- **Snapshot.** Before any write, the previous value of every key in the request goes into `/mecon_jobsnap`, bound to the job by id and checked with a CRC.
  - A profile key captures its whole profile. A profile that was empty is restored by clearing it.
  - Write-only values are read natively by the role into the snapshot and nowhere else.
  - The snapshot holds records, not CLI lines; a revert rebuilds each line from the key table.
  - A previous value with a byte below 0x20 or 0x7F, or a snapshot that cannot be saved, refuses the request (`busy` / `storage`) with nothing written.
  - The file goes when its job ends, and at boot when no awaiting job owns it. The flash file systems are copy-on-write, so the old blocks may keep the bytes until reused, as they do for the prefs.
- **Apply.** If a write fails, what did apply is reverted at once, and the job ends `failed` with `reverted`. Otherwise the result carries `job`, `state:"awaiting-confirm"` and `confirm_left_s`. A key that applies only after a reboot (`radio`, `wifi.enabled`) gets that reboot as its first.
  - The device does not reboot by itself for these keys: the manager asks for the reboot.
  - Until that boot, `within_s` bounds the wait, so a change nobody activates is reverted. At that boot the window starts again in full.
  - `confirm_left_s` counts running seconds, and is saved every 30 seconds and at every boot, so the job and its time survive the reboot.
- **Revert.** The snapshot's lines run as the caller who applied the change, under the posture in force at the time of the revert. A revert can do nothing that caller could not do, and the audit names that caller.
  - On the Companion, a revert at boot runs before the radio and the links start, so it takes effect at once.
  - A revert while running, of a key that needs a reboot, reboots after 1.5 s.
  - The servers apply the radio only at boot, so a restored `radio` reboots them once more.
  - A line that fails leaves the job `failed` with `revert-failed`, never pending.
  - **A job cut while it ran.** A job found `accepted` or `progress` at a boot was interrupted between its snapshot and `awaiting-confirm`, so part of the change may stand with nobody to confirm it. It is reverted at that boot, or ends `failed` with `revert-failed`, and is never left open to block the next job.
- **A safety net, not a boundary.** A caller who holds `write` can still change these settings with raw CLI lines over BLE or MQTT, which have no confirm step. Commit-or-revert keeps a manager from cutting its own path; it does not protect the device from a caller who already has write.

### 3.5 Reset scopes

`device.reset {"scope"}` removes exactly one named scope and keeps everything else. [AUTHORITY](AUTHORITY.md) section 11 owns the Deployment-related scopes and their rules; this page lists the scopes and what each keeps.

| Scope | Removes | Keeps | Class |
|---|---|---|---|
| `network` | every Wi-Fi and broker profile | identity, contacts, channels, radio, posture, Deployment record | destructive |
| `contacts` | the contact table and its blobs | everything else | destructive |
| `channels` | every channel except the public one | everything else | destructive |
| `radio` | radio settings, back to the build's defaults | everything else | destructive |
| `bonds` | BLE bonds (executed at the next BLE start if BLE is not running) | everything else | destructive; physical USB only (SECURITY_AND_AUTHORITY) |
| `deployment` | Deployment record and watermarks | per AUTHORITY section 11 | per AUTHORITY section 11 |
| `identity` | the node key, contacts and the message queue (a new key generated on the device) | per AUTHORITY section 11 | per AUTHORITY section 11 |
| `factory` | everything (the native factory reset) | nothing | destructive |

A reset scope that removes the caller's own path (`network` over MQTT) is not a commit-or-revert change. Its purpose is to remove the path, and the class decides who may ask for it.

## 4. Routing a request

```
transport adapter ──► envelope (v1.0; signed wrapper, AUTHORITY) ──► caller context ──► operation table
     MQTT req / 0x7F frame / console line / LoRa              │                    │
                                                                      ▼                    ▼
                                                    CLI privilege table cell ◄── class (+ per-line check)
                                                                      │
                                       refused: audited, nothing runs │ allowed
                                                                      ▼
                         dedupe (signed watermark / id cache / owed table) ──► run: native function or CLI line
                                                                      │
                                     result now ◄─────────────────────┴────► job: ev/job + owed table
```

1. The adapter frames bytes and builds the caller context (transport, how it authenticated, posture), as today.
2. A signed wrapper is verified first. A request that fails verification is refused before anything else and consumes no identifier.
3. The operation table gives the class. The privilege table decides with the caller's context; an operation that maps to a CLI line also checks that line.
4. Deduplication, then execution through the native function or CLI line. The result is either immediate, or a job (section 3).

## 5. The matrix

Legend: **C** Companion, **R** Repeater, **S** Room server. Transports: **USB** cable (a Reader bridge or a host), **BLE** paired link (a Reader bridge or the app), **TCP** the Companion's Wi-Fi or Ethernet companion port, **MQTT** a broker session, **Con** the Repeater/Room console (USB), **Eth** the Repeater/Room Ethernet console, **RF** LoRa remote admin. Classes are those of the CLI table.

### 5.1 Device-interface operations

| Operation | Does | Native mapping | Class | Mutating | Roles | Transports |
|---|---|---|---|---|---|---|
| `hello` | versions, ids, role, capabilities | | read | no | C R S | C: USB BLE MQTT; R S: Con, MQTT where built |
| `status.get` | status snapshot | | read | no | C R S | as above |
| `config.schema` | settings this build has | CLI registry | read | no | C R S | as above |
| `config.get` | values | CLI `get` per key | read; per key | no | C R S | as above |
| `config.apply` | writes, all-or-none checked | CLI `set` per key | write; per key | yes | C R S | as above |
| `sub.start`, `sub.stop` | telemetry streams | | read | no | C R S | as above |
| `job.get`, `job.list` | job state, owed results | | read | no | C R S | all |
| `job.ack` | release an owed result | | read; not a plain read once enrolled | yes | C R S | all |
| `job.confirm` | confirm a tentative change | | the job's classes | yes | C R S | all |
| `job.cancel` | cancel, or revert if applied | | the job's classes | yes | C R S | all |
| `device.reboot` | reboot | CLI `reboot`, frame `CMD_REBOOT` | reboot | yes | C R S | all |
| `device.reset` | one reset scope (3.5) | per scope | per scope | yes | C R S | per scope |
| `contact.list`, `contact.get` | contact table (name, type, protection flag, last advert, hops, position; `list` paged by up to 6, fewer when a page would not fit); the shared secret and the path bytes never | native contact table | private | no | C | all |
| `contact.put`, `contact.remove` | add, update, remove; a put names no flags, so it never grants telemetry or the remote CLI, and a full table behaves as for the app | `CMD_ADD_UPDATE_CONTACT`, `CMD_REMOVE_CONTACT` | write | yes | C | all |
| `contact.protect` | protect from auto-removal (favourite flag) | contact flags | write | yes | C | all |
| `message.send` | one native direct or channel message; a recipient or slot not there is an answer, and no contact is created | `sendMessage`, `sendGroupMessage`, as `CMD_SEND_TXT_MSG` and `CMD_SEND_CHANNEL_TXT_MSG` | write | yes | C | all |
| `message.get`, `message.list`, `message.ack` | what became of a send (section 6) | RAM table | read, then private | no | C | all |
| `inbox.list`, `inbox.ack` | what was received (section 6.4) | a RAM copy, beside the native offline queue | read, then private | no | C | all |
| `message.deliver` | a message the device missed, to the local stock app; never on the radio (section 6.5) | the native receive queue | write | yes | C | all |
| `channel.list`, `channel.get` | channels (name, one-byte hash, key size; `list` paged by 8); the secret never | native channel table; R S: one slot (MANAGEMENT_CHANNEL.md section 6) | private | no | C R S | C: USB BLE MQTT; R S: Con, MQTT where built |
| `channel.put`, `channel.remove` | add, rotate, remove a channel; 128-bit keys only, a key held by another slot refused | `CMD_SET_CHANNEL`; R S: one slot | write | yes | C R S | C: USB BLE MQTT; R S: Con, MQTT where built |
| `mgmt.channel.get` | the management channel binding: none, bound or stale | the native channel table plus its own record | read | no | C R S | C: USB BLE MQTT; R S: Con, MQTT where built |
| `mgmt.channel.bind`, `mgmt.channel.rotate`, `mgmt.channel.unbind` | bind a slot, rotate its key, end the binding; confirm-or-revert for anyone but the cable | `CMD_SET_CHANNEL` for a rotation | write | yes | C R S | C: USB BLE MQTT; R S: Con, MQTT where built |
| `acl.list`, `acl.put` | remote-admin ACL | CLI `setperm`; REQ `GET_ACCESS_LIST` | read / write | `acl.put` | R S | Con, MQTT where built, RF |
| `password.set` | admin or guest password (write-only) | CLI `password`, `set guest.password` | write; commit-or-revert over RF | yes | R S | Con, RF |
| `clock.get`, `clock.set` | device clock (forward only) | `CMD_SET_DEVICE_TIME`, CLI `time` | read / write | `set` | C R S | all |
| `advert.send` | self advert (zero-hop or flood) | `CMD_SEND_SELF_ADVERT`, CLI `advert` | write | yes | C R S | all |
| `identity.get` | node key, device id | | read | no | C R S | all |
| `identity.rotate` | new node key on the device | | per [AUTHORITY](AUTHORITY.md) | yes | C R S | per [AUTHORITY](AUTHORITY.md) |
| `identity.rotation` | the old key's signed statement of the rotation | | read | no | C R S | all |
| `identity.key.export` | private key under its custody rule | `CMD_EXPORT_PRIVATE_KEY`, CLI `get prv.key` | secret | no | C R S | per [AUTHORITY](AUTHORITY.md) |
| `ble.bonds` | bond count | | read | no | C (BLE builds) | all |
| `ble.pin.get` | the BLE PIN | device info, CLI | secret | no | C (BLE builds) | USB, MQTT (SECURITY_AND_AUTHORITY) |
| `posture.set` | `{"posture":"normal"}` or `{"posture":"hardened"}`; the device reboots about 1.5 s after a change. `status.get` reports the posture | CLI `set posture` | posture | yes | C R S | standalone: raising a cable or paired BLE, lowering a cable. Enrolled: a signature, with a job id |
| `deployment.*` | provisioning, authority updates, record, local unlock | | per [AUTHORITY](AUTHORITY.md) | | C R S | per [AUTHORITY](AUTHORITY.md) |
| `update.*` | firmware update lifecycle ([UPDATES.md](UPDATES.md)) | | per [UPDATES](UPDATES.md) | | C R S | per [UPDATES](UPDATES.md) |
| `report.now` | `{"fields":[...],"to":"<node key>"}` or `{"fields":[...],"slot":n}`: one [compact report](TELEMETRY.md) over LoRa. `seq` in the result | | write; `location` needs private | no | C R S | as the table |
| `standing.put` | `{"id","fields","every_s","runs","durable"}` with `to` or `slot`: send that report every `every_s` (900 s to 30 days) while the manager is away, with a jitter of up to a tenth of the period, at most 5 minutes. `runs`: stop after that many tries, sent or not; the count starts again after a boot. `durable: false`: it ends at the next boot. At most 4, and the same id replaces | | write | yes | C R S | as the table |
| `standing.list` | each instruction, with `runs`, `failed` and `skipped` since boot | | read | no | C R S | as the table |
| `standing.remove` | `{"id"}` | | write | yes | C R S | as the table |
| `rf.request` | `{"to":"<node key>","message":"<a signed request, as text>","slot":n,"xid":n}`: `xid` re-sends a message under the same number, so the target keeps the frames it has; this Companion carries it to another node over LoRa, on that channel slot or, with no slot, point to point, and the reply comes back on the `rf` stream. It adds no authority: the target verifies the message | | write | no | C | as the caller, per [LORA_MANAGEMENT](LORA_MANAGEMENT.md) |

**Keys `config.*` reaches.**
- Companion: 17 key patterns, 84 concrete keys: `name`, `tx`, `radio`, `connectivity.mode`, `wifi.enabled`, `wifi.profile.{0-9}.ssid|pwd|priority`, `mqtt.enabled` and `mqtt.profile.{0-5}.type|host|port|tls|site|user|pwd|priority`. The 22 password and user keys are write-only.
- Repeater and Room server: 12 keys: `name`, `tx`, `radio`, `lat`, `lon`, `repeat`, `advert.interval`, `flood.advert.interval`, `flood.max`, `loop.detect`, `allow.read.only` and `owner.info`. The Heltec builds also have the Wi-Fi and broker keys above; `connectivity.mode` stays on the Companion, because a server has no BLE to choose between.

Each of the following stands for its existing CLI line:
- **Companion:** `mqtt.profile.N.anchor` (with the CA lines as one operation), `tz.offset`, and the remaining radio preferences (`af`, `rxdelay`, `txdelay`, `direct.txdelay`, `int.thresh`, `cad`, `radio.rxgain`, `agc.reset.interval`, `path.hash.mode`, `multi.acks`, `dutycycle`).
- **Repeater and Room server:** the same radio preferences, plus `flood.max.advert`, `flood.max.unscoped` and `guest.password` (write-only).
- **Stays a separate posture-class operation:** `mqtt.profile.N.manage`, which is set locally, never remotely.

### 5.2 Native Companion frames

60 frame codes (59 native plus `CMD_MECON`). Every frame is gated as below by the privilege table with the caller's context (`MyMesh::frameGate`). "Untrusted" is the TCP port and BLE paired with a default PIN, which serve messaging only in Normal posture ([Standalone postures](../STANDALONE_POSTURES.md)). Hardened starts neither.

| Code | Frame | Class | Untrusted | Operation |
|---|---|---|---|---|
| 1 | `CMD_APP_START` | messaging | served | `hello` |
| 2 | `CMD_SEND_TXT_MSG` | messaging; **write** with a CLI text type or to a repeater | served / refused | native; `rf.request` for the CLI forms |
| 3 | `CMD_SEND_CHANNEL_TXT_MSG` | messaging | served | native |
| 4 | `CMD_GET_CONTACTS` | read | served | `contact.list` |
| 5 | `CMD_GET_DEVICE_TIME` | read | served | `clock.get` |
| 6 | `CMD_SET_DEVICE_TIME` | messaging (forward only; the app's clock sync) | served | `clock.set` |
| 7 | `CMD_SEND_SELF_ADVERT` | messaging | served | `advert.send` |
| 8 | `CMD_SET_ADVERT_NAME` | write | refused | `config.apply name` |
| 9 | `CMD_ADD_UPDATE_CONTACT` | messaging (contact handling) | served | `contact.put` |
| 10 | `CMD_SYNC_NEXT_MESSAGE` | messaging | served | native |
| 11 | `CMD_SET_RADIO_PARAMS` | write | refused | `config.apply radio` |
| 12 | `CMD_SET_RADIO_TX_POWER` | write | refused | `config.apply tx` |
| 13 | `CMD_RESET_PATH` | messaging (contact handling) | served | native |
| 14 | `CMD_SET_ADVERT_LATLON` | write | refused | `config.apply` (lat/lon keys) |
| 15 | `CMD_REMOVE_CONTACT` | messaging (contact handling) | served | `contact.remove` |
| 16 | `CMD_SHARE_CONTACT` | messaging | served | native |
| 17 | `CMD_EXPORT_CONTACT` | read | served | `contact.get` |
| 18 | `CMD_IMPORT_CONTACT` | messaging (contact handling) | served | `contact.put` |
| 19 | `CMD_REBOOT` | reboot | refused | `device.reboot` |
| 20 | `CMD_GET_BATT_AND_STORAGE` | read | served | `status.get` |
| 21 | `CMD_SET_TUNING_PARAMS` | write | refused | `config.apply` (its keys) |
| 22 | `CMD_DEVICE_QUERY` | read | served; the BLE PIN reads 0 | `hello` |
| 23 | `CMD_EXPORT_PRIVATE_KEY` | secret | refused | `identity.key.export` |
| 24 | `CMD_IMPORT_PRIVATE_KEY` | secret | refused | none (refused once provisioned) |
| 25 | `CMD_SEND_RAW_DATA` | write | refused | none |
| 26 | `CMD_SEND_LOGIN` | messaging (the client supplies the password) | served | `rf.request` |
| 27 | `CMD_SEND_STATUS_REQ` | messaging (a guest-level request) | served | `rf.request` |
| 28 | `CMD_HAS_CONNECTION` | read | served | native |
| 29 | `CMD_LOGOUT` | messaging | served | native |
| 30 | `CMD_GET_CONTACT_BY_KEY` | read | served | `contact.get` |
| 31 | `CMD_GET_CHANNEL` | read | served; the secret reads as zeros | `channel.get` |
| 32 | `CMD_SET_CHANNEL` | write | refused | `channel.put` |
| 33-35 | `CMD_SIGN_START`, `_DATA`, `_FINISH` | write (signs with the node key) | refused | none |
| 36 | `CMD_SEND_TRACE_PATH` | messaging | served | native |
| 37 | `CMD_SET_DEVICE_PIN` | write | refused | `config.apply` (PIN key) |
| 38 | `CMD_SET_OTHER_PARAMS` | write | refused | `config.apply` (its keys) |
| 39 | `CMD_SEND_TELEMETRY_REQ` | messaging | served | native; `rf.request` |
| 40 | `CMD_GET_CUSTOM_VARS` | read | served | `config.get` (its keys) |
| 41 | `CMD_SET_CUSTOM_VAR` | write | refused | `config.apply` (its keys) |
| 42 | `CMD_GET_ADVERT_PATH` | read | served | native |
| 43 | `CMD_GET_TUNING_PARAMS` | read | served | `config.get` |
| 50 | `CMD_SEND_BINARY_REQ` | write (requests another node answers by its ACL) | refused | `rf.request` |
| 51 | `CMD_FACTORY_RESET` | destructive | refused | `device.reset factory` |
| 52 | `CMD_SEND_PATH_DISCOVERY_REQ` | messaging | served | native |
| 54 | `CMD_SET_FLOOD_SCOPE_KEY` | messaging (the send scope, not persisted) | served | native |
| 55 | `CMD_SEND_CONTROL_DATA` | write | refused | none |
| 56 | `CMD_GET_STATS` | read | served | `status.get`, telemetry |
| 57 | `CMD_SEND_ANON_REQ` | messaging (anonymous) | served | `rf.request` |
| 58 | `CMD_SET_AUTOADD_CONFIG` | write | refused | `config.apply` (its keys) |
| 59 | `CMD_GET_AUTOADD_CONFIG` | read | served | `config.get` |
| 60 | `CMD_GET_ALLOWED_REPEAT_FREQ` | read | served | `config.schema` |
| 61 | `CMD_SET_PATH_HASH_MODE` | write | refused | `config.apply path.hash.mode` |
| 62 | `CMD_SEND_CHANNEL_DATA` | messaging | served | native |
| 63 | `CMD_SET_DEFAULT_FLOOD_SCOPE` | write | refused | `config.apply` (its keys) |
| 64 | `CMD_GET_DEFAULT_FLOOD_SCOPE` | read | served | `config.get` |
| 65 | `CMD_SEND_RAW_PACKET` | write | refused | none |
| 66 | `CMD_RUN_CLI_COMMAND` | read to reach the CLI; then the line's class | refused | none (the interface has no CLI-line operation) |
| 0x7F | `CMD_MECON` | per operation | refused (every operation) | the device interface |

"None" means the frame stays native and gets no interface operation. Raw transmission and signing arbitrary data with the node key are not management actions a manager should be able to ask for.

### 5.3 CLI lines

Every CLI line is classed by the registry; a line the registry does not name is `write`. The engine decides it for every transport the role has. Counts are of distinct line forms in the code.

| Role | Lines (core) | With options | Transports |
|---|---|---|---|
| Companion | 49 | 85 with connectivity mode, Wi-Fi and MQTT, plus up to 8 board-specific and 5 rescue-console-only | USB, BLE (paired, own PIN), MQTT `cli/req` (verified broker), RF (a contact flagged for remote CLI: reads and `reboot`, Normal only) |
| Repeater | 126 | plus GPS 8, bridge 12, LR2021 1, Ethernet console 3, board-specific up to 8 | Con, Eth (after `login`, Normal only), RF (remote admin) |
| Room server | 127 | as the Repeater | as the Repeater |

| Family | Lines | Class | Remote admin (RF, Eth), Normal | Remote admin, Hardened | Operation |
|---|---|---|---|---|---|
| posture | `get posture`; `get audit`; `set posture` | read; posture | read only | read only | `posture.*` |
| settings | `get`/`set` of the role's keys | read / write | yes | reads only | `config.*` |
| secrets | `get prv.key`, `set prv.key` | secret | refused (local only) | refused | `identity.key.export` |
| reboot | `reboot` (exact) | reboot | yes | yes | `device.reboot` |
| power | `poweroff`, `shutdown` | destructive | refused (local only) | refused | none (needs someone on site) |
| erase | `erase` (console only) | destructive | refused | refused | `device.reset factory` |
| ACL and passwords | `setperm`, `password`, `set guest.password` | write | yes | refused | `acl.put`, `password.set` |
| radio test | `tempradio` | write | yes | refused | none |
| actions | `advert`, `advert.zerohop`, `clock sync`, `time`, `clkreboot`, `neighbor.remove`, `discover.neighbors`, `clear stats`, `log start/stop/erase` | write (unregistered) | yes | refused | `advert.send`, `clock.set`; the rest stay CLI |
| region | `region` (list), `region get`, and the region writes | read; write | yes | reads only | `config.*` (region keys) |
| reads | `ver`, `board`, `clock`, `neighbors`, `power`, `get mecon.*`, `sensor get/list` | read | yes | yes | `hello`, `status.get`, telemetry |
| OTA | `start ota` | write | yes | refused | `update.*` |

Notes the matrix depends on, as the code stands:
- `start ota` on an ESP32 Repeater or Room server opens an unauthenticated Wi-Fi access point that accepts an image. This is stock behaviour, and it is unsigned. It is compiled out unless a build defines `ENABLE_WIFI_OTA`, and every build with the device interface refuses it ([CLI reference](../CLI_REFERENCE.md)).
- The Repeater runs any line that starts with `reboot`, while the registry classes only the exact `reboot` as reboot. A longer form is `write`, which is stricter (refused to a Hardened remote admin), not weaker.
- The sensor role (`simple_sensor`) is not a MECON target. It has no device interface and no posture front door, and nothing on this page applies to it. This is a recorded exception to the rule that the firmware works the same way in all modes: the owner's decision of 2026-11-04.

### Standing instructions

A standing instruction runs only what it names: a compact report to a named destination. There is no other action, script or rule.

- **Time.** It runs on the device's uptime, so a clock change does nothing to it. A tick missed while the device was off is gone: after a boot the first run comes within the jitter, and there is no catch-up burst. One runs on each pass, so two due together do not go out at once.
- **Who.** It carries the member who set it. When that member is no longer in the authority set, or the Deployment is gone, it is removed the next time it comes due, unsent. One set by an unsigned caller on a standalone device has no member and stays until removed.
- **Durable by default.** It lasts across boots in a file. A subscription stays short-lived and never becomes one of these.
- **Failure.** A report that cannot go counts in `failed`, and the schedule carries on.

### 5.4 LoRa remote administration (native)

| Facility | Repeater | Room server | Companion | Who |
|---|---|---|---|---|
| login (anonymous request with a password) | yes | yes (with `sync_since`) | no (not a server) | anyone; admin only when the admin password is not the default |
| anonymous info: regions, owner, basic clock | yes (direct-routed only, rate limited) | no | no | anyone |
| `REQ_TYPE_GET_STATUS` 0x01 | yes | yes | no | any ACL client |
| `REQ_TYPE_KEEP_ALIVE` 0x02 | defined, not handled | yes | no | ACL client |
| `REQ_TYPE_GET_TELEMETRY_DATA` 0x03 | yes (guests get the base set) | yes | yes (by its telemetry modes) | ACL client / contact |
| `REQ_TYPE_GET_ACCESS_LIST` 0x05 | yes | yes (admins only) | no | admin |
| `REQ_TYPE_GET_NEIGHBOURS` 0x06 | yes (version 0) | no | no | ACL client |
| `REQ_TYPE_GET_OWNER_INFO` 0x07 | yes | no | no | ACL client |
| CLI text | any text from an admin (plain, CLI data, CLI command) | CLI data or CLI command from an admin; plain text is a post | CLI command from a flagged contact (reads and `reboot`) | admin / flagged contact |

The device-interface operations go over LoRa point to point ([LORA_MANAGEMENT](LORA_MANAGEMENT.md)): `hello`, `status.get`, `config.schema`, `config.get`, `config.apply`, `deployment.get`, `job.get`, `job.list`, `job.ack`, `job.cancel`, `job.confirm`, `posture.set`, `report.now`, `standing.put`, `standing.list` and `standing.remove`. Only a signed request runs. Any other operation answers `unknown-op` there. On the management channel only `config.apply`, `job.get`, `job.ack`, `job.cancel`, `job.confirm`, `posture.set` and `report.now` go: every member can read a reply there.

These native facilities sit beside the device interface over LoRa, which is a separate path ([management over LoRa](LORA_MANAGEMENT.md)). A channel key, an RF route or a proxy is never authority on that path: the request carries its own signature.

### 5.5 Boards and roles

| Build | Interface | MQTT | BLE | Notes |
|---|---|---|---|---|
| Heltec V3 Companion standalone | yes | yes | yes | connectivity mode, USB, the section 5.1 operations for the Companion |
| Heltec V4 Companion standalone | yes | yes | yes | as V3 |
| Heltec V3 Repeater, Room server | yes (console) | yes | no (owner decision) | |
| Heltec V4 Repeater, Room server | yes (console) | yes | no (owner decision) | |
| Heltec V3/V4 Companion `_usb`, `_ble`, `_wifi` | no | `_wifi` only (CLI topic) | `_ble` | stock-shaped builds; native frames only |
| SenseCAP T1000-E Companion (`_usb`, `_ble`), Repeater, Room server | yes (Companion: frames; Repeater, Room server: console) | no | Companion `_ble` | no Wi-Fi. The Companion keeps the stock contact array. |
| Wio Tracker L1 Companion (`_usb`, `_ble`), Repeater, Room server | yes (Companion: frames; Repeater, Room server: console) | no | Companion `_ble` | no Wi-Fi |
| Other nRF52 builds | no | no | Companion builds | no Wi-Fi; software-verified only |
| simulated Companion, Repeater, Room server | yes | Companion | simulated | the conformance target |

### 5.6 Not supported, and why

| Case | Why |
|---|---|
| Any device-interface operation over the TCP port or default-PIN BLE | an untrusted caller; messaging only (5.2) |
| Remote `poweroff` | a powered-off device needs someone on site |
| Raw transmission and data signing as operations | not management (5.2) |
| BLE bond clear over any path but physical USB | SECURITY_AND_AUTHORITY: bonds are cleared over USB only |
| Backfill and outage forwarding over the interface | not generic firmware: a manager application's concern. Native messaging stays native |

## 6. Messages

`message.send` asks a Companion to send one native message. A direct message goes to a stored contact, named by its whole key; a channel message goes to a slot. Both are the native send, so a stock receiver reads them as it reads any other. A stock app on the sender is not told of a message it did not send.

Nothing here selects a gateway, retries by itself or decides which messages to catch up on. Those belong to a Backend.

### 6.1 Outcomes

| State | Means | Final |
|---|---|---|
| `queued` | handed to the radio queue | no |
| `duty-held` | in the queue, waiting for airtime; `wait_ms` is the wait the dispatcher last gave | no |
| `transmitted` | on the air | channel message: yes. Direct: no, it waits for the ACK |
| `acked` | the recipient's native ACK arrived | yes |
| `timed-out` | no ACK within `est_ms` of the transmission; a late ACK still moves it to `acked` | yes |
| `failed` | the radio dropped it or could not start it | yes |
| `unknown` | not in the table, or it left the queue with no report for 2 hours | yes |

- **An ACK proves the recipient's radio node had it**, not that an application read it. **A timeout does not prove non-receipt.**
- A channel message has no ACK, so `transmitted` is all the device can know.
- `hash` is the native packet hash, the same id a receiver or an observer computes. `ack` is the native ACK code.
- `message` is the identity of the request (section 3): the request id, or `#<issuer>:<job_id>` for a signed one.
- Each change of state is a `message` event (`ev/message` on MQTT), `{"message","hash","state"}`. It goes where a `job` event goes. Reading a recipient takes `message.get`, which is private, like `contact.get`.
- `status.get` carries `messages.open` and `messages.owed`, so a manager that reconnects learns there is something to collect.

### 6.2 Bounds

- **The table holds 8 entries, in RAM, and no message text.** A collected entry goes first (`message.ack`), then the oldest final one. When all 8 are open, `message.send` answers `busy` with `outcomes-full` and nothing is sent.
- **A full radio queue** answers `busy` with `queue-full`, and nothing is sent.
- **Text that does not fit** is failed with `text-too-long`, rather than cut. A direct message holds 160 bytes. A channel message holds 160 bytes including the sender's name, a colon and a space.
- **Why not a job.** Jobs run one at a time and each change writes flash. A send can wait seconds for its ACK, or an hour for airtime, and is routine traffic.

### 6.3 Never twice, and a reboot

- A repeat with the same identity answers the kept result. Past the result cache it answers `failed` with `result-expired`, or the entry's current state if the table still holds it. It is never a second send.
- **A signed request** is held to its persisted watermark, so a repeat after a reboot is refused: it never goes out twice.
- **An unsigned request** is remembered only in RAM, so after a reboot a repeat is a new send. A manager that cannot accept this signs its requests, or reads the epoch first.
- **A reboot empties the table.** A send whose reply carried another epoch and that now reads `unknown` may or may not have gone out. The device cannot tell, and says so rather than guessing.

### 6.4 Received messages

`inbox.list` reads what a Companion received, and `inbox.ack` releases it. Each is a copy kept for the manager, beside the native offline queue.

- **Two readers, and neither takes from the other.** The native queue stays the stock app's: a manager's read or acknowledgement never drains it, and the app's sync never touches the copy. A device with both sees each message in both.
- **A device with no manager keeps no copy.** The slots are made at the first `inbox.list`, or as soon as the device holds a Deployment, which is from the boot on for an enrolled one. Receptions before that are counted as `lost`, and the stock app still has them.
- **One mark.** `inbox.ack` moves a single mark for the device and wipes what is below it. Any number of readers can page with `after`, since a read changes nothing, but one of them owns the acknowledgement.

| Field | What it proves |
|---|---|
| `source` | `radio`, or `manager` for one `message.deliver` supplied (section 6.5). The device sets it, so no contact can |
| `from` (direct) | the stored contact whose shared key opened the message. With `source` `manager`, only what the manager said |
| `channel` (channel) | the slot whose key opened it. Anyone with that key could have sent it, and a sender's name at the start of `text` is only a claim |
| `signer` (type `signed`) | the key prefix the native frame carries |
| `ts` | nothing: the sender's own claim of the time |
| `rx_ts` | this device's clock at reception, absent until an app, a manager or GPS has set it. Still only as good as that source, so it is not proof of the time |
| `age_s` | seconds since reception by uptime, so it needs no clock |
| `hash` | the native packet hash, the same id the sender's `message.send` returned and any observer computes |

**Bounds**

- **8 entries, in RAM, on an enrolled device or after a first read:** about 1.8 KB. There is no expiry: an entry stays until it is acknowledged, pushed out or the device reboots.
- **When all 8 are unread,** a new reception pushes out the oldest channel message, or with none the oldest direct one, as the native queue prefers. `lost` counts each, and the gap shows in `seq`.
- **A text over 160 bytes** is kept cut and carries `cut`. A stock sender cannot send one.
- **A page** holds up to `limit` entries (8 at most) and stops early where a reply would overflow: `more` says to read again with `after` set to the last `seq`.
- Binary channel data (`RESP_CODE_CHANNEL_DATA_RECV`) and anything other than a text message are not kept.

**A reboot loses the copy.** `seq` restarts at 1 under a new `epoch`, and the device cannot say what was lost, so a manager that sees another epoch treats what it had not read as gone. For the same reason `inbox.ack` carries the `epoch`, and is failed with `wrong-epoch` under another one: an old `upto` would release messages its sender never saw.

**Duplicates.** One packet heard twice is one entry, since the native mesh drops a packet it has seen. A sender's own retry is a new packet and a new entry, with the same `from`, `ts` and `text` and another `hash`, as the stock app also sees it. Telling those apart is the manager's choice.

**Privacy.** Both operations are private, like `contact.get`: a caller who may not read private data is refused, with a signature or without one. Each new reception raises an `inbox` event (`ev/inbox` on MQTT), `{"received","unread","lost"}`, with no sender and no text, so it goes where a job event goes. `status.get` carries the same counts, so a manager that reconnects learns there is something to collect. Nothing is written to flash and no text reaches a log or telemetry.

### 6.5 Supplying a missed message

`message.deliver` puts a message the device missed on its native receive queue, so a stock app reads it. Args: `from` (a stored contact's whole key) or `channel` (a slot), `text` and `timestamp` (the message's own time). The result is `{"state","epoch"}`.

- **Never on the radio.** It transmits nothing and makes no ACK. Sending is `message.send`, and a manager asks for it separately.
- **A stock app sees it marked.** It appears in the sender's own thread or the slot's, with `[via manager] ` at the start of the text, because the native frame has no field to say how a message came. Its path reads as none and its SNR as 0.
- **The marker is a warning and proves nothing.** It is ordinary text, so any contact could send the same words. It says the device did not verify this message; it never shows that one was verified. A manager tells them apart by `source` in `inbox.list`, which the device sets. Neither the sender nor the time was checked: both are what the manager supplied.
- **Once, however it comes.** A message is the same one when its sender or slot, time and text are the same. The device holds the last 16 in RAM. A delivery of one already heard or delivered reads `duplicate` and queues nothing, and a radio copy arriving after a delivery still gets its native ACK but is not queued a second time. Past those 16, or after a reboot, it reads as new.
- **A repeat of the same request** answers its kept result. Once enrolled a delivery must be signed.

**Bounds.** A text that does not fit with the marker is failed with `text-too-long`, rather than cut: a direct message holds 146 bytes and a channel message 151. A native queue full of direct messages answers `busy` with `queue-full`, where the native queue would drop a new one unreported. When it holds a channel message, the oldest of those goes, as on a reception. A contact or slot not there is failed with `no-contact` or `no-channel`, and no contact is created. A reboot empties the queue, under a new `epoch`.

Which messages to supply, and when to retry, are the manager's.
