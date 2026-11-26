#!/usr/bin/env python3
"""The reference manager for signed firmware updates over LoRa (docs/contract/LORA_UPDATES.md sections 3 to 7 and 11).

    lora_ota_manager.py --gateway /dev/ttyACM0 --node <target node key, 64 hex> --image app.bin --manifest app.manifest.json
    lora_ota_manager.py ... --resume          the same manifest offered again with a later window
    lora_ota_manager.py ... --cancel          end the job and erase the target's slot
    lora_ota_manager.py vectors > test/fixtures/lora_updates_v1.json     write the fixtures
    lora_ota_manager.py check test/fixtures/lora_updates_v1.json         recompute and compare

It talks to a Companion over USB (Companion frames, tools/mecon_iface.py), which relays between it and the target
(`rf.request`, `rf.bulk`, `rf.status`, and the `rf` stream). It stages the release manifest in parts, sends a signed
`update.offer` with `source.lora`, builds bulk frames with tools/mecon_lora_bulk.py, paces blocks by the gateway's
`bulk_budget_ms`, reads the block answers, sends only missing frames, pauses at the window's end, resumes with a
same-manifest re-offer, reads progress with a signed `job.get` and confirms.

The gateway is trusted on its cable and is not signed to. The target's requests are signed with the member key in
`--key` (a JSON file {"seed_hex", "issuer", "deployment_id", "authority_revision"}; the public test key by default,
valid on no real device). Neither the key nor the transfer token is ever printed.

The control requests (staging, offer, job reads) go over the radio, signed, through the gateway. `--target-port` sends them over
the target's own USB interface instead."""
import argparse
import base64
import hashlib
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "sim"))
import ed25519_ref  # noqa: E402  (pure Python RFC 8032, the reference the sim suite checks against the vectors)
import mecon_iface  # noqa: E402
import mecon_lora_bulk as bulk  # noqa: E402

PART_BYTES = 176          # text and signature bytes in one update.manifest.part
MAX_PARTS = 4
WINDOW_MAX_S = 12 * 3600  # section 4: window_end at most 12 hours after the offer's issued_at
BLOCK_MAX = 8
EU_NARROW_FRAME_MS = 1460  # section 9: a full bulk frame at EU Narrow, computed
TEST_FIXTURE = os.path.join(ROOT, "test", "fixtures", "deployment_authority_public_v1.json")
SIGN_PREFIX = b"MECON-DEVMSG-v1\x00iface\x00"


class ManagerError(Exception):
    pass


class Refused(ManagerError):
    """The target (or the gateway) answered with a status other than ok."""

    def __init__(self, op, reply):
        super().__init__("%s: %s %s" % (op, reply.get("status"), reply.get("error", "")))
        self.reply = reply


class Stalled(ManagerError):
    pass


# ------------------------------------------------------------------ signing

class Signer:
    """Signs requests as a Deployment member (docs/contract/AUTHORITY.md section 3.3)."""

    def __init__(self, seed, issuer, deployment_id, revision=1, job_base=None):
        self._seed = seed
        self.issuer, self.deployment_id, self.revision = issuer, deployment_id, revision
        # job ids rise strictly per member (section 8); the clock is a base no earlier run of this tool can have passed
        self._job = (int(time.time()) if job_base is None else job_base) - 1

    @classmethod
    def public_test(cls, **kw):
        v = json.load(open(TEST_FIXTURE))
        return cls(bytes.fromhex(v["keys"]["instance a"]["seed_hex"]), v["instance_ids"]["a"], v["deployment_id"], **kw)

    @classmethod
    def from_file(cls, path, **kw):
        k = json.load(open(path))
        return cls(bytes.fromhex(k["seed_hex"]), k["issuer"], k["deployment_id"], k.get("authority_revision", 1), **kw)

    def next_job(self):
        self._job += 1
        return self._job

    def wrap(self, node_key, op, args=None, job=None, rid=None, issued_at=0):
        """A signed request: the body is signed as bytes and travels as the wrapper's `body` string."""
        body = {"v": 1, "id": rid or "m-%s-%s" % (job, op), "op": op}
        if args is not None:
            body["args"] = args
        body.update({"issuer": self.issuer, "deployment_id": self.deployment_id, "authority_revision": self.revision,
                     "issued_at": issued_at, "target_public_key": node_key})
        if job is not None:
            body["job_id"] = job
        text = json.dumps(body, separators=(",", ":"))
        sig = ed25519_ref.sign(self._seed, SIGN_PREFIX + text.encode())
        return {"mecon_auth": 1, "issuer": self.issuer, "sig": base64.b64encode(sig).decode(), "body": text}


