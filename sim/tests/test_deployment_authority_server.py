"""The Deployment authority layer on the simulated Repeater and Room server (docs/contract/AUTHORITY.md), through the
USB console's "mecon> " lines: first provisioning (the cable), signed requests with persist-before-run, the duplicate
cache and replay across a reboot, the channel nonce, unsigned writes refused once enrolled over the console, over LoRa
remote admin and over the Ethernet console, `deployment.reset`, `erase`, and the provisioned marker. Keys are the public
test vectors; requests are signed with sim/ed25519_ref.py, independently of the firmware's verifier."""
import json
import os
import sys
import time

import pytest

from sim.harness import CliClient, CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import mecon_iface  # noqa: E402
from test_deployment_authority import (bare, DEP, INST_A, INST_B, SET1, SECRET, SECRET2, WHAT_UNLOCK, apply_name,  # noqa: E402
                                       provision_args, record_files, try_unlock, unlock_answer, update_args,
                                       with_secret, wrap)

RTC = 1792800000
KINDS = ["repeater", "room_server"]
PW = "s3cretpw"
ENROLLED_REFUSAL = "ERR: this device belongs to a Deployment; the change must be signed"


class Srv:
    """A booted server with the stock CLI and the interface on its USB console."""

    def __init__(self, n):
        self.n = n
        self.attach()

    def attach(self):
        self.cli = CliClient(self.n)
        self.i = mecon_iface.ConsoleClient(self.cli.sock.sendall, mecon_iface.socket_lines(self.cli.sock))
        self.node_key = self.i.request("hello")["result"]["node_key"]

    def reboot(self):
        self.n.stop()
        self.n.start()
        self.attach()

    def send(self, wire, timeout=5.0):
        self.i.send_raw(json.dumps(wire, separators=(",", ":")))
        return self.i.wait_result(json.loads(wire["body"])["id"], timeout)

    def provision(self, revision=1):
        return self.i.request("deployment.provision", provision_args(self.node_key, revision))

    def nonce(self):
        return self.i.request("hello")["result"]["channel_nonce"]

    def name(self):
        return self.cli.command("get name")


def boot(nodes, kind="repeater", name="sv", seed=21, **kw):
    return Srv(nodes(kind, name, seed=seed, rtc=RTC, **kw).start())


def marker(d):
    return d.n.dir.rstrip("/") + ".prov"


def files(d):
    return [f for f in ("mecon_dep_a", "mecon_dep_b", "mecon_depm_a", "mecon_depm_b") if os.path.exists(os.path.join(d.n.dir, f))]


# ------------------------------------------------------------------ provisioning

@pytest.mark.parametrize("kind", KINDS)
def test_first_provisioning_over_the_console_and_the_read_back(nodes, kind):
    d = boot(nodes, kind, "p" + kind)
    assert d.i.request("deployment.get")["result"] == {"state": "none"}
    assert "deployment" not in d.i.request("status.get")["result"], "a standalone device reports no deployment"
    assert "channel_nonce" not in d.i.request("hello")["result"], "nothing to sign against yet"
    r = d.provision()
    assert r["status"] == "ok", r
    assert r["result"]["deployment_id"] == DEP and r["result"]["members"] == [
        {"instance_id": INST_A, "admitted_revision": 1, "mark": 0}]
    g = d.i.request("deployment.get")["result"]
    assert g["authority_set"] == SET1["authority_set"] and g["authority_set_sig"] == SET1["sig"]
    s = d.i.request("status.get")["result"]["deployment"]
    assert s["deployment_id"] == DEP and s["auth_rejections"] == 0, s
    h = d.i.request("hello")["result"]
    assert len(h["channel_nonce"]) == 32 and h["capabilities"].count("deployment.reset") == 1, h
    assert os.path.exists(marker(d)), "the marker is written at the first provisioning"
    again = d.provision()
    assert again["status"] == "ok" and again["result"]["duplicate"] is True
    # holding a record, an unsigned change is refused even from the cable
    r = d.i.request("deployment.provision", provision_args(d.node_key, 2))
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r


