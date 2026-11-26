"""tools/lora_ota_manager.py against a model of the target and the gateway (docs/contract/LORA_UPDATES.md sections 3 to 7 and
11), and the fixtures test/fixtures/lora_updates_v1.json read back through the Python tool. The model keeps the rules the
manager relies on and nothing else; sim/tests/test_lora_ota_manager.py runs the same manager against the real firmware.
The native test test/test_lora_updates reads the same fixtures through the firmware's codec."""
import base64
import hashlib
import json
import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(HERE, ".."))
import lora_ota_manager as lom  # noqa: E402
import mecon_lora_bulk as bulk  # noqa: E402

FIXTURE = os.path.join(ROOT, "test", "fixtures", "lora_updates_v1.json")
FRAME_MS = 100


def image_of(n):
    return bytes((i * 7 + 3) & 0xFF for i in range(n))


def manifest_of(img, tag=""):
    return lom._fixture_manifest(img) + tag   # a tag after the JSON is never parsed by the model: only its hash differs


class Target:
    """What the manager relies on in a target: a staged manifest, an offer that answers a token, frames written in order,
    one answer per block, a resume that keeps the job and token."""

    def __init__(self, img, sha, block_ref=None):
        self.size, self.sha = len(img), sha
        self.token = hashlib.sha256(b"model").digest()[:16]
        self.k = bulk.keys(self.token)
        self.job = "model-1"
        self.no = bulk.job_number(self.job)
        self.total = bulk.frames_total(self.size)
        self.held, self.staged, self.block, self.start = set(), set(), 8, 0
        self.state, self.window_end, self.offers = None, 0, 0
        self.parts = {}
        self.log = []   # every frame index the target heard, in order

    def next(self):
        n = 0
        while n in self.held:
            n += 1
        return n

    def control(self, op, args):
        if op == "update.manifest.part":
            self.parts[args["part"]] = args
            if len(self.parts) == args["parts"]:
                self.staged.add(args["sha256"])
                return {"status": "ok", "result": {"sha256": args["sha256"], "version": "v", "preflight": "ok"}}
            return {"status": "ok", "result": {"held": len(self.parts)}}
        if op == "update.offer":
            if args["manifest_sha256"] not in self.staged:
                return {"status": "refused", "error": "manifest-not-staged"}
            lora = args["source"]["lora"]
            if self.state is not None and lora["window_end"] <= self.window_end:
                return {"status": "invalid", "error": "bad-args"}
            self.offers += 1
            self.state, self.window_end, self.block = "progress", lora["window_end"], lora["block"]
            if self.offers == 1:
                self.start = 0
            self.start = self.next()
            return {"status": "ok", "result": {"job": self.job, "state": "accepted" if self.offers == 1 else "progress",
                                               "transfer_token": self.token.hex(), "size": self.size,
                                               "transfer_size": self.size, "encoding": "raw", "next": self.start,
                                               "frame_bytes": 115, "frames_total": self.total}}
        if op == "job.get":
            return {"status": "ok", "result": {"job": self.job, "state": self.state, "next": self.next(), "frames_total": self.total}}
        if op == "job.cancel":
            self.state = "cancelled"
            return {"status": "ok", "result": {"job": self.job, "state": "cancelled"}}
        if op == "job.confirm":
            return {"status": "ok", "result": {"job": self.job, "state": "confirmed"}}
        raise AssertionError(op)

    def hear(self, f):
        """One bulk frame; the answer if it is the last of its block."""
        verdict, index, _data = bulk.check_frame(self.k, self.no, self.size, f)
        self.log.append((index, verdict))
        if verdict != "ok":
            return None
        self.held.add(index)
        base = self.start + (index - self.start) // self.block * self.block
        last = min(base + self.block, self.total) - 1
        if index != last:
            return None
        have = sum(1 << (i - base) for i in range(base, last + 1) if i in self.held)
        return bulk.answer(self.k, self.no, base, self.next(), have)