# ------------------------------------------------------------------ the gateway

class Gateway:
    """A USB-attached Companion as a relay: rf.status, rf.bulk, rf.request and the `rf` stream (section 6)."""

    def __init__(self, iface, to, renew_s=120.0):
        self.i, self.to, self.renew_s = iface, to, renew_s
        self._renewed = None
        self._answers, self._replies = [], {}
        self.foreign = 0   # events from nodes other than the target

    def renew(self, force=False):
        """The rf stream lasts 300 s of the gateway's time; renewing it writes no mark (AUTHORITY section 7.1)."""
        if force or self._renewed is None or time.time() - self._renewed >= self.renew_s:
            r = self.i.request("sub.start", {"stream": "rf", "lifetime_s": 300})
            if r["status"] != "ok":
                raise Refused("sub.start rf", r)
            self._renewed = time.time()

    def status(self):
        r = self.i.request("rf.status")
        if r["status"] != "ok":
            raise Refused("rf.status", r)
        return r["result"]

    def bulk(self, frames):
        """The raw envelope: `busy` is the gateway's answer for a block that would not fit, not an error here."""
        return self.i.request("rf.bulk", {"to": self.to, "frames": [base64.b64encode(f).decode() for f in frames]})

    def pump(self, timeout):
        """Reads at most one message, sorting `rf` events into answers and replies."""
        while self.i.events:
            self._take(self.i.events.pop(0))
        m = self.i.read_message(timeout)
        if m is not None and "ev" in m:
            self._take(m)

    def _take(self, e):
        if e.get("ev") != "rf":
            return
        d = e["data"]
        if d.get("from") != self.to:
            self.foreign += 1
        elif d.get("answer") is not None:
            self._answers.append(base64.b64decode(d["answer"]))
        elif d.get("xid") is not None:
            self._replies[d["xid"]] = d

    def answers(self):
        """The block answers heard from the target since the last call, as the bytes that came."""
        got, self._answers = self._answers, []
        return got

    def request(self, message, timeout):
        """`rf.request` point to point; the reply's text, or None when none came in time (the same xid asks again)."""
        r = self.i.request("rf.request", {"to": self.to, "message": message})
        if r["status"] != "ok":
            raise Refused("rf.request", r)
        xid = r["result"]["xid"]
        end = time.time() + timeout
        while time.time() < end:
            self.renew()
            self.pump(min(1.0, max(0.05, end - time.time())))
            if xid in self._replies:
                d = self._replies.pop(xid)
                if "message" not in d:
                    raise ManagerError("the reply was not delivered: %s" % d.get("error"))
                return d["message"]
        return None


# ------------------------------------------------------------------ control links

class Control:
    """How requests reach the target. signer=None sends plain requests (a device with no Deployment record)."""

    def __init__(self, node_key, signer=None, prefix="m"):
        self.node_key, self.signer, self.prefix = node_key, signer, prefix
        self._n = 0
        self.history = []   # (op, args) of every request, for a caller that wants to see what was asked

    def _build(self, op, args, issued_at, read):
        """(signed wrapper or None, request id, job id): a signed write has a fresh job id; a plain read has none."""
        self._n += 1
        self.history.append((op, args))
        rid = "%s-%d" % (self.prefix, self._n)
        if self.signer is None:
            return None, rid, None
        job = None if read else self.signer.next_job()
        w = self.signer.wrap(self.node_key, op, args, job=job, rid=rid, issued_at=issued_at)
        return w, rid, job

    def request(self, op, args=None, *, issued_at=0, read=False, rid=None, timeout=None):
        r = self._once(op, args, issued_at, read, rid, timeout)
        if self.signer is not None and not read and r.get("status") == "refused" and r.get("error") == "replay":
            # After a reboot of the target its mark stands up to 256 ids ahead of the last one run (AUTHORITY section 8):
            # the next id is above the mark it reports.
            mark = (r.get("result") or {}).get("mark")
            if mark is None:
                return r
            self.signer._job = max(self.signer._job, int(mark))
            r = self._once(op, args, issued_at, read, rid, timeout)
        return r

    def _once(self, op, args, issued_at, read, rid, timeout):
        raise NotImplementedError


