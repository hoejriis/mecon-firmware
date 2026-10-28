# Second review of the authority layer: decisions

Outcome of the second security review (2026-10-25) of the device-side authority layer. This page records what was decided
for each finding. It describes no exploit detail. The contract itself (AUTHORITY.md) is not changed by this page; where a fix
is stricter than, or differs from, the contract, the contract needs a matching edit, listed under "Contract follow-ups".

## Fixed

| Id | Summary | Decision | Commit |
|---|---|---|---|
| H1 | The Companion rescue console's file commands could read or delete the record and identity files | Fixed: refused with an explicit reply while a record exists, and in Hardened with none | 188dea40 |
| H2 | The nRF52 crypto RNG could wait forever when the SoftDevice was not enabled | Fixed: bounded polling, checked errors, the RNG peripheral when the SoftDevice is off, the caller's RNG on failure | a68b0d1e |
| M1 | An unsigned first provisioning over BLE could not tell a device-drawn PIN from a static or chosen one | Fixed: the BLE path is removed; the cable is the only unsigned first provisioning | 65ebb79d |
| M2 | An enrolled Hardened device could not be lowered; Hardened was allowed without a local unlock secret | Fixed: a signed request may lower the posture; Hardened with a record needs a secret in it | 5d6efb38 |
| L1 | A damaged marks copy fell back silently to the older one | Fixed: every member is blocked until re-admitted, and `marks_fallback` is reported | f02dfeca |
| L2 | The stock-array rescue rebuild restored the provisioned marker late | Fixed: restored right after the format | 7d517312 |
| L3 | The local unlock secret could travel in the clear over any transport | Fixed: accepted only over the USB cable (`usb_only`) | 7846a1ca |

## Not fixed in this slice

| Id | Summary | Decision |
|---|---|---|
| L4 | A USB unlock outlives a change of cable user until it expires | Deferred: follow-up "clear the unlock on a USB line-state change and shorten the window" |
| L5 | An unlock in Normal opens the whole posture class, not only the posture command | Deferred: follow-up "narrow the unlocked cable to `set posture`", with the contract wording |
| L6 | The Ethernet console admin keeps `reboot` on an enrolled server | Deferred: follow-up "decide the Ethernet console's row in the caller table" |
| L7 | The highest `job_id` value is the blocked-mark sentinel | Deferred: follow-up "refuse that `job_id` and say so in section 8"; small, status-only effect |
| L8 | Entropy of the ESP32 RNG on server roles that run no radio | Deferred: follow-up "enable the bootloader random source around boot-time draws, or mix in radio noise" |
| I1 | The duplicate-result cache key uses a truncated instance id | Deferred: follow-up "use the full id or the member index" |
| I2 | The record is stored in plaintext on flash, like the node key | Accepted: same protection class as the node key; the contract wording "stored as a secret" is to be clarified |
| I3 | The unlock lockout counter lives in RAM | Accepted: the secret is 256 bits, so a reset of the counter buys nothing |
| I4 | The unlock HMAC has no domain prefix | Accepted: challenges are random per link; a prefix is a possible later hardening |
| I5 | A server `region load` in progress consumes later console lines | Deferred: follow-up "run the front door before a multi-line load consumer" (predates the branch) |
| I6 | Non-canonical point encodings are not rejected in the weak-key check | Accepted: replay protection keys on `job_id`, not on signature uniqueness |
| I7 | A corrupt record's salvaged unlock secret is trusted on the frame CRC | Accepted: planting one needs flash write access, which has stronger options already |

## Contract follow-ups

- Section 6.1: BLE no longer counts for an unsigned first provisioning (M1).
- Sections 9 and 11: the rescue console's file commands (H1), a signed request lowering the posture (M2), and the
  Ethernet console row (L6) belong in the caller table.
- Section 4: the marks fallback flag and its effect (L1).
- Sections 6.1 and 10: `usb_only` for the unlock secret (L3).
- Section 10: Hardened with a record needs the secret, reported as `no-unlock-secret` (M2).

## Review 3: signed events and results, identity.rotate (2026-10-28)

Outcome of the third review of the branch `feat/authority-95-provenance` (PR 132): 1 high, 4 medium, 6 low, 7 info. No exploit
detail here. Each fix landed as its own commit with a test that fails without it and a named mutant (`sim/mutate.sh`)
that the test kills.

