# Management channel

> Every role has a management channel. A Companion binds one slot of its native channel table, as this page specifies. A Repeater or Room server, which has no channel table, holds a single management channel with the same operations and commit-or-revert, and no general channel messaging (owner decision of 2026-11-01; its storage and any differences are not yet specified here). A device lists these operations in `hello` only where it supports them. What travels over the channel is defined in [management over LoRa](LORA_MANAGEMENT.md).

A device can mark one of its native channels as its **management channel**: the private channel a manager reaches it
on over LoRa. This page defines how that channel is configured. It does not define what travels over it.

## 1. What a binding is

- **One slot of the native channel table.** The channel is an ordinary native channel. It is set with `channel.put`
  or the app's `CMD_SET_CHANNEL`, it is listed by `channel.list`, and it carries native channel messages as any other
  channel does. Nothing keeps a second copy of its key.
- **A record of its own.** The binding is a small file (`/mgmt_channel`), written atomically and checked with a CRC.
  It holds the slot, a fingerprint of the bound key (the first 8 bytes of SHA-256 of the key: neither the key nor its
  on-air hash) and a generation that counts every bind, rotation and move of the binding.
- **Membership is not authority.** Knowing the channel's key lets a node read and send on the channel, nothing more.
  A command carried over it needs its own authority
  ([Management authority](AUTHORITY.md)), and a binding changes nothing about what any caller may do.
- **Nothing starts.** Binding starts no forwarding, backfill, sync, reporting or extra airtime.
- **Never the public channel.** A slot holding the well-known public key cannot be bound: everyone holds that key.

## 2. States

| State | Means |
|---|---|
| `none` | no binding |
| `bound` | the slot's key matches the record's fingerprint |
| `stale` | the record names a slot whose key no longer matches (it was changed natively, or the slot was cleared), or the record cannot be read; an unreadable record names no slot, so nothing is guarded or erased on its account |

A stale binding is never moved to the new key without being asked. `mgmt.channel.bind` binds again (to the same slot
or another), and `mgmt.channel.unbind` ends it. Rotation refuses a stale binding, and so does `unbind` with `remove`: a stale slot may now hold the app's own channel, so it is never erased on the binding's account.

## 3. Operations

| Operation | Args | Result | Class |
|---|---|---|---|
| `mgmt.channel.get` | none | `{"state":"none"}`, or `state`, `slot`, `generation`, and the slot's `name`, `hash` and `key_bits` while it holds a key | read |
| `mgmt.channel.bind` | `slot` | as `get` | write |
| `mgmt.channel.rotate` | `secret` (32 hex digits), optional `name` (kept when absent) | as `get`, `generation` + 1 | write |
| `mgmt.channel.unbind` | optional `remove` (bool, default false: the channel stays as an ordinary channel) | `{"state":"none","removed":bool}` | write |

Errors (`failed` unless noted):
- `empty-slot`: bind of a slot with no key.
- `public-channel`: bind of the public channel's slot, or rotation to its key.
- `not-bound`, `stale`: rotation without a healthy binding.
- `duplicate-secret`: rotation to a key that another slot holds.
- `unsupported-key`: a change whose snapshot would have to hold a 256-bit key.
- `write-failed`: the record or the slot could not be saved.
- `invalid` with `confirm-required` or `confirm-not-needed`: see section 4.
- `stale` on `unbind` with `remove`: the binding is not healthy, so the slot is kept.

Binding a slot that is already bound returns its state and writes nothing.

`mgmt.channel.get` is of the read class. On an enrolled device an unsigned caller may therefore learn which slot is the management channel, with its name and on-air hash, never its key.

## 4. Commit or revert

These changes can cut the path a remote manager uses ([Managed operations](OPERATIONS.md) section 3.4, "over LoRa,
the management channel"):
- `mgmt.channel.bind`, `mgmt.channel.rotate` and `mgmt.channel.unbind`;
- `channel.put` and `channel.remove` **of the bound slot**.

The rules are the same as for `config.apply`:
- **Who must confirm.** A caller other than the unsigned cable must send `"confirm":{"within_s":N}` (N from 30 to
  900, default 300). Without it, the request is refused with `invalid` / `confirm-required` and nothing changes. The
  unsigned cable may omit it. A `confirm` on a request that changes nothing it protects (a bind of the slot already
  bound, an unbind with nothing bound, a `channel.put` or `channel.remove` of another slot) is refused with `invalid` /
  `confirm-not-needed`, as `config.apply` does.
- **The job.** With `confirm`, the change becomes a job (`result.job`, `result.job_state`:`"awaiting-confirm"`,
  `result.confirm_left_s`). A `channel.put` or `channel.remove` job keeps its own operation's name as the job's `op`.
- **The snapshot.** Before anything is applied, the slot's previous name and key and the previous record are written
  to a snapshot file of their own, bound to the job by id and checked with a CRC. The snapshot is device-internal: it
  is never reported, and it is removed when its job ends. A snapshot that no awaiting job owns is removed at boot.
- **Confirm or revert.** `job.confirm` keeps the change. Otherwise the change is undone, and the job ends `reverted`,
  when `within_s` passes, or when the device boots a second time while the job is pending, or on `job.cancel`. A
  change that fails partway is undone at once, and the job ends `failed` with `reverted`. A job cut by a reset before it
  was awaiting confirmation is undone at the next boot.
- **A revert never overwrites someone else's change.** The snapshot also records what the change left in the slot (the
  new key's fingerprint, or an empty slot). The old name and key are written back only while the slot still shows
  that. If the slot still holds the old contents, the change never reached it, and nothing is written. If it holds
  anything else, the app changed it meanwhile: the revert is refused, the job ends `failed` (`revert-failed`), and the
  binding reads `stale`. The snapshot is removed in every case.
- **One at a time.** One state-changing job runs at a time ([Managed operations](OPERATIONS.md) section 3.3).

A `channel.put` of the bound slot that is allowed (by the cable, or confirmed) moves the binding to the new key, with
`generation` + 1. A `channel.remove` of the bound slot ends the binding.

## 5. Secrets

The key is written, never read back: results carry the slot's name, its one-byte on-air hash and its key size. The
fingerprint is not reported. The snapshot's copy of the old key is wiped from memory once written, and removed with the
snapshot. Every copy of a key the firmware handles for these operations is wiped once used.
