"""A signed update over LoRa on the simulated standalone Companion, the target's side (docs/contract/LORA_UPDATES.md): the
manifest staged in parts, an update.offer with source.lora, bulk frames and block answers, a pause and a resume, and the
install, attestation and confirmation a push has.

The LoRa link is a later slice. Here the frames reach the transfer through the simulator-only operation update.lora.frame,
which hands the target one bulk frame as hex and returns the block answer it gave, if any. The frames and the answers are
those of tools/mecon_lora_bulk.py, the manager's reference encoder, so the firmware is checked against an independent
implementation of the keys, tags and lengths."""
import base64
import hashlib
import time

import pytest
from test_jobs import tx_of
from test_update_install import NEW, declare, image, job_get, start
from test_update_dfu import dfu_board, stage_dfu
from test_updates import RELEASE_SEED, RTC, Dev, sign

import mecon_lora_bulk as bulk  # noqa: E402  (tools/, on the path once test_updates is imported)

DATA = bulk.BULK_DATA


def manifest_for(d, img, **over):
    fields = {"version": NEW, "image": {"size": len(img), "sha256": hashlib.sha256(img).hexdigest()}}
    fields.update(over)
    return d.manifest(**fields)


def parts_of(text, seed=RELEASE_SEED, each=176):
    """update.manifest.part arguments for a manifest: its text and its signature, cut into parts of at most `each` bytes."""
    blob = text.encode() + base64.b64decode(sign(seed, text))
    cut = [blob[i:i + each] for i in range(0, len(blob), each)]
    sha = hashlib.sha256(text.encode()).hexdigest()
    return [{"sha256": sha, "part": i + 1, "parts": len(cut), "data": base64.b64encode(c).decode()} for i, c in enumerate(cut)]


def stage_parts(d, text, order=None):
    """Stages a manifest by parts (in `order`, 0-based), returns its hash."""
    args = parts_of(text)
    last = None
    for i in order if order is not None else range(len(args)):
        last = d.i.request("update.manifest.part", args[i])
        assert last["status"] == "ok", last
    assert "version" in last["result"], last   # the last part ran the staging
    return args[0]["sha256"]


def offer(d, msha, window=3600, issued=RTC, block=8, rid="lo-1", within_s=120, **over):
    lora = {"block": block, "window_end": issued + window}
    args = {"manifest_sha256": msha, "issued_at": issued, "source": {"lora": lora}, "confirm": {"within_s": within_s}}
    args.update(over)
    return d.i.request("update.offer", args, rid=rid)


class Transfer:
    """The manager's side of one transfer: the keys its token gives and the frames of its image."""

    def __init__(self, d, img, res, job_id):
        self.d, self.img = d, img
        self.token = bytes.fromhex(res["transfer_token"])
        self.k = bulk.keys(self.token)
        self.no = bulk.job_number(job_id)
        self.frames = bulk.image_frames(self.k, self.no, img)
        assert res["frames_total"] == len(self.frames)

    def send(self, i):
        """Frame i; the block answer the target gave to it, decoded as the manager reads it, or None."""
        r = self.d.i.request("update.lora.frame", {"frame": self.frames[i].hex()})
        assert r["status"] == "ok", r
        a = r["result"]["answer"]
        if a is None:
            return None
        got = bulk.check_answer(self.k, self.no, bytes.fromhex(a))
        assert got is not None, "a block answer the manager can read: %s" % a
        return got

    def block(self, base, size=8):
        """Frames base .. base+size-1 in order; the answer to the last one."""
        end = min(base + size, len(self.frames))
        got = None
        for i in range(base, end):
            got = self.send(i)
            assert (got is not None) == (i == end - 1), (i, got)
        return got

    def run(self, start, size=8):
        """Every block from frame `start` to the end, each answer checked; the last one."""
        base, got = start, None
        while base < len(self.frames):
            end = min(base + size, len(self.frames))
            got = self.block(base, size)
            assert got == (base, end, (1 << (end - base)) - 1), got
            base = end
        return got


def wait_for(fn, what, timeout=15.0):
    end = time.time() + timeout
    while time.time() < end:
        v = fn()
        if v:
            return v
        time.sleep(0.1)
    raise AssertionError("no " + what)


