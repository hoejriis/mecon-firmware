"""update.manifest and update.status on the simulated standalone Companion (docs/contract/UPDATES.md sections 4 and 8).
Manifests are signed here with the release test key (docs/contract/release-keys.json; its seed is the SHA-256 of a
public label) through sim/ed25519_ref.py, independently of the firmware's verifier. Nothing is installed yet: staging
checks the signature and the target and answers what the rest of preflight would say."""
import base64
import hashlib
import json
import os
import sys
import time

import pytest
from sim import ed25519_ref
from sim.harness import CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402

RTC = 1792800000
RELEASE_SEED = hashlib.sha256(b"mecon-firmware public test key: release test").digest()
STRANGER_SEED = hashlib.sha256(b"mecon-firmware public test key: release stranger").digest()
KEY_ID = "15d0e5c92e53436a"


def key_id(seed):
    return hashlib.sha256(ed25519_ref.public_key(seed)).digest()[:8].hex()


def sign(seed, text):
    return base64.b64encode(ed25519_ref.sign(seed, b"MECON-RELEASE-v1\x00" + text.encode())).decode()


class Dev:
    def __init__(self, n):
        self.c = CompanionClient(n)
        self.c.app_start()
        self.i = mecon_iface.IfaceClient(self.c._send, self.c._read_frame)
        self.hello = self.i.request("hello")["result"]

    def manifest(self, **over):
        m = {"v": 1, "key_id": KEY_ID, "line": "public", "version": "v1.18.0-mecon.99.0.0",
             "build": "0" * 40, "upstream": "v1.18.0", "interface": "1.0",
             "hardware": self.hello["hardware"], "hardware_rev": ["1"], "role": self.hello["role"],
             "variant": "sim_companion_standalone", "layout": 1, "bootloader_min": None, "data_format": 1,
             "security_version": 1, "image": {"size": 4096, "sha256": hashlib.sha256(b"img").hexdigest()},
             "kind": "app", "min_from": "v1.18.0-mecon.0.0.0", "downgrade": False, "issued_at": 1790000000}
        m.update(over)
        return json.dumps(m, separators=(",", ":"))

    def stage(self, text, seed=RELEASE_SEED, sig_text=None):
        return self.i.request("update.manifest", {"manifest": text, "manifest_sig": sign(seed, sig_text or text)})


def boot(nodes, name, seed):
    return Dev(nodes("companion_standalone", name, seed=seed, rtc=RTC).start())


def test_a_signed_manifest_for_this_device_is_staged_by_its_hash(nodes):
    d = boot(nodes, "up", 31)
    assert "update.manifest" in d.hello["capabilities"] and "update.status" in d.hello["capabilities"]
    text = d.manifest()
    r = d.stage(text)
    assert r["status"] == "ok", r
    assert r["result"]["sha256"] == hashlib.sha256(text.encode()).hexdigest()
    assert r["result"]["version"] == "v1.18.0-mecon.99.0.0"
    # The rest of preflight, as for an unsigned offer now. A build without a version tag (no git describe in the build
    # container) does not know its running version, so it cannot show it is at least min_from.
    known = not d.hello["firmware"].endswith(".unknown")
    assert r["result"]["preflight"] in (("ok", "low-memory") if known else ("below-min-from",)), r
    s = d.i.request("update.status")["result"]
    assert s["job"] is None and s["security_floor"] >= 1, s
    assert s["staged"] == [{"sha256": r["result"]["sha256"], "version": "v1.18.0-mecon.99.0.0"}], s


def test_staging_refuses_a_wrong_signature_an_unknown_key_and_a_wrong_target(nodes):
    d = boot(nodes, "uq", 32)
    text = d.manifest()
    cases = [
        (lambda: d.stage(text, sig_text=text.replace("99.0.0", "99.0.1")), "refused", "bad-release-signature"),
        (lambda: d.stage(d.manifest(key_id=key_id(STRANGER_SEED)), seed=STRANGER_SEED), "refused", "untrusted-release-key"),
        (lambda: d.stage(d.manifest(variant="sim_repeater")), "refused", "wrong-target"),
        (lambda: d.stage(d.manifest(layout=2)), "refused", "wrong-target"),
        (lambda: d.stage(text[:-1] + ',"extra":1}'), "invalid", "bad-manifest"),
    ]
    for send, status, error in cases:
        r = send()
        assert (r["status"], r.get("error")) == (status, error), r
        time.sleep(1.05)   # one stage a second
    assert d.i.request("update.status")["result"]["staged"] == []


def test_the_rest_of_preflight_is_reported_not_enforced_at_staging(nodes):
    """Below the security floor is the first install check, ahead of the running version (the native tests cover the
    order of the rest)."""
    d = boot(nodes, "ur", 33)
    r = d.stage(d.manifest(security_version=0))
    assert r["status"] == "ok" and r["result"]["preflight"] == "below-security-floor", r
    assert len(d.i.request("update.status")["result"]["staged"]) == 1, "staged all the same: an offer decides"


def test_one_stage_a_second_and_two_slots_the_older_evicted(nodes):
    d = boot(nodes, "us", 34)
    a, b, c = (d.manifest(version="v1.18.0-mecon.99.0.%d" % k) for k in (1, 2, 3))
    assert d.stage(a)["status"] == "ok"
    r = d.stage(b)
    assert (r["status"], r.get("error")) == ("busy", "rate"), r
    time.sleep(1.05)
    assert d.stage(b)["status"] == "ok"
    time.sleep(1.05)
    assert d.stage(c)["status"] == "ok"
    staged = {s["version"] for s in d.i.request("update.status")["result"]["staged"]}
    assert staged == {"v1.18.0-mecon.99.0.2", "v1.18.0-mecon.99.0.3"}, staged

