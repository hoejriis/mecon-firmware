"""update.offer and update.chunk on the simulated standalone Companion: a pushed image is written to the inactive slot,
read back against the manifest's digest, activated, attested by the image that boots, and confirmed or reverted
(docs/contract/UPDATES.md sections 5 to 7).

The simulated board keeps its two slots as files beside the data directory, and the start of a node is its bootloader
(src/helpers/updates/UpdSlot.cpp): an image activated and reset before it is marked valid comes back as the previous one,
as on an ESP32 with app rollback. An image is any bytes that start with the ESP32 image magic, 0xE9. Its version is
whatever "<dir>.version" declares, since a build container without git tags compiles "unknown"."""
import base64
import hashlib
import json
import os
import re
import time

import pytest
from test_deployment_authority import boot as dep_boot, wrap
from test_device_interface import ble_boot
from test_updates import Dev, RTC

import mecon_iface  # noqa: E402  (tools/, on the path once test_updates is imported)

OLD = "v1.18.0-mecon.0.50.0"
NEW = "v1.18.0-mecon.99.0.0"
FACTORY_SHA = hashlib.sha256(b"mecon-sim-factory-image").hexdigest()
CHUNK = 2304


def image(n=9000, seed=b"img"):
    """An image of n bytes: the ESP32 image magic, then bytes derived from seed."""
    body = b""
    k = 0
    while len(body) < n - 1:
        body += hashlib.sha256(seed + str(k).encode()).digest()
        k += 1
    return b"\xe9" + body[:n - 1]


def declare(node, version):
    with open(node.dir.rstrip("/") + ".version", "w") as f:
        f.write(version + "\n")


def start(nodes, name, seed, version=OLD):
    n = nodes("companion_standalone", name, seed=seed, rtc=RTC)
    declare(n, version)
    return n, Dev(n.start())


def stage(d, img, **over):
    fields = {"version": NEW, "image": {"size": len(img), "sha256": hashlib.sha256(img).hexdigest()}}
    fields.update(over)
    text = d.manifest(**fields)
    r = d.stage(text)
    assert r["status"] == "ok", r
    return r["result"]["sha256"]


def offer(d, msha, within_s=120, rid=None):
    return d.i.request("update.offer", {"manifest_sha256": msha, "source": {"push": True},
                                        "confirm": {"within_s": within_s}}, rid=rid)


def push(d, job, token, img, start_at=0):
    """Every chunk from start_at, in order; the last answer."""
    r = None
    for off in range(start_at, len(img), CHUNK):
        data = base64.b64encode(img[off:off + CHUNK]).decode()
        r = d.i.request("update.chunk", {"job": job, "token": token, "offset": off, "data": data})
        assert r["status"] == "ok" and r["result"]["next"] == min(off + CHUNK, len(img)), r
    return r


def install(nodes, name, seed, img=None, within_s=120):
    """A device that has received, verified and activated img, and rebooted into it. Returns (node, device, job)."""
    img = img or image()
    n, d = start(nodes, name, seed)
    r = offer(d, stage(d, img), within_s)
    assert r["status"] == "ok" and r["result"]["state"] == "accepted", r
    job, token = r["result"]["job"], r["result"]["transfer_token"]
    push(d, job, token, img)
    assert n.wait_exit(15) == 75, "activating the image reboots the device"
    declare(n, NEW)   # the image that boots next is the new one
    n.start()
    return n, Dev(n), job


def job_get(d, job):
    r = d.i.request("job.get", {"job": job})
    assert r["status"] == "ok", r
    return r["result"]