| Id | Summary | Decision |
|---|---|---|
| H1 | The new key's only proof was one result that could not be fetched again | Fixed: the old key signs a rotation statement before the swap, `identity.rotation` and `deployment.get` serve it, and AUTHORITY.md section 11 says how a manager that lost the result verifies it. Mutants `rot_statement_signed_by_new_key`, `rot_stale_statement_kept`, `rot_statement_domain`, `rotation_not_a_plain_read`, `rotate_result_signed_with_new_key` |
| M1 | A power cut between the key save and the drop left the old owner's contacts with the new key | Fixed: the statement is drop-pending until the drop ends; the boot finishes it (power-cut sim tests at the key save and at the first drop step). Mutants `rot_pending_drop_not_finished`, `rot_pending_never_cleared` |
| M2 | The native signing command signed `MECON-` bytes | Fixed: refused (`ILLEGAL_ARG`) when the data starts with `MECON-`, enrolled or not. Mutant `native_signer_signs_mecon_bytes` |
| M3 | A signed result was not bound to its request | Fixed: results carry `op` and, for a signed request, `job_id`, inside the signed text; the manager's freshness rule is now written in 3.4. Mutants `prov_result_names_no_op`, `prov_result_names_no_job` |
| M4 | Unauthenticated input drove signing with no limit, and its cost was unmeasured | Fixed for refusals: at most 8 signed refusals a second (burst 8), beyond it unsigned with `"unsigned":"rate"`. Other results and events are always signed. Cost measured on the sim host: 12.4 µs per signature (see below). Mutant `prov_refusal_budget_unlimited`. Not done: measuring on a target MCU, moving observation signing out of the receive callback, and avoiding the per-signature allocation (deferred, below) |
| L1 | `usig_boot` can repeat after `/mecon_dev` is lost while the key and record persist | Deferred: needs `device_id` (or an epoch floor in a second place) in the signed prefix, a format change for managers; to be decided with the contract owner before the first release |
| L2 | The reset result, and every message after an unenrolment, was unsigned | Fixed for the reset result: whether to sign is decided when the request begins. Messages after the unenrolment stay unsigned by design (no record). Mutant `prov_reset_result_unsigned` |
| L3 | Key hygiene: elidable wipes, `fresh` not wiped on the storage path, the old key left in flash | Fixed: a volatile-loop wipe (`Wipe.h`) at every key wipe of the rotation and the retired-key holder, and `fresh` is wiped on every exit. Accepted: the temporary of `LocalIdentity(&r)` is not wiped (a stack temporary the type's constructor owns), and flash remanence (written in section 12) |
| L4 | The RNG check caught only an all-zero source | Fixed: at least 16 distinct byte values and not equal to the previous draw. The sim's RNG cannot be made to fail, so this refusal is untested there (deferred with I7) |
| L5 | A result in the last ~146 bytes below the buffer limit fails after the operation ran | Deferred: operations need a cap reduced by the signing overhead (now also `op` and `job_id`, about 30 bytes more); to be done per operation with the result-size work. Today the failure is a signed `busy` `too-large-to-sign` |
| L6 | An event whose signing fails disappeared without trace | Fixed: counted in `dropped`, which the next observation reports |
| I1 | Reconstruction: strip the trailing member | Fixed in 3.4 (wording) |
| I2 | Reserve `result` as a stream name | Fixed in 3.4 |
| I3 | Signing adds no secret | Accepted: nothing to do |
| I4 | A corrupt-record device signing gives nothing | Accepted: nothing to do |
| I5 | "Signed by the node key" is shown by code, not exercised | Accepted: the node key is never consulted as an issuer; no test can sign as the node key against the layer |
| I6 | Memory safety of the signing buffers | Accepted: no overflow found. The tail buffer grew to 160 bytes for `op` and `job_id`, still bounded by `snprintf` and a length check |
| I7 | Untested paths | Partly fixed: a power cut at each rotation step (key save, drop) is tested. Deferred: `r == 2` (contacts not dropped), the `rng` and `storage` refusals, a result near the size limit, the observations stream's signature, `seq_exhausted` |

Signing time: 12.4 µs per Ed25519 signature of a 250-byte message (20,000 signatures, `lib/ed25519`, gcc -O2, aarch64
container on the sim host, 2026-10-28). Not a target figure: the nRF52840 and ESP32 are expected to be in the millisecond
range, which is why the refusal budget exists.

Not established by this review: the signing time on a target MCU, and whether the ESP32 MQTT worker task ever calls `dispatch()`
or `sign()` (it appears to handle connects only).