class DirectControl(Control):
    """The target's own interface (USB or a console), as the sim tests drive it."""

    def __init__(self, iface, node_key, signer=None, prefix="m"):
        super().__init__(node_key, signer, prefix)
        self.i = iface

    def _once(self, op, args, issued_at, read, rid, timeout):
        timeout = timeout or 10.0
        w, wid, job = self._build(op, args, issued_at, read)
        if w is None:
            return self.i.request(op, args, rid=rid or wid, timeout=timeout)
        self.i.send_raw(json.dumps(w, separators=(",", ":")).encode())
        r = self.i.wait_result(wid, timeout)
        _check_echo(r, op, job)
        return r


class LoraControl(Control):
    """Signed requests over the radio, point to point, through the gateway's rf.request (LORA_MANAGEMENT)."""

    def __init__(self, gateway, node_key, signer, prefix="m", attempts=3, reply_timeout=60.0):
        super().__init__(node_key, signer, prefix)
        self.gw, self.attempts, self.reply_timeout = gateway, attempts, reply_timeout

    def _once(self, op, args, issued_at, read, rid, timeout):
        w, wid, job = self._build(op, args, issued_at, read)
        text = json.dumps(w, separators=(",", ":"))
        for _ in range(self.attempts):   # the same bytes again: a request that ran gets its stored result back
            msg = self.gw.request(text, timeout or self.reply_timeout)
            if msg is not None:
                r = json.loads(msg)
                _check_echo(r, op, job)
                return r
        raise TimeoutError("no reply to %s over the radio" % op)


def _check_echo(r, op, job):
    """A result names what it answers (AUTHORITY section 3.4): one that does not is not an answer."""
    if job is not None and r.get("status") == "ok" and (r.get("op") != op or r.get("job_id") != job):
        raise ManagerError("a result that does not name %s job %s: %s" % (op, job, {k: r.get(k) for k in ("op", "job_id")}))


# ------------------------------------------------------------------ the manager

def manifest_parts(text, sig_b64):
    """`update.manifest.part` arguments (section 5): the manifest text and its raw signature, cut into parts of at most
    176 bytes, counted from 1."""
    blob = text.encode() + base64.b64decode(sig_b64)
    cut = [blob[i:i + PART_BYTES] for i in range(0, len(blob), PART_BYTES)]
    if len(cut) > MAX_PARTS:
        raise ManagerError("the manifest takes %d parts, more than the %d a target holds" % (len(cut), MAX_PARTS))
    sha = hashlib.sha256(text.encode()).hexdigest()
    return [{"sha256": sha, "part": i + 1, "parts": len(cut), "data": base64.b64encode(c).decode()} for i, c in enumerate(cut)]


