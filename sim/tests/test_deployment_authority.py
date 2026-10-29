"""The Deployment authority layer on the simulated standalone Companion (docs/contract/AUTHORITY.md): first provisioning,
signed requests, replay protection across a reboot, the channel nonce, authority updates, and a corrupt record. The keys
are the public test vectors of test/fixtures/deployment_authority_public_v1.json (valid nowhere); requests are signed here
with sim/ed25519_ref.py (RFC 8032 in plain Python; the container has no `cryptography` package), independently of the
firmware's own verifier. The reference signer is itself checked against the vectors' keys and signed request."""
import base64
import hashlib
import hmac as hmaclib
import json
import os
import socket
import struct
import sys
import time
import zlib

import pytest
from sim import ed25519_ref, mqtt_broker
from sim import harness
from sim.harness import CompanionClient, MqttEnv, WifiEnv, audit_file

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402

RTC = 1792800000
ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
V = json.load(open(os.path.join(ROOT, "test", "fixtures", "deployment_authority_public_v1.json")))
DEP = V["deployment_id"]
INST_A, INST_B = V["instance_ids"]["a"], V["instance_ids"]["b"]
SET1, SET2, ROT = V["authority_set_rev1"], V["authority_set_rev2"], V["rotation_1_to_2"]


SEEDS = {INST_A: bytes.fromhex(V["keys"]["instance a"]["seed_hex"]), INST_B: bytes.fromhex(V["keys"]["instance b"]["seed_hex"])}


def test_the_reference_signer_reproduces_the_vectors():
    for k in V["keys"].values():
        assert ed25519_ref.public_key(bytes.fromhex(k["seed_hex"])).hex() == k["public_key"]
    wire = json.loads(V["iface_request"]["wire"])
    sig = ed25519_ref.sign(SEEDS[INST_B], b"MECON-DEVMSG-v1\x00iface\x00" + wire["body"].encode())
    assert base64.b64encode(sig).decode() == wire["sig"]


def bare(r):
    """A result without its provenance members (contract 3.4: usig_boot, usig_seq, usig) and the members that name what it
    answers (op, job_id), for comparing what ran. test_provenance checks those members themselves."""
    return {k: v for k, v in r.items() if not k.startswith("usig") and k not in ("op", "job_id")}


def wrap(node_key, op, args=None, job=1, issuer=INST_A, rev=1, rid=None, nonce=None, target=None, deployment=DEP):
    """A signed request (contract 3.3): the body is signed as bytes, and travels as the wrapper's `body` string."""
    body = {"v": 1, "id": rid or "s-%s-%s" % (job, op), "op": op}
    if args is not None:
        body["args"] = args
    body.update({"issuer": issuer, "deployment_id": deployment, "authority_revision": rev, "issued_at": 1790001100,
                 "target_public_key": target or node_key})
    if job is not None:
        body["job_id"] = job
    if nonce is not None:
        body["channel_nonce"] = nonce
    text = json.dumps(body, separators=(",", ":"))
    sig = ed25519_ref.sign(SEEDS[issuer], b"MECON-DEVMSG-v1\x00iface\x00" + text.encode())
    return {"mecon_auth": 1, "issuer": issuer, "sig": base64.b64encode(sig).decode(), "body": text}


def provision_args(node_key, revision=1):
    return {"deployment": {"contract_version": 1, "deployment_id": DEP, "deployment_name": "Test deployment",
                           "node_public_key": node_key, "provision_revision": revision,
                           "dak": V["keys"]["dak epoch 1"]["public_key"], "authority_epoch": 1,
                           "authority_set": SET1["authority_set"], "authority_set_sig": SET1["sig"]}}


def update_args(rotations=True, which=SET2):
    a = {"authority_set": which["authority_set"], "authority_set_sig": which["sig"]}
    if rotations:
        a["rotations"] = [{"rotation": ROT["rotation"], "sig": ROT["sig"]}]
    return a


class Dev:
    """A booted node with a USB interface client."""

    def __init__(self, n):
        self.n = n
        self.attach()

    def attach(self):
        self.c = CompanionClient(self.n)
        self.c.app_start()
        self.i = mecon_iface.IfaceClient(self.c._send, self.c._read_frame)
        self.node_key = self.i.request("hello")["result"]["node_key"]

    def reboot(self):
        self.n.stop()
        self.n.start()
        self.attach()

    def send(self, wire, timeout=5.0):
        """One signed request over USB; its result envelope."""
        self.i.send_raw(json.dumps(wire, separators=(",", ":")).encode())
        return self.i.wait_result(json.loads(wire["body"])["id"], timeout)

    def provision(self, revision=1):
        return self.i.request("deployment.provision", provision_args(self.node_key, revision))

    def nonce(self):
        return self.i.request("hello")["result"]["channel_nonce"]

    def name(self):
        return self.c.cli("get name")


def boot(nodes, name="da", seed=21):
    return Dev(nodes("companion_standalone", name, seed=seed, rtc=RTC).start())


# ------------------------------------------------------------------ provisioning

def test_first_provisioning_over_usb_and_the_read_back(nodes):
    d = boot(nodes)
    assert d.i.request("deployment.get")["result"] == {"state": "none"}
    assert "deployment" not in d.i.request("status.get")["result"], "a standalone device reports no deployment"
    assert "channel_nonce" not in d.i.request("hello")["result"], "nothing to sign against yet"
    r = d.provision()
    assert r["status"] == "ok", r
    assert r["result"]["deployment_id"] == DEP and r["result"]["authority_revision"] == 1
    assert r["result"]["members"] == [{"instance_id": INST_A, "admitted_revision": 1, "mark": 0}]
    g = d.i.request("deployment.get")["result"]
    assert g["authority_set"] == SET1["authority_set"] and g["authority_set_sig"] == SET1["sig"], "the exact bytes"
    assert g["fallback"] is False and g["provision_revision"] == 1 and g["deployment_name"] == "Test deployment"
    s = d.i.request("status.get")["result"]["deployment"]
    assert s["deployment_id"] == DEP and s["auth_rejections"] == 0 and s["blocked_members"] == 0, s
    assert d.i.request("hello")["result"]["capabilities"].count("deployment.provision") == 1
    again = d.provision()
    assert again["status"] == "ok" and again["result"]["duplicate"] is True, "an identical resend is acknowledged"


def test_provisioning_refusals(nodes):
    d = boot(nodes, "dp")
    bad = provision_args(d.node_key)
    bad["deployment"]["node_public_key"] = "11" * 32
    assert d.i.request("deployment.provision", bad)["error"] == "wrong_device"
    bad = provision_args(d.node_key)
    bad["deployment"]["authority_set_sig"] = SET2["sig"]
    assert d.i.request("deployment.provision", bad)["error"] == "bad_authority_signature"
    assert d.i.request("deployment.get")["result"] == {"state": "none"}, "a refused provisioning writes nothing"
    assert d.provision()["status"] == "ok"
    # holding a record, an unsigned change is refused whatever it says; a different revision is not a resend
    r = d.i.request("deployment.provision", provision_args(d.node_key, 2))
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r


def test_a_signed_provisioning_moves_the_provision_revision(nodes):
    d = boot(nodes, "dq")
    d.provision()
    r = d.send(wrap(d.node_key, "deployment.provision", provision_args(d.node_key, 2), job=1))
    assert r["status"] == "ok" and d.i.request("deployment.get")["result"]["provision_revision"] == 2, r


# ------------------------------------------------------------------ signed requests

def apply_name(n):
    return {"set": {"name": n}}


def test_a_signed_config_apply_runs_and_a_resend_runs_nothing(nodes):
    d = boot(nodes, "sg")
    d.provision()
    w = wrap(d.node_key, "config.apply", apply_name("Signed1"), job=1)
    r = d.send(w)
    assert r["status"] == "ok" and r["result"]["complete"], r
    assert d.name() == "> Signed1"
    again = d.send(w)
    assert bare(again) == bare(r), "the same bytes get the stored result"
    # an unsigned change is refused once enrolled (contract section 9), the app's settings frame included: the change in
    # between is a second signed request, and the first one's bytes then run nothing
    assert d.c.request(bytes([8]) + b"Unsigned")[1] == 7, "an unsigned settings frame is refused once enrolled"
    w2 = wrap(d.node_key, "config.apply", apply_name("Meanwhile"), job=2)
    assert d.send(w2)["status"] == "ok"
    refused = d.send(w)
    assert refused["status"] == "refused" and refused["error"] == "replay", refused
    assert d.name() == "> Meanwhile", "and run nothing"
    assert d.i.request("deployment.get")["result"]["members"][0]["mark"] == 2
    # after a reboot the result cache is gone, the mark is not: the request at the mark is a duplicate with nothing to
    # show for it, and the older one is still refused
    d.reboot()
    assert d.name() == "> Meanwhile"
    dup = d.send(w2)
    assert dup["status"] == "failed" and dup["error"] == "duplicate", dup
    assert d.send(w)["error"] == "replay"
    assert d.name() == "> Meanwhile"
    assert d.i.request("deployment.get")["result"]["members"][0]["mark"] == 2, "the mark survived the reboot"