def test_provisioning_refusals_write_nothing(nodes):
    d = boot(nodes, "repeater", "pr")
    bad = provision_args(d.node_key)
    bad["deployment"]["node_public_key"] = "11" * 32
    assert d.i.request("deployment.provision", bad)["error"] == "wrong_device"
    assert d.i.request("deployment.get")["result"] == {"state": "none"}
    assert files(d) == [] and not os.path.exists(marker(d))


# ------------------------------------------------------------------ signed requests

@pytest.mark.parametrize("kind", KINDS)
def test_a_signed_config_apply_runs_once_and_its_duplicate_and_replay_run_nothing(nodes, kind):
    d = boot(nodes, kind, "s" + kind)
    d.provision()
    w = wrap(d.node_key, "config.apply", apply_name("Signed1"), job=1)
    r = d.send(w)
    assert r["status"] == "ok" and r["result"]["complete"], r
    assert d.name() == "> Signed1"
    assert bare(d.send(w)) == bare(r), "the same bytes get the stored result"
    w2 = wrap(d.node_key, "config.apply", apply_name("Meanwhile"), job=2)
    assert d.send(w2)["status"] == "ok"
    refused = d.send(w)
    assert refused["status"] == "refused" and refused["error"] == "replay", refused
    assert d.name() == "> Meanwhile"
    assert d.i.request("deployment.get")["result"]["members"][0]["mark"] == 2
    d.reboot()   # the result cache is gone, the mark is not
    assert d.name() == "> Meanwhile"
    dup = d.send(w2)
    assert dup["status"] == "failed" and dup["error"] == "duplicate", dup
    assert d.send(w)["error"] == "replay"
    assert d.name() == "> Meanwhile"
    assert d.i.request("deployment.get")["result"]["members"][0]["mark"] == 2, "the mark survived the reboot"


def test_persist_before_run_a_mark_that_cannot_be_written_runs_nothing(nodes):
    d = boot(nodes, "repeater", "wf")
    d.provision()
    w = wrap(d.node_key, "config.apply", apply_name("Persisted"), job=1)
    os.chmod(d.n.dir, 0o555)
    try:
        r = d.send(w)
        assert r["status"] == "busy" and r["error"] == "storage", r
        assert d.name() != "> Persisted", "the request did not run"
    finally:
        os.chmod(d.n.dir, 0o755)
    assert d.i.request("deployment.get")["result"]["members"][0]["mark"] == 0, "the mark stayed where it was"
    r = d.send(w)
    assert r["status"] == "ok" and d.name() == "> Persisted", r


def test_the_mark_is_saved_before_the_request_runs_and_survives_a_restart(nodes):
    d = boot(nodes, "repeater", "pb")
    d.provision()
    assert d.send(wrap(d.node_key, "config.apply", apply_name("Five"), job=5))["status"] == "ok"
    d.reboot()
    old = d.send(wrap(d.node_key, "config.apply", apply_name("Three"), job=3))
    assert old["status"] == "refused" and old["error"] == "replay", old
    assert d.name() == "> Five"


def test_refusals_consume_no_job_id(nodes):
    d = boot(nodes, "repeater", "bs")
    d.provision()
    assert d.send(wrap(d.node_key, "config.apply", apply_name("W"), job=1, target="22" * 32))["error"] == "wrong_device"
    assert d.send(wrap(d.node_key, "config.apply", apply_name("W"), job=1, deployment="33" * 16))["error"] == "wrong_deployment"
    w = wrap(d.node_key, "config.apply", apply_name("W"), job=1)
    w["body"] = w["body"].replace("W", "V")
    assert d.send(w)["error"] == "bad_signature"
    assert d.send(wrap(d.node_key, "config.apply", apply_name("W"), job=1, rev=9))["error"] == "authority_revision_ahead"
    r = d.send(wrap(d.node_key, "config.apply", apply_name("W"), job=None))
    assert r["error"] == "unauthenticated", "a write without a job id"
    assert d.name() != "> W"
    assert d.i.request("status.get")["result"]["deployment"]["auth_rejections"] == 5
    assert d.send(wrap(d.node_key, "config.apply", apply_name("Ok"), job=1))["status"] == "ok"


def test_a_signed_request_with_no_record_is_unauthenticated(nodes):
    d = boot(nodes, "repeater", "nr")
    r = d.send(wrap(d.node_key, "config.apply", apply_name("N"), job=1))
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r
    assert d.name() != "> N"


