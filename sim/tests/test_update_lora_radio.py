"""A signed update over LoRa on the radio (docs/contract/LORA_UPDATES.md sections 6 and 9), two simulated nodes on the simulated
channel: a Companion as the gateway, driven over its console with rf.bulk and rf.status, and a Companion as the target, staged and
offered over its own console as test_update_lora does. The bulk frames are the manager's reference encoder's
(tools/mecon_lora_bulk.py); the block answers come back through the gateway as rf events, base64, and are read with the same.

Nothing here has run on real radios. The duty window is the point of the timescale: a node's window is seeded full at its first
use after a boot and ages out at the limit's rate, so bulk (80% of the window) has no budget for the first 12 minutes of the
node's hour, and a manager that paces itself with rf.status waits for it."""
import base64
import os
import time

import pytest
from test_update_install import NEW, OLD, declare, image, job_get
from test_update_lora import RTC, Transfer, bulk, install_and_confirm, manifest_for, offer, stage_parts
from test_updates import Dev
from test_lora_management import FAST, Radio  # noqa: F401  (FAST: the 10% band and short packets)

from sim.harness import CompanionClient

import mecon_schema  # noqa: E402  (tools/, on the path once test_updates is imported)

SCALE = 60   # one real second is a minute of the nodes' time


class Gateway(Radio):
    """The manager's Companion, as test_lora_management's Radio, with the bulk operations."""

    def renew(self):
        """The subscription lasts 300 s of the node's time, which at this timescale is five seconds of the test's."""
        r = self.i.request("sub.start", {"stream": "rf", "lifetime_s": 300})
        assert r["status"] == "ok", r

    def status(self):
        r = self.i.request("rf.status")
        assert r["status"] == "ok", r
        assert not mecon_schema.check(r["result"], mecon_schema.load("rf.status.result.json")), r
        return r["result"]

    def bulk(self, to, frames):
        r = self.i.request("rf.bulk", {"to": to, "frames": [base64.b64encode(f).decode() for f in frames]})
        if r["status"] == "ok":
            assert not mecon_schema.check(r["result"], mecon_schema.load("rf.bulk.result.json")), r
        return r

    def wait_budget(self, need_ms, timeout=60.0):
        """What a manager does: read rf.status until bulk has room for the next block."""
        end = time.time() + timeout
        while time.time() < end:
            self.renew()
            s = self.status()
            if s["bulk_budget_ms"] >= need_ms and s["queued"] == 0:
                return s
            time.sleep(0.5)
        raise AssertionError("no bulk budget: %s" % self.status())

    def answers(self, frm, want, timeout=30.0):
        """Block answers heard from node `frm`, as the bytes that came: until `want` have arrived or the time is up."""
        got = []
        end = time.time() + timeout
        while time.time() < end and len(got) < want:
            self.renew()
            for e in list(self.i.events):
                if e.get("ev") == "rf" and e["data"].get("answer") is not None:
                    problems = mecon_schema.check(e["data"], mecon_schema.load("event.rf.json"))
                    assert not problems, (problems, e)
                    self.i.events.remove(e)
                    assert e["data"]["from"] == frm
                    got.append(base64.b64decode(e["data"]["answer"]))
            if len(got) < want:
                m = self.i.read_message(1.5)
                if m and "ev" in m:
                    self.i.events.append(m)
        return got


def pair(nodes, ether, name, seed):
    """The target (an unenrolled Companion at the fast radio setting, running OLD) and the gateway beside it."""
    n = nodes("companion_standalone", name, seed=seed, rtc=RTC, ether_path=ether.path)
    declare(n, OLD)
    n.start()
    c = CompanionClient(n)
    c.app_start()
    assert c.cli("set radio " + FAST).startswith("OK")
    c.close()
    n.restart()
    d = Dev(n)
    return n, d, Gateway(nodes, ether, name)


def sent_by(ether, name):
    with ether.lock:
        return sum(1 for t in ether.tx_log if t.src == name)