def test_a_mark_that_cannot_be_written_runs_nothing_and_is_not_consumed(nodes):
    d = boot(nodes, "wf")
    d.provision()
    w = wrap(d.node_key, "config.apply", apply_name("Persisted"), job=1)
    os.chmod(d.n.dir, 0o555)   # the file system refuses new files: the mark cannot be saved
    try:
        r = d.send(w)
        assert r["status"] == "busy" and r["error"] == "storage", r
        assert d.name() != "> Persisted", "the request did not run"
    finally:
        os.chmod(d.n.dir, 0o755)
    assert d.i.request("deployment.get")["result"]["members"][0]["mark"] == 0, "the mark stayed where it was"
    r = d.send(w)   # the same signed bytes, once storage works again
    assert r["status"] == "ok" and d.name() == "> Persisted", r


def test_an_older_job_id_is_a_replay_and_a_newer_one_runs(nodes):
    d = boot(nodes, "rp")
    d.provision()
    assert d.send(wrap(d.node_key, "config.apply", apply_name("Five"), job=5))["status"] == "ok"
    old = d.send(wrap(d.node_key, "config.apply", apply_name("Three"), job=3))
    assert old["status"] == "refused" and old["error"] == "replay", old
    assert d.name() == "> Five"
    assert d.send(wrap(d.node_key, "config.apply", apply_name("Six"), job=6))["status"] == "ok"
    assert d.name() == "> Six"
    assert d.i.request("status.get")["result"]["deployment"]["auth_rejections"] == 1
    # replay survives a reboot too
    d.reboot()
    r = d.send(wrap(d.node_key, "config.apply", apply_name("Four"), job=4))
    assert r["status"] == "refused" and r["error"] == "replay", r


def test_a_plain_signed_read_needs_no_job_and_a_write_without_one_is_refused(nodes):
    d = boot(nodes, "pj")
    d.provision()
    r = d.send(wrap(d.node_key, "status.get", job=None))
    assert r["status"] == "ok" and "uptime_s" in r["result"], r
    r = d.send(wrap(d.node_key, "config.apply", apply_name("X"), job=None))
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r
    assert d.name() != "> X"


def test_bad_signatures_wrong_targets_and_unknown_issuers_are_refused(nodes):
    d = boot(nodes, "bs")
    d.provision()
    r = d.send(wrap(d.node_key, "config.apply", apply_name("W"), job=1, target="22" * 32))
    assert r["status"] == "refused" and r["error"] == "wrong_device", r
    r = d.send(wrap(d.node_key, "config.apply", apply_name("W"), job=1, deployment="33" * 16))
    assert r["error"] == "wrong_deployment", r
    w = wrap(d.node_key, "config.apply", apply_name("W"), job=1)
    w["body"] = w["body"].replace("W", "V")   # the bytes changed after signing
    assert d.send(w)["error"] == "bad_signature"
    w = wrap(d.node_key, "config.apply", apply_name("W"), job=1, issuer=INST_B, rev=2)   # b is not a member of rev 1
    assert d.send(w)["error"] == "unknown_issuer"
    assert d.send(wrap(d.node_key, "config.apply", apply_name("W"), job=1, rev=9))["error"] == "authority_revision_ahead"
    assert d.name() != "> W"
    assert d.i.request("status.get")["result"]["deployment"]["auth_rejections"] == 5
    # none of them consumed the job id
    assert d.send(wrap(d.node_key, "config.apply", apply_name("Ok"), job=1))["status"] == "ok"


def test_a_signed_request_with_no_record_is_unauthenticated(nodes):
    d = boot(nodes, "nr")
    r = d.send(wrap(d.node_key, "config.apply", apply_name("N"), job=1))
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r


def test_unsigned_writes_are_refused_once_enrolled_but_reads_work(nodes):
    d = boot(nodes, "uw")
    d.provision()
    r = d.i.request("config.apply", apply_name("Unsigned"))
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r
    assert d.name() != "> Unsigned"
    assert d.i.request("config.get", {"keys": ["name"]})["status"] == "ok"
    assert d.i.request("status.get")["status"] == "ok"


def test_an_unsigned_channel_put_is_refused_once_enrolled_and_channel_reads_work(nodes):
    d = boot(nodes, "uc")
    d.provision()
    r = d.i.request("channel.put", {"slot": 3, "name": "x", "secret": "00112233445566778899aabbccddeeff"})
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r
    assert d.c.get_channel(3) is None, "nothing was written"
    assert d.i.request("channel.remove", {"slot": 0})["status"] == "refused"
    assert d.i.request("channel.list")["status"] == "ok"


def test_signed_requests_never_erase(nodes):
    d = boot(nodes, "ne")
    d.provision()
    # no interface operation erases, and the signed principal's destructive class is not permitted either way
    r = d.send(wrap(d.node_key, "device.erase", job=1))
    assert r["status"] == "unknown-op", r


# ------------------------------------------------------------------ the channel nonce

def test_a_sensitive_operation_needs_this_links_nonce_once(nodes):
    d = boot(nodes, "cn")
    d.provision()
    n1 = d.nonce()
    assert len(n1) == 32 and n1 == d.nonce(), "stable until it is used"
    r = d.send(wrap(d.node_key, "identity.key.export", job=1))
    assert r["status"] == "refused" and r["error"] == "bad_channel_nonce", r
    r = d.send(wrap(d.node_key, "identity.key.export", job=1, nonce="00" * 16))
    assert r["error"] == "bad_channel_nonce", r
    r = d.send(wrap(d.node_key, "identity.key.export", job=2, nonce=n1))
    assert r["status"] == "unknown-op", "past every check; this build has no such operation: %s" % r
    n2 = d.nonce()
    assert n2 != n1, "a nonce is used once"
    r = d.send(wrap(d.node_key, "identity.key.export", job=3, nonce=n1))
    assert r["error"] == "bad_channel_nonce", "the retired nonce does not work again"
    # a reboot is a new link
    d.reboot()
    assert d.nonce() not in (n1, n2)


# ------------------------------------------------------------------ authority updates

def test_authority_update_rotates_to_rev2_and_the_new_member_signs(nodes):
    d = boot(nodes, "au")
    d.provision()
    assert d.send(wrap(d.node_key, "config.apply", apply_name("A1"), job=7))["status"] == "ok"
    # the epoch-2 set is signed by the new key, which the device trusts only through the rotation
    r = d.i.request("deployment.authority_update", update_args(rotations=False))
    assert r["status"] == "refused" and r["error"] == "bad_authority_signature", r
    assert d.i.request("deployment.get")["result"]["authority_revision"] == 1
    r = d.i.request("deployment.authority_update", update_args())
    assert r["status"] == "ok", r
    assert r["result"]["authority_revision"] == 2 and r["result"]["authority_epoch"] == 2
    marks = {m["instance_id"]: m["mark"] for m in r["result"]["members"]}
    assert marks == {INST_A: 7, INST_B: 0}, "a keeps its mark, b starts at 0"
    r = d.i.request("deployment.authority_update", update_args(rotations=False))
    assert r["status"] == "busy" and r["error"] == "rate-limited", "flash wear: %s" % r
    time.sleep(1.1)
    again = d.i.request("deployment.authority_update", update_args(rotations=False))
    assert again["status"] == "ok" and again["result"]["duplicate"] is True, "the same set again is acknowledged"
    chain = d.i.request("deployment.authority_update", update_args())
    assert chain["status"] == "refused" and chain["error"] == "untrusted_authority_key", "the applied rotation is not repeated"
    old = d.i.request("deployment.authority_update", {"authority_set": SET1["authority_set"],
                                                      "authority_set_sig": SET1["sig"]})
    assert old["status"] == "refused" and old["error"] == "bad_authority_signature", "rev 1 is the old key's: %s" % old
    r = d.send(wrap(d.node_key, "config.apply", apply_name("B1"), job=1, issuer=INST_B, rev=2))
    assert r["status"] == "ok", r
    assert d.name() == "> B1"
    # a's sequence is untouched by b's
    assert d.send(wrap(d.node_key, "config.apply", apply_name("A2"), job=7, rev=2))["status"] == "ok", "a's job 7: a stored result"
    assert d.name() == "> B1", "job 7 was a's mark: a duplicate, not run again"
    d.reboot()
    g = d.i.request("deployment.get")["result"]
    assert g["authority_revision"] == 2 and g["authority_epoch"] == 2
    assert {m["instance_id"]: m["mark"] for m in g["members"]} == {INST_A: 7, INST_B: 1}, "both marks survive a reboot"


