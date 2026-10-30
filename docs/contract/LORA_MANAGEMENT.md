# Management over LoRa

> **Status: draft contract, not built ([#115](https://github.com/hoejriis/mecon-firmware/issues/115)).** It carries
> the [device interface](DEVICE_INTERFACE.md) over LoRa, point to point and over the
> [management channel](MANAGEMENT_CHANNEL.md) of #114, under the [Management authority contract](AUTHORITY.md) of #95.
> It defines no new operation: the [managed operations](OPERATIONS.md) of #109 are what travels. Compact telemetry over
> LoRa is #116, and firmware-image bytes over LoRa are #123; neither is defined here. Section 10 lists what is still
> open for review.

## 1. LoRa is a transport

- **The same envelope, the same table.** A request over LoRa is a device-interface request: the same JSON envelope,
  dispatched through the same operation table, decided by the same CLI privilege cell, deduplicated by the same id
  cache and signed watermark. LoRa adds no operation, no permission and no second command model (OPERATIONS
  section 1).
- **One link per path.** The device has a LoRa link beside its console, BLE and MQTT links. Like them it has its own
  outbound queue and its own reassembly, and none of its state survives a change of session (a different peer, or a
  reboot): queued results, a partial message and any unlock are cleared, as for the other links.
- **Two paths.** *Point to point*: an encrypted native datagram between two nodes (section 3.1). *Channel*: a group
  datagram on the device's bound management channel (section 3.2). The same signed request is valid on both, and on
  MQTT, USB and BLE (AUTHORITY section 3.3: the signed bytes name no transport).
- **What is not carried.** Subscriptions and event streams are not carried over LoRa (their airtime is #116's
  concern). `hello` and a result are. Native LoRa remote administration (login, the native request types, CLI text,
  OPERATIONS section 5.4) is unchanged and is not this path.

## 2. Authority

- **Only a signed request runs.** Over LoRa the device accepts only a signed wrapper (`mecon_auth`, AUTHORITY
  section 3.3), checked by section 7 of that contract before anything else. An unsigned envelope is not run on either
  path. On the point-to-point path it is answered `refused`, `unauthenticated`; on the channel it is not answered
  (section 6). A device without a Deployment record therefore runs nothing over this path: there is nothing to verify
  against (AUTHORITY section 9).
- **A channel key, an RF route or a proxy is never authority.** Knowing the management channel's key, being a contact,
  being logged in as a native admin, being one hop away, or relaying for someone grants nothing here. The caller is
  the **signed** principal of AUTHORITY section 9, whatever the path.
- **Equivalent authorization.** Because the principal is the same, an operation over LoRa is allowed exactly when the
  same signed request would be allowed over MQTT, USB or BLE, with the same class, the same per-line check for an
  operation that maps to a CLI line, and the same audit of a refusal.
- **Sensitive operations need a link nonce** (AUTHORITY section 7.1, `channel_nonce`). Over LoRa the nonce belongs to
  the session of section 1. Which sensitive operations are offered over LoRa at all is open (section 10).

## 3. Paths and addressing

The device's `device_id` is never on the air in clear. It is carried only inside the signed body, which travels
encrypted on both paths. A node is addressed by its node public key, as native MeshCore addresses it.

### 3.1 Point to point

- The request travels as the native encrypted request datagram to the target node, keyed by the ECDH secret of the
  two node keys, as a native request to a contact is. The reply travels as the matching response datagram to the
  sender, on the path the request arrived by or by flood when that path is not known.
- The datagram carries a one-byte marker that selects this protocol, so a native request type is never read as a
  management frame and the other way round.
- Native addressing (the destination hash, then decryption with the pairwise secret) selects the target. The signed
  body's `target_public_key` then confirms it on the full key: a request whose key is not this node's is refused
  `wrong_device` (AUTHORITY section 7, check 4b) and never runs.

### 3.2 Channel

- The request travels as a native group datagram (`PAYLOAD_TYPE_GRP_DATA`) on the bound management channel, with a
  data type of its own. A device takes it only on its bound channel, and only while the binding is `bound`
  (MANAGEMENT_CHANNEL section 2): a `stale` or absent binding takes nothing.
- A group datagram names no sender and no recipient, so each frame header carries both, as 8-byte prefixes of the node
  public keys: the target's, and the sender's (for the reply). Every member of the channel can read them; they reveal
  which keys talk, never a `device_id`.
- Only the node whose key matches the target prefix reads further. The signed body's `target_public_key` then
  confirms the full key, as on the point-to-point path. Every other node drops the frame without answering.
- The reply travels on the same channel, addressed to the sender's prefix. Channel members can read it: a result
  never carries a secret (OPERATIONS section 2), and a result whose content the channel's members should not see is
  for the point-to-point path. Which results that excludes from the channel is open (section 10).

## 4. Frames

- **One message, several frames.** A message (one signed request, or one result) is cut into frames that fit one
  LoRa packet: up to 150 bytes of message per frame on the point-to-point path, up to 140 on the channel (the
  addresses take the rest). Each frame carries a message number per sender (`xid`), its index and the frame count,
  as the Companion's `0x7F` frames do (DEVICE_INTERFACE section 7).
- **A smaller bound than the other links.** A message over LoRa is at most 1,024 bytes (8 frames at most). A longer
  one is not sent; a request that would need more is answered `busy`, `too-large`, where an answer is possible. A
  result that would be larger is paged by the operation, as on every link. The bound keeps one message's airtime
  within what a duty-limited band allows (section 7).
- **Never mixed.** A device reassembles one message at a time per peer. A frame of another message from the same peer
  (another `xid`, or another frame count) discards the incomplete one. Parts of two messages are never combined, even
  when they carry the same job: a re-sent request starts again.
- **A gap is reported, not dropped.** A message incomplete when its deadline passes (section 8) is discarded. On the
  point-to-point path the device answers `parts-missing`, with the `xid` and a mask of the frames it holds, so the
  sender re-sends only those. On the channel it sends the same answer to the sender's prefix only, and only once per
  message.
- **A repeated frame is ignored**, so a frame heard twice (a flood, a retry) costs nothing.

## 5. Results and correlation

- **Correlated on the job, not the transport.** A signed request's result carries its `id`. A request that carries a
  `job_id` (every one but the plain reads of AUTHORITY section 7.1) also gets the job's identity,
  `#<issuer>:<job_id>`, with the issuer's first 12 hex digits (the identity the job table already keys on).
- **A refusal is correlated too.** A request refused after its signature verified (check 3 of AUTHORITY section 7:
  wrong device, replay, busy, a class the caller lacks) carries the job's identity, read from the verified body,
  never by scanning the wrapper's text. A request refused before that (too large, not a wrapper, an unknown issuer, a
  bad signature) has no verified body. Its answer carries the frames' `xid` and the wrapper's `issuer` only, so the
  manager can match it to the message it sent.
- **A retry is a retry, on any path.** A manager that gets no answer re-sends the same signed request, unchanged, on
  the same path or another (AUTHORITY section 8). It gets the stored result, or `duplicate`, or the job's current
  state. A stored id belongs to the operation that produced it (DEVICE_INTERFACE section 3), so a retry under another
  operation is `id-in-use`. Nothing runs twice.
- **No answer, refused and done are different.** A manager that gets nothing does not know whether the request
  arrived. It re-sends, and the answer tells it. `refused`, `invalid`, `unknown-op`, `unsupported-version` and `busy`
  guarantee that nothing ran, as on every link. A job's terminal result is owed (OPERATIONS section 3.2) and is
  fetched with `job.get` over any path if the LoRa reply was lost.

## 6. Replay

- AUTHORITY section 8 applies unchanged: one strictly increasing `job_id` sequence per member, persisted before the
  request runs, shared by every transport.
- **A watermark is never reset by a reboot, a route change, a new LoRa session or a change of path**, and a device never
  adopts a sequence that "restarted" below its mark, however many verified ids arrive in a row. A manager that lost its
  sequence reads its mark or is re-admitted (AUTHORITY section 8).
- A frame that is reassembled after a reboot does not exist: reassembly lives in RAM and is cleared by the reboot
  (section 1), so a half-received request never completes after it.

## 7. Airtime

- **Through the duty window.** Every frame the device sends on this path goes through the Dispatcher's sliding-hour
  duty window like any other transmission ([UPSTREAM_DELTA](../UPSTREAM_DELTA.md)), at the regulatory floor of the
  channel in use.
- **Deferred is a state, never a silent drop.** A reply the duty window holds is queued with its deadline. It is sent
  when the window allows, or dropped at its deadline and counted. It is never lost without a trace. The manager's
  re-send then gets the stored result. Every frame of a reply is the same message, so a window that holds frame 3
  holds the rest behind it, and frames of two messages never interleave.
- **Control is not starved by bulk.** No budget separate from the duty window applies to this path. Later bulk work on
  LoRa (#123's image bytes) must not be able to spend the airtime that a reply, a `parts-missing` or a revert needs.
- **No storms on a group channel.** On the channel only the addressed node ever answers. There is no "who is X"
  broadcast that every member answers, no acknowledgement of frames that are not for this node, and no answer to an
  unsigned or wrongly addressed frame. A device sends at most one `parts-missing` per message.
- **One request at a time per member** (AUTHORITY section 8). The device runs at most one state-changing job at a time
  (OPERATIONS section 3.3), so a busy channel cannot queue work faster than it is answered.

## 8. Every state ends

Each LoRa management state has a deadline, after which it returns to idle and frees its memory:

| State | Deadline | Then |
|---|---|---|
| a message being reassembled | 60 s from its first frame | discarded; `parts-missing` (section 4) |
| a reply held by the duty window | 10 min | dropped and counted; the result stays owed or cached |
| a LoRa session (peer, queue, nonce) | 15 min without traffic | cleared, as at a change of session |

A finished job leaves nothing behind on the LoRa link: its result is cached or owed (sections 5 and OPERATIONS 3.2),
and the link's buffers are freed. The figures are to be confirmed against measured airtime on the slowest preset in
use (section 10).

## 9. Conformance

- The same signed operation runs over the point-to-point path and over the channel, with the same result and the same
  effect, and is refused alike for the same caller.
- None of these runs a request twice: a wrong target or issuer, a replay, missing or reordered frames, a lost reply,
  a reboot between request and retry, a retry over the other path.
- A refused request's answer carries its job identity. A request that cannot be reassembled gets `parts-missing`
  where an answer is allowed.
- On the channel, only the addressed node transmits in answer to a request, in every test.
- A reply the duty window holds is sent later or counted as dropped, never lost silently. Every state of section 8
  returns to idle by its deadline.
- Software conformance in the simulator for every role that carries it; real-board evidence on the boards of #117.

## 10. Open for review

1. **Which operations over LoRa.** Every operation the signed principal may run, or a smaller set (no secret-class
   operation, no `deployment.*` over the channel)? The sensitive operations of AUTHORITY section 7.1 need a link nonce;
   whether the nonce handshake is offered over LoRa at all, or those operations stay off it, is to be decided.
2. **Which results the channel may carry.** Section 3.2 sends results on the channel, readable by its members. A
   result that names other nodes (`contact.list`) may belong to the point-to-point path only.
3. **The frame sizes and the 1,024-byte bound** (section 4) are first estimates against MeshCore's 184-byte payload,
   the pairwise MAC and the channel datagram's header. They are fixed by measuring a real signed `config.apply` and its
   result.
4. **The deadlines** of section 8 are to be confirmed against airtime on the slowest preset in use, with the duty
   window's boot seeding taken into account.
5. **Roles.** The Companion carries both paths once #114's binding and #109's job table are in. The Repeater and Room
   server have no channel table (MANAGEMENT_CHANNEL's status) and would carry the point-to-point path only, until #151.
