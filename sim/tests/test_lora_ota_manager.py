"""tools/lora_ota_manager.py, the reference manager, against the real firmware on the simulated channel
(docs/contract/LORA_UPDATES.md sections 3 to 7 and 11): two simulated Companions, the gateway the manager drives over its
interface (rf.status, rf.bulk, the rf stream) and the target. The manager stages the manifest, offers, paces, resends and
resumes; a test breaks the channel by wrapping the gateway object the manager reads, so each loss is exactly where the test
says. Most tests send the control requests (staging, offer, job reads) over the target's own interface, which keeps their radio
time to the bulk frames; two send them signed over the radio, through the gateway (LoraControl, the manager's default), and one
checks that the management channel does not carry them.

Nothing here has run on real radios. At 60 times real time the gateway's duty window is seeded full at its first use, so bulk
(80% of the window) has no budget for the first 12 minutes of its hour, which is about 12 real seconds: the manager waits
for it by reading rf.status."""
import base64
import hashlib
import struct
import time

import pytest
from test_deployment_authority import Dev as DepDev, RTC as DEP_RTC
from test_lora_management import FAST, Radio  # noqa: F401
from test_update_install import NEW, OLD, declare, image, job_get
from test_update_lora import manifest_for, stage_parts  # noqa: F401
from test_update_lora_radio import Gateway as RadioGw, pair
from test_updates import RELEASE_SEED, RTC, Dev as UDev, sign

from sim.harness import CompanionClient

import lora_ota_manager as lom  # noqa: E402  (tools/, on the path once test_updates is imported)
import mecon_lora_bulk as bulk  # noqa: E402

SCALE = 60
FRAME_MS = 250   # a full frame at the fast setting the sim nodes use


class Clock:
    """The manager's estimate of the target's clock: the node's time runs SCALE times real time. skew() moves it on."""

    def __init__(self):
        self.t0, self.extra = time.time(), 0.0

    def __call__(self):
        return RTC + (time.time() - self.t0) * SCALE + self.extra

    def skip(self, s):
        self.extra += s


class Tap(lom.Gateway):
    """The manager's gateway with the channel under a test's hand: frames dropped or reordered on their way out, answers lost
    or replaced on their way back. Every call and every budget reading is kept for the test to check."""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.sent = []            # (frame indices, budget read just before) per rf.bulk call
        self.polls = 0
        self.last_budget = None
        self.drop_once = set()    # frame indices lost once on their way out
        self.reverse = set()      # blocks (by first index) sent last frame first
        self.lose_answers = 0     # answers lost on their way back
        self.replace_base = {}    # block base -> base of an earlier answer heard once instead of this block's first answer
        self.noise = {}           # first frame index of a block -> frames sent to the target just ahead of it
        self.heard = []           # every answer that came

    def status(self):
        self.polls += 1
        s = super().status()
        self.last_budget = s["bulk_budget_ms"]
        return s

    def bulk(self, frames):
        idx = [struct.unpack_from("<I", f, 5)[0] for f in frames]
        self.sent.append((idx, self.last_budget))
        keep = [f for f, i in zip(frames, idx) if not (i in self.drop_once and not self.drop_once.discard(i))]
        if idx and idx[0] in self.reverse:
            self.reverse.discard(idx[0])
            keep.reverse()
        if not keep:
            return {"status": "ok", "result": {"queued": 0, "bulk_budget_ms": self.last_budget}}
        noise = self.noise.pop(idx[0], None) if idx else None
        if noise:   # wait until the noise and the block both fit, so the gateway never has to refuse the manager's block
            while True:
                s = super().status()
                if s["bulk_budget_ms"] >= (len(noise) + len(keep)) * FRAME_MS and s["queued"] + len(noise) + len(keep) <= 16:
                    break
                time.sleep(0.5)
            assert super().bulk(noise)["status"] == "ok"
        return super().bulk(keep)

    def answers(self):
        out = []
        for a in super().answers():
            self.heard.append(a)
            if self.lose_answers:
                self.lose_answers -= 1
                continue
            base = struct.unpack_from("<I", a, 5)[0]
            if base in self.replace_base:
                old = self.replace_base.pop(base)
                out.append(next(h for h in self.heard if struct.unpack_from("<I", h, 5)[0] == old))
                continue
            out.append(a)
        return out