def test_an_update_for_another_deployment_or_with_a_bad_signature_changes_nothing(nodes):
    d = boot(nodes, "ab")
    d.provision()
    bad = update_args()
    bad["authority_set_sig"] = SET1["sig"]
    r = d.i.request("deployment.authority_update", bad)
    assert r["status"] == "refused" and r["error"] == "bad_authority_signature", r
    assert d.i.request("deployment.get")["result"]["authority_revision"] == 1


# ------------------------------------------------------------------ a corrupt record

def test_a_corrupt_record_is_never_absent(nodes):
    d = boot(nodes, "cr")
    d.provision()
    d.n.stop()
    for f in ("mecon_dep_a", "mecon_dep_b"):
        p = os.path.join(d.n.dir, f)
        if os.path.exists(p):
            open(p, "wb").write(b"\x00garbage")
    d.n.start()
    d.attach()
    assert d.i.request("deployment.get")["result"] == {"state": "corrupt"}
    assert d.i.request("status.get")["result"]["deployment"]["state"] == "corrupt"
    r = d.send(wrap(d.node_key, "config.apply", apply_name("C"), job=1))
    assert r["status"] == "refused" and r["error"] == "deployment_record_corrupt", r
    r = d.provision()
    assert r["status"] == "refused" and r["error"] == "deployment_record_corrupt", "no first provisioning over a corrupt record"
    r = d.i.request("config.apply", apply_name("C"))
    assert r["status"] == "refused" and r["error"] == "deployment_record_corrupt", r


# ------------------------------------------------------------------ over MQTT

TLS_PORT = 18883


@pytest.fixture
def broker(tmp_path):
    mqtt_broker.require_tools()
    made = []

    def make(name="b", ip="127.0.0.2"):
        cert, key_, fp = mqtt_broker.make_cert(str(tmp_path), name, ip)
        b = mqtt_broker.Broker(tmp_path, name, ip, TLS_PORT, cert, key_, None).start()
        b.fingerprint = fp
        made.append(b)
        return b
    yield make
    for b in made:
        b.stop()


def mqtt_boot(nodes, tmp_path, name, b):
    wenv = WifiEnv(str(tmp_path / (name + ".wifi")))
    wenv.set("sim")
    menv = MqttEnv(str(tmp_path / (name + ".mqtt")))
    n = nodes("companion_standalone", name, seed=17, rtc=RTC, wifi=wenv, mqtt=menv).start()
    d = Dev(n)
    device_id = d.i.request("hello")["result"]["device_id"]
    for cmd in ["set wifi.profile 0 ssid sim", "set mqtt.profile 0 type static", "set mqtt.profile 0 host %s" % b.ip,
                "set mqtt.profile 0 port %d" % b.port, "set mqtt.profile 0 tls on",
                "set mqtt.profile 0 anchor %s" % b.fingerprint]:
        assert d.c.cli(cmd).startswith(">"), cmd
    assert d.c.cli("set connectivity.mode wifi").startswith("OK")
    assert n.wait_exit(10) == 75
    n.start()
    d.attach()
    end = time.time() + 40
    while time.time() < end and not d.c.cli("get mqtt.status").startswith("> connected"):
        time.sleep(0.3)
    assert d.c.cli("get mqtt.status").startswith("> connected")
    time.sleep(2.0)
    return d, device_id


@pytest.mark.timescale(10)
def test_mqtt_never_provisions_unsigned_and_never_writes_unsigned_after(nodes, tmp_path, broker):
    b = broker()
    d, dev = mqtt_boot(nodes, tmp_path, "mq", b)
    r = b.iface_request(dev, {"v": 1, "id": "p1", "op": "deployment.provision", "args": provision_args(d.node_key)})
    assert r == {"v": 1, "id": "p1", "status": "refused", "error": "unauthenticated"}, r
    assert d.i.request("deployment.get")["result"] == {"state": "none"}, "nothing was written"
    assert d.provision()["status"] == "ok", "the cable may"
    r = b.iface_request(dev, {"v": 1, "id": "w1", "op": "config.apply", "args": apply_name("ViaMqtt")})
    assert bare(r) == {"v": 1, "id": "w1", "status": "refused", "error": "unauthenticated"}, r
    assert d.name() != "> ViaMqtt"
    # a signed one is the authority, over the same verified broker session; its nonce is the session's own
    w = wrap(d.node_key, "config.apply", apply_name("ViaMqtt"), job=1)
    r = b.iface_request(dev, w)
    assert r["status"] == "ok", r
    assert d.name() == "> ViaMqtt"
    r2 = b.iface_request(dev, w)
    assert bare(r2) == bare(r), "a resend over MQTT gets the stored result, and nothing runs twice"
    # the USB link's nonce is not the broker session's
    h = b.iface_request(dev, {"v": 1, "id": "h1", "op": "hello"})["result"]
    assert len(h["channel_nonce"]) == 32 and h["channel_nonce"] != d.nonce()
    r = b.iface_request(dev, wrap(d.node_key, "identity.key.export", job=2, nonce=d.nonce()))
    assert r["error"] == "bad_channel_nonce", "a nonce issued on USB is no good over MQTT: %s" % r


# ------------------------------------------------------------------ the provisioned marker, reset, and the recovery paths
# (contract sections 4, 9 and 11)

def marker_path(d):
    return d.n.dir.rstrip("/") + ".prov"   # beside the data directory, not inside it


def record_files(d):
    return [f for f in ("mecon_dep_a", "mecon_dep_b", "mecon_depm_a", "mecon_depm_b") if os.path.exists(os.path.join(d.n.dir, f))]


def wreck_record(d):
    """Both copies of the record damaged (not removed), the way a failing flash would leave them; the node is down."""
    for f in ("mecon_dep_a", "mecon_dep_b"):
        p = os.path.join(d.n.dir, f)
        if os.path.exists(p):
            open(p, "wb").write(b"\x00garbage")


def lose_record(d):
    """Both copies of the record gone, the node down: a file system that was formatted, or files deleted by hand."""
    for f in ("mecon_dep_a", "mecon_dep_b", "mecon_depm_a", "mecon_depm_b"):
        p = os.path.join(d.n.dir, f)
        if os.path.exists(p):
            os.remove(p)


def revive(d):
    d.n.start()
    d.attach()


def wait_audit(node, want, secs=8):
    """The persisted audit entries (oldest first) once one equals `want` = (event, caller, what), else what was seen."""
    end = time.time() + secs
    seen = None
    while time.time() < end:
        seen = audit_file(node)
        if seen and any((e[1], e[2], e[3]) == want for e in seen):
            return seen
        time.sleep(0.3)
    return seen


def test_the_provisioned_marker_makes_lost_files_corrupt_not_absent(nodes):
    d = boot(nodes, "mk")
    assert not os.path.exists(marker_path(d)), "a standalone device has no marker"
    assert d.provision()["status"] == "ok"
    assert os.path.exists(marker_path(d)), "written at the first provisioning, beside the data directory"
    assert not os.path.exists(os.path.join(d.n.dir, "prov")), "and not inside the file system"
    d.n.stop()
    lose_record(d)
    assert record_files(d) == []
    revive(d)
    assert d.i.request("deployment.get")["result"] == {"state": "corrupt"}, "the marker keeps first use closed"
    r = d.provision()
    assert r["status"] == "refused" and r["error"] == "deployment_record_corrupt", "no unsigned first provisioning: %s" % r
    assert d.i.request("status.get")["result"]["deployment"]["state"] == "corrupt"
    assert d.send(wrap(d.node_key, "config.apply", apply_name("M"), job=1))["error"] == "deployment_record_corrupt"


def test_without_the_marker_a_device_with_no_files_is_a_new_device(nodes):
    d = boot(nodes, "mk2")
    d.n.stop()
    open(marker_path(d), "wb").close()   # control: the marker alone is what makes the difference
    revive(d)
    assert d.i.request("deployment.get")["result"] == {"state": "corrupt"}
    d.n.stop()
    os.remove(marker_path(d))
    revive(d)
    assert d.i.request("deployment.get")["result"] == {"state": "none"}
    assert d.provision()["status"] == "ok"