def install_and_confirm(n, img, job):
    """After the last block: the device verifies, activates, boots the image, which attests; then it is confirmed."""
    assert n.wait_exit(15) == 75, "verified and activated: the device reboots into the image"
    declare(n, NEW)
    n.start()
    d = Dev(n)
    att = {"job": job, "version": NEW, "sha256": hashlib.sha256(img).hexdigest()}
    assert d.hello["update"] == att, "the image that boots hashes to the manifest's digest: %s" % d.hello.get("update")
    assert job_get(d, job)["state"] == "awaiting-confirm"
    r = d.i.request("job.confirm", {"job": job})
    assert r["status"] == "ok" and r["result"]["state"] == "confirmed", r
    return d


def test_an_image_arrives_over_lora_installs_attests_and_is_confirmed(nodes):
    img = image(9000)   # 79 frames: ten blocks of eight, the last of seven
    n, d = start(nodes, "lo", 61)
    assert "update.manifest.part" in d.hello["capabilities"]
    text = manifest_for(d, img)
    assert len(parts_of(text)) == 4, "a manifest of this length is four parts"
    msha = stage_parts(d, text, order=[2, 0, 3, 1])
    assert d.i.request("update.status")["result"]["staged"][0]["sha256"] == msha

    r = offer(d, msha)
    assert r["status"] == "ok", r
    res = r["result"]
    assert (res["job"], res["state"], res["next"], res["size"], res["transfer_size"], res["encoding"], res["frame_bytes"],
            res["frames_total"]) == ("lo-1", "accepted", 0, len(img), len(img), "raw", 115, 79), res
    assert len(res["transfer_token"]) == 32 and int(res["transfer_token"], 16)
    t = Transfer(d, img, res, "lo-1")

    # An open transfer reports its progress; the token is in no read.
    g = job_get(d, "lo-1")
    assert g["state"] == "accepted" and (g["next"], g["frames_total"], g["window_end"]) == (0, 79, RTC + 3600), g
    assert "reason" not in g
    for text_ in (str(g), str(d.i.request("update.status")), str(d.i.request("job.list"))):
        assert res["transfer_token"] not in text_

    # Block 0 arrives backwards. The block's last frame is the first to come, so the answer to it says that only that frame is
    # held; the rest are written in order as the gap fills, and a resend of the last frame asks for the answer again.
    assert t.send(7) == (0, 0, 0b10000000)
    for i in (6, 5, 4, 3, 2, 1, 0):
        assert t.send(i) is None
    assert job_get(d, "lo-1")["next"] == 8
    assert t.send(7) == (0, 8, 0xFF), "a held frame is never written twice; the block is answered whole"

    # Block 1 loses a frame in the middle: the answer says which is missing, and only that one is sent again.
    for i in (8, 9, 11, 12, 13, 14):
        assert t.send(i) is None
    assert t.send(15) == (8, 10, 0b11111011)
    assert job_get(d, "lo-1")["next"] == 10 and job_get(d, "lo-1")["state"] == "progress"
    assert t.send(10) is None
    assert t.send(15) == (8, 16, 0xFF), "whole now"

    got = t.run(16)
    assert got == (72, 79, 0b1111111), "the last block is seven frames and the answer says the transfer is whole: %s" % (got,)
    install_and_confirm(n, img, "lo-1")