class Manager:
    def __init__(self, gateway, control, image, manifest_text, manifest_sig, *, block=BLOCK_MAX, window_s=8 * 3600,
                 issued_at=None, now=time.time, frame_ms=EU_NARROW_FRAME_MS, poll_s=2.0, answer_timeout_s=None,
                 confirm_s=300, max_attempts=6, log=print):
        if not 1 <= block <= BLOCK_MAX:
            raise ValueError("block is 1 to %d" % BLOCK_MAX)
        if not 0 < window_s <= WINDOW_MAX_S:
            raise ValueError("the window is at most 12 hours (section 4)")
        m = json.loads(manifest_text)
        if m["image"]["size"] != len(image) or m["image"]["sha256"] != hashlib.sha256(image).hexdigest():
            raise ManagerError("the image is not the one the manifest names")
        self.gw, self.control, self.image = gateway, control, image
        self.text, self.sig = manifest_text, manifest_sig
        self.sha256 = hashlib.sha256(manifest_text.encode()).hexdigest()
        self.block, self.window_s, self.now, self.frame_ms, self.poll_s = block, window_s, now, frame_ms, poll_s
        self.answer_timeout_s = answer_timeout_s if answer_timeout_s is not None else 30.0 + block * frame_ms / 250.0
        self.confirm_s, self.max_attempts, self.log = confirm_s, max_attempts, log
        # section 5 puts a part's request without a job id; the firmware's authority layer takes only its plain reads
        # (AUTHORITY section 7.1) without one, and update.manifest.part is not among them: a signed part is `unauthenticated`
        self.parts_need_job = True
        self.issued_at = issued_at
        self.window_end = None
        self.job = self.job_no = self.token = self.keys = None
        self.total = self.next = 0
        self.stats = {"frames": 0, "resent": 0, "blocks": 0, "answers": 0, "stale": 0, "bad_answers": 0, "lost": 0,
                      "refused": 0, "paused": 0}

    # -- staging and the offer

    def stage(self, attempts=4):
        """Sends the manifest in parts, in order; a part that gets no answer is sent again."""
        last = None
        for p in manifest_parts(self.text, self.sig):
            for _ in range(attempts):
                try:
                    last = self.control.request("update.manifest.part", p, read=not self.parts_need_job)
                    break
                except TimeoutError:
                    continue
            else:
                raise Stalled("no answer to part %d of %d" % (p["part"], p["parts"]))
            if last["status"] != "ok":
                raise Refused("update.manifest.part", last)
        if "version" not in last["result"]:
            raise ManagerError("the last part did not complete the staging: %s" % last["result"])
        self.log("staged %s (%d parts), preflight %s" % (last["result"]["version"], p["parts"], last["result"].get("preflight")))
        return last["result"]

    def _offer(self, manifest_sha256):
        issued = int(self.now())
        self.issued_at, self.window_end = issued, issued + self.window_s
        args = {"manifest_sha256": manifest_sha256, "confirm": {"within_s": self.confirm_s},
                "source": {"lora": {"block": self.block, "window_end": self.window_end}}}
        if self.control.signer is None:
            args["issued_at"] = issued   # an unsigned request has no signed body to carry it
        r = self.control.request("update.offer", args, issued_at=issued)
        if r["status"] != "ok":
            raise Refused("update.offer", r)
        res = r["result"]
        if res.get("encoding") != "raw" or res.get("frame_bytes") != bulk.BULK_DATA or res["transfer_size"] != len(self.image):
            raise ManagerError("an offer answer this manager does not read: %s" % {k: v for k, v in res.items() if k != "transfer_token"})
        token = bytes.fromhex(res["transfer_token"])
        if self.token is not None and token != self.token:
            raise ManagerError("a resume answered another token: it is not the same transfer")
        self.token, self.keys = token, bulk.keys(token)
        self.job, self.job_no = res["job"], bulk.job_number(res["job"])
        self.total, self.next = res["frames_total"], res["next"]
        self.log("job %s %s: frame %d of %d, window ends %d" % (self.job, res["state"], self.next, self.total, self.window_end))
        return res

    def start(self):
        """Stages the manifest and offers it. A paused job of the same manifest is resumed by the offer."""
        self.stage()
        return self._offer(self.sha256)

    def resume(self):
        """Section 7: the same manifest again with a later window. The token, the job and `next` are the target's."""
        again = self.sha256   # the same manifest, named by its hash: a resume is matched by it
        try:
            return self._offer(again)
        except Refused as e:
            if e.reply.get("error") != "manifest-not-staged":
                raise
        self.stage()   # a reboot of the target loses the staged manifest
        return self._offer(again)

    # -- the transfer

    def _frame(self, index):
        n = bulk.frame_data_len(len(self.image), index)
        return bulk.frame(self.keys, self.job_no, len(self.image), index, self.image[index * bulk.BULK_DATA:index * bulk.BULK_DATA + n])

    def _window_over(self):
        return self.now() >= self.window_end

    def _wait_budget(self, frames):
        """Waits until the gateway's bulk budget holds `frames` frames and its queue has room; False when the window ends."""
        need = frames * self.frame_ms
        while not self._window_over():
            self.gw.renew()
            s = self.gw.status()
            if s["bulk_budget_ms"] >= need and s["queued"] + frames <= 16:
                return True
            time.sleep(self.poll_s)
        return False

    def _send(self, base, offsets):
        """Frames base+o in order, through the budget; False when the window ends first."""
        for i in range(0, len(offsets), BLOCK_MAX):
            part = offsets[i:i + BLOCK_MAX]
            while True:
                if not self._wait_budget(len(part)):
                    return False
                r = self.gw.bulk([self._frame(base + o) for o in part])
                if r["status"] == "ok":
                    break
                if r["status"] == "busy":   # the gateway's own ceiling, more careful than the figures read a moment ago
                    self.stats["refused"] += 1
                    time.sleep(self.poll_s)
                    continue
                raise Refused("rf.bulk", r)
            self.stats["frames"] += len(part)
        return True

    def _await_answer(self, base, n):
        """The `have` mask of the answer to the block at `base`, or None when none came in time."""
        end = time.time() + self.answer_timeout_s
        while time.time() < end:
            self.gw.renew()
            for raw in self.gw.answers():
                got = bulk.check_answer(self.keys, self.job_no, raw)
                if got is None:   # a stranger's, another job's, or a bulk frame where an answer should be
                    self.stats["bad_answers"] += 1
                    continue
                a_base, _next, have = got
                if a_base != base:   # a late answer to an earlier block is never read against this one
                    self.stats["stale"] += 1
                    continue
                self.stats["answers"] += 1
                return have
            self.gw.pump(min(1.0, max(0.05, end - time.time())))
        return None

    def _block(self, base, n):
        """One block to its whole answer. False when the window ended first."""
        sending = list(range(n))
        attempts = 0
        while True:
            if not self._send(base, sending):
                return False
            have = self._await_answer(base, n)
            if have is None:   # the answer was lost: the block again, whole, which gets its answer again
                self.stats["lost"] += 1
                attempts += 1
                if attempts >= self.max_attempts:
                    raise Stalled("no answer for the block at frame %d after %d tries" % (base, attempts))
                self.stats["resent"] += n
                sending = list(range(n))
                continue
            attempts = 0
            missing = [o for o in range(n) if not have >> o & 1]
            self.log("block at %d of %d: %d of %d held" % (base, self.total, n - len(missing), n))
            if not missing:
                return True
            self.stats["resent"] += len(missing)
            # the block's last frame asks for the answer again; it goes last
            sending = missing if n - 1 in missing else missing + [n - 1]

    def run(self):
        """Blocks from `next` to the end. Returns "complete", or "paused" at the window's end."""
        while self.next < self.total:
            if self._window_over():
                return self._pause()
            n = min(self.block, self.total - self.next)
            if not self._block(self.next, n):
                return self._pause()
            self.next += n
            self.stats["blocks"] += 1
        self.log("all %d frames held" % self.total)
        return "complete"

    def _pause(self):
        self.stats["paused"] += 1
        self.log("window ended at frame %d of %d: paused; resume with the same manifest" % (self.next, self.total))
        return "paused"

    # -- progress, confirmation, cancellation

    def progress(self):
        r = self.control.request("job.get", {"job": self.job}, read=True)
        if r["status"] != "ok":
            raise Refused("job.get", r)
        g = r["result"]
        self.log("job %s: %s, next %s of %s%s" % (self.job, g.get("state"), g.get("next"), g.get("frames_total"),
                                                  ", paused: " + g["reason"] if g.get("reason") else ""))
        return g

    def confirm(self, timeout_s=600.0, poll_s=5.0):
        """After the last block the target verifies, activates and attests; then job.confirm."""
        end = time.time() + timeout_s
        while time.time() < end:
            try:
                g = self.progress()
            except (TimeoutError, Refused):
                g = None
            if g and g.get("state") == "awaiting-confirm":
                r = self.control.request("job.confirm", {"job": self.job})
                if r["status"] != "ok":
                    raise Refused("job.confirm", r)
                self.log("job %s: %s" % (self.job, r["result"].get("state")))
                return r["result"]
            if g and g.get("state") in ("failed", "cancelled", "reverted"):
                raise ManagerError("the job ended %s: %s" % (g["state"], g.get("result")))
            time.sleep(poll_s)
        raise Stalled("the target did not reach awaiting-confirm")

    def cancel(self):
        r = self.control.request("job.cancel", {"job": self.job})
        if r["status"] != "ok":
            raise Refused("job.cancel", r)
        self.log("job %s: %s" % (self.job, r["result"].get("state")))
        return r["result"]