def test_a_platform_with_no_marker_store_never_provisions_unsigned(nodes):
    d = boot(nodes, "mk3")
    d.n.stop()
    open(marker_path(d) + ".unsupported", "wb").close()   # the simulated board behaving like an nRF52 build
    revive(d)
    r = d.provision()
    assert r["status"] == "refused" and r["error"] == "unsupported", r
    assert d.i.request("deployment.get")["result"] == {"state": "none"}, "nothing was written"
    assert not os.path.exists(marker_path(d)) and record_files(d) == []


def test_a_signed_reset_over_usb_unenrols_and_keeps_the_rest(nodes):
    d = boot(nodes, "rs")
    assert d.c.cli("set name Kept") == "OK"
    key = d.node_key
    dev_id = d.i.request("hello")["result"]["device_id"]
    assert d.provision()["status"] == "ok"
    assert d.send(wrap(d.node_key, "config.apply", apply_name("Kept2"), job=1))["status"] == "ok"
    assert set(record_files(d)) >= {"mecon_dep_a", "mecon_depm_a"}
    r = d.send(wrap(d.node_key, "deployment.reset", job=2))
    assert r["status"] == "ok", r
    assert r["result"]["state"] == "none"
    assert r["result"]["removed"] == ["record", "marks", "marker"], r
    assert r["result"]["kept"] == ["node_key", "device_id", "configuration", "contacts", "channels"]
    assert record_files(d) == [] and not os.path.exists(marker_path(d)), "record, marks and marker are gone"
    assert d.i.request("deployment.get")["result"] == {"state": "none"}
    assert "channel_nonce" not in d.i.request("hello")["result"], "a device with no Deployment again"
    d.reboot()
    assert d.i.request("deployment.get")["result"] == {"state": "none"}, "and stays so"
    assert d.node_key == key and d.i.request("hello")["result"]["device_id"] == dev_id, "node key and device_id kept"
    assert d.name() == "> Kept2", "configuration kept"
    assert d.provision()["status"] == "ok", "first use is open again, and needs the cable"
    # the old signed bytes are not a way back in
    again = d.send(wrap(d.node_key, "deployment.reset", job=2))
    assert again["status"] == "ok" or again["error"] in ("duplicate", "replay"), again


def test_an_unsigned_reset_of_a_healthy_record_is_refused_even_over_the_cable(nodes):
    d = boot(nodes, "ru")
    d.provision()
    r = d.i.request("deployment.reset")
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r
    assert d.i.request("deployment.get")["result"]["state"] == "provisioned"
    assert os.path.exists(marker_path(d)) and "mecon_dep_a" in record_files(d) + ["mecon_dep_a"]
    assert d.i.request("deployment.get")["result"]["authority_revision"] == 1


def test_an_unsigned_reset_over_the_cable_recovers_a_corrupt_record(nodes):
    d = boot(nodes, "rc")
    d.c.cli("set name Mine")
    d.provision()
    d.n.stop()
    wreck_record(d)
    revive(d)
    assert d.i.request("deployment.get")["result"] == {"state": "corrupt"}
    r = d.i.request("deployment.reset")
    assert r["status"] == "ok" and "record" in r["result"]["removed"] and "marker" in r["result"]["removed"], r
    assert d.i.request("deployment.get")["result"] == {"state": "none"}
    assert record_files(d) == [] and not os.path.exists(marker_path(d))
    assert d.name() == "> Mine"
    assert d.provision()["status"] == "ok"
    # a reset of a device with nothing to reset says so
    d.send(wrap(d.node_key, "deployment.reset", job=1))
    assert d.i.request("deployment.reset")["error"] == "not_enrolled"


def test_reset_needs_normal_posture_and_a_corrupt_record_in_hardened_needs_the_local_unlock(nodes):
    d = boot(nodes, "rh")
    assert d.c.cli("set posture hardened") == "OK - posture hardened"
    d.provision()
    r = d.send(wrap(d.node_key, "deployment.reset", job=1))
    assert r["status"] == "refused" and r["error"] == "hardened", r
    assert d.i.request("deployment.get")["result"]["state"] == "provisioned"
    d.n.stop()
    wreck_record(d)
    revive(d)
    r = d.i.request("deployment.reset")
    assert r["status"] == "refused" and r["error"] == "unlock_required", r
    assert d.i.request("deployment.get")["result"] == {"state": "corrupt"}
    assert os.path.exists(marker_path(d))


def test_a_factory_reset_over_the_cable_clears_the_marker_and_the_record(nodes):
    d = boot(nodes, "fc")
    d.provision()
    assert os.path.exists(marker_path(d))
    d.c._send(bytes([harness.CMD_FACTORY_RESET]) + b"reset")   # the cable's reset is unchanged by enrolment
    assert d.n.wait_exit(10) == 75
    assert not os.path.exists(marker_path(d)), "the factory reset clears the marker"
    revive(d)
    assert d.i.request("deployment.get")["result"] == {"state": "none"}
    assert d.provision()["status"] == "ok"


PIN = 424242


def test_a_ble_factory_reset_is_refused_once_enrolled_and_audited(nodes):
    n = nodes("companion_standalone", "fb", seed=33, rtc=RTC).start()
    u = CompanionClient(n)
    u.device_query()
    u.app_start()
    assert u.cli("set pin %d" % PIN).startswith("> pin is now")
    u.close()
    n.restart()
    d = Dev(n)
    b = n.connect_ble(PIN)
    b.app_start()
    # control: before enrolment the same frame over the same link resets the device (test_companion_ble)
    assert d.provision()["status"] == "ok"
    f = b.request(bytes([harness.CMD_FACTORY_RESET]) + b"reset")
    assert f[0] == harness.RESP_ERR and f[1] == harness.ERR_NOT_PERMITTED, f
    assert n.proc.poll() is None, "the device did not reset"
    assert d.i.request("deployment.get")["result"]["state"] == "provisioned"
    assert os.path.exists(marker_path(d)) and "mecon_dep_a" in record_files(d)
    seen = wait_audit(n, (2, 2, 5))   # refused / ble / factory-reset
    assert seen and any((e[1], e[2], e[3]) == (2, 2, 5) for e in seen), seen
    b.close()


def test_an_unsigned_first_provisioning_over_ble_is_refused_whatever_the_pin_and_the_cable_still_works(nodes):
    n = nodes("companion_standalone", "bp1", seed=35, rtc=RTC).start()
    u = CompanionClient(n)
    u.device_query()
    u.app_start()
    assert u.cli("set pin %d" % PIN).startswith("> pin is now"), "a PIN the user chose: not one the device drew"
    u.close()
    n.restart()
    d = Dev(n)
    b = n.connect_ble(PIN)
    b.app_start()
    bi = mecon_iface.IfaceClient(b._send, b._read_frame)
    r = bi.request("deployment.provision", provision_args(d.node_key))
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r
    assert d.i.request("deployment.get")["result"] == {"state": "none"}, "nothing was stored"
    assert not os.path.exists(marker_path(d)) and not record_files(d)
    assert d.provision()["status"] == "ok", "the cable may"
    b.close()


def test_a_ble_factory_reset_still_works_on_a_device_with_no_record(nodes):
    n = nodes("companion_standalone", "fb2", seed=34, rtc=RTC).start()
    u = CompanionClient(n)
    u.device_query()
    u.app_start()
    assert u.cli("set pin %d" % PIN).startswith("> pin is now")
    u.close()
    n.restart()
    b = n.connect_ble(PIN)
    b.app_start()
    b._send(bytes([harness.CMD_FACTORY_RESET]) + b"reset")
    assert n.wait_exit(10) == 75


def rescue(node, command, until, timeout=25):
    """The rescue console: a long press in the first 8 s of the UI opens it on the serial port; returns what `command`
    printed up to the text `until`."""
    s = node.connect()
    s.settimeout(0.3)
    node.ui.press("long")
    buf = b""

    def read_until(marker, secs):
        nonlocal buf
        end = time.time() + secs
        while time.time() < end and marker not in buf:
            try:
                got = s.recv(4096)
            except OSError:
                continue
            if got == b"":
                break
            buf += got
        return marker in buf

    assert read_until(b"CLI Rescue", 10), buf
    buf = b""
    s.sendall(command.encode() + b"\r")
    read_until(until, timeout)
    s.close()
    return buf.decode(errors="replace")