def manager(d, g, img, name, *, block=8, window=7200, gw=None, control=None, **kw):
    text = manifest_for(d, img)
    key = d.hello["node_key"]
    gw = gw or Tap(g.i, key, renew_s=1.0)
    control = control or lom.DirectControl(d.i, key, None, prefix=name)
    clock = kw.pop("now", None) or Clock()
    m = lom.Manager(gw, control, img, text, sign(RELEASE_SEED, text), block=block, window_s=window, now=clock,
                    frame_ms=FRAME_MS, poll_s=0.5, answer_timeout_s=kw.pop("answer_timeout_s", 12.0),
                    log=kw.pop("log", lambda s: None), **kw)
    return m, gw, clock


def finish(n, m, img, new_dev):
    """After the last block: the device verifies, activates and boots the image, which attests; the manager confirms."""
    assert n.wait_exit(15) == 75, "verified and activated: the device reboots into the image"
    declare(n, NEW)
    n.start()
    d = new_dev(n)
    assert d.hello["update"] == {"job": m.job, "version": NEW, "sha256": hashlib.sha256(img).hexdigest()}, d.hello.get("update")
    return d


def wait_for(fn, what, timeout=30.0):
    end = time.time() + timeout
    while time.time() < end:
        v = fn()
        if v:
            return v
        time.sleep(0.2)
    raise AssertionError("no " + what)


@pytest.mark.timescale(SCALE)
def test_the_manager_moves_an_image_through_loss_reordering_a_lost_and_a_stale_answer_and_a_strangers_frames(ether, nodes):
    img = image(1200)   # 11 frames: blocks of four, 4 + 4 + 3
    n, d, g = pair(nodes, ether, "ma", 81)
    m, gw, clock = manager(d, g, img, "ma", block=4)
    m.start()
    assert m.total == 11
    # Block 0 goes out last frame first. Block 1 is preceded by a stranger's frames under the right job and indices, and
    # loses frame 5. Block 2 loses frame 9, and the answer to its first try is replaced by the old answer to block 1: a good
    # answer, for another block.
    gw.reverse.add(0)
    gw.drop_once.update({5, 9})
    other = bulk.keys(b"a stranger's token")
    gw.noise[4] = [bulk.frame(other, m.job_no, len(img), i, img[i * 115:(i + 1) * 115]) for i in range(4, 8)]
    gw.replace_base[8] = 4
    assert m.run() == "complete"
    sent = [s[0] for s in gw.sent]
    # block 0 arrived backwards, so its first answer holds only the last frame: the three missing go again, with the last
    assert sent[0:2] == [[0, 1, 2, 3], [0, 1, 2, 3]], sent
    # block 1: frame 5 alone is missing; it goes again with the block's last frame, never the frames held
    assert sent[2:4] == [[4, 5, 6, 7], [5, 7]], sent
    # block 2: nothing is skipped on a stale answer's word; after the wait the whole block goes again
    assert sent[4:6] == [[8, 9, 10], [8, 9, 10]] and len(sent) == 6, sent
    assert m.stats["stale"] == 1 and m.stats["lost"] == 1 and m.stats["refused"] == 0 and m.stats["bad_answers"] == 0, m.stats
    # The image that boots hashes to the manifest's digest only if no frame was skipped on a stale answer's word.
    d2 = finish(n, m, img, UDev)
    m.control = lom.DirectControl(d2.i, d2.hello["node_key"], None, prefix="ma2")
    assert m.confirm(timeout_s=30, poll_s=0.5)["state"] == "confirmed"


@pytest.mark.timescale(SCALE)
def test_a_pause_at_the_window_then_the_same_manifest_resumes_the_same_job_and_token(ether, nodes):
    img = image(1200)
    n, d, g = pair(nodes, ether, "mb", 82)
    logs = []
    clock = Clock()
    m, gw, _ = manager(d, g, img, "mb", block=4, window=1500, now=clock, log=logs.append)

    def log(s):
        logs.append(s)
        if s.startswith("block at 4 "):   # two blocks are held: the manager's window is over
            clock.skip(10 ** 5)
    m.log = log
    m.start()
    token, job = m.token, m.job
    assert m.run() == "paused" and m.next == 8 and m.stats["paused"] == 1
    sent_before = len(gw.sent)
    # The target's own window ends too: its job is paused, not failed.
    g1 = wait_for(lambda: job_get(d, job)["state"] == "paused" and job_get(d, job), "the target's pause at its window", 60)
    assert (g1["next"], g1["frames_total"], g1["reason"]) == (8, 11, "window"), g1
    clock.extra = 0.0
    res = m.resume()
    assert (m.job, m.token, res["next"]) == (job, token, 8), "the same job and token, the frame number the target kept"
    assert res["state"] == "progress"
    assert job_get(d, job)["state"] == "progress"
    assert m.run() == "complete"
    assert [s[0] for s in gw.sent[sent_before:]] == [[8, 9, 10]], "only the rest is sent"
    assert m.stats["refused"] == 0
    assert token.hex() not in "\n".join(logs), "the token is never printed"
    d2 = finish(n, m, img, UDev)
    m.control = lom.DirectControl(d2.i, d2.hello["node_key"], None, prefix="mb2")
    assert m.confirm(timeout_s=30, poll_s=0.5)["state"] == "confirmed"