def test_a_pushed_image_installs_attests_and_is_confirmed(nodes):
    img = image()
    n, d = start(nodes, "ui", 41)
    for op in ("update.offer", "update.chunk"):
        assert op in d.hello["capabilities"]
    r = offer(d, stage(d, img), rid="off-1")
    assert r["status"] == "ok", r
    res = r["result"]
    assert res["job"] == "off-1" and res["state"] == "accepted" and res["next"] == 0, res
    token = res["transfer_token"]
    assert re.fullmatch(r"[0-9a-f]{32}", token), "16 random bytes, hex"
    # The token is a credential: only the offer's own result carries it.
    for text in (json.dumps(d.i.request("update.status")), json.dumps(d.i.request("job.get", {"job": "off-1"})),
                 json.dumps(d.i.request("job.list"))):
        assert token not in text
    s = d.i.request("update.status")["result"]["job"]
    assert s == {"job": "off-1", "state": "accepted", "received": 0, "next": 0, "size": len(img), "version": NEW,
                 "sha256": hashlib.sha256(img).hexdigest()}, s

    # Resuming: a chunk at another offset gets the offset expected back, and writes nothing.
    r = d.i.request("update.chunk", {"job": "off-1", "token": token, "offset": 100, "data": "AAAA"})
    assert r["status"] == "ok" and r["result"] == {"next": 0}, r
    push(d, "off-1", token, img[:CHUNK])
    assert job_get(d, "off-1")["state"] == "progress"
    push(d, "off-1", token, img, start_at=CHUNK)
    assert n.wait_exit(15) == 75, "verified and activated: the device reboots into the image"

    declare(n, NEW)
    n.start()
    d = Dev(n)
    att = {"job": "off-1", "version": NEW, "sha256": hashlib.sha256(img).hexdigest()}
    assert d.hello["update"] == att, "the image that boots attests"
    assert d.i.request("status.get")["result"]["update"] == att
    g = job_get(d, "off-1")
    assert g["state"] == "awaiting-confirm" and g["result"]["update"] == att, g

    r = d.i.request("job.confirm", {"job": "off-1"})
    assert r["status"] == "ok" and r["result"]["state"] == "confirmed", r
    g = job_get(d, "off-1")
    assert g["result"]["running"] == {"version": NEW, "sha256": att["sha256"]}, g
    assert "update" not in d.i.request("hello")["result"], "confirmed: nothing left to attest"
    assert d.i.request("update.status")["result"]["job"] is None

    n.restart()   # the confirmed image stays, though the restart comes before its start-up checks would have passed
    d = Dev(n)
    assert "update" not in d.hello
    assert job_get(d, "off-1")["state"] == "confirmed"
    assert d.i.request("update.status")["result"]["running"]["sha256"] == att["sha256"]


def test_a_chunk_needs_the_jobs_token(nodes):
    img = image(3000)
    n, d = start(nodes, "ut", 42)
    r = offer(d, stage(d, img))
    job, token = r["result"]["job"], r["result"]["transfer_token"]
    wrong = ("0" if token[0] != "0" else "1") + token[1:]
    for args in ({"job": job, "token": wrong, "offset": 0, "data": "6Q=="},
                 {"job": "other", "token": token, "offset": 0, "data": "6Q=="}):
        r = d.i.request("update.chunk", args)
        assert (r["status"], r.get("error")) == ("refused", "bad-token"), r
    for data in ("6Q", "6Q==\n", "6R==", "!!!!"):   # not canonical base64
        r = d.i.request("update.chunk", {"job": job, "token": token, "offset": 0, "data": data})
        assert r["status"] == "invalid", (data, r)
    assert d.i.request("update.status")["result"]["job"]["received"] == 0
    push(d, job, token, img[:CHUNK])
    r = d.i.request("update.chunk", {"job": job, "token": token, "offset": CHUNK,
                                     "data": base64.b64encode(img[:CHUNK]).decode()})
    assert (r["status"], r.get("error")) == ("refused", "too-large"), "past the manifest's image size"
    assert d.i.request("update.status")["result"]["job"]["received"] == CHUNK, "nothing written, the job still open"


def test_a_wrong_digest_fails_and_activates_nothing(nodes):
    img = image(5000)
    n, d = start(nodes, "uw", 43)
    text = d.manifest(version=NEW, image={"size": len(img), "sha256": hashlib.sha256(b"other").hexdigest()})
    assert d.stage(text)["status"] == "ok"
    r = offer(d, hashlib.sha256(text.encode()).hexdigest())
    job, token = r["result"]["job"], r["result"]["transfer_token"]
    push(d, job, token, img)
    end = time.time() + 10
    while job_get(d, job)["state"] != "failed" and time.time() < end:
        time.sleep(0.1)
    g = job_get(d, job)
    assert g["state"] == "failed" and g["result"]["error"] == "digest-mismatch", g
    assert g["result"]["running"] == {"version": OLD, "sha256": FACTORY_SHA}, "the result names what still runs"
    assert n.proc.poll() is None, "nothing activated, so no reboot"
    r = d.i.request("update.chunk", {"job": job, "token": token, "offset": 0, "data": "6Q=="})
    assert r["error"] == "bad-token", "the token died with the job"