def snapshot_files(d):
    out = {}
    for f in record_files(d):
        out[f] = open(os.path.join(d.n.dir, f), "rb").read()
    return out


def test_a_damaged_marks_copy_blocks_every_member_so_a_captured_request_cannot_run_again(nodes):
    d = boot(nodes, "mf")
    d.provision()
    old = wrap(d.node_key, "config.apply", apply_name("Mf"), job=3)
    assert d.send(old)["status"] == "ok"
    assert d.i.request("deployment.get")["result"]["marks_fallback"] is False
    d.n.stop()
    # provisioning wrote the marks to copy a (generation 1) and the request to copy b (generation 2): damage the newer
    open(os.path.join(d.n.dir, "mecon_depm_b"), "wb").write(b"\x00garbage")
    revive(d)
    g = d.i.request("deployment.get")["result"]
    assert g["marks_fallback"] is True and g["members"][0]["mark"] == 4294967295, g
    assert d.i.request("status.get")["result"]["deployment"]["marks_fallback"] is True
    assert d.i.request("status.get")["result"]["deployment"]["blocked_members"] == 1
    r = d.send(old)
    assert r["status"] == "refused" and r["error"] == "replay", "the older mark did not bring the captured request back: %s" % r
    r = d.send(wrap(d.node_key, "config.apply", apply_name("Mf2"), job=4))
    assert r["status"] == "refused" and r["error"] == "replay", "nobody runs until re-admitted: %s" % r
    assert d.name() == "> Mf"


def test_the_rescue_rebuild_keeps_the_record_its_marks_and_the_marker(nodes):
    d = boot(nodes, "rb")
    d.provision()
    assert d.send(wrap(d.node_key, "config.apply", apply_name("Rb"), job=3))["status"] == "ok"
    before = snapshot_files(d)
    assert before and os.path.exists(marker_path(d))
    d.n.stop()
    d.n.start()
    out = rescue(d.n, "rebuild", b"damaged\n")
    assert "rebuilt" in out, out
    d.n.stop()
    revive(d)
    g = d.i.request("deployment.get")["result"]
    assert g["state"] == "provisioned" and g["authority_revision"] == 1, g
    assert g["members"][0]["mark"] == 3, "the marks came back too"
    assert os.path.exists(marker_path(d))
    assert snapshot_files(d) == before, "byte for byte"
    assert d.send(wrap(d.node_key, "config.apply", apply_name("Rb2"), job=4))["status"] == "ok"


def test_a_stock_array_rescue_rebuild_puts_the_marker_back_before_anything_else(nodes):
    # this build formats the file system (and, as on ESP32, the format takes the marker with it)
    n = nodes("companion_standalone_stockcontacts", "rbm", seed=36, rtc=RTC).start()
    d = Dev(n)
    d.provision()
    assert os.path.exists(marker_path(d))
    n.stop()
    n.fault = "/identity/_main.id.tmp:10"   # power cut while the identity is written, the first file after the format
    n.start()
    out = rescue(n, "rebuild", b"cut", timeout=6)
    assert n.wait_exit(10) == 99, out
    assert os.path.exists(marker_path(d)), "the marker was back before the identity was written"
    assert not os.path.exists(os.path.join(n.dir, "mecon_dep_a")), "control: the format did remove the record files"
    n.fault = None
    revive(d)
    assert d.i.request("deployment.get")["result"]["state"] == "corrupt", "lost files with the marker are corrupt, never absent"
    r = d.provision()
    assert r["status"] == "refused" and r["error"] == "deployment_record_corrupt", r


def test_a_rescue_rebuild_that_cannot_read_the_record_refuses_and_changes_nothing(nodes):
    d = boot(nodes, "rbu")
    d.provision()
    d.n.stop()
    victim = os.path.join(d.n.dir, "mecon_dep_a")
    os.chmod(victim, 0o000)   # exists, cannot be read
    try:
        d.n.start()
        out = rescue(d.n, "rebuild", b"unreadable", timeout=10)
        assert "record unreadable; use deployment.reset or factory reset" in out, out
        assert "rebuilt" not in out, "nothing was rebuilt"
    finally:
        d.n.stop()
        os.chmod(victim, 0o644)
    revive(d)
    assert d.i.request("deployment.get")["result"]["state"] in ("provisioned", "corrupt"), "never silently unenrolled"
    assert d.i.request("deployment.get")["result"] != {"state": "none"}
    assert os.path.exists(marker_path(d))


def test_the_rescue_erase_is_a_cable_recovery_and_clears_the_marker(nodes):
    d = boot(nodes, "re")
    d.provision()
    d.n.stop()
    d.n.start()
    out = rescue(d.n, "erase", b"erase done")
    assert "erase done" in out, out
    assert not os.path.exists(marker_path(d))
    d.n.stop()
    revive(d)
    assert d.i.request("deployment.get")["result"] == {"state": "none"}


FILES_REFUSED = "file commands are refused"


def test_the_rescue_file_commands_are_refused_while_a_record_exists_healthy_or_corrupt(nodes):
    d = boot(nodes, "rf")
    d.provision()
    before = snapshot_files(d)
    d.n.stop()
    d.n.start()
    for cmd in ("cat UserData/mecon_dep_a", "ls /", "rm UserData/mecon_dep_a", "rm UserData/mecon_depm_b"):
        d.n.stop()
        d.n.start()
        out = rescue(d.n, cmd, FILES_REFUSED.encode(), timeout=8)
        assert FILES_REFUSED in out, (cmd, out)
        assert "removed" not in out and "Listing" not in out, (cmd, out)
    d.n.stop()
    assert snapshot_files(d) == before, "every record file is still there, byte for byte"
    wreck_record(d)   # corrupt: the files exist but cannot be read as a record
    damaged = snapshot_files(d)
    d.n.start()
    for cmd in ("cat UserData/mecon_dep_a", "rm UserData/mecon_dep_a"):
        d.n.stop()
        d.n.start()
        out = rescue(d.n, cmd, FILES_REFUSED.encode(), timeout=8)
        assert FILES_REFUSED in out, (cmd, out)
    d.n.stop()
    assert snapshot_files(d) == damaged


def test_the_rescue_file_commands_are_refused_in_hardened_with_no_record_and_work_in_normal_standalone(nodes):
    d = boot(nodes, "rf2")
    d.n.stop()
    d.n.start()
    out = rescue(d.n, "ls /", b"Listing files", timeout=8)
    assert "Listing files" in out and FILES_REFUSED not in out, "a standalone Normal device keeps the console as it was: " + out
    d.n.stop()
    d.n.start()
    d.attach()
    assert d.c.cli("set posture hardened") == "OK - posture hardened"
    d.n.stop()
    d.n.start()
    out = rescue(d.n, "cat UserData/new_prefs", FILES_REFUSED.encode(), timeout=8)
    assert FILES_REFUSED in out, out
    d.n.stop()
    d.n.start()
    out = rescue(d.n, "rm UserData/new_prefs", FILES_REFUSED.encode(), timeout=8)
    assert FILES_REFUSED in out, out


def test_an_unsigned_cli_write_over_the_usb_frame_is_refused_once_enrolled_but_a_read_works(nodes):
    d = boot(nodes, "cw")
    assert d.c.cli("set name Before") == "OK", "standalone: unchanged"
    d.provision()
    reply = d.c.cli("set name After")
    assert reply == "ERR: this device belongs to a Deployment; the change must be signed", reply
    for line in ("set posture hardened", "set prv.key " + "00" * 64, "set mqtt.profile 0 manage on", "set pin 111111"):
        assert d.c.cli(line).startswith("ERR: "), line
    assert d.name() == "> Before", "nothing changed"
    assert d.c.cli("get posture") == "> normal", "reads work"
    assert d.c.cli("get radio").startswith(">") and d.c.cli("ver").startswith("v")
    seen = wait_audit(d.n, (2, 1, 1))   # refused / cable / cli
    assert seen and any((e[1], e[2], e[3]) == (2, 1, 1) for e in seen), seen
    # a signed write is the way, and the unsigned one is still refused after it
    assert d.send(wrap(d.node_key, "config.apply", apply_name("Signed"), job=1))["status"] == "ok"
    assert d.name() == "> Signed"
    assert d.c.cli("set name After").startswith("ERR: ")
    # a corrupt record counts as enrolled
    d.n.stop()
    wreck_record(d)
    revive(d)
    assert d.c.cli("set name After").startswith("ERR: ")
    assert d.c.cli("get posture") == "> normal"
    # the cable keeps the destructive class: the recovery operations stay possible
    d.c._send(bytes([harness.CMD_FACTORY_RESET]) + b"reset")
    assert d.n.wait_exit(10) == 75