@pytest.mark.timescale(SCALE)
def test_a_reboot_of_the_target_mid_transfer_is_resumed_from_the_frame_it_kept(ether, nodes):
    img = image(1200)
    n, d, g = pair(nodes, ether, "mc", 83)
    clock = Clock()
    m, gw, _ = manager(d, g, img, "mc", block=4, now=clock)

    def log(s):
        if s.startswith("block at 4 "):
            clock.skip(10 ** 5)   # stop after two blocks
    m.log = log
    m.start()
    token, job = m.token, m.job
    assert m.run() == "paused"
    clock.extra = 0.0
    n.restart()   # the power goes
    d = UDev(n)
    m.control = lom.DirectControl(d.i, d.hello["node_key"], None, prefix="mc2")
    g1 = job_get(d, job)
    assert (g1["state"], g1["reason"]) == ("paused", "manager"), g1
    kept = g1["next"]
    assert 0 <= kept <= 9
    time.sleep(1.1)
    res = m.resume()   # the manifest is not staged any more: the manager stages it again, then offers
    assert (m.job, m.token, res["next"]) == (job, token, kept), res
    before = len(gw.sent)
    assert m.run() == "complete"
    first = gw.sent[before][0][0]
    assert first == kept, "frames go on from the number the target kept: %s" % gw.sent[before:]
    assert m.stats["refused"] == 0
    d2 = finish(n, m, img, UDev)
    m.control = lom.DirectControl(d2.i, d2.hello["node_key"], None, prefix="mc3")
    assert m.confirm(timeout_s=30, poll_s=0.5)["state"] == "confirmed"


@pytest.mark.timescale(SCALE)
def test_cancelling_ends_the_job_and_the_slot_takes_no_more_frames(ether, nodes):
    img = image(1200)
    n, d, g = pair(nodes, ether, "md", 84)
    m, gw, clock = manager(d, g, img, "md", block=4)
    m.log = lambda s: clock.skip(10 ** 5) if s.startswith("block at 0 ") else None
    m.start()
    assert m.run() == "paused" and m.next == 4
    r = m.cancel()
    assert r["state"] == "cancelled", r
    assert job_get(d, m.job)["state"] == "cancelled"
    time.sleep(0.5)
    assert d.i.request("update.status")["result"]["job"] is None, "the lock is released"
    r = d.i.request("update.lora.frame", {"frame": m._frame(4).hex()})
    assert r["status"] == "ok" and r["result"]["answer"] is None, "no transfer takes frames now"


@pytest.mark.timescale(SCALE)
def test_the_manager_never_queues_past_the_gateways_budget(ether, nodes):
    img = image(1200)
    n, d, g = pair(nodes, ether, "me", 85)
    m, gw, clock = manager(d, g, img, "me", block=8)
    m.start()
    assert m.run() == "complete"
    assert gw.sent and all(budget is not None and budget >= len(idx) * FRAME_MS for idx, budget in gw.sent), gw.sent
    assert m.stats["refused"] == 0, "the gateway never had to refuse a block: %s" % m.stats
    assert gw.polls > 1, "a gateway whose window starts full has no budget for the first minutes: the manager read rf.status until there was"
    assert g.status()["queued"] == 0
    for idx, _ in gw.sent:
        assert len(idx) <= lom.BLOCK_MAX
    d2 = finish(n, m, img, UDev)
    m.control = lom.DirectControl(d2.i, d2.hello["node_key"], None, prefix="me2")
    assert m.confirm(timeout_s=30, poll_s=0.5)["state"] == "confirmed"


