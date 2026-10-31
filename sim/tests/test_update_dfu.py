"""The nRF52 path of signed updates (docs/contract/UPDATES.md section 6, kind bootloader-dfu) on a simulated board that
plays an nRF52 ("<dir>.dfu-board"): the offer runs preflight, opens a job that spans the bootloader step, and resets into
the bootloader's serial DFU, which the simulated board shows by exiting with status 76. The test then plays the
bootloader: it installs the image by declaring the new version, and starts the node. The image that boots attests by
version, since the device cannot hash the package the manifest names; there is no slot to revert to."""
import hashlib
import time

import pytest
from test_device_interface import mecon_schema
from test_update_install import NEW, OLD, declare, job_get
from test_updates import Dev, RTC

PACKAGE = b"PK\x03\x04 a serial-DFU package, as the Reader would send it"


def dfu_board(nodes, name, seed):
    n = nodes("companion_standalone", name, seed=seed, rtc=RTC)
    open(n.dir.rstrip("/") + ".dfu-board", "w").close()
    declare(n, OLD)
    return n, Dev(n.start())


def stage_dfu(d, **over):
    fields = {"version": NEW, "kind": "bootloader-dfu",
              "image": {"size": len(PACKAGE), "sha256": hashlib.sha256(PACKAGE).hexdigest()}}
    fields.update(over)
    text = d.manifest(**fields)
    r = d.stage(text)
    assert r["status"] == "ok", r
    return r["result"]["sha256"]


def offer(d, msha, within_s=120, rid=None, source=None):
    return d.i.request("update.offer", {"manifest_sha256": msha, "source": source or {"push": True},
                                        "confirm": {"within_s": within_s}}, rid=rid)


def test_a_dfu_offer_resets_into_the_bootloader_and_the_new_image_attests(nodes):
    n, d = dfu_board(nodes, "nf1", 91)
    r = offer(d, stage_dfu(d), rid="nf-1")
    assert r["status"] == "ok", r
    assert r["result"] == {"job": "nf-1", "state": "awaiting-confirm", "dfu": "serial"}, "no token: the bootloader takes it"
    assert n.wait_exit(10) == 76, "into the bootloader once the answer has gone out"
    declare(n, NEW)   # the bootloader installed the package
    n.start()
    d = Dev(n)
    assert d.hello["update"] == {"job": "nf-1", "version": NEW, "sha256": None}, "attested by version"
    assert not mecon_schema.check(d.hello, mecon_schema.load("hello.result.json"))
    st = d.i.request("status.get", {})["result"]
    assert st["update"]["sha256"] is None and not mecon_schema.check(st, mecon_schema.load("status.get.result.json")), st
    g = job_get(d, "nf-1")
    assert g["state"] == "awaiting-confirm" and g["result"]["update"]["version"] == NEW, g
    r = d.i.request("job.confirm", {"job": "nf-1"})
    assert r["status"] == "ok" and r["result"]["state"] == "confirmed", r
    n.restart()
    assert "update" not in Dev(n).hello


def test_a_dfu_that_did_not_happen_ends_not_installed(nodes):
    n, d = dfu_board(nodes, "nf2", 92)
    r = offer(d, stage_dfu(d))["result"]
    assert n.wait_exit(10) == 76
    n.start()   # the old image again: the DFU was abandoned
    d = Dev(n)
    g = job_get(d, r["job"])
    assert g["state"] == "failed" and g["result"]["error"] == "not-installed", g
    assert "update" not in d.hello


@pytest.mark.timescale(10)
def test_an_unconfirmed_dfu_image_stays_and_the_job_says_so(nodes):
    """Section 7, step 6: no slot to return to. The deadline ends the job unconfirmed, and the new image stays."""
    n, d = dfu_board(nodes, "nf3", 93)
    r = offer(d, stage_dfu(d), within_s=30)["result"]
    assert n.wait_exit(10) == 76
    declare(n, NEW)
    n.start()
    d = Dev(n)
    end = time.time() + 15
    while job_get(d, r["job"])["state"] == "awaiting-confirm" and time.time() < end:
        time.sleep(0.3)
    g = job_get(d, r["job"])
    assert g["state"] == "failed" and g["result"]["error"] == "unconfirmed" and g["result"]["reverted"] is False, g
    assert g["result"]["running"]["version"] == NEW, "the new image stays"
    assert n.proc.poll() is None, "no reboot: nothing to revert to"


def test_a_dfu_board_takes_no_ip_source_and_no_app_image(nodes):
    n, d = dfu_board(nodes, "nf4", 94)
    msha = stage_dfu(d)
    r = offer(d, msha, source={"url": "http://127.0.0.1:9/p.zip"})
    assert (r["status"], r.get("error")) in (("refused", "unsupported-source"),), r
    time.sleep(1.05)
    text = d.manifest(version=NEW, image={"size": 4096, "sha256": hashlib.sha256(b"x").hexdigest()})   # kind app
    r = d.stage(text)
    assert (r["status"], r.get("error")) == ("refused", "wrong-target"), r


def test_a_dfu_offer_while_the_last_one_awaits_its_confirmation_is_busy(nodes):
    n, d = dfu_board(nodes, "nf5", 95)
    assert offer(d, stage_dfu(d), rid="nf-5a")["status"] == "ok"
    assert n.wait_exit(10) == 76
    declare(n, NEW)
    n.start()
    d = Dev(n)
    r = offer(d, stage_dfu(d, version="v1.18.0-mecon.99.1.0"), rid="nf-5b")
    assert (r["status"], r.get("error")) == ("busy", "job-open:nf-5a"), r
    time.sleep(2)
    assert n.proc.poll() is None, "no second reset"