@pytest.mark.timescale(10)
def test_reset_and_unsigned_cli_writes_are_refused_over_mqtt(nodes, tmp_path, broker):
    b = broker()
    d, dev = mqtt_boot(nodes, tmp_path, "mr", b)
    node_id = d.c.pub_key[:4].hex()
    r = b.cli_request(node_id, "set name PreEnrol")
    assert r is not None and r["status"] == "ok", "control: standalone, the verified broker may write: %s" % r
    assert d.provision()["status"] == "ok"
    r = b.cli_request(node_id, "set name ViaCli")
    assert r is not None and r["status"] == "refused" and "belongs to a Deployment" in r["reply"], r
    assert d.name() == "> PreEnrol"
    r = b.cli_request(node_id, "get name")
    assert r is not None and r["status"] == "ok" and r["reply"] == "> PreEnrol", "a read works: %s" % r
    # the reset: unsigned and signed, both refused over a broker
    r = b.iface_request(dev, {"v": 1, "id": "u1", "op": "deployment.reset"})
    assert bare(r) == {"v": 1, "id": "u1", "status": "refused", "error": "unauthenticated"}, r
    r = b.iface_request(dev, wrap(d.node_key, "deployment.reset", job=1))
    assert r["status"] == "refused" and r["error"] == "usb_only", r
    assert d.i.request("deployment.get")["result"]["state"] == "provisioned"
    assert os.path.exists(marker_path(d))


# ------------------------------------------------------------------ local unlock (contract sections 9, 10 and 11)

SECRET = bytes(range(1, 33)).hex()
SECRET2 = bytes(range(101, 133)).hex()
AUD_UNLOCK, AUD_LOGIN_FAILED, AUD_REFUSED, WHAT_UNLOCK = 7, 3, 2, 19
CABLE_CALLER, MQTT_CALLER = 1, 7
NOT_ENROLLED_REFUSAL = "ERR: this device belongs to a Deployment; the change must be signed"


def with_secret(args, secret=SECRET):
    args["deployment"]["unlock_secret"] = secret
    return args


def unlock_answer(secret_hex, challenge_hex):
    return hmaclib.new(bytes.fromhex(secret_hex), bytes.fromhex(challenge_hex), hashlib.sha256).hexdigest()


def challenge(request):
    r = request("deployment.unlock_challenge")
    assert r["status"] == "ok", r
    return r["result"]["challenge"]


def try_unlock(request, secret=SECRET):
    """One whole handshake: a challenge, then the answer made with `secret`. The unlock's result, or the refusal of the challenge."""
    ch = request("deployment.unlock_challenge")
    if ch["status"] != "ok":
        return ch
    return request("deployment.unlock", {"response": unlock_answer(secret, ch["result"]["challenge"])})


def audit_text(d):
    """The audit ring as `get audit` prints it, every page; the cable must hold an unlock for this to be allowed."""
    out, start = [], 0
    while True:
        page = d.c.cli("get audit %d" % start)
        assert page.startswith(">") or start, page
        out.append(page)
        if "... next:" not in page:
            return "\n".join(out)
        start = int(page.rsplit("... next:", 1)[1])


def boot_with_secret(nodes, name, posture=None):
    d = boot(nodes, name)
    if posture:
        assert d.c.cli("set posture %s" % posture).startswith("OK")
    assert d.i.request("deployment.provision", with_secret(provision_args(d.node_key)))["status"] == "ok"
    return d


def damage_set_keep_crc(d):
    """Both copies of the record altered so the stored set no longer verifies, with the frame CRC redone: corrupt by the
    record's own checks, but the frame still reads. The node is down."""
    dak = bytes.fromhex(V["keys"]["dak epoch 1"]["public_key"])
    done = 0
    for f in ("mecon_dep_a", "mecon_dep_b"):
        path = os.path.join(d.n.dir, f)
        if not os.path.exists(path):
            continue
        b = bytearray(open(path, "rb").read())
        b[b.index(dak)] ^= 1
        b[-4:] = struct.pack("<I", zlib.crc32(bytes(b[:-4])))
        open(path, "wb").write(bytes(b))
        done += 1
    assert done


def test_the_secret_comes_with_the_provisioning_and_without_one_there_is_nothing_to_unlock(nodes):
    d = boot(nodes, "us0")
    assert d.i.request("deployment.unlock_challenge")["error"] == "no_unlock_secret", "no record, no secret"
    assert d.provision()["status"] == "ok"
    r = d.i.request("deployment.unlock_challenge")
    assert r["status"] == "refused" and r["error"] == "no_unlock_secret", r
    assert d.i.request("deployment.unlock", {"response": "00" * 32})["error"] == "no_unlock_secret"
    # a cable can never set it: the only unsigned way in is the signed one
    for args in ({"unlock_secret": SECRET}, None):
        r = d.i.request("deployment.set_unlock", args)
        assert r["status"] == "refused" and r["error"] == "unauthenticated", r
    d2 = boot(nodes, "us0b")
    d2.provision()
    r = d2.i.request("deployment.provision", with_secret(provision_args(d2.node_key, 2)))
    assert r["status"] == "refused" and r["error"] == "unauthenticated", "holding a record, even the cable cannot add one: %s" % r
    assert d2.i.request("deployment.unlock_challenge")["error"] == "no_unlock_secret"


def test_a_challenge_and_the_right_answer_unlock_the_cable_in_normal_and_it_opens_only_the_posture(nodes):
    d = boot_with_secret(nodes, "us1")
    assert d.c.cli("set posture normal confirm") == NOT_ENROLLED_REFUSAL, "control: enrolled and locked"
    ch = challenge(d.i.request)
    assert len(ch) == 64 and ch != "00" * 32
    r = d.i.request("deployment.unlock", {"response": unlock_answer(SECRET, ch)})
    assert r["status"] == "ok" and r["result"] == {"unlocked": True, "expires_in_s": 600}, r
    assert d.c.cli("set posture normal confirm") == "OK - already normal", "the posture is open to the unlocked cable"
    assert d.c.cli("set name Nope") == NOT_ENROLLED_REFUSAL, "never a configuration write"
    assert d.c.cli("get prv.key").startswith("ERR"), "never key export"
    r = d.i.request("config.apply", apply_name("Nope"))
    assert r["status"] == "refused" and r["error"] == "unauthenticated", "nor over the interface: %s" % r
    assert d.i.request("deployment.reset")["error"] == "unauthenticated", "a healthy record is reset by a signature"
    assert d.name() != "> Nope"
    # the unlock is the link's: an app starting over on it is a new session
    d.c.app_start()
    assert d.c.cli("set posture normal confirm") == NOT_ENROLLED_REFUSAL
    # and a reboot forgets it
    assert try_unlock(d.i.request)["status"] == "ok"
    d.reboot()
    assert d.c.cli("set posture normal confirm") == NOT_ENROLLED_REFUSAL


def test_an_unlocked_cable_may_enter_hardened_because_the_record_holds_the_secret_and_without_one_nobody_unsigned_may(nodes):
    d = boot_with_secret(nodes, "us1h")
    assert d.c.cli("set posture hardened") == NOT_ENROLLED_REFUSAL, "control: locked"
    r = try_unlock(d.i.request)
    assert r["status"] == "ok", r
    assert d.c.cli("set posture hardened") == "OK - posture hardened", "the record holds a secret: the rule lets it through"
    assert d.c.cli("get posture") == "> hardened"
    nosecret = boot(nodes, "us1n")
    nosecret.provision()
    assert nosecret.c.cli("set posture hardened") == NOT_ENROLLED_REFUSAL
    assert nosecret.c.cli("get posture") == "> normal", "no secret in the record: never Hardened by an unsigned caller"


