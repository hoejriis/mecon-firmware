# Management authority contract v1

Who may command a device, and how the device knows. This page is the public, generic contract: a device and any conforming manager can implement it from this page, the [device interface](DEVICE_INTERFACE.md), and the test vectors in this repository. It needs no particular Backend, broker, domain or product workflow. How MECON maps its own Deployments, ownership and federation onto it is [#118](https://github.com/hoejriis/mecon-firmware/issues/118). Checking conformance with any existing Backend is done there too, not here.

> **Status: design under review ([#95](https://github.com/hoejriis/mecon-firmware/issues/95)), not shipped.** The authority-set, rotation, record, replay and signed-request checks are implemented on a development branch with unit tests and public test vectors. None of it is in a release, and no role enforces it yet. The first security review is folded in (section 15). The implementation is being brought in line with it, and the wire objects marked *provisional* may still change.

## 1. Terms

- **Deployment**: one management domain. It is identified by a random 16-byte `deployment_id` and rooted in one Ed25519 key pair, the **Deployment authority key (DAK)**. Whoever holds the DAK's private key decides who the domain's managers are. A device belongs to at most one Deployment.
- **Manager**: a party that signs requests to devices, such as a Backend instance or a signing service. Each manager has a random 16-byte `instance_id` and its own Ed25519 key pair. A manager is a **member** of the Deployment while the current authority set lists it.
- **Authority set**: the DAK-signed list of current members and revoked managers. It has an `authority_epoch` (which DAK signed it) and an `authority_revision` that only increases.
- **Rotation statement**: the current DAK's signature on its successor.
- **Record**: what a device persists about its Deployment: the DAK, the current set, and per-member replay state (section 4).
- **Node key**: the device's native MeshCore Ed25519 identity. It proves who the device is. It never decides who may command the device.

A broker login, a TLS session, a paired BLE link, a USB cable, a LoRa route and knowing a channel key are **transports**. None of them is authority.

## 2. Encodings

- `deployment_id` and `instance_id`: 32 lowercase hex characters. Public keys: 64 hex characters (32 bytes). Signatures: standard base64 with padding, 88 characters (64 bytes). A device accepts hex in either case and nothing else; a wrong length is malformed.
- Every signature is Ed25519 over `DOMAIN || 0x00 || [SUFFIX || 0x00] || TEXT`. Nothing is canonicalised: the device verifies the bytes it has, then parses them, and a manager must send the same bytes it signed.
  - Where the signed text travels as a JSON string inside another object (an authority set, a request `body`), TEXT is that string's **decoded value**: its bytes after the enclosing JSON's escapes are undone. Any escaping that decodes to the same bytes verifies the same.

| Object | Domain | Suffix |
|---|---|---|
| authority set | `MECON-AUTHSET-v1` | none |
| rotation statement | `MECON-DAKROT-v1` | none |
| signed request (*provisional*) | `MECON-DEVMSG-v1` | `iface` |
| signed event | `MECON-UPLINK-v1` | the stream name, e.g. `health` |
| signed result | `MECON-UPLINK-v1` | the literal `result` |

- A key a device would trust is checked first: a member's key, a rotation's new DAK, and the DAK of a first provisioning. A key that is not a valid curve point, or whose point has small order, is refused (`bad_authority_set`, or `untrusted_authority_key` for a DAK). A signature against such a key proves nothing.

## 3. Objects

### 3.1 Authority set

```json
{"v":1,"deployment_id":"<id>","authority_epoch":1,"authority_revision":1,"issued_at":1790000000,
 "members":[{"instance_id":"<id>","public_key":"<hex>","admitted_revision":1}],
 "revoked":["<id>"]}
```

Carried as a string beside its signature (`authority_set`, `authority_set_sig`).

- **Members:** at most 8. No two members may share an `instance_id` or a public key.
- **`admitted_revision`:** the revision at which that member was (re)admitted. It is never greater than the set's `authority_revision`.
- **`revoked`:** at most 16 ids. A revoked id may not also be a member. Revoked ids that no longer matter (the manager can no longer be admitted under that id) may be dropped by a later revision.
- **`authority_epoch`:** 1 to 2³¹. A device refuses a larger epoch, which could never rotate.
- **Size:** a set text is at most 2,048 bytes. That is 8 members and 16 revoked ids with room to spare, and it keeps a provisioning inside the 4,096-byte message bound after escaping (section 6.1).

### 3.2 Rotation statement

```json
{"v":1,"deployment_id":"<id>","from_epoch":1,"to_epoch":2,"new_public_key":"<hex>","issued_at":1790001000}
```

Signed by the DAK of `from_epoch`, and `to_epoch` is `from_epoch + 1`. A device applies a chain of up to 8 in order, stopping at the first that fails (`untrusted_authority_key` for the whole message).

### 3.3 Signed request (*provisional*)

A v1.0 device-interface request travels as the exact `body` string of a wrapper:

```json
{"mecon_auth":1,"issuer":"<instance_id>","sig":"<base64>","body":"<the request JSON text>"}
```

The body is the v1.0 request plus six bound fields:

| Field | Meaning |
|---|---|
| `issuer` | the same `instance_id` as the wrapper's `issuer`; a mismatch is `bad_signature` |
| `deployment_id` | the Deployment the request is for |
| `authority_revision` | the authority-set revision the manager signed under |
| `issued_at` | the manager's clock, informational |
| `target_public_key` | the device's node key; binds the request to one device |
| `job_id` | an unsigned 32-bit id, strictly increasing per manager (section 8). Required on every operation except the plain reads of section 7.1 |
| `channel_nonce` | required only on the sensitive operations of section 7.1: the nonce the device issued for the link the request arrives on |

One suffix (`iface`) for every operation, so the same signed bytes are valid over MQTT, USB, BLE and LoRa. The operation is inside the body, so a request for one operation can never be replayed as another. A wrapper is at most 4,096 bytes, the interface's message bound. A frame whose top-level object has `mecon_auth` is a wrapper. The v1.0 `auth` member stays reserved, and a request carrying it is still refused.

### 3.4 Signed events and results

While the device holds a record (healthy or corrupt), it signs every interface event and every result with its node key, so the origin survives a Reader bridge or a broker, which forward the bytes unchanged. A device with no record signs nothing; whether to sign is decided when a request begins, so the result of the `deployment.reset` that removes the record is still signed.

- The message is one JSON object. The device adds `"usig_boot": <epoch>, "usig_seq": <n>` as its last two members, then signs, then appends `"usig":"<base64>"` as the last member.
  - `usig_boot` is the boot epoch of the device interface (the `epoch` of `hello`).
  - `usig_seq` counts every signed message of one boot, across all streams and results, from 1. It is separate from each stream's `seq`. The pair `(usig_boot, usig_seq)` never repeats while the boot counter is intact.
- The signature is Ed25519 over `MECON-UPLINK-v1` 0x00 `<stream>` 0x00 `<text>`. `<stream>` is the event's stream name (`health`, `observations`, ...) or the literal `result`. `<text>` is the object **up to and including its closing brace, with `usig` not yet added**: that is, the bytes the manager has after removing the `,"usig":"..."` member textually. A manager strips exactly the trailing `,"usig":"<88 base64 characters>"}` (a fixed length, always last) and puts `}` back, then verifies against the node key it enrolled; it never searches for the first `"usig"`, which a nested member could imitate. `result` is reserved as a stream name. The stream is part of what is signed, so a result cannot pass as an event, nor an event of one stream as another's.
- A signed event is covered by the public vector `uplink_health` and a signed result by `uplink_result`.
- When the boot counter could not be saved at boot, `usig_boot` might repeat. The device then signs nothing and appends `"unsigned":"epoch_unsaved"` instead (and `"unsigned":"seq_exhausted"` in the 2³² messages after which `usig_seq` would wrap). A manager that requires signed messages drops those.
- A message too large to carry its signature is not sent as an event, and answered as a short signed `busy` (`too-large-to-sign`) for a result.
- The answer to `identity.rotate` (section 11) is signed with the key it retires, so that the manager that enrolled that key can verify it and learn the new one from inside it. Every later message is signed with the new key.
- **A result names what it answers.** Before signing, the device adds `"op":"<operation>"` and, for a signed request, `"job_id":<n>` ahead of the signature members. A bridge that withholds a signed request therefore cannot answer it with an earlier result of the same `id`: the manager checks `op` and `job_id` against what it sent, and a mismatch is not an answer. A result for a request that was not understood far enough to name an operation carries neither.
- **Freshness is the manager's rule.** Per node key, a manager rejects a message whose `usig_boot` is lower than any it has seen, and within one boot a `usig_seq` that is not higher than the highest seen. The device guarantees the counter; the manager enforces the order.
- **Refusals are signed within a budget.** A device signs at most 8 refusals a second (a burst of 8), so unauthenticated input cannot keep the signer busy. A refusal beyond it goes out unsigned with `"unsigned":"rate"`, and a manager treats it as no information. Results that are not refusals, and events, are always signed.
- The native signing command of the Companion (`CMD_SIGN_*`) refuses data that starts with `MECON-`, so no native signature can be a protocol signature, enrolled or not.
- Signing cost: about 12 µs per signature of a 250-byte message on the simulated board's host (aarch64, gcc -O2, the firmware's own `lib/ed25519`, 20,000 signatures, measured 2026-10-28; see Review 3 in [AUTHORITY_REVIEW_2.md](AUTHORITY_REVIEW_2.md)). It has not been measured on a target MCU.

## 4. The record on the device

- **Two copies.** The device keeps two copies, A and B. Each is framed as `"MDR1" | u32 generation | u16 length | payload | u32 CRC-32`.
- **Writes.** A write goes to the copy that does not hold the newest record, with generation + 1, so a power cut leaves the previous record intact.
- **Load.** On load, the intact copy with the highest generation wins. A copy whose stored set no longer verifies against its stored DAK counts as damaged.
- **Corrupt is never absent.** If something exists and nothing is intact, the record is **corrupt**. A corrupt record is never treated as absent, because absent means "no Deployment" and would reopen an unsigned first provisioning. Failing to allocate memory while loading is corrupt too.
- **Provisioned marker.** A one-byte marker is written at the first provisioning. If both copies are missing but the marker is present, the record is corrupt, not absent; without the marker, losing both files would reopen first use.
  - The marker lives **outside the file system that holds the record**, because a file system that fails to mount may be formatted by the platform: in NVS on ESP32, and in a reserved flash page on nRF52.
  - Only two operations clear it: `deployment.reset` and the factory reset (section 11). Nothing else, a file system format included, clears it.
- **Contents.** The payload holds:
  - the contract version, `deployment_id` and a 1 to 64 byte display name;
  - the DAK, `authority_epoch` and `authority_revision`, and the set text with its signature;
  - `provision_revision`;
  - the local unlock secret, if set (section 10);
  - optional broker-profile roles (transport metadata, never authority).
- **Replay marks are kept apart.** Each member's sequence mark (section 8) lives in a small separate record. It has two copies framed the same way, so a power cut leaves the previous marks. A signed request then rewrites tens of bytes, not the whole record with its set text. That matters on nRF52, where the file system is 28 KB and flash wears out.
- **A write that fails is a refusal.** When the device cannot persist a mark or a record, it answers `busy` and runs nothing.
- **Fallback flag.** When the device falls back to the older copy of either record because the newer one is damaged, it says so in `status.deployment` (`fallback: true`). The older marks may be behind; a manager that sees the flag raises its sequence before its next request (section 8).

## 5. Accepting an authority set

In this order, stopping at the first failure; nothing changes on a failure:

1. The signature verifies with the DAK of the set's `authority_epoch`, after any rotation delivered with it → else `bad_authority_signature`.
2. The text parses, and every field is in range (section 3.1) → else `bad_authority_set`.
3. `deployment_id` is the stored one → else `wrong_deployment`.
4. `authority_epoch` is the device's epoch after the rotations → else `untrusted_authority_key`. Steps 3 and 4 use these codes, not `bad_authority_set`.
5. `authority_revision` against the stored revision:
   - lower → `stale_revision`;
   - equal → `duplicate`: acknowledged, nothing written. The text must be byte-identical to the stored set, or it is `bad_authority_set`.
6. Membership against the stored record:
   - a member the device does not hold must have `admitted_revision` greater than the stored revision;
   - a member it holds may not have its `admitted_revision` lowered.

   Either breach → `bad_authority_set`. A removed and re-added manager therefore always carries the revision of its re-admission.
7. Commit (section 4). A member still present at the same `admitted_revision` keeps its mark. A re-admitted member's mark is reset to 0, so its sequence restarts while check 6 refuses everything it signed before. A removed member's mark is dropped, and a new member starts at 0.

Section 5 applies to every delivery of a set: `deployment.provision` and `deployment.authority_update` alike, the byte-identical rule for an equal revision included. A set at the device's revision whose bytes differ is evidence that the DAK signed two different sets for one revision. It is refused and counted in `auth_rejections`, never acknowledged.

A device that has not received a rotation still trusts the previous DAK. A DAK that is compromised is therefore recovered by rotating it on every device; a device the rotation cannot reach needs physical re-provisioning.

## 6. Provisioning and updates

### 6.1 `deployment.provision`

Arguments, as one `deployment` object:
- `contract_version` (1), `deployment_id`, `deployment_name`;
- `node_public_key` (must be this device's: else `wrong_device`);
- `provision_revision`;
- `dak` and `authority_epoch`;
- `authority_set` and `authority_set_sig`;
- `unlock_secret` (optional; section 10);
- `broker_roles` (optional: `[{"profile":n,"role":"primary"|"secondary"|"local"}]`, each naming an existing MQTT profile, else `bad_broker_roles`).

Product profiles (section 14) may add fields; a device ignores fields it does not know.

A provisioning carries no rotations. A device whose DAK has rotated gets `deployment.authority_update` first, so a provisioning always fits the 4,096-byte message with its set text escaped, even inside a signed wrapper.

- **First provisioning** (no record, no marker) is trust on first use: it pins the DAK. It is accepted unsigned from the USB cable only. Never over BLE (a device cannot tell its own random PIN from one a user or a build chose), the TCP port, a broker of any kind (a broker would gain first-use trust over every reset or unenrolled device it can reach) or LoRa. *(Narrowed 2026-10-28 by the second security review; this section earlier also allowed BLE with the device's own random PIN.)*
- A platform with no store for the provisioned marker (section 4) refuses an unsigned first provisioning.
- **A device that holds a record** accepts `deployment.provision` only when signed by a current member. An unsigned re-send of a byte-identical `deployment` object returns the stored read-back; anything else unsigned is `unauthenticated`.
- **Order and commit.** Checks run in the order above, then section 5 on the set. One commit, and the answer is read back from storage. An equal `provision_revision` with identical content is `duplicate`; a lower one is `stale_revision`.

### 6.2 `deployment.authority_update`

A newer authority set, with any rotations before it, for a device that holds a record. It is DAK-signed itself, so it needs no manager signature and no `job_id`, and any caller that can reach the interface may deliver it. The device rate-limits it to protect flash. Section 5 applies.

### 6.3 `deployment.get`

An open read: the stored set and its signature, `deployment_id`, the epoch and revisions, the fallback flag, and each member's current sequence mark. A manager that lost track of its sequence (a restore from backup) reads its mark and continues above it. It reveals nothing secret. The same set is published retained at `mecon/<device_id>/authority` over MQTT.

## 7. Checking a signed request

In this order; a failure refuses the request (`status:"refused"`, `error` the code) and nothing runs:

| # | Check | Code on failure |
|---|---|---|
| 0 | the wrapper is at most 4,096 bytes | `payload_too_large` |
| 1 | a wrapper, `mecon_auth` = 1 | `unauthenticated` (not wrapped where a signature is required), `unsupported_auth_version` |
| 2 | `issuer` is a current member | `issuer_revoked` if revoked, else `unknown_issuer` |
| 3 | `sig` verifies over the body bytes with the member's key | `bad_signature` |
| 4 | `deployment_id` is the record's | `wrong_deployment` |
| 4b | `target_public_key` is this device's node key | `wrong_device` |
| 5 | `authority_revision` ≤ the device's stored revision | `authority_revision_ahead`: the manager knows a newer set than the device; it delivers it with `deployment.authority_update` |
| 6 | `authority_revision` ≥ the member's `admitted_revision` | `stale_authority`: signed before this member's (re)admission |
| 6b | `issuer` in the body equals the wrapper's | `bad_signature` |
| — | an operation that is not a plain read (7.1) carries `job_id` | `unauthenticated` |
| — | a sensitive operation (7.1) carries the `channel_nonce` of this link | `bad_channel_nonce` |
| 7 | the sequence (section 8) | `replay`; `duplicate` for the id at the mark |

- The body is parsed only after check 3, so nothing unsigned is ever parsed past the wrapper. When a transport fragments a message (LoRa, [#115](https://github.com/hoejriis/mecon-firmware/issues/115)), check 3 runs on the reassembled body. Reassembly is bounded per message id, and parts of different messages are never combined.
- A device whose record is corrupt answers every request except the recovery ones (section 11) with `deployment_record_corrupt`.
- A request refused before execution consumes no `job_id` and moves no watermark.
- Every refusal increments `auth_rejections` in `status.deployment`. A Reader's single unsigned readiness probe on attach is answered and is not counted.

### 7.1 Plain, state-changing and sensitive operations

- **Plain reads** are an allowlist: `hello`, `status.get`, `config.schema`, `config.get` of keys that are not secret, `deployment.get`, `job.get`, `job.list`, and `sub.start`/`sub.stop` of the streams that are not private. A signed plain read carries no `job_id`. Replaying it elsewhere returns only what that operation returns to anyone allowed to read.
- **Every other operation**, including any the device does not recognise as a plain read, carries a `job_id` and is subject to section 8. A captured request can then run at most once.
- **Sensitive operations** are those whose reply is itself secret or private:
  - `identity.key.export` and `ble.pin.get`;
  - `sub.start` of a private stream: `screen`, `location`, `logs` and `inventory` (which carries the contact and channel entries on the Companion).

  On top of the `job_id`, each must carry the `channel_nonce` the device issued for the link it arrives on. A device issues a fresh nonce per link, readable with `hello` on that link and replaced when the link drops, and a nonce is used once. A request captured on one link and replayed over another link (the attacker's own USB, BLE or broker session) fails before the reply exists.

## 8. Replay

- **One strictly increasing sequence per member.** The device keeps one mark per member: the highest `job_id` it accepted. A request with a `job_id` above the mark runs. One equal to the mark is a duplicate: it never runs again, and it gets the stored result, or `duplicate` with no result when the result was not kept. One below the mark is refused as `replay`. Id 0 is never valid. Every operation that carries a `job_id` shares the one sequence, whatever it does.
- **Why strict, and not a window.** With a window, a request that an intermediary (a broker, a bridge) held back could still run after newer ones, out of order. A held `config.apply` released after its successor would put back an old setting, and a held reboot or reset could fire late. With one strict sequence, the moment a newer request runs, every older one the intermediary still holds is dead.
- **Retry with the same bytes.** A manager that gets no answer resends the **same signed request**, never the same work under a new id. The answer is the stored result, or `duplicate` (it ran, or a power cut lost it after the mark was saved) and the manager reads the state. Signing a new id is how a manager deliberately abandons the older request.
- **One request at a time per member.** A manager sends its next state-changing request when the previous one has an answer. Managers of one Deployment do not drive one device concurrently. Their sequences are independent, so the device cannot order one manager's held request against another's.
- **At most once.** The mark is persisted **before** the request runs. The other order would let a redelivered request run twice after a power cut.
- **No adoption.** A run of verified ids below the mark can only be old signed requests, so a device never adopts a "restarted" sequence. A manager that lost its sequence reads its mark (`deployment.get`) and continues above it. If it cannot, it is re-admitted with a higher `admitted_revision`, which resets its mark while check 6 refuses everything it signed before.

## 9. Caller kinds

#95 adds one principal to the [CLI privilege table](../CLI.md): **signed**, a request that passed section 7 on any transport. The table below is how unsigned callers change once a device holds a record, and it is the documented transition from the standalone postures ([Standalone postures](../STANDALONE_POSTURES.md)), not a silent change of what a posture means. Standalone devices (no record) keep today's table unchanged.

| Caller | No record (standalone) | Record, Normal | Record, Hardened | Record corrupt |
|---|---|---|---|---|
| signed | not applicable: nothing to verify against (`unauthenticated`) | per class: read, write, reboot, posture (with a controlled reboot), and the reset scopes section 11 allows | the same | `deployment_record_corrupt` |
| USB cable, unsigned | today's table | reads, native messaging, and the recovery operations of section 11; **no configuration writes** | identification, and the local-unlock handshake | identification; `deployment.reset` |
| BLE paired (own PIN), unsigned | today's table | native messaging and reads; no configuration writes, **no factory reset** | BLE is not started | reads |
| MQTT, TLS verified, unsigned | today's table | reads (`status.get`, `config.schema`, `deployment.get`); state changes refused `unauthenticated` | the same | reads; else `deployment_record_corrupt` |
| local unlock (section 10) | not applicable | as signed, for reads and to unlock the posture | the management surface | `deployment.reset` only |
| LoRa remote admin (native) | today's table | reads only | reads and `reboot` | reads |

Its point is that a transport never becomes authority: once a device belongs to a Deployment, changing it needs a member's signature or someone physically present.

- Entering Hardened while a record exists is refused unless the record holds a local unlock secret. A signed request may lower the posture, as the cable may.
- BLE loses the native factory reset once a record exists. Otherwise a BLE factory reset followed by a BLE first provisioning would let a paired phone take a device over without touching it.
- Every path that formats or rebuilds storage keeps the record, or is gated as `deployment.reset` is. That includes the rescue console's `rebuild`, which reformats and saves back identity, settings, contacts and channels: it saves the record back too. A recovery path is never a silent unenrollment.

## 10. Local unlock

A device entering Hardened while it holds a record must first hold a **local unlock secret**, so that a Reader can always unlock it over USB.
- The secret is set only by an authority: in the `deployment` object of a provisioning (`unlock_secret`, 32 random bytes), or by a signed request (`deployment.set_unlock`, a sensitive operation). It is never set unsigned, so the first person with a cable cannot set it. Either request is accepted only over the USB cable, so the secret never crosses a broker or a radio link.
- The device stores the secret itself, as a secret: an HMAC challenge needs the key, so a hash would not do. It lives in the record, is never readable over any surface, and is removed by `deployment.reset` and the factory reset.
- The unlock is an HMAC-SHA-256 challenge over USB only. The device sends a fresh random challenge, and a Reader holding the secret answers it.

The unlock grants the reads and posture unlock shown in section 9. It never grants key export or configuration writes beyond what signed callers have.

## 11. Enrollment changes, reset and recovery

| Operation | Removes | Keeps | Who |
|---|---|---|---|
| broker or Wi-Fi profile change | that profile | everything else | signed (commit-or-revert, [Operations](OPERATIONS.md) section 3.4) |
| `deployment.authority_update` | nothing; the set changes | everything | anyone who can reach the device (DAK-signed) |
| re-enrollment into the same Deployment | nothing | everything; the set and revision move forward | signed `deployment.provision` |
| `deployment.reset` (unenroll), healthy record | the record, watermarks, local unlock secret, provisioned marker | node key, `device_id`, configuration, contacts, channels | signed, over USB only, in Normal |
| `deployment.reset`, corrupt record | the same | the same | USB cable in Normal; local unlock in Hardened |
| posture lower (Hardened → Normal) | continuity material ([Security and authority](SECURITY_AND_AUTHORITY.md)) | the record | signed, or the cable |
| `identity.rotate` (new owner) | node key, contacts, message queue | configuration, record, channels, posture, `device_id` | signed and sensitive; refused without a readable record. Companion only |
| rescue `rebuild` | nothing: it reformats and saves everything back, the record included | everything | the cable |
| factory reset | everything, the record, the provisioned marker and `device_id` included | nothing | the cable only; never remote, never BLE once enrolled |

Rules:
- Each operation removes exactly what it names.
- A factory reset regenerates `device_id` from the cryptographic RNG. After erase the device is a new device, so a manager cannot re-attach old state to it.
- Removing the record's two copies deletes the older copy first, so a power cut in between never brings an older record back.
- Moving to a different Deployment is unenrollment followed by a first provisioning, which needs physical presence again.
- Rotation runs in this order: generate the new key, write the rotation statement, save the new key, swap, drop contacts and queue, clear the statement's pending mark, then one self-advert. The device reports the old and the new public key.
  - **The rotation statement** is what lets a manager that lost the result trust the new key. Before anything changes, the old key signs `MECON-NODEROT-v1` 0x00 `<text>`, where `<text>` is `{"v":1,"old":"<hex>","new":"<hex>","device_id":"<hex>","job_id":<n>}` (that member order, no spaces). It is stored atomically with its signature in a file of its own, marked drop-pending (the mark is not part of the signed text: the old key is gone by the time it is cleared).
  - `identity.rotation` (a plain read, in `hello`'s capabilities) returns `{"rotation":{"statement":"<the text, as a string>","sig":"<base64>"}}`, or an empty result when there is none; `deployment.get` carries the same `rotation` member. A statement stays until a later rotation replaces it.
  - **A manager that lost the result** (the link, the bridge or the broker dropped it, or the device lost power after the key save) cannot repeat the request: a request aimed at the old key is `wrong_device`. It reads `hello`, whose `node_key` is the new key but is signed by that same new key and so proves nothing, and `identity.rotation`. It trusts the new key only if the statement's `old` is the key it enrolled, the signature verifies against that old key over the domain and the text, `device_id` is the one it enrolled, and `new` equals the `hello` key. A `hello` forged by a party on the path fails the last check or has no statement the old key signed.
  - A power cut at each step: before the key save, the statement's `new` is not the node key at boot, the statement is deleted and the key is unchanged; after the key save and before the drop ends, the statement is still drop-pending, and the boot finishes the drop (contacts, their cached blobs, the queue) and clears the mark. A drop that cannot finish stays pending for the next boot. The old owner's contacts are therefore never left to the new key.
  - The new key comes from the platform's cryptographic RNG (section 12), redrawn if it is the current key or starts with a reserved id hash (`00`, `FF`); a source that delivers only zeros is a refusal (`rng`), never a fixed key. It is saved atomically (a failed save is `storage`, the old key stays), and only then swapped in.
  - Requests aimed at the old key are then `wrong_device`; the authority state (DAK, set, revision, members and their marks) is untouched, and the node key, old or new, grants nothing.
  - The self-advert is one zero-hop advert, like the app's default.
  - A device whose contacts cannot all be dropped after the swap answers `failed` (`contacts_not_dropped`); the key has changed, `hello` shows it, the statement stays drop-pending (the next boot retries), and the manager recovers the new key from `identity.rotation` as above.
  - The Repeater and Room server do not offer it: `identity.rotate` is an unknown operation there.
  - A revoked member's rotation is refused `issuer_revoked` like any other request of its.

## 12. Keys, randomness and secrets

- **Device keys and secrets** come from the platform's cryptographic RNG, fed by the hardware RNG: `esp_fill_random` on ESP32, and the nRF52's hardware RNG peripheral. This covers the node key, `device_id`, the BLE PIN, the local unlock challenge and the channel nonces. Never Arduino `random()`, which is a seeded libc PRNG on some cores.
- A signed update that names a minimum firmware version is bound to the record before a device enforces it, so downgrading the firmware cannot drop the rule ([#110](https://github.com/hoejriis/mecon-firmware/issues/110)).
- **Wiping.** Key material is cleared with a wipe the compiler may not drop (a volatile loop), on every exit path of a rotation. A rotation's new key is drawn from a source that must pass a health test: not all zero, at least 16 distinct byte values, and not equal to the previous draw; otherwise the refusal is `rng`. After a rotation the old private key can remain in unerased flash blocks until they are reused (the file system replaces files atomically and does not erase in place), so a rotation protects against later use of the old key by the device, not against someone who images the flash of the same unit.
- **The DAK and manager keys** never exist on a device. Only public keys and signatures do.
- **The node private key** leaves a device only under the custody rule in [Security and authority](SECURITY_AND_AUTHORITY.md):
  - before provisioning, over a USB cable;
  - after provisioning, by a signed request answered only to the requester's channel.
  It never leaves in Hardened.
- **Never in any status, event, log, audit entry or screen telemetry stream:** Wi-Fi and broker passwords, the broker user, private keys, the local unlock secret, channel secrets. The screen stream is private because a frame can show message text and the pairing PIN. It is sent only on a signed `sub.start`, never retained and never redacted (pixels cannot be redacted reliably), so access control is its protection.

## 13. Error codes

`unauthenticated`, `unsupported_auth_version`, `unknown_issuer`, `issuer_revoked`, `bad_signature`, `wrong_deployment`, `wrong_device`, `authority_revision_ahead`, `stale_authority`, `replay`, `bad_channel_nonce`, `payload_too_large`, `deployment_record_corrupt`, `bad_authority_signature`, `bad_authority_set`, `stale_revision`, `untrusted_authority_key`, `unsupported_contract_version`, `bad_broker_roles`, `bad_deployment_name`, and `duplicate` (an acknowledgement, not a refusal). An unknown code is a refusal.

## 14. Product profiles

A product built on this contract may add fields to the `deployment` object and its own job classes. These must not weaken any check above, and the profile must say which fields it adds. MECON's profile is [#118](https://github.com/hoejriis/mecon-firmware/issues/118); its outage sync-key generation and its migration of legacy job sequences are defined there, not here. A generic device ignores profile fields it does not implement.

## 15. Review outcome

The first security review (2026-10-25) decided the questions this page had left open. The answers are folded into the sections above:

| Question | Decision | Section |
|---|---|---|
| Who may provision unsigned | the USB cable, and BLE paired with the device's own random PIN; never a broker, the TCP port, a default or static PIN, or LoRa | 6.1 |
| Unsigned configuration writes over USB once enrolled | refused; recovery paths keep the record | 9, 11 |
| One suffix for every operation | kept; replay across transports is closed by `job_id` on everything but plain reads, and by the channel nonce on sensitive replies | 3.3, 7.1 |
| Remote erase | never | 11 |
| Boot counter save failure | sign nothing | 3.4 |
| `device_id` at factory reset | regenerated, from the cryptographic RNG | 11, 12 |
| The provisioned marker | kept, outside the formatted file system | 4 |
| Sequence adoption | never; one strict sequence per member, marks readable | 6.3, 8 |
| LoRa fragmentation | check 3 on the reassembled body; bounded per message id | 7 |

The review also changed: the size limits (2,048-byte set, 16 revoked ids, no rotations in a provisioning); marks kept apart from the record, and a failed write refused; the local unlock secret set only by an authority and stored as a secret; the byte-identical rule for every set delivery; the delete order; the error codes of section 5; the epoch bound; and binding `issuer` into the body.

## 16. Conformance

- Public test vectors: `test/fixtures/deployment_authority_public_v1.json`, generated by `tools/mecon_auth_vectors.py` from this page. Both land with the implementation branch. Every key is derived from a public label, so these are test keys and valid nowhere. The vectors cover two authority sets, a rotation, a signed request, a signed event and a signed result. `--check` fails if the fixture drifts from the generator.
- Unit tests cover:
  - every check of sections 5 and 7, in order;
  - the record framing under damage of either copy and a power cut at each step;
  - the membership and re-admission rules;
  - the sequence at the mark, below it and across a reboot, and a failed mark write refusing the request.
- Simulated-board tests over every transport follow when the roles enforce the record.