# ------------------------------------------------------------------ fixtures (section 11)

FIXTURE_RELEASE_SEED = hashlib.sha256(b"mecon-firmware public test key: release test").digest()
FIXTURE_KEY_ID = "15d0e5c92e53436a"
FIXTURE_ISSUED = 1792800000
FIXTURE_SIZE = 580    # 5 frames of 115 and a last of 5 bytes: a 22-byte frame, an answer's length


def _fixture_manifest(image):
    m = {"v": 1, "key_id": FIXTURE_KEY_ID, "line": "public", "version": "v1.18.0-mecon.99.0.0", "build": "0" * 40,
         "upstream": "v1.18.0", "interface": "1.0", "hardware": "fixture-board", "hardware_rev": ["1"], "role": "companion",
         "variant": "fixture_variant", "layout": 1, "bootloader_min": None, "data_format": 1, "security_version": 1,
         "image": {"size": len(image), "sha256": hashlib.sha256(image).hexdigest()}, "kind": "app",
         "min_from": "v1.18.0-mecon.0.0.0", "downgrade": False, "issued_at": 1790000000}
    return json.dumps(m, separators=(",", ":"))


def vectors():
    v = json.load(open(TEST_FIXTURE))
    node_key = v["keys"]["device node"]["public_key"]
    parts_first = 96   # the parts' job ids, then the offer's 100 and the resume's 101: one rising sequence
    signer = Signer.public_test(job_base=parts_first)
    image = bytes((i * 7 + 3) & 0xFF for i in range(FIXTURE_SIZE))
    text = _fixture_manifest(image)
    sig = base64.b64encode(ed25519_ref.sign(FIXTURE_RELEASE_SEED, b"MECON-RELEASE-v1\x00" + text.encode())).decode()
    sha = hashlib.sha256(text.encode()).hexdigest()
    parts = manifest_parts(text, sig)
    signed_parts = [signer.wrap(node_key, "update.manifest.part", p, job=signer.next_job(), issued_at=FIXTURE_ISSUED) for p in parts]
    assert [json.loads(w["body"])["job_id"] for w in signed_parts] == list(range(96, 96 + len(parts))) and len(parts) == 4
    token = hashlib.sha256(b"mecon-firmware public test token: lora updates").digest()[:16]
    k = bulk.keys(token)
    job = signer.next_job()
    job_id = "#%s:%d" % (signer.issuer, job)
    no = bulk.job_number(job_id)
    assert no == job
    block, window = 2, 8 * 3600

    def offer(job_n, window_end):
        args = {"manifest_sha256": sha, "confirm": {"within_s": 300}, "source": {"lora": {"block": block, "window_end": window_end}}}
        return signer.wrap(node_key, "update.offer", args, job=job_n, issued_at=FIXTURE_ISSUED)

    frames = bulk.image_frames(k, no, image)
    total = len(frames)
    ans = lambda base, nxt, have: bulk.answer(k, no, base, nxt, have)
    a0, a1 = ans(0, 2, 0b11), ans(2, 4, 0b11)
    reflected = bytes([bulk.BULK_MARKER]) + ans(total - 1, total, 1)[1:]   # base at the last index, so only the tag can refuse it
    last_as_answer = bytes([bulk.ANSWER_MARKER]) + frames[-1][1:]
    resume_job = signer.next_job()
    first_end, later_end = FIXTURE_ISSUED + 600, FIXTURE_ISSUED + 3600 + window
    out = {
        "contract": "docs/contract/LORA_UPDATES.md sections 4 to 7 and 11",
        "about": "Signed with the public test keys (test/fixtures/deployment_authority_public_v1.json and docs/contract/release-keys.json); valid on no real device. The token is made up.",
        "image": {"rule": "byte i is (7 * i + 3) mod 256", "size": FIXTURE_SIZE, "sha256": hashlib.sha256(image).hexdigest(),
                  "frames_total": total, "block": block},
        "node_key": node_key, "issued_at": FIXTURE_ISSUED,
        "issuer_public_key": ed25519_ref.public_key(signer._seed).hex(),
        "release_public_key": ed25519_ref.public_key(FIXTURE_RELEASE_SEED).hex(),
        "manifest": {"text": text, "sig_b64": sig, "sha256": sha, "parts": parts, "signed_part_wires": signed_parts},
        "offer": {"job_id": job_id, "job": job, "wire": offer(job, FIXTURE_ISSUED + window),
                  "answer_example": {"job": job_id, "state": "accepted", "transfer_token_hex": token.hex(), "size": FIXTURE_SIZE,
                                     "transfer_size": FIXTURE_SIZE, "encoding": "raw", "next": 0, "frame_bytes": bulk.BULK_DATA,
                                     "frames_total": total}},
        "bulk": {"token_hex": token.hex(), "key_bulk_hex": k[0].hex(), "key_answer_hex": k[1].hex(), "job": no,
                 "frames_hex": [f.hex() for f in frames]},
        # a block answer arrives after the next block started: it names base 0, and the block now open starts at 2
        "stale_answer": {"answer_hex": a0.hex(), "open_block_base": 2, "verdict": "stale-base"},
        "answer_current": {"answer_hex": a1.hex(), "base": 2, "next": 4, "have": 3},
        # a pause at window_end, then a same-manifest resume under a newer job id: the answer names the first job and the
        # frames keep its number; the frames numbered for the resuming request are another transfer's
        "pause_resume": {
            "first_offer_window_end": first_end, "first_offer_wire": offer(job, first_end),
            "resume_job": resume_job, "resume_wire": offer(resume_job, later_end), "resume_window_end": later_end,
            "resume_answer_example": {"job": job_id, "state": "progress", "transfer_token_hex": token.hex(), "next": 4,
                                      "frames_total": total},
            "frames_keep_job_number": no,
            "frame_for_the_resuming_request_hex": bulk.frame(k, resume_job, FIXTURE_SIZE, 4, image[4 * 115:5 * 115]).hex(),
        },
        "frame_cases": {   # what a target does with each (the verdicts of mecon_lora_bulk.check_frame)
            "captured_answer_replayed_as_frame": {"hex": a1.hex(), "verdict": "not-bulk"},
            "captured_answer_marker_changed_as_frame": {"hex": reflected.hex(), "verdict": "bad-tag"},
            "frame_short_before_last": {"hex": (frames[0][:-bulk.BULK_TAG - 1] + frames[0][-bulk.BULK_TAG:]).hex(), "verdict": "bad-length"},
            "frame_long_before_last": {"hex": (frames[0][:-bulk.BULK_TAG] + b"\0" + frames[0][-bulk.BULK_TAG:]).hex(), "verdict": "bad-length"},
            "frame_long_last": {"hex": (frames[-1][:-bulk.BULK_TAG] + b"\0" + frames[-1][-bulk.BULK_TAG:]).hex(), "verdict": "bad-length"},
            "frame_short_last": {"hex": (frames[-1][:-bulk.BULK_TAG - 1] + frames[-1][-bulk.BULK_TAG:]).hex(), "verdict": "bad-length"},
        },
        "answer_cases": {   # what a manager does with each
            "answer": {"hex": a1.hex(), "accepted": True},
            "bulk_frame_as_answer": {"hex": frames[-1].hex(), "accepted": False},
            "bulk_frame_marker_changed_as_answer": {"hex": last_as_answer.hex(), "accepted": False},
        },
    }
    for name, c in out["frame_cases"].items():
        got = bulk.check_frame(k, no, FIXTURE_SIZE, bytes.fromhex(c["hex"]))[0]
        assert got == c["verdict"], (name, got)
    for name, c in out["answer_cases"].items():
        assert (bulk.check_answer(k, no, bytes.fromhex(c["hex"])) is not None) == c["accepted"], name
    return out