def test_an_image_the_platform_does_not_accept_fails(nodes):
    img = b"\x00" + image(4000)[1:]   # the digest matches, but it is not an ESP32 image
    n, d = start(nodes, "ub", 44)
    r = offer(d, stage(d, img))
    push(d, r["result"]["job"], r["result"]["transfer_token"], img)
    end = time.time() + 10
    while job_get(d, r["result"]["job"])["state"] != "failed" and time.time() < end:
        time.sleep(0.1)
    assert job_get(d, r["result"]["job"])["result"]["error"] == "bad-image"
    assert n.proc.poll() is None


def test_offers_that_preflight_refuses(nodes):
    img = image(3000)
    n, d = start(nodes, "uo", 45)
    r = offer(d, "ab" * 32)
    assert (r["status"], r.get("error")) == ("refused", "manifest-not-staged"), r
    msha = stage(d, img)
    r = d.i.request("update.offer", {"manifest_sha256": msha, "source": {"url": "https://example.invalid/i.bin"}})
    assert (r["status"], r.get("error")) == ("refused", "unsupported-source"), "IP delivery is a later stage"
    r = d.i.request("update.offer", {"manifest_sha256": msha, "source": {"push": True}, "confirm": {"within_s": 29}})
    assert r["status"] == "invalid", "the window is 30 to 3,600 s"
    time.sleep(1.05)
    old = stage(d, img, version="v1.18.0-mecon.0.49.0", min_from="v1.18.0-mecon.0.0.0")
    r = offer(d, old)
    assert (r["status"], r.get("error")) == ("refused", "downgrade-refused"), r
    r = offer(d, msha)
    assert r["status"] == "ok", r
    time.sleep(1.05)
    r = offer(d, msha)
    assert (r["status"], r.get("error")) == ("busy", "rate"), "a second erase of the slot within 60 s"


def test_job_cancel_ends_the_transfer(nodes):
    img = image(6000)
    n, d = start(nodes, "uc", 46)
    r = offer(d, stage(d, img))
    job, token = r["result"]["job"], r["result"]["transfer_token"]
    push(d, job, token, img[:CHUNK])
    r = d.i.request("job.cancel", {"job": job})
    assert r["status"] == "ok" and r["result"]["state"] == "cancelled", r
    time.sleep(0.5)
    g = job_get(d, job)
    assert g["result"]["running"] == {"version": OLD, "sha256": FACTORY_SHA}, g
    r = d.i.request("update.chunk", {"job": job, "token": token, "offset": CHUNK, "data": "6Q=="})
    assert r["error"] == "bad-token", r
    assert d.i.request("update.status")["result"]["job"] is None


@pytest.mark.timescale(10)
def test_a_new_offer_supersedes_a_transfer_in_progress(nodes):
    img = image(6000)
    n, d = start(nodes, "us", 47)
    a = offer(d, stage(d, img))["result"]
    push(d, a["job"], a["transfer_token"], img[:CHUNK])
    time.sleep(6.5)   # past the 60 s between two offers, at ten times real time
    b = offer(d, stage(d, image(6000, b"second")))
    assert b["status"] == "ok", b
    g = job_get(d, a["job"])
    assert g["state"] == "cancelled" and g["result"]["reason"] == "superseded", g
    assert d.i.request("update.status")["result"]["job"]["job"] == b["result"]["job"]


