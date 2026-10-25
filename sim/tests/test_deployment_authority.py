"""The Deployment authority layer on the simulated standalone Companion (docs/contract/AUTHORITY.md): first provisioning,
signed requests, replay protection across a reboot, the channel nonce, authority updates, and a corrupt record. The keys
are the public test vectors of test/fixtures/deployment_authority_public_v1.json (valid nowhere); requests are signed here
with sim/ed25519_ref.py (RFC 8032 in plain Python; the container has no `cryptography` package), independently of the
firmware's own verifier. The reference signer is itself checked against the vectors' keys and signed request."""
import base64
import json
import os
import sys
import time

import pytest
from sim import ed25519_ref, mqtt_broker
from sim.harness import CompanionClient, MqttEnv, WifiEnv

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
    assert d.c.cli("set name Meanwhile") == "OK"
    again = d.send(w)
    assert again == r, "the same bytes get the stored result"
    assert d.name() == "> Meanwhile", "and run nothing"
    assert d.i.request("deployment.get")["result"]["members"][0]["mark"] == 1
    # after a reboot the result cache is gone, the mark is not: duplicate, with nothing to show for it
    d.reboot()
    assert d.name() == "> Meanwhile"
    dup = d.send(w)
    assert dup["status"] == "failed" and dup["error"] == "duplicate", dup
    assert d.name() == "> Meanwhile"
    assert d.i.request("deployment.get")["result"]["members"][0]["mark"] == 1, "the mark survived the reboot"


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
    assert r == {"v": 1, "id": "w1", "status": "refused", "error": "unauthenticated"}, r
    assert d.name() != "> ViaMqtt"
    # a signed one is the authority, over the same verified broker session; its nonce is the session's own
    w = wrap(d.node_key, "config.apply", apply_name("ViaMqtt"), job=1)
    r = b.iface_request(dev, w)
    assert r["status"] == "ok", r
    assert d.name() == "> ViaMqtt"
    r2 = b.iface_request(dev, w)
    assert r2 == r, "a resend over MQTT gets the stored result, and nothing runs twice"
    # the USB link's nonce is not the broker session's
    h = b.iface_request(dev, {"v": 1, "id": "h1", "op": "hello"})["result"]
    assert len(h["channel_nonce"]) == 32 and h["channel_nonce"] != d.nonce()
    r = b.iface_request(dev, wrap(d.node_key, "identity.key.export", job=2, nonce=d.nonce()))
    assert r["error"] == "bad_channel_nonce", "a nonce issued on USB is no good over MQTT: %s" % r