def test_a_sensitive_operation_needs_the_consoles_nonce_once(nodes):
    d = boot(nodes, "repeater", "cn")
    d.provision()
    n1 = d.nonce()
    assert n1 == d.nonce(), "stable until used"
    assert d.send(wrap(d.node_key, "identity.key.export", job=1))["error"] == "bad_channel_nonce"
    r = d.send(wrap(d.node_key, "identity.key.export", job=2, nonce=n1))
    assert r["status"] == "unknown-op", "past every check; this build has no such operation: %s" % r
    assert d.nonce() != n1, "a nonce is used once"


def test_authority_update_over_the_console_and_the_new_member_signs(nodes):
    d = boot(nodes, "repeater", "au")
    d.provision()
    assert d.i.request("deployment.authority_update", update_args())["status"] == "ok"
    r = d.send(wrap(d.node_key, "config.apply", apply_name("B1"), job=1, issuer=INST_B, rev=2))
    assert r["status"] == "ok" and d.name() == "> B1", r


# ------------------------------------------------------------------ unsigned writes once enrolled

@pytest.mark.parametrize("kind", KINDS)
def test_unsigned_writes_are_refused_once_enrolled_over_the_console(nodes, kind):
    d = boot(nodes, kind, "uw" + kind)
    assert d.cli.command("set name Before") in ("OK", "OK - name set"), "standalone: unchanged"
    d.provision()
    assert d.cli.command("set name After") == ENROLLED_REFUSAL
    for line in ("set posture hardened", "set prv.key " + "00" * 64, "password abc"):
        assert d.cli.command(line).startswith("ERR"), line
    r = d.i.request("config.apply", apply_name("After"))
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r
    assert d.name() == "> Before"
    assert d.cli.command("get posture") == "> normal", "reads work"
    assert d.i.request("config.get", {"keys": ["name"]})["status"] == "ok"
    # a signed write is the way
    assert d.send(wrap(d.node_key, "config.apply", apply_name("Signed"), job=1))["status"] == "ok"
    assert d.name() == "> Signed"
    assert d.cli.command("set name After").startswith("ERR")
    # a corrupt record counts as enrolled
    d.n.stop()
    for f in ("mecon_dep_a", "mecon_dep_b"):
        p = os.path.join(d.n.dir, f)
        if os.path.exists(p):
            open(p, "wb").write(b"\x00garbage")
    d.n.start()
    d.attach()
    assert d.i.request("deployment.get")["result"] == {"state": "corrupt"}
    assert d.cli.command("set name After").startswith("ERR")


@pytest.mark.parametrize("kind", ["repeater_eth", "room_server_eth"])
def test_unsigned_writes_are_refused_over_the_ethernet_console_once_enrolled(nodes, kind):
    n = nodes(kind, "et", seed=41, rtc=RTC).start()
    d = Srv(n)
    d.cli.command("password " + PW)
    eth = CliClient(n, ethernet=True)
    assert eth.command("login " + PW) == "OK - logged in"
    assert eth.command("set name Pre") != "" and not eth.command("set name Pre").startswith("ERR"), "control: standalone"
    d.provision()
    reply = eth.command("set name Post")
    assert reply.startswith("ERR"), reply
    assert eth.command("get name") == "> Pre"


