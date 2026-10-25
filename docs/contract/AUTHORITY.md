# Management authority contract v1

Who may command a device, and how the device knows. This page is the public, generic contract: a device and any conforming manager can implement it from this page, the [device interface](DEVICE_INTERFACE.md), and the test vectors in this repository. It needs no particular Backend, broker, domain or product workflow. How MECON maps its own Deployments, ownership and federation onto it is [#118](https://github.com/hoejriis/mecon-firmware/issues/118). Checking conformance with any existing Backend is done there too, not here.

> **Status: design under review ([#95](https://github.com/hoejriis/mecon-firmware/issues/95)), not shipped.** The authority-set, rotation, record, replay and signed-request checks are implemented on a development branch with unit tests and public test vectors. None of it is in a release, and no role enforces it yet. Section 15 lists what the security review is asked to decide. This page changes if the review changes it, and the wire objects marked *provisional* are the ones most likely to change.

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
- Every signature is Ed25519 over `DOMAIN || 0x00 || [SUFFIX || 0x00] || TEXT`. TEXT is the signed JSON text **exactly as transmitted**. Nothing is canonicalised: the device verifies the bytes it received, then parses them. A manager must send the same bytes it signed.

| Object | Domain | Suffix |
|---|---|---|
| authority set | `MECON-AUTHSET-v1` | none |
| rotation statement | `MECON-DAKROT-v1` | none |
| signed request (*provisional*) | `MECON-DEVMSG-v1` | `iface` |
| signed event (*provisional*) | `MECON-UPLINK-v1` | the stream name, e.g. `health` |

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
- **`revoked`:** at most 32 ids. A revoked id may not also be a member.
- **Size:** a set text is at most 3,072 bytes.

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

The body is the v1.0 request plus five bound fields:

| Field | Meaning |
|---|---|
| `deployment_id` | the Deployment the request is for |
| `authority_revision` | the authority-set revision the manager signed under |
| `issued_at` | the manager's clock, informational |
| `target_public_key` | the device's node key; binds the request to one device |
| `job_id` | an unsigned 32-bit id, per manager and job class (section 8). Required on every state-changing operation; absent on reads |

One suffix (`iface`) for every operation, so the same signed bytes are valid over MQTT, USB, BLE and LoRa. The operation is inside the body, so a request for one operation can never be replayed as another. A wrapper is at most 4,096 bytes, the interface's message bound. A frame whose top-level object has `mecon_auth` is a wrapper. The v1.0 `auth` member stays reserved, and a request carrying it is still refused.

### 3.4 Signed event (*provisional*)

A device signs each interface event and result with its node key, so its origin survives a Reader bridge or a broker:

- The signed text is the event as sent, carrying `"usig_boot": <epoch>, "usig_seq": <n>`.
  - `n` counts every signed message across all streams within one boot, and is separate from each stream's `seq`.
  - The pair `(usig_boot, usig_seq)` never repeats while the boot counter is intact.
- The device appends `"usig":"<base64>"` as the last member. A manager removes that member textually, then verifies the rest against the node key it enrolled.
- When the boot counter could not be saved, so that `usig_boot` might repeat, the device signs nothing. It sends `"unsigned":"epoch_unsaved"` instead, and a manager that requires signed events drops them.

## 4. The record on the device

- **Two copies.** The device keeps two copies, A and B. Each is framed as `"MDR1" | u32 generation | u16 length | payload | u32 CRC-32`.
- **Writes.** A write goes to the copy that does not hold the newest record, with generation + 1, so a power cut leaves the previous record intact.
- **Load.** On load, the intact copy with the highest generation wins. A copy whose stored set no longer verifies against its stored DAK counts as damaged.
- **Corrupt is never absent.** If something exists and nothing is intact, the record is **corrupt**. A corrupt record is never treated as absent, because absent means "no Deployment" and would reopen an unsigned first provisioning. Failing to allocate memory while loading is corrupt too.
- **Provisioned marker.** A separate one-byte marker is written at the first provisioning and removed only by an unenrollment scope (section 11). If both copies are missing but the marker is present, the record is corrupt, not absent. Without the marker, losing both files would reopen first use. *(Decision for the review, section 15.)*
- **Contents.** The payload holds:
  - the contract version, `deployment_id` and a 1 to 64 byte display name;
  - the DAK, `authority_epoch` and `authority_revision`, and the set text with its signature;
  - `provision_revision`;
  - the hash of the local unlock secret, if set (section 10);
  - optional broker-profile roles (transport metadata, never authority);
  - per-member replay state (section 8).
- **Fallback flag.** When the device falls back to the older copy because the newer one is damaged, it says so in `status.deployment`. The older copy's watermarks may be behind.

## 5. Accepting an authority set

In this order, stopping at the first failure; nothing changes on a failure:

1. The signature verifies with the DAK of the set's `authority_epoch`, after any rotation delivered with it → else `bad_authority_signature`.
2. The text parses, and every field is in range (section 3.1) → else `bad_authority_set`.
3. `deployment_id` is the stored one → else `wrong_deployment`.
4. `authority_epoch` is the device's epoch after the rotations → else `untrusted_authority_key`.
5. `authority_revision` against the stored revision:
   - lower → `stale_revision`;
   - equal → `duplicate`: acknowledged, nothing written. The text must be byte-identical to the stored set, or it is `bad_authority_set`.
6. Membership against the stored record:
   - a member the device does not hold must have `admitted_revision` greater than the stored revision;
   - a member it holds may not have its `admitted_revision` lowered.

   Either breach → `bad_authority_set`. A removed and re-added manager therefore always carries the revision of its re-admission.
7. Commit (section 4). Replay state is kept for members still present at the same `admitted_revision`, emptied for a re-admitted member, dropped for removed members, and empty for new ones.

## 6. Provisioning and updates

### 6.1 `deployment.provision`

Arguments, as one `deployment` object:
- `contract_version` (1), `deployment_id`, `deployment_name`;
- `node_public_key` (must be this device's: else `wrong_device`);
- `provision_revision`;
- `dak` and `authority_epoch`;
- `authority_set` and `authority_set_sig`, with `rotations` optional;
- `broker_roles` (optional: `[{"profile":n,"role":"primary"|"secondary"|"local"}]`, each naming an existing MQTT profile, else `bad_broker_roles`).

Product profiles (section 14) may add fields; a device ignores fields it does not know.

- **First provisioning** (no record, no marker) is trust on first use: it pins the DAK. It is accepted unsigned only from a caller physically present or locally bound (section 10, *decision for the review*).
- **A device that holds a record** accepts `deployment.provision` only when signed by a current member. An unsigned re-send of a byte-identical `deployment` object returns the stored read-back; anything else unsigned is `unauthenticated`.
- **Order and commit.** Checks run in the order above, then section 5 on the set. One commit, and the answer is read back from storage. An equal `provision_revision` with identical content is `duplicate`; a lower one is `stale_revision`.

### 6.2 `deployment.authority_update`

A newer authority set, with any rotations before it, for a device that holds a record. It is DAK-signed itself, so it needs no manager signature and no `job_id`, and any caller that can reach the interface may deliver it. The device rate-limits it to protect flash. Section 5 applies.

### 6.3 `deployment.get`

An open read: the stored set and its signature, `deployment_id`, the epoch and revisions. It reveals nothing secret. The same set is published retained at `mecon/<device_id>/authority` over MQTT.

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
| — | a state-changing operation carries `job_id` | `unauthenticated` |
| 7 | replay (section 8) | `replay` |

- The body is parsed only after check 3, so nothing unsigned is ever parsed past the wrapper.
- A device whose record is corrupt answers every request except the recovery ones (section 11) with `deployment_record_corrupt`.
- A request refused before execution consumes no `job_id` and moves no watermark.
- Every refusal increments `auth_rejections` in `status.deployment`. A Reader's single unsigned readiness probe on attach is answered and is not counted.

## 8. Replay

- **Windows.** One sliding window of 24 per member per job class. The classes are `cmd` (operations that are not settings) and `settings` (`config.apply`). The record keeps room for 4 classes.
- **State.** The state is the highest accepted `job_id` and a 24-bit map of the ids below it. A new id runs, a duplicate inside the window is answered from the result cache (never run again), and an id below the window is refused as `replay`. Id 0 is never valid.
- **At most once.** The watermark is persisted **before** the request runs. A power cut between the write and the run loses the request: its manager sees no result and signs it again under a new `job_id`. The other order would let a redelivered request run twice.
- **No adoption.** A run of out-of-window ids that verify can only be old signed requests replayed in order, so a device never adopts a "restarted" sequence. A manager whose sequence really restarted is re-admitted with a higher `admitted_revision`. That empties its watermarks, and check 6 refuses everything it signed before.
- The per-member state belongs to the member's current `admitted_revision`, and section 5 step 7 decides what survives a set change.

## 9. Caller kinds

#95 adds one principal to the [CLI privilege table](../CLI.md): **signed**, a request that passed section 7 on any transport. The table below is how unsigned callers change once a device holds a record, and it is the documented transition from the standalone postures ([Standalone postures](../STANDALONE_POSTURES.md)), not a silent change of what a posture means. Standalone devices (no record) keep today's table unchanged.

| Caller | No record (standalone) | Record, Normal | Record, Hardened | Record corrupt |
|---|---|---|---|---|
| signed | not applicable: nothing to verify against (`unauthenticated`) | per class: read, write, reboot, posture (with a controlled reboot), and the reset scopes section 11 allows | the same | `deployment_record_corrupt` |
| USB cable, unsigned | today's table | reads, native messaging, and the first-use and recovery operations of section 11; **no configuration writes** | identification, and the local-unlock handshake | identification; `deployment.reset` |
| BLE paired (own PIN), unsigned | today's table | native messaging and reads; no configuration writes | BLE is not started | reads |
| MQTT, TLS verified, unsigned | today's table | reads (`status.get`, `config.schema`, `deployment.get`); state changes refused `unauthenticated` | the same | reads; else `deployment_record_corrupt` |
| local unlock (section 10) | not applicable | as signed, for reads and to unlock the posture | the management surface | `deployment.reset` only |
| LoRa remote admin (native) | today's table | reads only | reads and `reboot` | reads |

The table is the proposal for the review (section 15). Its point is that a transport never becomes authority: once a device belongs to a Deployment, changing it needs a member's signature or someone physically present.

## 10. Local unlock

A device entering Hardened while it holds a record must first hold a **local unlock secret**, so that a Reader can always unlock it over USB.
- The secret is written once over USB and stored only as its hash.
- It is never the provisioning credential, never readable, and never valid over any other transport.
- The unlock is an HMAC challenge over USB from a Reader that holds the secret.

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
| `identity.rotate` (new owner) | node key, contacts, message queue | configuration, record, channels, posture | signed; refused without a readable record |
| factory reset | everything, the record and `device_id` included | nothing | the cable only; never remote |

Rules:
- Each operation removes exactly what it names.
- A factory reset regenerates `device_id`. After erase the device is a new device, so a manager cannot re-attach old state to it. *(Decision for the review.)*
- Moving to a different Deployment is unenrollment followed by a first provisioning, which needs physical presence again.
- Rotation runs in this order: generate, save, swap, drop contacts and queue, then one self-advert. The device reports the old and the new public key.

## 12. Keys, randomness and secrets

- **Device keys and secrets** come from the hardware RNG, through the platform's cryptographic RNG: the node key, the local unlock challenge, the BLE PIN and `device_id`.
- **The DAK and manager keys** never exist on a device. Only public keys and signatures do.
- **The node private key** leaves a device only under the custody rule in [Security and authority](SECURITY_AND_AUTHORITY.md):
  - before provisioning, over a USB cable;
  - after provisioning, by a signed request answered only to the requester's channel.
  It never leaves in Hardened.
- **Never in any status, event, log, audit entry or screen telemetry stream:** Wi-Fi and broker passwords, the broker user, private keys, the local unlock secret, channel secrets. The screen stream is private because a frame can show message text and the pairing PIN. It is sent only on a signed `sub.start`, never retained and never redacted (pixels cannot be redacted reliably), so access control is its protection.

## 13. Error codes

`unauthenticated`, `unsupported_auth_version`, `unknown_issuer`, `issuer_revoked`, `bad_signature`, `wrong_deployment`, `wrong_device`, `authority_revision_ahead`, `stale_authority`, `replay`, `payload_too_large`, `deployment_record_corrupt`, `bad_authority_signature`, `bad_authority_set`, `stale_revision`, `untrusted_authority_key`, `unsupported_contract_version`, `bad_broker_roles`, `bad_deployment_name`, and `duplicate` (an acknowledgement, not a refusal). An unknown code is a refusal.

## 14. Product profiles

A product built on this contract may add fields to the `deployment` object and its own job classes. These must not weaken any check above, and the profile must say which fields it adds. MECON's profile is [#118](https://github.com/hoejriis/mecon-firmware/issues/118); its outage sync-key generation and its migration of legacy job sequences are defined there, not here. A generic device ignores profile fields it does not implement.

## 15. Open for the security review

1. **First provisioning.** Which callers may provision unsigned. Proposed: the USB cable in either posture, and BLE paired with the device's own PIN through a Reader. Never the TCP port, a default-PIN link, an unverified broker or LoRa. Should a verified broker profile flagged for management (the flag is set only locally) also count?
2. **Unsigned configuration writes over USB in Normal once enrolled.** Proposed: refused (section 9).
3. **One `iface` suffix** for all operations, against one suffix per operation.
4. **Remote erase:** never (section 11).
5. **Boot counter save failure:** sign nothing (section 3.4).
6. **`device_id`** regenerated at factory reset.
7. **The provisioned marker** (section 4).
8. **No sequence adoption** for signed managers (section 8).
9. **LoRa:** #115 carries this same wrapper, so the size bound and fragmentation there must keep check 3 over the reassembled body. A route, proxy or channel key adds nothing.

## 16. Conformance

- Public test vectors: `test/fixtures/deployment_authority_public_v1.json`, generated by `tools/mecon_auth_vectors.py` from this page. Both land with the implementation branch. Every key is derived from a public label, so these are test keys and valid nowhere. The vectors cover two authority sets, a rotation, a signed request and a signed event. `--check` fails if the fixture drifts from the generator.
- Unit tests cover:
  - every check of sections 5 and 7, in order;
  - the record framing under damage of either copy and a power cut at each step;
  - the membership and re-admission rules;
  - replay at the window edges and across a reboot.
- Simulated-board tests over every transport follow when the roles enforce the record.