def enrolled_pair(nodes, ether, name, seed):
    """An enrolled target at the fast setting, running OLD, and the gateway beside it. `u` serves the manifest's fields."""
    n = nodes("companion_standalone", name, seed=seed, rtc=RTC, ether_path=ether.path)
    declare(n, OLD)
    n.start()
    c = CompanionClient(n)
    c.app_start()
    assert c.cli("set radio " + FAST).startswith("OK")
    c.close()
    n.restart()
    d = DepDev(n)
    assert d.provision()["status"] == "ok"
    u = UDev.__new__(UDev)
    u.hello = d.i.request("hello")["result"]
    return n, d, u, RadioGw(nodes, ether, name)


def reattach(d):
    def f(n):
        d.attach()
        return type("Booted", (), {"hello": d.i.request("hello")["result"], "i": d.i, "node_key": d.node_key})()
    return f


@pytest.mark.timescale(SCALE)
def test_a_signed_offer_and_a_signed_job_get_through_the_manager(ether, nodes):
    img = image(1200)
    n, d, u, g = enrolled_pair(nodes, ether, "mf", 86)
    signer = lom.Signer.public_test(job_base=10)
    control = lom.DirectControl(d.i, d.node_key, signer, prefix="mf")
    m, gw, clock = manager(u, g, img, "mf", block=8, control=control)
    m.start()
    assert m.job.startswith("#") and m.job_no == bulk.job_number(m.job) and m.job_no >= 10
    offer = [a for op, a in control.history if op == "update.offer"][0]
    assert "issued_at" not in offer, "a signed offer's issued_at is the signed body's"
    g1 = m.progress()   # a signed job.get
    assert (g1["state"], g1["next"], g1["frames_total"]) == ("accepted", 0, 11), g1
    assert m.run() == "complete"
    assert m.stats["refused"] == 0
    d2 = finish(n, m, img, reattach(d))
    m.control = lom.DirectControl(d2.i, d2.node_key, signer, prefix="mfx")
    assert m.confirm(timeout_s=30, poll_s=0.5)["state"] == "confirmed"


@pytest.mark.timescale(SCALE)
def test_the_manager_stages_offers_and_reads_the_job_over_the_radio_signed(ether, nodes):
    """update.manifest.part, update.offer and job.get all travel as signed requests through rf.request (LoraControl), and
    the first block goes the same way; cancelling over the radio ends the job."""
    img = image(1200)
    n, d, u, g = enrolled_pair(nodes, ether, "mg", 87)
    key = d.node_key
    signer = lom.Signer.public_test(job_base=10)
    gw = Tap(g.i, key, renew_s=1.0)
    control = lom.LoraControl(gw, key, signer, prefix="mg")
    m, _, clock = manager(u, g, img, "mg", gw=gw, control=control, block=4)
    m.log = lambda s: clock.skip(10 ** 5) if s.startswith("block at 0 ") else None
    m.start()
    assert [op for op, _ in control.history].count("update.manifest.part") == 4
    assert d.i.request("update.status")["result"]["job"]["job"] == m.job
    assert m.run() == "paused" and m.next == 4
    g1 = m.progress()   # a signed job.get, over the radio
    assert (g1["state"], g1["next"], g1["frames_total"]) == ("progress", 4, 11), g1
    assert m.cancel()["state"] == "cancelled"
    assert job_get(d, m.job)["state"] == "cancelled"
    assert m.stats["refused"] == 0


def test_an_update_operation_on_the_management_channel_is_unknown(ether, nodes):
    """Offers and staging are point to point only: on the channel every member would hear them."""
    from test_lora_management import CH, on_channel
    from test_deployment_authority import wrap
    d, m = on_channel(nodes, ether, "companion_standalone", "mh")
    for op, args, job in (("update.offer", {"manifest_sha256": "ab" * 32, "issued_at": DEP_RTC,
                                            "source": {"lora": {"block": 8, "window_end": DEP_RTC + 600}}}, 5),
                          ("update.manifest.part", {"sha256": "ab" * 32, "part": 1, "parts": 1, "data": "AA=="}, 6),
                          ("update.status", {}, 7)):
        r = m.send(d.node_key, wrap(d.node_key, op, args, job=job), slot=CH)
        assert r and r["status"] == "unknown-op", (op, r)
    r = m.send(d.node_key, wrap(d.node_key, "update.status", {}, job=8, rid="us"))
    assert r and r["status"] == "ok", "the same read, point to point, is carried: %s" % r