@pytest.mark.lockstep()
@pytest.mark.parametrize("kind", KINDS)
def test_lora_remote_admin_keeps_reads_only_once_enrolled(ether, nodes, kind):
    rep = nodes(kind, "rp", seed=41, rtc=RTC, ether_path=ether.path).start()
    adm = nodes("companion", "ad", seed=42, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes(["rp", "ad"])
    ether.set_link("rp", "ad")
    assert ether.wait_tx("rp") and ether.wait_quiet()
    d = Srv(rep)
    d.cli.command("password " + PW)
    pub = bytes.fromhex(d.cli.command("get public.key").lstrip("> ").strip())
    c = CompanionClient(adm)
    c.device_query()
    c.app_start()
    assert c.add_contact(pub, "srv", ctype=2 if kind == "repeater" else 3)[0] == 0
    assert c.login(pub, PW)
    assert c.remote_admin(pub, "set name Remote1") == "OK", "control: standalone remote admin writes"
    assert d.provision()["status"] == "ok"
    assert c.remote_admin(pub, "get name") == "> Remote1", "reads work"
    r = c.remote_admin(pub, "set name Remote2")
    assert r.startswith("ERR"), r
    assert d.name() == "> Remote1"


# ------------------------------------------------------------------ reset, erase, marker

@pytest.mark.parametrize("kind", KINDS)
def test_a_signed_reset_over_the_console_unenrols_and_keeps_the_rest(nodes, kind):
    d = boot(nodes, kind, "rs" + kind)
    d.cli.command("set name Kept")
    key = d.node_key
    dev = d.i.request("hello")["result"]["device_id"]
    d.provision()
    assert d.send(wrap(d.node_key, "config.apply", apply_name("Kept2"), job=1))["status"] == "ok"
    r = d.send(wrap(d.node_key, "deployment.reset", job=2))
    assert r["status"] == "ok" and r["result"]["state"] == "none", r
    assert r["result"]["removed"] == ["record", "marks", "marker"], r
    assert files(d) == [] and not os.path.exists(marker(d))
    assert "channel_nonce" not in d.i.request("hello")["result"]
    assert d.cli.command("set name Again") != ENROLLED_REFUSAL, "standalone again"
    d.cli.command("set name Kept2")
    d.reboot()
    assert d.node_key == key and d.i.request("hello")["result"]["device_id"] == dev
    assert d.i.request("deployment.get")["result"] == {"state": "none"}
    assert d.provision()["status"] == "ok", "first use is open again, and needs the cable"


def test_an_unsigned_reset_of_a_healthy_record_is_refused(nodes):
    d = boot(nodes, "repeater", "ru")
    d.provision()
    r = d.i.request("deployment.reset")
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r
    assert d.i.request("deployment.get")["result"]["state"] == "provisioned" and os.path.exists(marker(d))


def test_an_unsigned_reset_over_the_cable_recovers_a_corrupt_record(nodes):
    d = boot(nodes, "repeater", "rc")
    d.provision()
    d.n.stop()
    for f in ("mecon_dep_a", "mecon_dep_b"):
        p = os.path.join(d.n.dir, f)
        if os.path.exists(p):
            open(p, "wb").write(b"\x00garbage")
    d.n.start()
    d.attach()
    assert d.i.request("deployment.get")["result"] == {"state": "corrupt"}
    r = d.i.request("deployment.reset")
    assert r["status"] == "ok" and "record" in r["result"]["removed"] and "marker" in r["result"]["removed"], r
    assert d.i.request("deployment.get")["result"] == {"state": "none"} and not os.path.exists(marker(d))
    assert d.i.request("deployment.reset")["error"] == "not_enrolled"


@pytest.mark.parametrize("kind", KINDS)
def test_the_marker_makes_deleted_record_files_corrupt_not_absent(nodes, kind):
    d = boot(nodes, kind, "mk" + kind)
    d.provision()
    d.n.stop()
    for f in ("mecon_dep_a", "mecon_dep_b", "mecon_depm_a", "mecon_depm_b"):
        p = os.path.join(d.n.dir, f)
        if os.path.exists(p):
            os.remove(p)
    assert files(d) == [] and os.path.exists(marker(d))
    d.n.start()
    d.attach()
    assert d.i.request("deployment.get")["result"] == {"state": "corrupt"}
    r = d.provision()
    assert r["status"] == "refused" and r["error"] == "deployment_record_corrupt", r
    assert d.send(wrap(d.node_key, "config.apply", apply_name("M"), job=1))["error"] == "deployment_record_corrupt"
    assert d.cli.command("set name M").startswith("ERR"), "corrupt counts as enrolled"


def test_without_the_marker_a_device_with_no_files_is_a_new_device(nodes):
    d = boot(nodes, "repeater", "mk2")
    d.n.stop()
    open(marker(d), "wb").close()
    d.n.start()
    d.attach()
    assert d.i.request("deployment.get")["result"] == {"state": "corrupt"}
    d.n.stop()
    os.remove(marker(d))
    d.n.start()
    d.attach()
    assert d.i.request("deployment.get")["result"] == {"state": "none"}
    assert d.provision()["status"] == "ok"


@pytest.mark.parametrize("kind", KINDS)
def test_erase_works_from_the_cable_once_enrolled_and_clears_the_marker(nodes, kind):
    d = boot(nodes, kind, "er" + kind)
    d.provision()
    assert os.path.exists(marker(d))
    assert d.cli.command("erase") == "File system erase: OK"
    assert not os.path.exists(marker(d)), "the marker goes with the file system"
    assert files(d) == []
    assert d.i.request("deployment.get")["result"] == {"state": "none"}, "the state in memory follows"
    assert d.cli.command("set name Free") != ENROLLED_REFUSAL
    assert d.provision()["status"] == "ok", "first use is open again"


def test_erase_is_refused_to_a_signed_request_and_over_ethernet(nodes):
    n = nodes("repeater_eth", "ee", seed=41, rtc=RTC).start()
    d = Srv(n)
    d.cli.command("password " + PW)
    d.provision()
    eth = CliClient(n, ethernet=True)
    assert eth.command("login " + PW) == "OK - logged in"
    assert eth.command("erase").startswith("ERR")
    assert d.send(wrap(d.node_key, "device.erase", job=1))["status"] == "unknown-op"
    assert d.i.request("deployment.get")["result"]["state"] == "provisioned" and os.path.exists(marker(d))


# ------------------------------------------------------------------ local unlock (contract sections 9, 10 and 11)

def boot_with_secret(nodes, kind, name, posture=None):
    d = boot(nodes, kind, name)
    if posture:
        assert d.cli.command("set posture %s" % posture).startswith("OK"), posture
    assert d.i.request("deployment.provision", with_secret(provision_args(d.node_key)))["status"] == "ok"
    return d


def damage_set_keep_crc(d):
    import struct
    import zlib
    from test_deployment_authority import V
    dak = bytes.fromhex(V["keys"]["dak epoch 1"]["public_key"])
    done = 0
    for f in ("mecon_dep_a", "mecon_dep_b"):
        path = os.path.join(d.n.dir, f)
        if os.path.exists(path):
            b = bytearray(open(path, "rb").read())
            b[b.index(dak)] ^= 1
            b[-4:] = struct.pack("<I", zlib.crc32(bytes(b[:-4])))
            open(path, "wb").write(bytes(b))
            done += 1
    assert done


@pytest.mark.parametrize("kind", KINDS)
def test_the_console_unlock_opens_the_posture_in_normal_and_nothing_else(nodes, kind):
    d = boot(nodes, kind, "ua" + kind)
    d.provision()
    assert d.i.request("deployment.unlock_challenge")["error"] == "no_unlock_secret", "no secret was provisioned"
    assert d.i.request("deployment.set_unlock", {"unlock_secret": SECRET})["error"] == "unauthenticated"
    r = d.send(wrap(d.node_key, "deployment.set_unlock", {"unlock_secret": SECRET}, job=1, nonce=d.nonce()))
    assert r["status"] == "ok" and r["result"] == {"unlock": "set"}, r
    assert d.cli.command("set posture normal confirm") == ENROLLED_REFUSAL, "control: locked"
    r = try_unlock(d.i.request)
    assert r["status"] == "ok" and r["result"] == {"unlocked": True, "expires_in_s": 600}, r
    assert d.cli.command("set posture normal confirm") == "OK - already normal"
    assert d.cli.command("set name Nope") == ENROLLED_REFUSAL
    assert d.cli.command("get prv.key").startswith("ERR")
    assert d.i.request("deployment.reset")["error"] == "unauthenticated", "a healthy record is reset by a signature"
    assert d.i.request("config.apply", apply_name("Nope"))["error"] == "unauthenticated"
    d.reboot()
    assert d.cli.command("set posture normal confirm") == ENROLLED_REFUSAL, "a reboot forgets it"


@pytest.mark.parametrize("kind", KINDS)
def test_an_unlocked_console_may_enter_hardened_because_the_record_holds_the_secret(nodes, kind):
    d = boot_with_secret(nodes, kind, "uj" + kind)
    assert d.cli.command("set posture hardened") == ENROLLED_REFUSAL, "control: locked"
    r = try_unlock(d.i.request)
    assert r["status"] == "ok", r
    assert d.cli.command("set posture hardened").startswith("OK"), "the record holds a secret: the rule lets it through"
    assert d.cli.command("get posture") == "> hardened"


@pytest.mark.parametrize("kind", KINDS)
def test_in_hardened_the_console_unlock_resets_a_corrupt_record_and_opens_nothing_else(nodes, kind):
    d = boot_with_secret(nodes, kind, "uh" + kind, posture="hardened")
    assert try_unlock(d.i.request)["status"] == "ok"
    assert d.cli.command("set posture normal confirm").startswith("ERR"), "the posture stays"
    assert d.i.request("deployment.reset")["error"] == "unauthenticated"
    d.n.stop()
    damage_set_keep_crc(d)
    d.n.start()
    d.attach()
    assert d.i.request("deployment.get")["result"] == {"state": "corrupt"}
    r = d.i.request("deployment.reset")
    assert r["status"] == "refused" and r["error"] == "unlock_required", r
    assert try_unlock(d.i.request, SECRET2)["error"] == "bad_response"
    assert d.i.request("deployment.reset")["error"] == "unlock_required"
    assert try_unlock(d.i.request)["status"] == "ok"
    assert d.cli.command("set name Nope").startswith("ERR")
    r = d.i.request("deployment.reset")
    assert r["status"] == "ok" and "record" in r["result"]["removed"], r
    assert d.i.request("deployment.get")["result"] == {"state": "none"} and not os.path.exists(marker(d))
    assert d.i.request("deployment.unlock_challenge")["error"] == "no_unlock_secret", "the secret went with the record"


def test_an_unreadable_corrupt_record_in_hardened_cannot_be_unlocked(nodes):
    d = boot_with_secret(nodes, "repeater", "ux", posture="hardened")
    d.n.stop()
    for f in ("mecon_dep_a", "mecon_dep_b"):
        p = os.path.join(d.n.dir, f)
        if os.path.exists(p):
            open(p, "wb").write(b"\x00garbage")
    d.n.start()
    d.attach()
    assert d.i.request("deployment.unlock_challenge")["error"] == "no_unlock_secret"
    assert d.i.request("deployment.reset")["error"] == "unlock_required"


@pytest.mark.parametrize("kind", KINDS)
def test_the_console_lockout_a_spent_challenge_and_a_signed_attempt(nodes, kind):
    d = boot_with_secret(nodes, kind, "ul" + kind)
    for i in range(5):
        r = try_unlock(d.i.request, SECRET2)
        assert r["status"] == "refused" and r["error"] == "bad_response", (i, r)
    assert d.i.request("deployment.unlock_challenge")["error"] == "locked_out"
    assert d.cli.command("set posture normal confirm") == ENROLLED_REFUSAL
    d.reboot()   # the lockout is the running device's: a reboot is also a power cut, and the cable is physical presence
    r = d.i.request("deployment.unlock_challenge")
    assert r["status"] == "ok"
    ch = r["result"]["challenge"]
    assert d.i.request("deployment.unlock", {"response": unlock_answer(SECRET, ch)})["status"] == "ok"
    assert d.i.request("deployment.unlock", {"response": unlock_answer(SECRET, ch)})["error"] == "no_challenge", "spent"
    # a member's signature is not a cable
    r = d.send(wrap(d.node_key, "deployment.unlock_challenge", job=1))
    assert r["status"] == "refused" and r["error"] == "usb_only", r


def test_the_server_secret_is_in_no_reply_or_text_and_goes_with_the_record(nodes):
    d = boot_with_secret(nodes, "repeater", "us")
    seen = [d.i.request("deployment.get"), d.i.request("status.get"), d.i.request("hello")]
    ch = d.i.request("deployment.unlock_challenge")
    seen += [ch, d.i.request("deployment.unlock", {"response": unlock_answer(SECRET, ch["result"]["challenge"])}),
             d.i.request("deployment.unlock", {"response": "11" * 32}), d.cli.command("get audit"), d.cli.command("ver")]
    assert SECRET not in json.dumps(seen)
    r = d.send(wrap(d.node_key, "deployment.reset", job=1))
    assert r["status"] == "ok" and "unlock_secret" in r["result"]["removed"], r
    d.n.stop()
    for f in os.listdir(d.n.dir):
        p = os.path.join(d.n.dir, f)
        assert not os.path.isfile(p) or bytes.fromhex(SECRET) not in open(p, "rb").read(), f
    from sim import harness
    assert harness.files_containing(d.n, SECRET) == []