@pytest.mark.timescale(10)
def test_the_deadline_reverts_to_the_previous_image(nodes):
    n, d, job = install(nodes, "ud", 48, within_s=30)
    assert job_get(d, job)["state"] == "awaiting-confirm"
    assert n.wait_exit(20) == 75, "no confirmation within 30 running seconds: the device reverts and reboots"
    declare(n, OLD)
    n.start()
    d = Dev(n)
    g = job_get(d, job)
    assert g["state"] == "reverted" and g["result"]["reverted"] is True, g
    assert g["result"]["running"] == {"version": OLD, "sha256": FACTORY_SHA}, "it names the image the reboot returned to"
    assert "update" not in d.hello
    assert d.i.request("update.status")["result"]["running"] == {"version": OLD, "sha256": FACTORY_SHA}, \
        "the previous image is what runs again"
    r = d.i.request("job.confirm", {"job": job})
    assert r["status"] == "invalid" and r["error"] == "not-awaiting", "too late"


@pytest.mark.timescale(10)
def test_an_abnormal_reset_reverts_at_once(nodes):
    n, d, job = install(nodes, "ua", 49)
    assert job_get(d, job)["state"] == "awaiting-confirm"
    time.sleep(2.5)   # past the start-up checks (20 s): from here the bootloader no longer rolls it back
    n.stop()
    with open(n.dir.rstrip("/") + ".reset", "w") as f:
        f.write("Panic / exception reset\n")
    n.start()
    assert n.wait_exit(10) == 75, "a panic while awaiting confirmation reverts, and the reboot returns to the old image"
    declare(n, OLD)
    n.start()
    d = Dev(n)
    g = job_get(d, job)
    assert g["state"] == "reverted" and g["result"]["running"]["sha256"] == FACTORY_SHA, g
    assert d.i.request("update.status")["result"]["running"]["sha256"] == FACTORY_SHA


@pytest.mark.timescale(10)
def test_a_normal_reboot_does_not_revert(nodes):
    """Anyone allowed to reboot could otherwise veto a security update (section 7, step 5)."""
    n, d, job = install(nodes, "un", 50)
    time.sleep(2.5)   # past the start-up checks
    n.restart()
    d = Dev(n)
    assert job_get(d, job)["state"] == "awaiting-confirm"
    assert d.hello["update"]["job"] == job


def test_a_reset_before_the_image_is_marked_valid_rolls_back(nodes):
    """Stage 1: the bootloader returns to the previous slot when the new image is reset before its start-up checks."""
    img = image()
    n, d = start(nodes, "ur", 51)
    r = offer(d, stage(d, img))["result"]
    push(d, r["job"], r["transfer_token"], img)
    assert n.wait_exit(15) == 75
    declare(n, NEW)
    n.start()
    n.stop()   # well inside the 20 s before the image is marked valid
    declare(n, OLD)
    n.start()
    d = Dev(n)
    g = job_get(d, r["job"])
    assert g["state"] == "reverted" and g["result"]["error"] == "rolled-back", g
    assert g["result"]["running"]["sha256"] == FACTORY_SHA


def test_an_enrolled_device_installs_only_on_a_signed_offer(nodes):
    d = dep_boot(nodes, "ue")
    declare(d.n, OLD)
    d.reboot()
    assert d.provision()["status"] == "ok"
    u = Dev.__new__(Dev)   # test_updates' manifest and stage helpers, on the same link
    u.c, u.i = d.c, d.i
    u.hello = d.i.request("hello")["result"]
    img = image(3000)
    msha = stage(u, img)
    r = offer(u, msha)
    assert (r["status"], r.get("error")) == ("refused", "unauthenticated"), "the cable no longer installs unsigned"
    args = {"manifest_sha256": msha, "source": {"push": True}, "confirm": {"within_s": 300}}
    r = d.send(wrap(d.node_key, "update.offer", args, job=7))
    assert r["status"] == "ok", r
    job = r["result"]["job"]
    assert job.startswith("#") and job.endswith(":7"), job
    push(u, job, r["result"]["transfer_token"], img)   # the token, not a signature, authorises the chunks
    assert d.n.wait_exit(15) == 75
    declare(d.n, NEW)
    d.n.start()
    d.attach()
    r = d.i.request("job.confirm", {"job": job})
    assert (r["status"], r.get("error")) == ("refused", "unauthenticated"), "an unsigned confirmation does not count"
    r = d.send(wrap(d.node_key, "job.confirm", {"job": job}, job=8))
    assert r["status"] == "ok" and r["result"]["state"] == "confirmed", r