# ------------------------------------------------------------------ the command line

def open_companion(port):
    """A Companion on a serial port as a client: waits until it answers CMD_APP_START (opening the port resets an ESP32)."""
    from mecon_iface_hw import serial_transport
    send, read, close = serial_transport(port)
    end = time.time() + 90
    while time.time() < end:
        send(bytes([1, 3]) + bytes(6) + b"lora-ota")
        f = read(2.0)
        while f is not None and f[:1] != b"\x05":
            f = read(0.5)
        if f is not None:
            return mecon_iface.IfaceClient(send, read), close
    sys.exit("the board on %s never answered CMD_APP_START" % port)


def main(argv):
    ap = argparse.ArgumentParser(description="Signed firmware update over LoRa (reference manager).")
    ap.add_argument("--gateway", required=True, help="serial port of the Companion that relays")
    ap.add_argument("--node", required=True, help="the target's node key, 64 hex")
    ap.add_argument("--image", required=True, help="the release image")
    ap.add_argument("--manifest", required=True, help="the signed manifest, as the release's .manifest.json: {manifest, manifest_sig}")
    ap.add_argument("--key", help="signing key file (JSON: seed_hex, issuer, deployment_id, authority_revision); default: the public test key")
    ap.add_argument("--block", type=int, default=BLOCK_MAX, help="frames per block, 1 to 8 (default 8)")
    ap.add_argument("--window-hours", type=float, default=8.0, help="how long from now the target takes frames, at most 12 (default 8)")
    ap.add_argument("--frame-ms", type=int, default=EU_NARROW_FRAME_MS, help="airtime of one full frame at the radio setting, ms")
    ap.add_argument("--target-port", help="serial port of the target itself, for the control requests (staging, offer, job reads)")
    ap.add_argument("--unsigned", action="store_true", help="plain requests: only a target with no Deployment record takes them")
    ap.add_argument("--resume", action="store_true", help="offer the same manifest again with a new window, and go on")
    ap.add_argument("--cancel", action="store_true", help="cancel the job")
    a = ap.parse_args(argv)

    meta = json.load(open(a.manifest))
    image = open(a.image, "rb").read()
    signer = None if a.unsigned else (Signer.from_file(a.key) if a.key else Signer.public_test())
    gw_iface, gw_close = open_companion(a.gateway)
    gw = Gateway(gw_iface, a.node)
    gw.renew(force=True)
    if a.target_port:
        t_iface, t_close = open_companion(a.target_port)
        control = DirectControl(t_iface, a.node, signer)
    else:
        if signer is None:
            sys.exit("the radio carries signed requests only")
        control = LoraControl(gw, a.node, signer)
    m = Manager(gw, control, image, meta["manifest"], meta["manifest_sig"], block=a.block, window_s=int(a.window_hours * 3600),
                frame_ms=a.frame_ms, log=lambda s: print(s, flush=True))
    try:
        if a.cancel:
            m.start()
            m.cancel()
            return 0
        m.resume() if a.resume else m.start()
        if m.run() == "complete":
            m.confirm()
            return 0
        return 3
    except KeyboardInterrupt:
        print("stopped: the job stays open (the target pauses on its own); --resume goes on, --cancel ends it", flush=True)
        return 130
    finally:
        gw_close()


if __name__ == "__main__":
    if sys.argv[1:2] == ["vectors"]:
        print(json.dumps(vectors(), indent=1))
    elif sys.argv[1:2] == ["check"]:
        ok = json.load(open(sys.argv[2])) == vectors()
        print("OK" if ok else "MISMATCH")
        sys.exit(0 if ok else 1)
    else:
        sys.exit(main(sys.argv[1:]))