@pytest.mark.timescale(10)
def test_a_transfer_pauses_at_its_window_and_a_reboot_keeps_it_until_the_same_manifest_resumes_it(nodes):
    img = image(30000)   # 261 frames
    n, d = start(nodes, "lp", 62)
    text = manifest_for(d, img)
    msha = stage_parts(d, text)
    r = offer(d, msha, window=40, rid="lp-1")
    res = r["result"]
    t = Transfer(d, img, res, "lp-1")
    for base in range(0, 72, 8):
        assert t.block(base) == (base, base + 8, 0xFF)
    # The window ends (40 s of the device's clock, 4 s here): the job is paused, not failed.
    g = wait_for(lambda: job_get(d, "lp-1")["state"] == "paused" and job_get(d, "lp-1"), "pause at the window")
    assert (g["next"], g["frames_total"], g["window_end"], g["reason"]) == (72, 261, RTC + 40, "window"), g
    assert t.send(72) is None and job_get(d, "lp-1")["next"] == 72, "a frame that arrives while paused is dropped"
    # The one-job lock holds while paused.
    r = d.i.request("config.apply", {"set": {"tx": tx_of(d.i) - 1}, "confirm": {"within_s": 60}})
    assert r["status"] == "busy" and r["error"] == "job-open:lp-1", r
    assert d.i.request("update.status")["result"]["job"]["state"] == "paused"

    n.restart()
    d = Dev(n)
    g = job_get(d, "lp-1")
    assert (g["state"], g["next"], g["reason"], g["window_end"]) == ("paused", 72, "window", RTC + 40), g
    assert d.i.request("update.status")["result"]["job"]["state"] == "paused"
    r = d.i.request("config.apply", {"set": {"tx": tx_of(d.i) - 1}, "confirm": {"within_s": 60}})
    assert r["status"] == "busy" and r["error"].startswith("job-open:"), "and so after the reboot"

    # The same manifest again, with a later window: the same job, the same token, next as kept.
    time.sleep(1.1)
    msha = stage_parts(d, text)
    assert offer(d, msha, window=20, rid="lp-1")["error"] == "bad-args", "a resume needs a window later than the last"
    r = offer(d, msha, window=7200, rid="lp-1")
    assert r["status"] == "ok", r
    again = r["result"]
    assert again["transfer_token"] == res["transfer_token"], "a resume never issues a new token"
    assert (again["job"], again["state"], again["next"], again["frames_total"]) == ("lp-1", "progress", 72, 261), again
    g = job_get(d, "lp-1")
    assert (g["state"], g["next"], g["window_end"]) == ("progress", 72, RTC + 7200) and "reason" not in g, g
    t = Transfer(d, img, again, "lp-1")
    got = t.run(72)
    assert got == (256, 261, 0b11111), got
    install_and_confirm(n, img, "lp-1")


def test_a_transfer_cut_by_a_reboot_comes_back_paused_and_goes_on_from_the_kept_next(nodes):
    img = image(30000)
    n, d = start(nodes, "lc", 63)
    text = manifest_for(d, img)
    res = offer(d, stage_parts(d, text), rid="lc-1")["result"]
    t = Transfer(d, img, res, "lc-1")
    for base in range(0, 72, 8):
        assert t.block(base) == (base, base + 8, 0xFF)
    assert t.send(72) is None
    n.restart()   # the power goes mid-transfer
    d = Dev(n)
    t.d = d   # the manager reconnects
    g = job_get(d, "lc-1")
    assert g["state"] == "paused" and g["reason"] == "manager", g
    kept = g["next"]
    assert 0 < kept <= 73 and g["frames_total"] == 261, "what the job table last saved: %s" % g
    assert t.send(kept) is None and job_get(d, "lc-1")["next"] == kept, "no frame is taken until the manager resumes it"
    # The same manifest, offered again under another request id (a signed request cannot repeat its job id): it is the same job.
    time.sleep(1.1)
    r = offer(d, stage_parts(d, text), rid="lc-2", window=7200)
    assert r["status"] == "ok", r
    again = r["result"]
    assert (again["job"], again["transfer_token"], again["next"]) == ("lc-1", res["transfer_token"], kept), again
    assert d.i.request("update.status")["result"]["job"]["job"] == "lc-1"
    t = Transfer(d, img, again, "lc-1")
    t.run(kept)
    install_and_confirm(n, img, "lc-1")


@pytest.mark.parametrize("lora, error", [
    ({"block": 0, "window_end": RTC + 600}, "bad-args"),
    ({"block": 9, "window_end": RTC + 600}, "bad-args"),
    ({"block": "8", "window_end": RTC + 600}, "bad-args"),
    ({"block": 8}, "bad-args"),
    ({"window_end": RTC + 600}, "bad-args"),
    ({"block": 8, "window_end": RTC}, "bad-args"),
    ({"block": 8, "window_end": RTC + 12 * 3600 + 1}, "bad-args"),
    ({"block": 8, "window_end": RTC + 600, "extra": 1}, "bad-args"),
    ("window", "bad-args"),
])
def test_a_malformed_lora_source_is_invalid_and_opens_nothing(nodes, lora, error):
    img = image(3000)
    n, d = start(nodes, "lv", 64)
    msha = stage_parts(d, manifest_for(d, img))
    r = d.i.request("update.offer", {"manifest_sha256": msha, "issued_at": RTC, "source": {"lora": lora}}, rid="lv-1")
    assert (r["status"], r.get("error")) == ("invalid", error), r
    assert d.i.request("update.status")["result"]["job"] is None