def test_an_image_whose_record_is_lost_goes_back_to_the_previous_one(nodes):
    """Without the record that names the job's slots and digest, the new image cannot show it is the one installed. It
    boots on trial, so the slot it replaced is known: the device goes back to it, and nothing is left unconfirmed."""
    img = image()
    n, d = start(nodes, "uv", 52)
    r = offer(d, stage(d, img))["result"]
    push(d, r["job"], r["transfer_token"], img)
    assert n.wait_exit(15) == 75
    os.remove(n.dir.rstrip("/") + ".updjob")
    declare(n, NEW)
    n.start()
    assert n.wait_exit(10) == 75, "unproven: back to the previous image"
    declare(n, OLD)
    n.start()
    d = Dev(n)
    g = job_get(d, r["job"])
    assert g["state"] == "reverted" and g["result"]["error"] == "unproven", g
    assert d.i.request("update.status")["result"]["running"]["sha256"] == FACTORY_SHA


def test_an_image_that_is_not_the_installed_one_goes_back(nodes):
    """The new slot runs but its image does not hash to the manifest's digest: not proven, so back to the previous one."""
    img = image()
    n, d = start(nodes, "uh", 54)
    r = offer(d, stage(d, img))["result"]
    push(d, r["job"], r["transfer_token"], img)
    assert n.wait_exit(15) == 75
    with open(n.dir.rstrip("/") + ".ota1", "r+b") as f:   # the written slot (the node started on slot 0)
        f.seek(len(img) - 1)
        f.write(b"\x00" if img[-1] != 0 else b"\x01")
    declare(n, NEW)
    n.start()
    assert n.wait_exit(10) == 75
    declare(n, OLD)
    n.start()
    d = Dev(n)
    g = job_get(d, r["job"])
    assert g["state"] == "reverted" and g["result"]["error"] == "unproven", g
    assert d.i.request("update.status")["result"]["running"]["sha256"] == FACTORY_SHA


def test_a_cancel_before_the_reboot_keeps_the_previous_image(nodes):
    """Between activation and the reboot that follows it, the previous image still runs: a cancel then points the next
    boot back at it, rather than claiming a revert while the new image boots."""
    img = image()
    n, d = start(nodes, "ux", 55)
    r = offer(d, stage(d, img))["result"]
    push(d, r["job"], r["transfer_token"], img)
    end = time.time() + 5
    while job_get(d, r["job"])["state"] != "awaiting-confirm" and time.time() < end:
        time.sleep(0.02)
    c = d.i.request("job.cancel", {"job": r["job"]})
    assert c["status"] == "ok" and c["result"]["state"] == "reverted", c
    assert n.wait_exit(10) == 75, "the activation's reboot still happens"
    n.start()
    d = Dev(n)
    assert d.i.request("update.status")["result"]["running"]["sha256"] == FACTORY_SHA, "the previous image boots"
    assert "update" not in d.hello


def test_no_confirmation_before_the_new_image_runs(nodes):
    """Activated but not yet rebooted: the job awaits confirmation, but its image does not run, so a confirmation now
    would vouch for an image nothing has seen boot."""
    img = image()
    n, d = start(nodes, "uq2", 60)
    r = offer(d, stage(d, img))["result"]
    push(d, r["job"], r["transfer_token"], img)
    end = time.time() + 5
    while job_get(d, r["job"])["state"] != "awaiting-confirm" and time.time() < end:
        time.sleep(0.02)
    c = d.i.request("job.confirm", {"job": r["job"]})
    assert (c["status"], c.get("error")) == ("failed", "confirm-failed"), c
    assert n.wait_exit(10) == 75


def test_a_lost_job_table_does_not_leave_an_unconfirmed_image(nodes):
    img = image()
    n, d = start(nodes, "uj", 56)
    r = offer(d, stage(d, img))["result"]
    push(d, r["job"], r["transfer_token"], img)
    assert n.wait_exit(15) == 75
    os.remove(os.path.join(n.dir, "mecon_jobs"))
    declare(n, NEW)
    n.start()
    assert n.wait_exit(10) == 75, "no job to confirm it: back to the previous image"
    declare(n, OLD)
    n.start()
    assert Dev(n).i.request("update.status")["result"]["running"]["sha256"] == FACTORY_SHA


