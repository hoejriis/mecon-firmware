# Managed operations (design and matrix)

Every management action a device offers, on which board, role and transport, under which authority, and with which result. This page extends the [device interface](DEVICE_INTERFACE.md) rather than adding a second command model: an operation is a device-interface `op`, its authority is the CLI privilege table ([Command-line engine](../CLI.md)) plus Deployment authority, and its settings are the CLI's settings.

> This page specifies every operation. Which of them a device offers depends on its build, board and role: `hello` lists the operation's own name (`channel.get`, with no prefix; only streams carry one, `stream.<name>`) for each one it can run, and any other answers `unknown-op`. *(Corrected 2026-11-11: this page said `op.<name>`, but the builds list bare names (checked against v0.3.0 to v0.3.2).)*

## 1. One operation, every transport

- **One name, one meaning.** An operation is `<family>.<verb>` (`device.reboot`, `contact.put`, `job.confirm`). It has the same arguments, result, errors and authority over MQTT, over a Reader's USB or BLE bridge, on the Repeater and Room server console, and over LoRa where the operation opts in ([management over LoRa](LORA_MANAGEMENT.md)). A transport never adds a permission, and a bridge forwards envelopes verbatim (DEVICE_INTERFACE section 7).
- **Native state stays authoritative.** Each operation is implemented by the native MeshCore function or CLI line it stands for. `contact.put` writes the native contact table, `channel.put` the native channel table, and `config.apply` the CLI settings. Nothing keeps a second copy. A native Companion frame or CLI line and the operation that stands for it change the same state and are decided by the same table cell.
- **Allowlisted, not a shell.** Only named operations exist. There is no "run this CLI line" operation on the interface. The MQTT `cli/req` topic stays only until the interface covers what it is used for (DEVICE_INTERFACE section 7), and then it is withdrawn.
- **What a target can do, it says.** `hello` lists an operation in `capabilities`, by its bare name, only when this build, board and role can run it. An operation absent from the list answers `unknown-op`. Permission is never inferred from the transport: a listed operation can still be `refused` for this caller.
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
- A `job` event is sent once. On a Companion, one that a link's queue has no room for waits up to 10 s, one at a time; a newer state of the same job replaces it. After that it is dropped and counted in `status.get` `events_dropped`. A manager that has not heard a job's final state reads it with `job.get`. *(Since v0.3.6.)*
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
- One state-changing job runs at a time. A second state-changing request while one is open answers `busy` with `error` `job-open:<job>`, and nothing runs. A signed one keeps its `job_id` unused, so the same bytes can be sent again once the job has ended (AUTHORITY section 8). Reads, `job.get`, `job.list`, `job.ack`, `job.confirm` and `job.cancel` are always served.
- **A failed or pending job never blocks recovery.** A reset scope (3.5) or `job.cancel` from a caller allowed to run it first reverts an `awaiting-confirm` job, then runs. A job that never ends is impossible by construction: every non-terminal state has a deadline (3.4) or belongs to an operation that ends by itself.

### 3.4 Commit or revert

A change that can cut the path its caller uses is applied **tentatively**, and reverted unless confirmed. These are:
- the Wi-Fi profile in use or its credentials;
- the broker profile in use, or its host, port, TLS, anchor or credentials;
- `connectivity.mode`;
- the radio (`radio`, `tx`, `radio.rxgain`, and any key that changes frequency, bandwidth, spreading factor or coding rate; a wrong receive gain can leave the radio deaf);
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
  - `radio`, `tx` and `radio.rxgain` on every role;
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

**As built.**
- **Who may ask:** the destructive class of the privilege table. The unsigned cable may ask on any device, enrolled or not. Paired BLE may ask on a device with no Deployment record, in Normal. A signed request is refused whatever its signature, as is a broker session. The operation is not on the LoRa link.
- **The answer** is `{"scope","reboot":true}`. Each scope is saved before the answer, and the device reboots about 1.5 s after it, so nothing runs half reset. `factory` answers only once the erase is done: 13.8 s on a Heltec V3, so a caller waits longer than for any other request. A contact change still waiting for its lazy save is saved first, as the native reboot does.
- **The scopes built:**
  - `radio`: frequency, bandwidth, spreading factor, coding rate and transmit power, back to the build's defaults. Other radio tuning stays.
  - `network`: every stored Wi-Fi and broker profile. A network compiled into the build is not stored, so it stays. Only on builds with Wi-Fi or a broker.
  - `contacts` (Companion): the contact table and the adverts kept with it.
  - `channels` (Companion): every slot emptied, and slot 0 the public channel again.
  - `bonds` (Companion, BLE builds):
    - Every BLE bond goes. The request comes from the cable only; any other caller gets `refused` `usb-only`, as SECURITY_AND_AUTHORITY requires.
    - The clear is kept, and it runs at the next BLE start, after the stack is up and before it advertises. So it waits through a boot in Wi-Fi mode, or in Hardened with the default PIN.
    - Its marker goes only once the stack reports no bond left. `ble.bonds` shows `clear_pending` until then.
  - `factory`: the native factory reset, as `CMD_FACTORY_RESET` and the console's `erase` do it, with its audit record.
- **The scopes not on this operation:**
  - `deployment`: answered `invalid` `use-deployment.reset`.
  - `identity`: answered `invalid` `use-identity.rotate`.
  - `contacts` and `channels` on the Repeater and Room server: `failed` `not-on-this-device`.
  - An unknown or missing scope: `invalid` `bad-scope`.