def test_the_rest_of_the_lora_offer_refusals(nodes):
    img = image(3000)
    n, d = start(nodes, "lr", 65)
    msha = stage_parts(d, manifest_for(d, img))
    ok = {"block": 8, "window_end": RTC + 600}
    r = d.i.request("update.offer", {"manifest_sha256": msha, "issued_at": RTC, "source": {"lora": dict(ok, preset="fast")}}, rid="lr-1")
    assert (r["status"], r["error"]) == ("refused", "unsupported"), "the fast preset is a later slice: %s" % r
    r = d.i.request("update.offer", {"manifest_sha256": msha, "source": {"lora": ok}}, rid="lr-2")
    assert (r["status"], r["error"]) == ("invalid", "bad-args"), "no issued_at"
    r = d.i.request("update.offer", {"manifest_sha256": msha, "issued_at": RTC, "source": {"lora": ok, "push": True}}, rid="lr-3")
    assert (r["status"], r["error"]) == ("invalid", "bad-args"), "one source at a time"
    r = d.i.request("update.offer", {"manifest_sha256": "ab" * 32, "issued_at": RTC, "source": {"lora": ok}}, rid="lr-4")
    assert (r["status"], r["error"]) == ("refused", "manifest-not-staged"), "then preflight, as for a push"
    r = d.i.request("update.offer", {"manifest_sha256": msha, "issued_at": RTC, "source": {"lora": ok}, "confirm": {"within_s": 5}}, rid="lr-5")
    assert r["status"] == "invalid", "the confirmation window is 30 to 3,600 s"
    assert d.i.request("update.status")["result"]["job"] is None, "none of them opened a job"
    r = d.i.request("update.offer", {"manifest_sha256": msha, "issued_at": RTC, "source": {"lora": ok}}, rid="lr-6")
    assert r["status"] == "ok", r


def test_a_board_that_installs_through_its_bootloader_has_no_lora_source(nodes):
    n, d = dfu_board(nodes, "ld", 66)
    msha = stage_dfu(d)
    r = d.i.request("update.offer", {"manifest_sha256": msha, "issued_at": RTC,
                                     "source": {"lora": {"block": 8, "window_end": RTC + 600}}}, rid="ld-1")
    assert (r["status"], r["error"]) == ("refused", "unsupported-source"), r
    assert d.i.request("update.status")["result"]["job"] is None


@pytest.mark.timescale(10)
def test_any_other_offer_supersedes_a_paused_transfer(nodes):
    img = image(9000)
    n, d = start(nodes, "ls", 67)
    res = offer(d, stage_parts(d, manifest_for(d, img)), window=20, rid="ls-1")["result"]
    t = Transfer(d, img, res, "ls-1")
    assert t.block(0) == (0, 8, 0xFF)
    wait_for(lambda: job_get(d, "ls-1")["state"] == "paused", "pause at the window")
    time.sleep(6.5)   # past the 60 s between two offers, at ten times real time
    other = image(6000, b"other")
    r = d.i.request("update.offer", {"manifest_sha256": stage_parts(d, manifest_for(d, other)),
                                     "source": {"push": True}}, rid="ls-2")
    assert r["status"] == "ok", r
    g = job_get(d, "ls-1")
    assert g["state"] == "cancelled" and g["result"]["reason"] == "superseded", g
    assert "frames_total" not in g, "a cancelled job reports no transfer"
    assert d.i.request("update.status")["result"]["job"]["job"] == "ls-2"


@pytest.mark.timescale(10)
def test_cancelling_a_paused_transfer_ends_it(nodes):
    img = image(9000)
    n, d = start(nodes, "lk", 68)
    res = offer(d, stage_parts(d, manifest_for(d, img)), window=20, rid="lk-1")["result"]
    t = Transfer(d, img, res, "lk-1")
    assert t.block(0) == (0, 8, 0xFF)
    wait_for(lambda: job_get(d, "lk-1")["state"] == "paused", "pause at the window")
    r = d.i.request("job.cancel", {"job": "lk-1"})
    assert r["status"] == "ok" and r["result"]["state"] == "cancelled", r
    time.sleep(0.5)
    assert d.i.request("update.status")["result"]["job"] is None, "the lock is released"
    assert t.send(8) is None, "no transfer takes frames now"