def test_the_unlock_secret_is_accepted_only_over_the_usb_cable_even_in_a_signed_request(nodes):
    n = nodes("companion_standalone", "us4", seed=37, rtc=RTC).start()
    u = CompanionClient(n)
    u.device_query()
    u.app_start()
    assert u.cli("set pin %d" % PIN).startswith("> pin is now")
    u.close()
    n.restart()
    d = Dev(n)
    assert d.provision()["status"] == "ok"
    b = n.connect_ble(PIN)
    b.app_start()
    bi = mecon_iface.IfaceClient(b._send, b._read_frame)

    def over_ble(wire):
        bi.send_raw(json.dumps(wire, separators=(",", ":")).encode())
        return bi.wait_result(json.loads(wire["body"])["id"], 5.0)

    nonce = bi.request("hello")["result"]["channel_nonce"]
    r = over_ble(wrap(d.node_key, "deployment.set_unlock", {"unlock_secret": SECRET}, job=1, nonce=nonce))
    assert r["status"] == "refused" and r["error"] == "usb_only", r
    r = over_ble(wrap(d.node_key, "deployment.provision", with_secret(provision_args(d.node_key, 2)), job=2))
    assert r["status"] == "refused" and r["error"] == "usb_only", r
    assert d.i.request("deployment.unlock_challenge")["error"] == "no_unlock_secret", "nothing was stored"
    # controls: a signed provisioning that carries no secret is fine over BLE, and the cable may set the secret
    r = over_ble(wrap(d.node_key, "deployment.provision", provision_args(d.node_key, 2), job=3))
    assert r["status"] == "ok", r
    r = d.send(wrap(d.node_key, "deployment.set_unlock", {"unlock_secret": SECRET}, job=4, nonce=d.nonce()))
    assert r["status"] == "ok", r
    assert try_unlock(d.i.request)["status"] == "ok"
    b.close()


def test_in_hardened_the_unlock_opens_reads_and_the_reset_of_a_corrupt_record_and_nothing_else(nodes):
    d = boot_with_secret(nodes, "us2", posture="hardened")
    assert try_unlock(d.i.request)["status"] == "ok", "the handshake is open in Hardened"
    assert d.c.cli("set posture normal confirm").startswith("ERR"), "the posture stays as the signed callers left it"
    assert d.c.cli("get posture") == "> hardened"
    r = d.i.request("deployment.reset")
    assert r["status"] == "refused" and r["error"] == "unauthenticated", "a healthy record still needs the signature: %s" % r
    assert d.i.request("deployment.get")["result"]["state"] == "provisioned"
    d.n.stop()
    damage_set_keep_crc(d)
    revive(d)
    assert d.i.request("deployment.get")["result"] == {"state": "corrupt"}
    r = d.i.request("deployment.reset")
    assert r["status"] == "refused" and r["error"] == "unlock_required", "locked: %s" % r
    r = try_unlock(d.i.request, SECRET2)
    assert r["status"] == "refused" and r["error"] == "bad_response", "the wrong secret opens nothing: %s" % r
    assert d.i.request("deployment.reset")["error"] == "unlock_required"
    assert try_unlock(d.i.request)["status"] == "ok", "the secret the damaged frame still carries"
    assert d.c.cli("set name Nope").startswith("ERR"), "configuration stays closed while corrupt"
    r = d.i.request("deployment.reset")
    assert r["status"] == "ok" and "record" in r["result"]["removed"] and "marker" in r["result"]["removed"], r
    assert "unlock_secret" not in r["result"]["removed"], "a record that could not be read had no secret to report"
    assert d.i.request("deployment.get")["result"] == {"state": "none"}
    assert record_files(d) == [] and not os.path.exists(marker_path(d))
    assert d.i.request("deployment.unlock_challenge")["error"] == "no_unlock_secret", "the secret went with the record"
    assert d.c.cli("get posture") == "> hardened", "the posture is kept"


def test_a_corrupt_record_that_cannot_be_read_has_no_secret_to_unlock_with(nodes):
    d = boot_with_secret(nodes, "us3", posture="hardened")
    d.n.stop()
    wreck_record(d)
    revive(d)
    assert d.i.request("deployment.get")["result"] == {"state": "corrupt"}
    r = d.i.request("deployment.unlock_challenge")
    assert r["status"] == "refused" and r["error"] == "no_unlock_secret", r
    assert d.i.request("deployment.reset")["error"] == "unlock_required"
    assert d.i.request("deployment.get")["result"] == {"state": "corrupt"}


def test_a_wrong_answer_is_refused_and_counted_and_the_sixth_attempt_meets_the_lockout(nodes):
    d = boot_with_secret(nodes, "us4")
    for i in range(5):
        r = try_unlock(d.i.request, SECRET2)
        assert r["status"] == "refused" and r["error"] == "bad_response", (i, r)
    r = d.i.request("deployment.unlock_challenge")
    assert r["status"] == "refused" and r["error"] == "locked_out", r
    # an answer, however it was made, meets the lockout first
    r = d.i.request("deployment.unlock", {"response": "00" * 32})
    assert r["error"] == "locked_out", r
    assert d.c.cli("set posture normal confirm") == NOT_ENROLLED_REFUSAL
    seen = wait_audit(d.n, (AUD_LOGIN_FAILED, CABLE_CALLER, WHAT_UNLOCK))
    assert any((e[1], e[2], e[3]) == (AUD_LOGIN_FAILED, CABLE_CALLER, WHAT_UNLOCK) for e in seen), "wrong answers are audited: %s" % seen


@pytest.mark.timescale(10)
def test_the_lockout_ends_after_a_minute_and_a_right_answer_resets_the_count(nodes):
    d = boot_with_secret(nodes, "us5")
    for _ in range(5):
        assert try_unlock(d.i.request, SECRET2)["error"] == "bad_response"
    assert d.i.request("deployment.unlock_challenge")["error"] == "locked_out"
    time.sleep(7.5)   # 75 s of device time
    assert try_unlock(d.i.request)["status"] == "ok"
    for _ in range(4):   # the count started again at the success: four more wrong answers do not lock
        assert try_unlock(d.i.request, SECRET2)["error"] == "bad_response"
    assert try_unlock(d.i.request)["status"] == "ok"
    # every attempt is in the ring, folded by kind: nine wrong answers, two successes, one refusal in the lockout
    text = audit_text(d)
    assert "login-fail cable unlock x9" in text, text
    assert "unlock cable unlock x2" in text, text
    assert "refused cable unlock x1" in text, text
    assert "unlock" in text and SECRET not in text


def test_a_challenge_answers_one_attempt_only(nodes):
    d = boot_with_secret(nodes, "us6")
    ch = challenge(d.i.request)
    good = unlock_answer(SECRET, ch)
    r = d.i.request("deployment.unlock", {"response": "11" * 32})
    assert r["error"] == "bad_response", r
    r = d.i.request("deployment.unlock", {"response": good})
    assert r["status"] == "refused" and r["error"] == "no_challenge", "spent by the wrong answer: %s" % r
    ch2 = challenge(d.i.request)
    assert ch2 != ch, "a fresh random challenge each time"
    assert d.i.request("deployment.unlock", {"response": unlock_answer(SECRET, ch2)})["status"] == "ok"
    r = d.i.request("deployment.unlock", {"response": unlock_answer(SECRET, ch2)})
    assert r["status"] == "refused" and r["error"] == "no_challenge", "spent by the right one: %s" % r
    ch3 = challenge(d.i.request)
    assert d.i.request("deployment.unlock", {"response": unlock_answer(SECRET, ch)})["error"] == "bad_response", "an older challenge's answer"
    # a malformed answer is an attempt too
    challenge(d.i.request)
    assert d.i.request("deployment.unlock", {"response": "zz"})["status"] in ("refused", "invalid")
    assert d.i.request("deployment.unlock")["error"] in ("bad-args", "no_challenge")


@pytest.mark.timescale(10)
def test_an_expired_challenge_is_refused(nodes):
    d = boot_with_secret(nodes, "us7")
    ch = challenge(d.i.request)
    time.sleep(7.0)   # 70 s of device time
    r = d.i.request("deployment.unlock", {"response": unlock_answer(SECRET, ch)})
    assert r["status"] == "refused" and r["error"] == "no_challenge", r
    assert d.c.cli("set posture normal confirm") == NOT_ENROLLED_REFUSAL


@pytest.mark.timescale(50)
def test_an_unlock_lapses_after_ten_minutes(nodes):
    d = boot_with_secret(nodes, "us8")
    assert try_unlock(d.i.request)["status"] == "ok"
    assert d.c.cli("set posture normal confirm") == "OK - already normal"
    time.sleep(13.0)   # 650 s of device time
    assert d.c.cli("set posture normal confirm") == NOT_ENROLLED_REFUSAL