- **A save that fails** answers `failed` `io-error`, and the device does not reboot.

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
| `region.list` | the region tree, paged; the first page adds the wildcard's flood, `home` and `default` | `RegionMap` | read | no | R S | Con, MQTT where built |
| `region.put`, `region.remove`, `region.flood`, `region.home`, `region.default` | change the tree; each is saved at once | `RegionMap`, `/regions2` | write; confirm for all but the unsigned cable | yes | R S | Con, MQTT where built |
| `scope.default.get` | the default flood scope's name and kind, never its key | frame 64, prefs | read | no | C | USB BLE MQTT |
| `scope.default.set`, `scope.default.clear` | set or clear it; saved in the prefs | frame 63, prefs | write; confirm for all but the unsigned cable | yes | C | USB BLE MQTT |
| `job.cancel` | cancel, or revert if applied | | the job's classes | yes | C R S | all |
| `device.reboot` | reboot, about 1.5 s after the answer; an unsigned local caller (cable, paired BLE) keeps it on an enrolled device, an unsigned remote one does not | CLI `reboot`, frame `CMD_REBOOT` | reboot | yes | C R S | C: USB BLE MQTT; R S: Con, MQTT where built; not over LoRa |
| `device.reset` | one reset scope (3.5), then a reboot | CMD_FACTORY_RESET, console `erase`, the prefs, channels and contacts files | destructive | yes | C R S | the unsigned cable; paired BLE on a device with no record; not over a broker or LoRa |
| `contact.list`, `contact.get` | contact table (name, type, protection flag, last advert, hops, position; `list` paged by up to 6, fewer when a page would not fit); the shared secret and the path bytes never | native contact table | private | no | C | all |
| `contact.put`, `contact.remove` | add, update, remove; a put names no flags, so it never grants telemetry or the remote CLI, and a full table behaves as for the app | `CMD_ADD_UPDATE_CONTACT`, `CMD_REMOVE_CONTACT` | write | yes | C | all |
| `contact.protect` | protect from auto-removal (favourite flag) | contact flags | write | yes | C | all |
| `contact.capacity` | every figure of `get contacts.capacity`: `{"store","used","capacity","cache_used","cache_slots","pinned","favourites","evicted","refused_full","refused_all_favourites","loads","damaged","write_errors","ram_bytes","unmigrated"}`, `store` `"adaptive"` or `"ram-only"` | CLI `get contacts.capacity` | read | no | C | all |
| `message.send` | one native direct or channel message, optionally in a flood scope of its own (`scope`, section 5.3); a recipient or slot not there is an answer, and no contact is created | `sendMessage`, `sendGroupMessage`, as `CMD_SEND_TXT_MSG` and `CMD_SEND_CHANNEL_TXT_MSG` | write | yes | C | all |
| `message.get`, `message.list`, `message.ack` | what became of a send (section 6) | RAM table | read, then private | no | C | all |
| `inbox.list`, `inbox.ack` | what was received (section 6.4) | a RAM copy, beside the native offline queue | read, then private | no | C | all |
| `message.deliver` | a message the device missed, to the local stock app; never on the radio (section 6.5) | the native receive queue | write | yes | C | all |
| `channel.list`, `channel.get` | channels (name, one-byte hash, key size; `list` paged by 8); the secret never | native channel table; R S: one slot (MANAGEMENT_CHANNEL.md section 6) | private | no | C R S | C: USB BLE MQTT; R S: Con, MQTT where built |
| `channel.put`, `channel.remove` | add, rotate, remove a channel; 128-bit keys only, a key held by another slot refused | `CMD_SET_CHANNEL`; R S: one slot | write | yes | C R S | C: USB BLE MQTT; R S: Con, MQTT where built |
| `mgmt.channel.get` | the management channel binding: none, bound or stale | the native channel table plus its own record | read | no | C R S | C: USB BLE MQTT; R S: Con, MQTT where built |
| `mgmt.channel.bind`, `mgmt.channel.rotate`, `mgmt.channel.unbind` | bind a slot, rotate its key, end the binding; confirm-or-revert for anyone but the cable | `CMD_SET_CHANNEL` for a rotation | write | yes | C R S | C: USB BLE MQTT; R S: Con, MQTT where built |
| `acl.list`, `acl.put` | remote-admin ACL: `list` gives each client's node key and role, 8 a page, never its shared secret; `put {"key","role"}` sets a role (`guest`, `read-only`, `read-write`, `admin`), adding the key if new | CLI `setperm`; REQ `GET_ACCESS_LIST` | private (list) / write | `acl.put` | R S | Con, MQTT where built. Over the air the native remote admin keeps `setperm` and `GET_ACCESS_LIST`; the LoRa management link does not carry these two |
| `password.set` | `{"which", "password"}`: `which` is `"admin"` or `"guest"`; sets the admin or guest password, at most 15 characters (`too-long` otherwise, never cut); write-only, the answer names which one and never the value | CLI `password`, `set guest.password` | write | yes | R S | Con only: a broker session is refused (`not-over-this-link`), and the LoRa management link carries no write-only setting. Over the air the native remote admin keeps its own `password` line |
| `clock.reset` | the clock set to the CLI's guess (2024-05-15), then a reboot about 1.5 s after the answer | CLI `clkreboot` | write | yes | R S | Con, MQTT where built; not over LoRa |
| `stats.clear` | the packet and radio counters back to zero | CLI `clear stats` | write | yes | R S | Con, MQTT where built; not over LoRa |
| `log.start`, `log.stop`, `log.erase` | the packet log on, off, or removed | CLI `log start`, `log stop`, `log erase` | write | yes | R S | Con, MQTT where built; not over LoRa |
| `log.read` | `{"offset"}`: the packet log's text from there, at most 512 bytes: `{"size","offset","text","next"}`, `next` null at the end; an empty log reads `size` 0. Of the write class, as the CLI's `log`, which runs from the console only; a broker session is refused (`not-over-this-link`) | CLI `log` (prints the file to the serial port) | write | no | R S | Con only; not over LoRa |
| `radio.temp` | `{"radio":"freq,bw,sf,cr","minutes"}`: radio settings for 1 to 1,440 minutes, then the saved ones again. It reverts itself, and never cuts its caller's path, since it does not go over LoRa, so it is not confirm-or-revert | CLI `tempradio` | write | yes | R S | Con, MQTT where built; not over LoRa |
| `neighbour.list` | `{"cursor"}`: the heard neighbours, newest first, 8 a page: `{"neighbours":[{"key","heard_s_ago","snr"}],"next"}`, the whole node key (the CLI shows 4 bytes), the SNR in dB | CLI `neighbors` | read | no | R | Con, MQTT where built; not over LoRa |
| `neighbour.remove` | `{"key"}`, one whole node key in lower-case hex (the CLI also takes a prefix, which can match several) | CLI `neighbor.remove` | write | yes | R | Con, MQTT where built; not over LoRa |
| `neighbour.discover` | a zero-hop discovery request on the air, within the radio's own duty-cycle rules | CLI `discover.neighbors` | write | yes | R | Con, MQTT where built; not over LoRa |
| `room.post` | `{"text"}`, 1 to 149 characters: a system post to the room's clients | CLI `room.post` | write | yes | S | Con, MQTT where built; not over LoRa |
| `packet.transmit` | `{"packet","within_s"}`: a manager's native packet on the air as given ("Packet transmission" below) | none (`CMD_SEND_RAW_PACKET` stays refused) | write; signed only | yes, a job | R | Con, MQTT where built; not over LoRa |
| `board.get` | the board's own readings, `{"bootloader","adc_multiplier","power_management","power_source","boot_mv","boot_reason","powersaving"}`; each `null` where the board cannot tell (`bootloader` on nRF52 only, `power_source` and `boot_mv` with nRF52 power management). `power_source` is the same board call as the power stream's `external` | CLI `get bootloader.ver`, `get adc.multiplier`, `get pwrmgt.support`, `get pwrmgt.source`, `get pwrmgt.bootmv`, `get pwrmgt.bootreason`, `powersaving` | read | no | R S | Con, MQTT where built; not over LoRa |
| `board.adc.set` | `{"multiplier":"<decimal>"}`, 0 to 10 with at most three places, as a string; `0` is the board's default. `unsupported` on a board with no adjustable battery divider | CLI `set adc.multiplier` | write | yes | R S | Con, MQTT where built; not over LoRa |
| `power.saving.set` | `{"on": bool}`; `unsupported` where the board, or a bridge build, has no power saving | CLI `powersaving on`, `powersaving off` | write | yes | R S | Con, MQTT where built; not over LoRa |
| `sensor.list` | `{"cursor"}`: the board's sensor settings from that index, as many as the CLI's page holds: `{"total","settings":[{"name","value"}],"next"}`, `next` null on the last page; a cursor past the end is `invalid` | CLI `sensor list` | read | no | R S | Con, MQTT where built; not over LoRa |
| `sensor.get` | `{"name"}`: one setting, `{"name","value"}`; `unknown-setting` when the board has none of that name | CLI `sensor get` | read | no | R S | Con, MQTT where built; not over LoRa |
| `sensor.set` | `{"name","value"}`, a value with no spaces (the CLI splits its line at them; `bad-value`); `unknown-setting` as above | CLI `sensor set` | write | yes | R S | Con, MQTT where built; not over LoRa |
| `gps.get` | `{"receiver","powered","active","fix","sats","advert","diag"}`: the `gps` line as fields, null without a receiver; `advert` the self advert's position policy (`none`, `share`, `prefs`) and `diag` the `gps diag` line, each null for a caller the CLI gate refuses that line (both are of the write class) | CLI `gps`, `gps advert`, `gps diag` | read | no | R S (builds with GPS) | Con, MQTT where built; not over LoRa |
| `gps.set` | `{"on": bool}`: the GPS setting, saved; `no-gps` where the board has no GPS toggle | CLI `gps on`, `gps off` | write | yes | R S (builds with GPS) | Con, MQTT where built; not over LoRa |
| `gps.sync` | set the clock from the receiver's next fix; `no-gps` without a receiver | CLI `gps sync` | write | yes | R S (builds with GPS) | Con, MQTT where built; not over LoRa |
| `gps.setloc` | the sensor manager's position into the saved `lat` and `lon`; the answer is `{"lat","lon"}` as saved | CLI `gps setloc` | write | yes | R S (builds with GPS) | Con, MQTT where built; not over LoRa |
| `gps.advert.set` | `{"policy"}`, `none`, `share` or `prefs`: which position the self advert carries | CLI `gps advert` | write | yes | R S (builds with GPS) | Con, MQTT where built; not over LoRa |
| `clock.get` | `{"epoch": n}`, wall-clock seconds; `null` while the clock is not set | `CMD_GET_DEVICE_TIME` | read | no | C R S | C: USB BLE MQTT; R S: Con, MQTT where built; not over LoRa |
| `clock.set` | `{"epoch": n}`: sets the device clock, forward only (`backwards` otherwise); the answer is the clock after it | `CMD_SET_DEVICE_TIME`, CLI `time` | write | yes | C R S | C: USB BLE MQTT; R S: Con, MQTT where built; not over LoRa |
| `clock.back` | `{"epoch": n}`: sets a clock that ran ahead back, to no earlier than 2024-01-01 and earlier than the clock now (`not-earlier` otherwise); the answer is `{"epoch","back_s"}`. Peers that hold the contact ignore its adverts for `back_s`. Audited as `clock-back` | CLI `clock back` | destructive | yes | C | the cable only (`cable-only` to any other caller, a member's signature among them) |
| `advert.send` | self advert: `{"flood": true}` floods it in the default scope, otherwise zero hop; the result names which | `CMD_SEND_SELF_ADVERT`, CLI `advert`, `advert.zerohop` | write | yes | C R S | C: USB BLE MQTT; R S: Con, MQTT where built; not over LoRa |
| `identity.get` | `device_id`, `node_key` and `device_id_sig`, as `hello` gives them | | read | no | C R S | C: USB BLE MQTT; R S: Con, MQTT where built; not over LoRa |
| `identity.rotate` | new node key on the device | | per [AUTHORITY](AUTHORITY.md) | yes | C R S | per [AUTHORITY](AUTHORITY.md) |
| `identity.rotation` | the old key's signed statement of the rotation | | read | no | C R S | all |
| `identity.key.export` | the node private key: `{"key"}` in clear with no Deployment record (or a corrupt one); once enrolled `{"wrapped","for","alg"}`, wrapped for the member that signed (mecon-kx-v1, below); never cached | `CMD_EXPORT_PRIVATE_KEY`, CLI `get prv.key` | secret | no | C R S | no record: the cable, and on a Companion a phone paired with its own PIN; enrolled: a member's signed request with the link's nonce, on any wire transport; corrupt record: the cable; Hardened: none; never over LoRa |
| `ble.bonds` | `{"count","clear_pending"}`: the centrals the BLE stack holds a bond for, `null` while the stack is not running; whether a `device.reset bonds` still waits for a BLE start | NimBLE, Bluefruit and Bluedroid bond stores | read | no | C (BLE builds) | all |
| `ble.pin.get` | `{"pin"}`, the BLE passkey in use; never answered from the result cache | device info, CLI | secret | no | C (BLE builds) | the cable on any device; a verified broker session on a device with no Deployment record; a member's signed request, with its nonce, once enrolled; refused to a paired phone and over LoRa |
| `ble.pin.set` | `{"pin": n}`, `0` (a PIN shown per session where the board has a screen) or six digits (`bad-pin` otherwise); saved, in use from the next BLE start, so the answer is `{"reboot_needed": true}` and never the PIN; never answered from the result cache | CLI `set pin`, `CMD_SET_DEVICE_PIN` | write | yes | C (BLE builds) | the write class: a phone paired with a published default PIN (0 or 123456) is refused, as for any write; not over LoRa |
| `audit.list` | `{"cursor","limit"}`: the audit ring's entries, newest first, `limit` a page (1 to 8, 8 when absent), as `get audit` gives them: `{"total","entries","next"}`, each entry `{"ts","event","caller","what","count"}` in the `logs` stream's names. The `logs` stream sends only what is recorded after it starts; this reads the history | CLI `get audit` | posture | no | C R S | C: USB BLE MQTT; R S: Con, MQTT where built; not over LoRa |
| `net.profile.clear` | `{"kind","profile"}`, `kind` `"wifi"` or `"mqtt"`: removes that Wi-Fi or broker profile. A caller other than the cable may not clear the profile its own session runs on (`in-use`) | CLI `set wifi.profile <n> clear`, `set mqtt.profile <n> clear` | write | yes | C R S (builds with Wi-Fi or MQTT) | C: USB BLE MQTT; R S: Con, MQTT where built; not over LoRa |
| `mqtt.profile.anchor` | `{"profile":n}` and one of `"fingerprint":"<64 hex>"`, `"ca":"<base64 DER>"` (at most 1,920 characters, checked whole as the CLI's `ca end` checks it) or `"clear":true`; the answer names the anchor held, `fingerprint`, `ca` or `unset`. The same `in-use` rule | CLI `set mqtt.profile <n> anchor`, `ca begin`, `ca add`, `ca end` | write | yes | C R S (builds with MQTT) | C: USB BLE MQTT; R S: Con, MQTT where built; not over LoRa |
| `posture.set` | `{"posture":"normal"}` or `{"posture":"hardened"}`; the device reboots about 1.5 s after a change, or 60 s over LoRa so that the answer goes out first. `status.get` reports the posture | CLI `set posture` | posture | yes | C R S | standalone: raising a cable or paired BLE, lowering a cable. Enrolled: a signature, with a job id |
| `deployment.*` | provisioning, authority updates, record, local unlock | | per [AUTHORITY](AUTHORITY.md) | | C R S | per [AUTHORITY](AUTHORITY.md) |
| `update.*` | firmware update lifecycle ([UPDATES.md](UPDATES.md)) | | per [UPDATES](UPDATES.md) | | C R S | per [UPDATES](UPDATES.md) |
| `report.now` | `{"fields":[...],"to":"<node key>"}` or `{"fields":[...],"slot":n}`: one [compact report](TELEMETRY.md) over LoRa. `seq` in the result | | write; `location` needs private | no | C R S | as the table |
| `standing.put` | `{"id","fields","every_s","runs","durable"}` with `to` or `slot`: send that report every `every_s` (900 s to 30 days) while the manager is away, with a jitter of up to a tenth of the period, at most 5 minutes. `runs`: stop after that many tries, sent or not; what is left of it is kept across a boot (`runs_left` in `standing.list`), so a board that reboots does not get its runs back. `durable: false`: it ends at the next boot. At most 4, and the same id replaces | | write | yes | C R S | as the table |
| `standing.list` | each instruction, with `runs_left` when it was given `runs` (kept across a boot), and since boot: `runs`, `failed` (the report could not be built or sent), and `skipped` (the duty window had no room for a report, after looking once a minute for 5 minutes). A skipped run waits for the next period, and does not count towards `runs` in `standing.put` | | read | no | C R S | as the table |
| `standing.remove` | `{"id"}` | | write | yes | C R S | as the table |
| `rf.request` | `{"to":"<node key>","message":"<a signed request, as text>","slot":n,"xid":n}`: `xid` re-sends a message under the same number, so the target keeps the frames it has; this Companion carries it to another node over LoRa, on that channel slot or, with no slot, point to point, and the reply comes back on the `rf` stream. It adds no authority: the target verifies the message | | write | no | C | as the caller, per [LORA_MANAGEMENT](LORA_MANAGEMENT.md) |

**The key wrap, mecon-kx-v1.** Once enrolled, the exported key travels only wrapped for the member that signed the request, so a broker, a log or a relay holding the signed result never holds a usable key.
- **Shared secret:** X25519 of the node key and that member's key from the stored set. A Backend computes it from the member's Ed25519 seed and the node's public key, as libsodium's `crypto_sign_ed25519_sk_to_curve25519` and `crypto_sign_ed25519_pk_to_curve25519` convert them.
- **Per-export key:** `K = HMAC-SHA256(shared, "mecon key-export v1" ‖ job_id as 4 bytes big-endian)`.
- **Cipher and tag:** `ct = AES-128-CTR(K[0..15], counter from zero)`, then `tag = HMAC-SHA256(K[16..31], ct)` cut to 16 bytes.
- **On the wire:** `wrapped` is base64 of `ct ‖ tag`.

`tools/mecon_kx.py` is a plain-Python reference. `test/fixtures/key_export_v1.json` is a public vector, and the firmware reproduces it exactly (sim/tests/test_key_export.py).

`contact.*`, `message.*`, `inbox.*`, `message.deliver` and `rf.request` are Companion only. This is a recorded exception to the rule that the firmware works the same way in all modes, and it follows from the roles: they act on a user's own contacts, messages and app, and only a Companion has those. A Repeater keeps neighbours, not contacts; a Room server's posts belong to its clients and are served through the room.

`neighbour.*` are the Repeater's, which keeps a neighbour table, and `room.post` the Room server's, which keeps the posts. The other role does not list them, and answers `failed` `not-on-this-device`.

**Keys `config.*` reaches.**
- Companion: 41 key patterns, 118 concrete keys on a build with Wi-Fi: `name`, `tz.offset` (whole hours, -12 to 14), `tx`, `radio`, the radio preferences `af`, `int.thresh`, `cad`, `radio.rxgain`, `rxdelay`, `txdelay`, `direct.txdelay`, `agc.reset.interval`, `path.hash.mode` and `multi.acks`, the contact and telemetry settings of frames 38 and 58 (`contacts.manual_add`, `contacts.autoadd.chat|repeater|room|sensor|overwrite` on or off, `contacts.autoadd.max_hops` 0 to 64 with 0 no limit, `telemetry.base|location|environment` `deny`, `flags` or `all`, `advert.location` `none` or `share`), `connectivity.mode`, `wifi.enabled`, `wifi.profile.{0-9}.ssid|pwd|priority`, `mqtt.enabled` and `mqtt.profile.{0-5}.type|host|port|tls|transport|path|site|user|pwd|priority`. The 22 password and user keys are write-only. A build without Wi-Fi has 25: `name`, `tz.offset`, `tx`, `radio`, the radio preferences and the contact and telemetry settings.
- Repeater and Room server: 24 keys: `name`, `tx`, `radio`, the radio preferences `af`, `int.thresh`, `cad`, `radio.rxgain`, `rxdelay`, `txdelay`, `direct.txdelay`, `agc.reset.interval`, `path.hash.mode` and `multi.acks`, `lat`, `lon`, `repeat`, `advert.interval`, `flood.advert.interval`, `flood.max`, `flood.max.unscoped`, `flood.max.advert` (each 0 to 64), `loop.detect`, `allow.read.only` and `owner.info`. The Heltec builds also have the Wi-Fi and broker keys above; `connectivity.mode` stays on the Companion, because a server has no BLE to choose between.

Not in `config.*` in this firmware: neither key table (`KEYS`, `SERVER_KEYS` in `src/helpers/iface/IfaceConfig.cpp`) has them, so each is reached by its CLI line only:
- **Companion:** `dutycycle`, which is `af` seen as a percentage, so the `af` key reaches it.
- **Repeater and Room server:** `guest.password` (write-only), which `password.set` sets.
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
| 6 | `CMD_SET_DEVICE_TIME` | messaging for the first set since boot (the app's clock sync); **write** after it (forward only) | served / refused | `clock.set` |
| 7 | `CMD_SEND_SELF_ADVERT` | messaging | served | `advert.send` |
| 8 | `CMD_SET_ADVERT_NAME` | write | refused | `config.apply name` |
| 9 | `CMD_ADD_UPDATE_CONTACT` | messaging (contact handling); **write** when it grants telemetry or the remote CLI | served / refused | `contact.put` |
| 10 | `CMD_SYNC_NEXT_MESSAGE` | messaging | served | native |
| 11 | `CMD_SET_RADIO_PARAMS` | write | refused | `config.apply radio` |
| 12 | `CMD_SET_RADIO_TX_POWER` | write | refused | `config.apply tx` |
| 13 | `CMD_RESET_PATH` | messaging (contact handling) | served | native |
| 14 | `CMD_SET_ADVERT_LATLON` | write | refused | none yet: the Companion has no `lat`/`lon` key in `config.*` (the Repeater and Room server have) |
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
| 37 | `CMD_SET_DEVICE_PIN` | write | refused | `ble.pin.set` |
| 38 | `CMD_SET_OTHER_PARAMS` | write | refused | `config.apply`: `contacts.manual_add`, `telemetry.*`, `advert.location`, `multi.acks` |
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
| 58 | `CMD_SET_AUTOADD_CONFIG` | write | refused | `config.apply`: `contacts.autoadd.*` |
| 59 | `CMD_GET_AUTOADD_CONFIG` | read | served | `config.get` |
| 60 | `CMD_GET_ALLOWED_REPEAT_FREQ` | read | served | `config.schema` |
| 61 | `CMD_SET_PATH_HASH_MODE` | write | refused | `config.apply path.hash.mode` |
| 62 | `CMD_SEND_CHANNEL_DATA` | messaging | served | native |
| 63 | `CMD_SET_DEFAULT_FLOOD_SCOPE` | write | refused | `scope.default.set`, `scope.default.clear` |
| 64 | `CMD_GET_DEFAULT_FLOOD_SCOPE` | read | served | `scope.default.get` |
| 65 | `CMD_SEND_RAW_PACKET` | write | refused | none |
| 66 | `CMD_RUN_CLI_COMMAND` | read to reach the CLI; then the line's class | refused | none (the interface has no CLI-line operation) |
| 0x7F | `CMD_MECON` | per operation | refused (every operation) | the device interface |

"None" means the frame stays native and gets no interface operation. Raw transmission and signing arbitrary data with the node key are not management actions a manager should be able to ask for, with one exception the owner approved: a Repeater transmits a packet a manager built (`packet.transmit`, "Packet transmission" below). It signs nothing with the node key, and the Companion's raw frames stay refused.

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
| radio test | `tempradio` | write | yes | refused | `radio.temp` |
| actions | `advert`, `advert.zerohop`, `clock sync`, `time`, `clkreboot`, `neighbor.remove`, `discover.neighbors`, `clear stats`, `log start/stop/erase`, `log` (console only), `room.post` (Room server) | write (unregistered) | yes | refused | `advert.send`, `clock.set`, `clock.reset`, `neighbour.remove`, `neighbour.discover`, `stats.clear`, `log.start`, `log.stop`, `log.erase`, `log.read`, `room.post`; `clock sync` stays CLI, since it takes the remote admin's own clock |
| region | `region` (list), `region get`, and the region writes | read; write | yes | reads only | `region.*` (see Region scopes) |
| reads | `ver`, `board`, `clock`, `neighbors`, `power`, `get mecon.*`, `sensor get/list` | read | yes | yes | `hello`, `status.get`, telemetry; `neighbors` is `neighbour.list` |
| OTA | `start ota` | write | yes | refused | `update.*` |

Notes the matrix depends on, as the code stands:
- `start ota` on an ESP32 Repeater or Room server opens an unauthenticated Wi-Fi access point that accepts an image. This is stock behaviour, and it is unsigned. It is compiled out unless a build defines `ENABLE_WIFI_OTA`, and every build with the device interface refuses it ([CLI reference](../CLI_REFERENCE.md)).
- The Repeater runs any line that starts with `reboot`, while the registry classes only the exact `reboot` as reboot. A longer form is `write`, which is stricter (refused to a Hardened remote admin), not weaker.
- The sensor role (`simple_sensor`) is not a MECON target. It has no device interface and no posture front door, and nothing on this page applies to it. This is a recorded exception to the rule that the firmware works the same way in all modes: the owner's decision of 2026-11-04. One part of the posture rules does apply to it, by the owner's decision of 2026-11-08: it refuses remote admin while its admin password is the stock default or empty, as the Repeater and Room server do, until a password is set on the local console.

### Standing instructions

A standing instruction runs only what it names: a compact report to a named destination. There is no other action, script or rule.

- **Time.** It runs on the device's uptime, so a clock change does nothing to it. After a put, the first run comes one period later, with the jitter. A tick missed while the device was off is gone: after a boot the first run comes within the jitter, and there is no catch-up burst. One runs on each pass, so two due together do not go out at once.
- **Who.** It carries the member who set it. When that member is no longer in the authority set, or the Deployment is gone, it is removed the next time it comes due, unsent. One set by an unsigned caller on a standalone device has no member and stays until removed.
- **Durable by default.** It lasts across boots in a file. A subscription stays short-lived and never becomes one of these.
- **Failure.** A report that cannot go counts in `failed`, and the schedule carries on.

### Region scopes

A Repeater's and a Room server's native region tree, the one the console's `region` commands edit (`RegionMap`, saved in `/regions2`). A region is a flood scope: `$name` is a private one with its own transport keys, any other name a hashtag one keyed from its name. The wildcard `*` is the root and stands for unscoped floods.

- **`region.list {"cursor"?}`** gives `{"regions":[{"name","parent","flood":"allow"|"deny","kind":"hashtag"|"private"}...],"next"}`, 8 to a page. `parent` is `"*"` for a child of the root, and `next` is `null` after the last page. The first page (cursor 0) also gives `"capacity"`, `"wildcard":{"flood"}`, `"home"` and `"default"` (a name or `null`). No key material appears in it.
- **`region.put {"name","parent"?,"flood"?}`** adds a region, or moves an existing one. `parent` defaults to `"*"`, `flood` to `"allow"`, as the console's `region put` does. The result is the entry.
- **`region.remove {"name"}`**, **`region.flood {"name","flood"}`** (`name` may be `"*"`), **`region.home {"name"}`** and **`region.default {"name"}`** (a name, or `null` for none). `region.default` makes its region allow floods, as the console's `region default` does.

**Rules the operations add to the console's.** The console keeps stock's behaviour.
- Exact names only, with an optional leading `#`, where the console matches a prefix and takes the last.
- At most 30 bytes, in the native character set. Longer is `invalid` `name-too-long`, where the console truncates. Other characters are `bad-char`.
- A parent must exist (`failed` `not-found`), and a move must not close a loop (`invalid` `cycle`).
- A region with children is not removed (`busy` `not-empty`), nor the home or the default (`busy` `referenced`). The console leaves the default pointing at nothing.
- 32 regions besides the wildcard; one more is `busy` `full`.
- Every change saves the tree at once, atomically, as the console's `region default` does, so it survives a reboot. It also saves any unsaved console edits made before it.

**Confirmation.** A change can cut a remote caller's path: the default scope carries the server's adverts and its unsolicited replies, and a flood flag decides what it forwards. So for every caller but the unsigned cable, each change needs `"confirm":{"within_s":N}` (otherwise `invalid` `confirm-required`) and runs as a job (section 3.4). A copy of the whole tree is written to `/region_snap` before the change. Confirmed, the copy goes; not confirmed in time, or across a second boot, the tree is put back from it and saved. An unsigned caller on an enrolled device changes nothing.

**A Companion's default scope.** The scope its own floods go out in, the same one native frames 63 and 64 set and read (`default_scope_name` and `default_scope_key` in the prefs).
- **`scope.default.get`** gives `{"name","kind"}`, or both `null` when there is none. The key is never read back, for either kind.
- **`scope.default.set {"name","key"?}`** names a hashtag scope, with an optional leading `#`; its key is derived as the firmware's own default derives it (SHA-256 of `"#name"`, 16 bytes). A private scope (`$name`) needs `key`, 32 hex characters, not all zero (`key-required`, `bad-key`). The same names as `region.put` take, and the same errors (`name-too-long`, `bad-char`).
- **`scope.default.clear`** sends the Companion's floods unscoped again.
- **Confirmation** as for the region tree: a scope the network does not forward would cut the Companion's reports to its manager, so every caller but the unsigned cable must confirm. The previous name and key are kept in `/scope_snap` until the job ends.

**Over LoRa** the region operations run point to point, and the region changes also run on the management channel ([LORA_MANAGEMENT](LORA_MANAGEMENT.md)). `scope.default.*` do not run over LoRa: `scope.default.set` can carry a private scope's key, and key material never goes on the air.

**A scope for one send.** `message.send` takes `"scope"`, named as `scope.default.set` names one (`"scope_key"` for a private scope), or `null` for an unscoped flood. It applies to that send only. The device's default, and an override a stock app set with frame 54, are as they were afterwards. A direct message that goes by a known path is not flooded, so its scope does not matter.

**Every stock region and scope command and frame, and where it goes.** The console column is `CommonCLI::handleRegionCmd` (`region ...`, a Repeater or Room server) and the Companion's frames in `MyMesh.cpp`.

| Stock command or frame | Operation | Note |
|---|---|---|
| `region` (no argument) | `region.list` | The whole tree, paged. |
| `region get <name>` | `region.list` | One entry of the list: `name`, `parent`, `flood`, `kind`. There is no single-entry read. |
| `region list allowed`, `region list denied` | `region.list` | Excluded as a separate operation: every entry carries `flood` (`allow` or `deny`), so a manager filters the list itself, and the wildcard's flag is `wildcard.flood`. |
| `region home` (query) | `region.list`, first page, `home` | A name or `null`. No config key reads it. |
| `region default` (query) | `region.list`, first page, `default` | A name or `null`. No config key reads it. |
| `region home <name>` | `region.home` | `null` clears it. |
| `region default <name>`, `region default <null>` | `region.default` | `null` for none. A name that does not exist is created by the console and is `not-found` here: put it first. |
| `region put <name> [parent]` | `region.put` | Exact names, and the checks listed above. |
| `region remove <name>` | `region.remove` | `busy` `not-empty` or `referenced` where the console would orphan something. |
| `region allowf <name>`, `region denyf <name>` | `region.flood` | `"flood":"allow"` and `"deny"`; `name` may be `"*"`, which the console cannot do. |
| `region save` | none | Excluded: every managed change is saved at once (see above), so there is nothing left to save. The console's `region save` also stamps the discovery modification time and the prefs; no operation does, because a managed change is not an edit the stock discovery reply needs to announce. |
| `region def <path...>`, `region load` | `region.put` (one call per region) | Excluded as batch editors over the same tree. |
| Frame 54 `CMD_SET_FLOOD_SCOPE_KEY` | `message.send {scope}` for one send | The stock app's override is the app's own and is not persisted. |
| Frame 63 `CMD_SET_DEFAULT_FLOOD_SCOPE` | `scope.default.set`, `scope.default.clear` | The same prefs. The key is never read back. |
| Frame 64 `CMD_GET_DEFAULT_FLOOD_SCOPE` | `scope.default.get` | Name and kind; the key is excluded on purpose. |
| `get`/`set flood.max.unscoped` | `config.*` | An ordinary setting (5.1). |

**Why the cable stands for each transport.** The same dispatcher runs these operations on the cable, over paired BLE, over a verified MQTT session and over LoRa, with the same privilege table, so a transport adds no semantics of its own. The Companion's `scope.default.*` are exercised over the cable, over paired BLE and over a verified MQTT session in `sim/tests/test_region_scope_replay.py`, each with a resend under the same job id and a replay. The region operations are exercised on the cable and over LoRa (`test_regions.py`, `test_lora_management`); a Repeater's or Room server's region operations over MQTT run the same dispatcher and are not exercised on that transport separately.

**A resent change changes nothing twice.** The same signed bytes get the stored answer and open no second job; an older job id is refused `replay` and changes nothing, across a reboot too.

**The scope key never leaves the device.** It is read from the request and written to the prefs; it is in no result, job record, audit entry (`audit.list` and the `logs` stream carry fixed event fields only) or telemetry event.

### Packet transmission (Repeater)

*Since v0.3.6.*

`packet.transmit` puts a native MeshCore packet on the air from a Repeater. A manager builds the packet, for example a direct or channel message under a Companion identity it is authorised to use. The Repeater only provides the transmitter: it keeps its own identity, signs and decrypts nothing, and goes on repeating as before.

- **Who may.** A signed caller only: the Deployment's authority, on every transport, the cable included. A standalone device answers `refused` / `signature-required`; an enrolled one refuses an unsigned request `unauthenticated` before the operation runs. Not over LoRa, and not on the Room server or the Companion, which do not list it.
- **What is sent.** `packet` is base64 of the bytes `Packet::writeTo` writes: the header, the transport codes when the route has them, `path_len`, the path and the payload, at most 255 bytes. They go out exactly as given: the route type, path and transport codes are not changed, and no hop is added or removed. A manager originating a flood sends an empty path; a forwarded packet keeps its own.
- **Checked before anything is queued.** Every length against what arrived. The payload version must be the one this firmware reads, the path valid, and the payload 1 to 184 bytes; otherwise `invalid` / `malformed`. Only messaging and what a bridge forwards may be sent: REQ, RESPONSE, TXT_MSG, ACK, GRP_TXT, GRP_DATA, ANON_REQ and PATH. An advert re-announces an identity and a trace writes its path into the payload, so ADVERT, TRACE, MULTIPART, CONTROL and RAW_CUSTOM answer `invalid` / `unsupported-type`.
- **Never twice.** The signed job id is the send's identity. The same id again gets its job, not a second send, across a lost answer, a reconnect and a reboot. A packet whose hash the Repeater has already seen, heard over the radio or sent, is refused `refused` / `already-heard`, so a packet heard and then also injected is never a silent success. The hash is marked seen before the send, so the Repeater does not repeat its own echo. The hash covers the type and payload, so the same packet with another route counts as the same. Each Repeater keeps its own table: two addressed in turn each send.
- **Bounds.** One send at a time; another, or any other job, while one is open answers `busy` / `queue-full`. At most 6 a minute, in bursts of 3: `busy` / `rate`. A packet longer on air than the whole duty window allows is `refused` / `over-duty-limit`. Otherwise the Repeater holds the packet until the window has room for it now and its packet pool has 8 free, so repeating is never starved, for at most `within_s` (5 to 120, default 60). It goes through the ordinary send queue and duty accounting, never around them.
- **The answer and the outcome.** The answer gives the job (`accepted`), the packet's hash, the airtime estimate and `duty_held`. The job ends as `applied` with outcome `transmitted` once the radio has sent it. It ends as `failed` with outcome `expired` (not sent within `within_s`), `failed` (the radio refused it), or `unknown` (queued but never confirmed sent, or cut by a reboot while open). `job.get` and the `job` event carry it. `transmitted` means on the air. It never means delivered or acknowledged: that is the native receiver's to say and the manager's to read.
- **Private.** The packet's bytes are in no result, job record, log, audit entry or stream. The hash and the outcome are the record.

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

The device-interface operations go over LoRa point to point ([LORA_MANAGEMENT](LORA_MANAGEMENT.md)): `hello`, `status.get`, `config.schema`, `config.get`, `config.apply`, `deployment.get`, `job.get`, `job.list`, `job.ack`, `job.cancel`, `job.confirm`, `posture.set`, `report.now`, `standing.put`, `standing.list`, `standing.remove`, and on the Repeater and Room server the six `region.*` operations. Only a signed request runs. Any other operation answers `unknown-op` there. On the management channel only `config.apply`, `job.get`, `job.ack`, `job.cancel`, `job.confirm`, `posture.set`, `report.now` and the five region changes go (`region.put`, `.remove`, `.flood`, `.home`, `.default`): every member can read a reply there, so `region.list`, which reads the whole tree back, is point to point only.

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
| Raw transmission and data signing as operations | not management (5.2); the one exception is the Repeater's `packet.transmit` |
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