class Iface:
    """The gateway's interface as the manager's Gateway class reads it, with hooks a test sets to break the channel."""

    def __init__(self, target, budget_ms=10 ** 6, refill_ms=0):
        self.t, self.events = target, []
        self.budget, self.refill = budget_ms, refill_ms
        self.calls = []            # (frames indices, budget the manager had last read)
        self.drop = lambda index, n_sent: False      # frame lost on its way
        self.drop_answer = lambda n_answers: False   # answer lost on its way
        self.on_answer = lambda raw: [raw]           # what the manager hears for one answer
        self.reorder = False
        self.read_budget = None
        self.busy = 0
        self.n_answers = 0
        self.sent = 0

    def request(self, op, args=None, rid=None, timeout=5.0):
        if op == "sub.start":
            return {"status": "ok", "result": {}}
        if op == "rf.status":
            self.budget += self.refill
            self.read_budget = self.budget
            return {"status": "ok", "result": {"queued": 0, "bulk_budget_ms": self.budget}}
        assert op == "rf.bulk", op
        frames = [base64.b64decode(f) for f in args["frames"]]
        if self.budget < len(frames) * FRAME_MS:
            self.busy += 1
            return {"status": "busy", "error": "bulk-budget"}
        self.budget -= len(frames) * FRAME_MS
        self.calls.append(([bulk.check_frame(self.t.k, self.t.no, self.t.size, f)[1] for f in frames], self.read_budget))
        order = list(reversed(frames)) if self.reorder else frames
        for f in order:
            self.sent += 1
            idx = bulk.check_frame(self.t.k, self.t.no, self.t.size, f)[1]
            if self.drop(idx, self.sent):
                continue
            a = self.t.hear(f)
            if a is not None:
                self.n_answers += 1
                if not self.drop_answer(self.n_answers):
                    for raw in self.on_answer(a):
                        self.events.append({"ev": "rf", "data": {"from": "model", "answer": base64.b64encode(raw).decode()}})
        return {"status": "ok", "result": {"queued": len(frames), "bulk_budget_ms": self.budget}}

    def read_message(self, timeout):
        return None


class ModelControl(lom.Control):
    def __init__(self, target):
        super().__init__("model", None, "mdl")
        self.t = target
        self.requests = []

    def request(self, op, args=None, *, issued_at=0, read=False, rid=None, timeout=10.0):
        self.requests.append((op, args))
        return self.t.control(op, args)


class Clock:
    def __init__(self, t0=1000):
        self.t = t0

    def __call__(self):
        return self.t


def build(size=1200, block=4, **kw):
    img = image_of(size)
    text = manifest_of(img)
    # the model parses nothing but the manager's own fields: the text is the fixture's, with the image the model serves
    sha = hashlib.sha256(text.encode()).hexdigest()
    t = Target(img, sha)
    ifc = Iface(t, **{k: kw.pop(k) for k in ("budget_ms", "refill_ms") if k in kw})
    gw = lom.Gateway(ifc, "model", renew_s=1e9)
    ctl = ModelControl(t)
    logs = []
    sig = base64.b64encode(bytes(64)).decode()
    clock = kw.pop("now", Clock())
    m = lom.Manager(gw, ctl, img, text, sig, block=block, now=clock, frame_ms=FRAME_MS, poll_s=0.001,
                    answer_timeout_s=0.05, log=logs.append, **kw)
    return m, t, ifc, ctl, logs


def test_a_clean_transfer_sends_each_frame_once_and_prints_no_secret():
    m, t, ifc, ctl, logs = build()
    m.start()
    assert m.run() == "complete"
    assert [i for i, _ in t.log] == list(range(m.total)) and m.total == 11
    assert m.stats["resent"] == 0 and m.stats["stale"] == 0 and ifc.busy == 0
    parts = [a for op, a in ctl.requests if op == "update.manifest.part"]
    assert [p["part"] for p in parts] == list(range(1, len(parts) + 1)) and len(parts) <= 4
    offer = [a for op, a in ctl.requests if op == "update.offer"][0]
    assert offer["source"]["lora"]["block"] == 4 and offer["source"]["lora"]["window_end"] == offer["issued_at"] + 8 * 3600
    assert t.token.hex() not in "\n".join(logs), "the token is never printed"


def test_only_the_frames_an_answer_shows_missing_are_sent_again():
    m, t, ifc, ctl, logs = build(block=8)
    lost = {3, 9}
    ifc.drop = lambda idx, n: idx in lost and (lost.discard(idx) or True)   # each of them lost once
    m.start()
    assert m.run() == "complete"
    assert ifc.calls[0][0] == list(range(8))
    assert ifc.calls[1][0] == [3, 7], "the missing frame, then the block's last frame, which asks for the answer again"
    assert ifc.calls[2][0] == [8, 9, 10]
    assert ifc.calls[3][0] == [9, 10]
    assert m.stats["resent"] == 2, "frames 3 and 9"
    assert t.held == set(range(11))


def test_a_lost_answer_costs_the_whole_block_and_no_more():
    m, t, ifc, ctl, logs = build(block=4, size=500)
    ifc.drop_answer = lambda n: n == 1
    m.start()
    assert m.run() == "complete"
    assert ifc.calls[0][0] == [0, 1, 2, 3] and ifc.calls[1][0] == [0, 1, 2, 3]
    assert m.stats["lost"] == 1 and m.stats["resent"] == 4