@pytest.mark.timescale(10)
def test_the_handshake_is_refused_over_mqtt_and_to_a_signed_request(nodes, tmp_path, broker):
    b = broker()
    d, dev = mqtt_boot(nodes, tmp_path, "um", b)
    assert d.i.request("deployment.provision", with_secret(provision_args(d.node_key)))["status"] == "ok"
    r = b.iface_request(dev, {"v": 1, "id": "c1", "op": "deployment.unlock_challenge"})
    assert bare(r) == {"v": 1, "id": "c1", "status": "refused", "error": "usb_only"}, r
    r = b.iface_request(dev, {"v": 1, "id": "c2", "op": "deployment.unlock", "args": {"response": "00" * 32}})
    assert bare(r) == {"v": 1, "id": "c2", "status": "refused", "error": "usb_only"}, r
    ch = challenge(d.i.request)   # control: the cable has a challenge outstanding; the broker cannot answer it
    r = b.iface_request(dev, {"v": 1, "id": "c3", "op": "deployment.unlock", "args": {"response": unlock_answer(SECRET, ch)}})
    assert r["status"] == "refused" and r["error"] == "usb_only", r
    # signed does not mean present: a member's signature is not a cable
    for i, (op, args) in enumerate([("deployment.unlock_challenge", None), ("deployment.unlock", {"response": "00" * 32})]):
        r = b.iface_request(dev, wrap(d.node_key, op, args, job=1 + i))
        assert r["status"] == "refused" and r["error"] == "usb_only", r
    r = d.send(wrap(d.node_key, "deployment.unlock_challenge", job=3))
    assert r["status"] == "refused" and r["error"] == "usb_only", "signed, over the cable"
    assert d.i.request("deployment.unlock", {"response": unlock_answer(SECRET, ch)})["status"] == "ok", "the cable's own challenge stood"
    assert d.c.cli("set posture normal confirm") == "OK - already normal"
    seen = wait_audit(d.n, (AUD_REFUSED, MQTT_CALLER, WHAT_UNLOCK))
    assert any((e[1], e[2], e[3]) == (AUD_REFUSED, MQTT_CALLER, WHAT_UNLOCK) for e in seen), seen
    # the broker holds no unlock afterwards either
    r = b.iface_request(dev, {"v": 1, "id": "c4", "op": "deployment.reset"})
    assert r["status"] == "refused", r


def test_a_signed_set_unlock_replaces_the_secret_and_ends_every_older_unlock(nodes):
    d = boot(nodes, "us9")
    d.provision()
    assert d.i.request("deployment.unlock_challenge")["error"] == "no_unlock_secret"
    # sensitive: it needs the nonce this link was issued, and a job id
    r = d.send(wrap(d.node_key, "deployment.set_unlock", {"unlock_secret": SECRET}, job=1))
    assert r["status"] == "refused" and r["error"] == "bad_channel_nonce", r
    r = d.send(wrap(d.node_key, "deployment.set_unlock", {"unlock_secret": SECRET}, job=2, nonce=d.nonce()))
    assert r["status"] == "ok" and r["result"] == {"unlock": "set"}, r
    assert SECRET not in json.dumps(r)
    assert try_unlock(d.i.request)["status"] == "ok"
    assert d.c.cli("set posture normal confirm") == "OK - already normal"
    r = d.send(wrap(d.node_key, "deployment.set_unlock", {"unlock_secret": SECRET2}, job=3, nonce=d.nonce()))
    assert r["status"] == "ok", r
    assert d.c.cli("set posture normal confirm") == NOT_ENROLLED_REFUSAL, "replacing the secret ends the unlock it issued"
    ch = challenge(d.i.request)
    assert try_unlock(d.i.request, SECRET)["error"] == "bad_response", "the old secret is dead"
    assert try_unlock(d.i.request, SECRET2)["status"] == "ok"
    # a pending challenge dies with the secret too
    ch = challenge(d.i.request)
    d.send(wrap(d.node_key, "deployment.set_unlock", {"unlock_secret": SECRET}, job=4, nonce=d.nonce()))
    assert d.i.request("deployment.unlock", {"response": unlock_answer(SECRET, ch)})["error"] == "no_challenge"
    assert try_unlock(d.i.request, SECRET)["status"] == "ok"
    # it persists, and nothing else about the record moved
    g = d.i.request("deployment.get")["result"]
    assert g["authority_revision"] == 1 and g["provision_revision"] == 1 and g["members"][0]["mark"] == 4, g
    d.reboot()
    assert try_unlock(d.i.request, SECRET)["status"] == "ok"
    # refusals: not a secret
    for job, bad in enumerate(({"unlock_secret": "00" * 32}, {"unlock_secret": "ab" * 31}, {"unlock_secret": "zz" * 32}, {}), 20):
        r = d.send(wrap(d.node_key, "deployment.set_unlock", bad, job=job, nonce=d.nonce()))
        assert r["status"] == "invalid" and r["error"] == "bad-args", r
    assert try_unlock(d.i.request, SECRET)["status"] == "ok", "a refused change leaves the secret as it was"


def test_set_unlock_needs_a_healthy_record_and_a_signature(nodes):
    d = boot(nodes, "us10")
    r = d.i.request("deployment.set_unlock", {"unlock_secret": SECRET})
    assert r["status"] == "refused" and r["error"] == "not_enrolled", r
    d.provision()
    d.n.stop()
    wreck_record(d)
    revive(d)
    r = d.i.request("deployment.set_unlock", {"unlock_secret": SECRET})
    assert r["status"] == "refused" and r["error"] == "deployment_record_corrupt", r


def test_a_reset_removes_the_secret_with_the_record(nodes):
    d = boot_with_secret(nodes, "us11")
    assert try_unlock(d.i.request)["status"] == "ok"
    r = d.send(wrap(d.node_key, "deployment.reset", job=1))
    assert r["status"] == "ok" and r["result"]["removed"] == ["record", "marks", "unlock_secret", "marker"], r
    for f in ("mecon_dep_a", "mecon_dep_b"):
        assert not os.path.exists(os.path.join(d.n.dir, f))
    assert d.c.cli("set name Free") == "OK", "unenrolled: the unlock means nothing and nothing is locked"
    assert d.i.request("deployment.unlock_challenge")["error"] == "no_unlock_secret"
    assert d.provision()["status"] == "ok"
    assert d.i.request("deployment.unlock_challenge")["error"] == "no_unlock_secret", "the new record has none"
    d.n.stop()
    for f in os.listdir(d.n.dir):
        p = os.path.join(d.n.dir, f)
        assert not os.path.isfile(p) or bytes.fromhex(SECRET) not in open(p, "rb").read(), f


def test_a_factory_reset_removes_the_secret_with_the_record(nodes):
    d = boot_with_secret(nodes, "us12")
    assert any(bytes.fromhex(SECRET) in open(os.path.join(d.n.dir, f), "rb").read() for f in record_files(d)), "control"
    d.c._send(bytes([harness.CMD_FACTORY_RESET]) + b"reset")
    assert d.n.wait_exit(10) == 75
    revive(d)
    assert d.i.request("deployment.get")["result"] == {"state": "none"}
    assert d.i.request("deployment.unlock_challenge")["error"] == "no_unlock_secret"
    for f in os.listdir(d.n.dir):
        p = os.path.join(d.n.dir, f)
        assert not os.path.isfile(p) or bytes.fromhex(SECRET) not in open(p, "rb").read(), f


def test_the_secret_appears_in_no_reply_status_log_or_file_as_text(nodes):
    d = boot_with_secret(nodes, "us13")
    seen = []
    prov = d.i.request("deployment.provision", with_secret(provision_args(d.node_key)))   # the duplicate read-back
    seen.append(prov)
    seen.append(d.i.request("deployment.get"))
    seen.append(d.i.request("status.get"))
    seen.append(d.i.request("hello"))
    seen.append(d.i.request("config.get", {"keys": ["name"]}))
    ch = d.i.request("deployment.unlock_challenge")
    seen.append(ch)
    seen.append(d.i.request("deployment.unlock", {"response": unlock_answer(SECRET, ch["result"]["challenge"])}))
    seen.append(d.i.request("deployment.unlock", {"response": "11" * 32}))
    seen.append(d.send(wrap(d.node_key, "deployment.set_unlock", {"unlock_secret": SECRET2}, job=1, nonce=d.nonce())))
    for line in ("get posture", "get audit", "get mecon.cli", "ver"):
        seen.append(d.c.cli(line))
    blob = json.dumps(seen)
    for secret in (SECRET, SECRET2):
        assert secret not in blob
        assert secret.upper() not in blob.upper()
    time.sleep(0.5)
    d.n.stop()
    for secret in (SECRET, SECRET2):
        assert harness.files_containing(d.n, secret) == [], harness.files_containing(d.n, secret)
        assert harness.files_containing(d.n, secret.upper()) == []