@pytest.mark.timescale(10)
def test_a_transfer_nobody_resumes_for_seven_days_fails_stalled(nodes):
    """A pause waits 7 days for its manager. The wait is counted in the job table, so a reboot does not restart it: the node
    is started again at 20,000 times real time (7 days in about 30 s) and the paused job runs out."""
    img = image(9000)
    n, d = start(nodes, "lz", 69)
    res = offer(d, stage_parts(d, manifest_for(d, img)), window=20, rid="lz-1")["result"]
    assert res["state"] == "accepted"
    wait_for(lambda: job_get(d, "lz-1")["state"] == "paused", "pause at the window")
    n.time_scale = 20000
    n.restart()
    d = Dev(n)
    assert job_get(d, "lz-1")["state"] == "paused"
    g = wait_for(lambda: job_get(d, "lz-1")["state"] == "failed" and job_get(d, "lz-1"), "the seven days", 120)
    assert g["result"]["error"] == "stalled" and "frames_total" not in g, g
    assert g["result"]["running"]["version"], g
    assert d.i.request("update.status")["result"]["job"] is None, "the lock is released"


def test_a_manifest_in_parts(nodes):
    img = image(3000)
    n, d = start(nodes, "lm", 70)
    text = manifest_for(d, img)
    args = parts_of(text, each=60)   # more than four parts
    assert len(args) > 4
    r = d.i.request("update.manifest.part", args[0])
    assert (r["status"], r["error"]) == ("invalid", "bad-args"), "at most 4 parts"
    args = parts_of(text)
    assert len(args) == 4, len(args)
    sha = args[0]["sha256"]
    for i in (0, 1):
        r = d.i.request("update.manifest.part", args[i])
        assert r["status"] == "ok" and r["result"] == {"sha256": sha, "part": i + 1, "parts": 4, "held": i + 1}, r
    assert d.i.request("update.status")["result"]["staged"] == [], "nothing is staged until every part is here"
    # Another manifest's part discards what is held.
    other = parts_of(manifest_for(d, image(2000, b"x")))
    r = d.i.request("update.manifest.part", other[0])
    assert r["status"] == "ok" and r["result"]["held"] == 1, r
    r = d.i.request("update.manifest.part", args[0])
    assert r["result"]["held"] == 1, "the first manifest starts again, with none of its earlier parts"
    for i in (1, 2):
        r = d.i.request("update.manifest.part", args[i])
        assert r["result"]["held"] == i + 1, r
    time.sleep(1.1)
    r = d.i.request("update.manifest.part", args[3])
    assert r["status"] == "ok" and r["result"]["sha256"] == sha and r["result"]["version"] == NEW, r
    assert "preflight" in r["result"]
    staged = d.i.request("update.status")["result"]["staged"]
    assert staged == [{"sha256": sha, "version": NEW}], staged
    # What update.manifest answers for the same text.
    time.sleep(1.1)
    whole = d.stage(text)
    assert whole["status"] == "ok" and whole["result"] == r["result"], (whole, r)
    # A part whose hash is not that of the text it carries is refused at the end, and what it held is spent.
    time.sleep(1.1)
    lie = [dict(a, sha256="cd" * 32) for a in args]
    for a in lie[:3]:
        assert d.i.request("update.manifest.part", a)["status"] == "ok"
    r = d.i.request("update.manifest.part", lie[3])
    assert (r["status"], r["error"]) == ("invalid", "bad-args"), r
    # Bad parts: outside the numbering, not canonical base64, empty.
    for bad in (dict(args[0], part=0), dict(args[0], part=5), dict(args[0], parts=5), dict(args[0], data=""),
                dict(args[0], data=args[0]["data"][:-1]), dict(args[0], sha256="zz")):
        r = d.i.request("update.manifest.part", bad)
        assert (r["status"], r["error"]) == ("invalid", "bad-args"), (bad, r)
    # A signature that is not the release key's is refused as update.manifest refuses it.
    time.sleep(1.1)
    forged = parts_of(text, seed=hashlib.sha256(b"someone else").digest())
    for a in forged[:3]:
        assert d.i.request("update.manifest.part", a)["status"] == "ok"
    r = d.i.request("update.manifest.part", forged[3])
    assert r["status"] == "refused" and r["error"] == "bad-release-signature", r