def test_frames_may_arrive_in_any_order():
    m, t, ifc, ctl, logs = build(block=4)
    ifc.reorder = True
    m.start()
    assert m.run() == "complete"
    assert t.held == set(range(m.total))


def test_an_answer_for_an_earlier_block_is_not_read_against_this_one():
    m, t, ifc, ctl, logs = build(block=4)
    seen = []

    def stale(raw):
        seen.append(raw)
        # the answer to block 0 is heard again just before block 1's own: an old answer, base 0, every frame held
        return [seen[0], raw] if len(seen) == 2 else [raw]
    ifc.on_answer = stale
    m.start()
    assert m.run() == "complete"
    assert m.stats["stale"] == 1
    assert [c[0] for c in ifc.calls] == [[0, 1, 2, 3], [4, 5, 6, 7], [8, 9, 10]], "no frame is sent twice for it"


def test_a_bulk_frame_or_a_strangers_answer_is_not_an_answer():
    m, t, ifc, ctl, logs = build(block=4)
    stranger = bulk.answer(bulk.keys(b"x" * 16), t.no, 0, 4, 0xF)
    frame = bulk.frame(t.k, t.no, t.size, 0, image_of(t.size)[:115])
    ifc.on_answer = lambda raw: [frame, stranger, raw]
    m.start()
    assert m.run() == "complete"
    assert m.stats["bad_answers"] >= 2 * 3


def test_the_manager_never_asks_the_gateway_for_more_than_the_budget_it_read():
    m, t, ifc, ctl, logs = build(block=4, budget_ms=0, refill_ms=150)
    m.start()
    assert m.run() == "complete"
    assert ifc.busy == 0 and m.stats["refused"] == 0
    assert all(budget >= len(frames) * FRAME_MS for frames, budget in ifc.calls), ifc.calls


def test_a_gateway_that_refuses_a_block_busy_is_waited_on_not_failed():
    m, t, ifc, ctl, logs = build(block=4, size=500)
    real = ifc.request
    refused = []

    def flaky(op, args=None, **kw):
        if op == "rf.bulk" and not refused:
            refused.append(1)
            return {"status": "busy", "error": "bulk-budget"}
        return real(op, args, **kw)
    ifc.request = flaky
    m.start()
    assert m.run() == "complete" and m.stats["refused"] == 1


def test_a_transfer_pauses_at_its_window_and_a_resume_offers_the_same_manifest():
    clock = Clock()
    m, t, ifc, ctl, logs = build(block=4, now=clock, window_s=100)
    m.start()
    assert m.window_end == 1100
    real = ifc.request

    def ticking(op, args=None, **kw):
        r = real(op, args, **kw)
        if op == "rf.bulk":
            clock.t += 60   # a block takes a minute: the window ends during the second
        return r
    ifc.request = ticking
    assert m.run() == "paused"
    kept = m.next
    assert 0 < kept < m.total and m.stats["paused"] == 1
    token, job = m.token, m.job
    ifc.request = real
    clock.t += 10
    res = m.resume()
    assert (m.token, m.job, res["next"]) == (token, job, kept), "the same job, the same token, the next as kept"
    offers = [a for op, a in ctl.requests if op == "update.offer"]
    assert offers[0]["manifest_sha256"] == offers[1]["manifest_sha256"], "the same manifest"
    assert offers[1]["source"]["lora"]["window_end"] > offers[0]["source"]["lora"]["window_end"]
    assert m.run() == "complete"
    assert t.held == set(range(m.total))
    assert sorted(set(i for i, _ in t.log if _ == "ok")) == list(range(m.total))
    assert m.progress()["next"] == m.total


def test_cancel_names_the_job():
    m, t, ifc, ctl, logs = build()
    m.start()
    assert m.cancel()["state"] == "cancelled"
    assert ctl.requests[-1] == ("job.cancel", {"job": "model-1"})


def test_a_window_over_twelve_hours_or_a_block_over_eight_is_refused_before_anything_is_sent():
    with pytest.raises(ValueError):
        build(window_s=12 * 3600 + 1)
    with pytest.raises(ValueError):
        build(block=9)


# ------------------------------------------------------------------ fixtures

def load():
    return json.load(open(FIXTURE))


def test_the_fixtures_are_what_the_tool_writes():
    assert load() == lom.vectors()


