"""The Deployment continuity package (AUTHORITY.md section 10a; #118). A member stores it, signed, in Normal; the unsigned cable
exports it in Normal; Hardened, deployment.reset and a factory reset destroy it. The device never reads it: a manager
encrypts it under the user's passphrase first. Here it is random bytes standing for that ciphertext."""
import base64
import json
import os

from test_deployment_authority import SECRET, boot, boot_with_secret, wrap  # noqa: E402
from test_deployment_authority_server import boot as srv_boot  # noqa: E402

PKG = bytes((i * 37 + 11) % 256 for i in range(300))   # stands for the passphrase-encrypted package


def put(d, gen, pkg=PKG, job=1, nonce=True):
    args = {"package": base64.b64encode(pkg).decode(), "generation": gen}
    return d.send(wrap(d.node_key, "deployment.continuity.put", args, job=job, nonce=d.nonce() if nonce else None))


def files_holding(d, needle):
    d.n.stop()
    hits = [f for f in os.listdir(d.n.dir) if os.path.isfile(os.path.join(d.n.dir, f)) and
            needle in open(os.path.join(d.n.dir, f), "rb").read()]
    d.n.start()
    d.attach()
    return hits


def test_a_member_stores_the_package_and_the_cable_exports_it_unchanged_across_a_reboot(nodes):
    d = boot(nodes, "ct1")
    assert d.provision()["status"] == "ok"
    assert d.i.request("deployment.get")["result"]["continuity"] == {"present": False, "generation": 0}
    r = put(d, 3)
    assert r["status"] == "ok" and r["result"] == {"continuity": "stored", "generation": 3}, r
    assert PKG.hex() not in json.dumps(r), "the package is never echoed"
    assert d.i.request("deployment.get")["result"]["continuity"] == {"present": True, "generation": 3}
    assert files_holding(d, PKG) == [], "sealed at rest: the package's bytes are in no file"
    d.reboot()
    e = d.i.request("deployment.continuity.export")
    assert e["status"] == "ok" and base64.b64decode(e["result"]["package"]) == PKG and e["result"]["generation"] == 3, e
    r = put(d, 4, pkg=PKG[::-1], job=1000)   # above the watermark a reboot leaves (its reservation)
    assert r["status"] == "ok", r
    assert base64.b64decode(d.i.request("deployment.continuity.export")["result"]["package"]) == PKG[::-1]


def test_only_a_signed_member_with_a_rising_generation_stores_one_and_only_the_cable_exports(nodes):
    d = boot(nodes, "ct2")
    u = d.i.request("deployment.continuity.put", {"package": base64.b64encode(PKG).decode(), "generation": 1})
    assert u["status"] == "refused" and u["error"] == "not_enrolled", u
    assert d.provision()["status"] == "ok"
    u = d.i.request("deployment.continuity.put", {"package": base64.b64encode(PKG).decode(), "generation": 1})
    assert u["status"] == "refused" and u["error"] == "unauthenticated", u
    r = put(d, 1, nonce=False)
    assert r["status"] == "refused" and r["error"] == "bad_channel_nonce", "a sensitive operation: %s" % r
    assert put(d, 5, job=2)["status"] == "ok"
    s = put(d, 5, job=3)
    assert s["status"] == "refused" and s["error"] == "stale_generation", "never back, nor sideways: %s" % s
    s = put(d, 4, job=4)
    assert s["error"] == "stale_generation", s
    big = put(d, 9, pkg=bytes(769), job=5)
    assert big["status"] == "invalid" and big["error"] == "bad-args", "past 768 bytes: %s" % big
    x = d.send(wrap(d.node_key, "deployment.continuity.export", {}, job=6, nonce=d.nonce()))
    assert x["status"] == "refused" and x["error"] == "usb_only", "a signature is not presence: %s" % x
    assert d.i.request("deployment.get")["result"]["continuity"]["generation"] == 5


def test_hardened_destroys_the_package_and_normal_does_not_bring_it_back(nodes):
    d = boot_with_secret(nodes, "ct3")
    assert put(d, 2)["status"] == "ok"
    assert d.send(wrap(d.node_key, "posture.set", {"posture": "hardened"}, job=3))["status"] == "ok"   # signed, as enrolled
    d.i.request("hello")   # a pass of the loop: the reconcile has run
    e = d.i.request("deployment.continuity.export")
    assert e["status"] == "refused" and e["error"] == "hardened", e
    assert files_holding(d, b"MCC1") == [], "the sealed file and its key are gone"
    assert d.send(wrap(d.node_key, "posture.set", {"posture": "normal", "confirm": True}, job=1000))["status"] == "ok"
    assert d.i.request("deployment.get")["result"]["continuity"] == {"present": False, "generation": 0}
    e = d.i.request("deployment.continuity.export")
    assert e["status"] == "failed" and e["error"] == "no_continuity", "Normal again holds none until a new put: %s" % e


def test_a_deployment_reset_removes_the_package(nodes):
    d = boot(nodes, "ct4")
    assert d.provision()["status"] == "ok"
    assert put(d, 1)["status"] == "ok"
    r = d.send(wrap(d.node_key, "deployment.reset", job=2))
    assert r["status"] == "ok" and "continuity" in r["result"]["removed"], r
    assert files_holding(d, b"MCC1") == []
    assert d.provision()["status"] == "ok"
    assert d.i.request("deployment.get")["result"]["continuity"]["present"] is False, "a new Deployment starts with none"


def test_a_repeater_holds_and_exports_one_too(nodes):
    s = srv_boot(nodes, "repeater", "ct5")
    caps, cur = [], None
    for _ in range(10):
        h = s.i.request("hello", {"cursor": cur} if cur is not None else None)["result"]
        caps += h["capabilities"]
        cur = h.get("next")
        if cur is None:
            break
    assert "deployment.continuity.put" in caps and "deployment.continuity.export" in caps
    assert s.provision()["status"] == "ok"
    args = {"package": base64.b64encode(PKG).decode(), "generation": 7}
    r = s.send(wrap(s.node_key, "deployment.continuity.put", args, job=1, nonce=s.nonce()))
    assert r["status"] == "ok", r
    e = s.i.request("deployment.continuity.export")
    assert e["status"] == "ok" and base64.b64decode(e["result"]["package"]) == PKG, e
    assert SECRET not in json.dumps(e)