def test_a_confirmed_image_stays_when_the_job_table_is_lost(nodes):
    n, d, job = install(nodes, "uy", 57)
    assert d.i.request("job.confirm", {"job": job})["result"]["state"] == "confirmed"
    n.stop()
    os.remove(os.path.join(n.dir, "mecon_jobs"))
    n.start()
    d = Dev(n)
    s = d.i.request("update.status")["result"]
    assert s["running"]["sha256"] == hashlib.sha256(image()).hexdigest(), "the record says it was confirmed"
    assert not os.path.exists(n.dir.rstrip("/") + ".updjob"), "and the record is done with"


def test_a_confirmation_the_job_table_never_saved_still_counts(nodes):
    """A power cut after job.confirm's record write and before the job table's: the record says confirmed, so the job
    is confirmed at the next boot, not reverted. The record's layout on the simulated board: its confirmed flag is the
    byte at offset 188."""
    n, d, job = install(nodes, "uf", 58)
    n.stop()
    path = n.dir.rstrip("/") + ".updjob"
    with open(path, "r+b") as f:
        f.seek(188)
        f.write(b"\x01")
    n.start()
    d = Dev(n)
    g = job_get(d, job)
    assert g["state"] == "confirmed" and g["result"]["confirmed_by"] == "record", g


def test_no_offer_while_the_new_image_is_on_trial(nodes):
    n, d, job = install(nodes, "up", 59)
    # Its own id: the client counts from r-1 again after the reboot, and r-3 names the install's job.
    r = offer(d, stage(d, image(3000, b"next")), rid="next-1")
    assert (r["status"], r.get("error")) == ("busy", "verifying"), r


@pytest.mark.timescale(60)
def test_a_transfer_with_no_chunk_for_ten_minutes_fails(nodes):
    img = image(5000)
    n, d = start(nodes, "uz", 53)
    r = offer(d, stage(d, img))["result"]
    # One small chunk, in one frame: at sixty times real time a request spread over frames would outlast its reassembly.
    x = d.i.request("update.chunk", {"job": r["job"], "token": r["transfer_token"], "offset": 0,
                                     "data": base64.b64encode(img[:120]).decode()})
    assert x["status"] == "ok" and x["result"] == {"next": 120}, x
    end = time.time() + 20
    while job_get(d, r["job"])["state"] != "failed" and time.time() < end:
        time.sleep(0.2)
    g = job_get(d, r["job"])
    assert g["state"] == "failed" and g["result"]["error"] == "stalled", g
    assert d.i.request("update.status")["result"]["job"] is None, "the one-job lock is released"


@pytest.mark.timescale(10)
def test_the_token_not_the_link_authorises_a_chunk(nodes):
    """A chunk is authorised by its job's transfer token in place of the class check (section 5): a link the class
    table refuses everything, here BLE paired with a published default PIN, pushes the bytes of a job opened on the
    cable, and opens no job itself."""
    n = ble_boot(nodes, "uk", 123456)
    declare(n, OLD)
    n.restart()
    d = Dev(n)
    img = image(5000)
    msha = stage(d, img)
    b = n.connect_ble(123456)
    b.app_start()
    bi = mecon_iface.IfaceClient(b._send, b._read_frame)
    r = bi.request("update.offer", {"manifest_sha256": msha, "source": {"push": True}}, rid="ble-1")
    assert r["status"] == "refused", "the class table refuses this link an offer"
    r = offer(d, msha)["result"]
    data = base64.b64encode(img[:CHUNK]).decode()
    wrong = ("0" if r["transfer_token"][0] != "0" else "1") + r["transfer_token"][1:]
    x = bi.request("update.chunk", {"job": r["job"], "token": wrong, "offset": 0, "data": data}, rid="ble-2")
    assert (x["status"], x.get("error")) == ("refused", "bad-token"), x
    # Own ids: the device keeps one result cache for every link, and both clients count from r-1.
    x = bi.request("update.chunk", {"job": r["job"], "token": r["transfer_token"], "offset": 0, "data": data}, rid="ble-3")
    assert x["status"] == "ok" and x["result"] == {"next": CHUNK}, x