@pytest.mark.timescale(SCALE)
def test_an_image_crosses_the_radio_through_the_gateway_is_resent_where_a_frame_was_lost_and_installs(ether, nodes):
    img = image(1200)   # 11 frames: a block of eight, then one of three
    n, d, g = pair(nodes, ether, "lr", 71)
    msha = stage_parts(d, manifest_for(d, img))
    res = offer(d, msha, rid="lr-1", window=7200)["result"]
    t = Transfer(d, img, res, "lr-1")
    to = d.hello["node_key"]
    assert "rf.bulk" in g.i.request("hello")["result"]["capabilities"]

    start = time.time()
    s = g.wait_budget(8 * 250)
    assert s["queued"] == 0
    waited = time.time() - start

    # Block 0 with frame 3 missing: seven frames go out, the answer says which one is missing.
    r = g.bulk(to, [t.frames[i] for i in (0, 1, 2, 4, 5, 6, 7)])
    assert r["status"] == "ok" and r["result"]["queued"] == 7, r
    (a,) = g.answers(to, 1)
    assert bulk.check_answer(t.k, t.no, a) == (0, 3, 0b11110111), "the target holds 0 to 2 written and 4 to 7 in RAM"
    assert job_get(d, "lr-1")["next"] == 3
    # The manager sends frame 3 again, and the block's last frame, which asks for the answer again.
    g.wait_budget(2 * 250)
    assert g.bulk(to, [t.frames[3], t.frames[7]])["status"] == "ok"
    (a,) = g.answers(to, 1)
    assert bulk.check_answer(t.k, t.no, a) == (0, 8, 0xFF)
    assert job_get(d, "lr-1")["next"] == 8
    # The last block, of three.
    g.wait_budget(3 * 250)
    assert g.bulk(to, t.frames[8:11])["status"] == "ok"
    (a,) = g.answers(to, 1)
    assert bulk.check_answer(t.k, t.no, a) == (8, 11, 0b111), "the transfer is whole"
    print("MEASURED: %d frames over the simulated radio, first budget after %.1f s real (%d s of the nodes' time)"
          % (len(t.frames), waited, waited * SCALE))
    install_and_confirm(n, img, "lr-1")


@pytest.mark.timescale(SCALE)
def test_a_strangers_frames_through_the_gateway_cost_the_target_nothing(ether, nodes):
    img = image(1200)
    n, d, g = pair(nodes, ether, "ls", 72)
    res = offer(d, stage_parts(d, manifest_for(d, img)), rid="ls-1", window=7200)["result"]
    t = Transfer(d, img, res, "ls-1")
    to = d.hello["node_key"]
    # Frames of the right job and indices under another token: the last of the block is the one an answer would follow.
    other = bulk.keys(os.urandom(16))
    strange = [bulk.frame(other, t.no, len(img), i, img[i * bulk.BULK_DATA:(i + 1) * bulk.BULK_DATA]) for i in range(8)]
    g.wait_budget(8 * 250)
    before = sent_by(ether, "ls")
    assert g.bulk(to, strange)["status"] == "ok"
    assert g.answers(to, 1, timeout=6.0) == [], "a stranger's frames are never answered"   # six minutes of the nodes' time: a real answer takes seconds
    assert job_get(d, "ls-1")["next"] == 0, "and never written"
    with ether.lock:
        heard = sum(1 for tx in ether.tx_log if tx.src == "mgrls" and "ls" in tx.delivered_to)
    assert heard >= 8, "the frames did reach the target: %d" % heard
    assert sent_by(ether, "ls") == before, "the target put nothing on the air for them"
    # The real manager's block then goes through as if nothing had happened.
    g.wait_budget(8 * 250)
    assert g.bulk(to, t.frames[:8])["status"] == "ok"
    (a,) = g.answers(to, 1)
    assert bulk.check_answer(t.k, t.no, a) == (0, 8, 0xFF)


@pytest.mark.timescale(SCALE)
def test_the_gateway_refuses_what_it_cannot_send_and_queues_nothing(ether, nodes):
    img = image(1200)
    n, d, g = pair(nodes, ether, "lq", 73)
    res = offer(d, stage_parts(d, manifest_for(d, img)), rid="lq-1", window=7200)["result"]
    t = Transfer(d, img, res, "lq-1")
    to = d.hello["node_key"]
    ok = base64.b64encode(t.frames[0]).decode()
    cases = [
        ({"to": to, "frames": []}, "bad-args"),
        ({"to": to, "frames": [ok] * 9}, "bad-args"),   # BULK_BLOCK_MAX is 8
        ({"to": to}, "bad-args"),
        ({"to": "zz" * 32, "frames": [ok]}, "bad-args"),
        ({"to": to, "frames": [base64.b64encode(t.frames[0] + b"\0").decode()]}, "bad-args"),   # past one datagram
        ({"to": to, "frames": [base64.b64encode(b"\x1d" + t.frames[0][1:]).decode()]}, "bad-args"),   # a request marker
        ({"to": to, "frames": [ok, "!!!"]}, "bad-args"),
        ({"to": to, "frames": [base64.b64encode(t.frames[0][:16]).decode()]}, "bad-args"),   # no byte of image
    ]
    for args, error in cases:
        r = g.i.request("rf.bulk", args)
        assert (r["status"], r.get("error")) == ("invalid", error), (args, r)
    assert g.status()["queued"] == 0, "nothing the checks refused was queued"
    # The window is seeded full at the node's first use: bulk has no budget in the first minutes, and nothing is queued.
    time.sleep(1.0)   # about a minute of the nodes' time, which frees no more than a few seconds of a 288 s ceiling
    s = g.status()
    assert s["bulk_budget_ms"] == 0, "the ceiling is 80%% of the window, so a seeded hour has none: %s" % s
    r = g.bulk(to, t.frames[:8])
    assert (r["status"], r.get("error")) == ("busy", "bulk-budget"), r
    s = g.status()
    assert s["queued"] == 0, s