def test_the_signed_offer_in_the_fixtures_is_what_the_manager_signs():
    f = load()
    signer = lom.Signer.public_test(job_base=f["offer"]["job"])
    args = {"manifest_sha256": f["manifest"]["sha256"], "confirm": {"within_s": 300},
            "source": {"lora": {"block": f["image"]["block"], "window_end": f["issued_at"] + 8 * 3600}}}
    w = signer.wrap(f["node_key"], "update.offer", args, job=f["offer"]["job"], issued_at=f["issued_at"])
    assert w == f["offer"]["wire"]
    body = json.loads(w["body"])
    assert body["issued_at"] == f["issued_at"] and "issued_at" not in body["args"]
    assert body["args"]["source"]["lora"]["window_end"] - body["issued_at"] <= lom.WINDOW_MAX_S
    assert f["offer"]["job_id"] == "#%s:%d" % (body["issuer"], body["job_id"])


def test_the_manifest_parts_join_to_the_signed_manifest():
    f = load()
    m = f["manifest"]
    blob = b"".join(base64.b64decode(p["data"]) for p in sorted(m["parts"], key=lambda p: p["part"]))
    assert blob == m["text"].encode() + base64.b64decode(m["sig_b64"])
    assert [p["part"] for p in m["parts"]] == list(range(1, len(m["parts"]) + 1))
    assert all(p["parts"] == len(m["parts"]) and len(base64.b64decode(p["data"])) <= 176 and p["sha256"] == m["sha256"]
               for p in m["parts"])
    assert hashlib.sha256(m["text"].encode()).hexdigest() == m["sha256"]


def test_the_bulk_tag_and_the_frames_are_the_reference_codecs():
    f = load()
    b = f["bulk"]
    token = bytes.fromhex(b["token_hex"])
    k = bulk.keys(token)
    assert (k[0].hex(), k[1].hex()) == (b["key_bulk_hex"], b["key_answer_hex"])
    img = image_of(f["image"]["size"])
    assert hashlib.sha256(img).hexdigest() == f["image"]["sha256"]
    assert [x.hex() for x in bulk.image_frames(k, b["job"], img)] == b["frames_hex"]
    for i, h in enumerate(b["frames_hex"]):
        assert bulk.check_frame(k, b["job"], len(img), bytes.fromhex(h))[:2] == ("ok", i)


def test_a_stale_answer_names_a_base_other_than_the_open_block():
    f = load()
    k = bulk.keys(bytes.fromhex(f["bulk"]["token_hex"]))
    base, nxt, have = bulk.check_answer(k, f["bulk"]["job"], bytes.fromhex(f["stale_answer"]["answer_hex"]))
    assert base != f["stale_answer"]["open_block_base"] and (base, nxt, have) == (0, 2, 3), "the tag is good; the base is the tell"
    cur = f["answer_current"]
    assert bulk.check_answer(k, f["bulk"]["job"], bytes.fromhex(cur["answer_hex"])) == (cur["base"], cur["next"], cur["have"])


def test_a_pause_then_a_resume_names_the_same_manifest_and_a_later_window():
    f = load()
    pr = f["pause_resume"]
    first, again = json.loads(pr["first_offer_wire"]["body"]), json.loads(pr["resume_wire"]["body"])
    assert first["args"]["manifest_sha256"] == again["args"]["manifest_sha256"] == f["manifest"]["sha256"]
    assert again["args"]["source"]["lora"]["window_end"] == pr["resume_window_end"] > first["args"]["source"]["lora"]["window_end"]
    assert again["job_id"] == pr["resume_job"] > first["job_id"], "a signed request cannot reuse a job number"
    k = bulk.keys(bytes.fromhex(f["bulk"]["token_hex"]))
    frame = bytes.fromhex(pr["frame_for_the_resuming_request_hex"])
    assert bulk.check_frame(k, pr["frames_keep_job_number"], f["image"]["size"], frame)[0] == "other-job"


def test_frames_a_target_drops_and_answers_a_manager_drops():
    f = load()
    k = bulk.keys(bytes.fromhex(f["bulk"]["token_hex"]))
    job, size = f["bulk"]["job"], f["image"]["size"]
    for name, c in f["frame_cases"].items():
        assert bulk.check_frame(k, job, size, bytes.fromhex(c["hex"]))[0] == c["verdict"], name
    for name, c in f["answer_cases"].items():
        assert (bulk.check_answer(k, job, bytes.fromhex(c["hex"])) is not None) == c["accepted"], name
    assert {"frame_short_before_last", "frame_long_before_last", "frame_short_last", "frame_long_last"} <= set(f["frame_cases"])
