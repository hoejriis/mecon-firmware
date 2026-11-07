"""posture.set (docs/contract/OPERATIONS.md, AUTHORITY.md sections 9 and 11): the signed way to change the posture of an
enrolled device, on the Companion, Repeater and Room server."""
import subprocess
import time

import pytest

from test_deployment_authority import SECRET, boot_with_secret, boot as boot_companion, unlock_answer, wrap
from test_deployment_authority_server import boot as boot_server

KINDS = ["companion_standalone", "repeater", "room_server"]


def enrolled(nodes, kind, name):
    """Enrolled, holding the unlock secret that Hardened needs."""
    if kind == "companion_standalone":
        return boot_with_secret(nodes, name)
    d = boot_server(nodes, kind, name, 81)
    assert d.provision()["status"] == "ok"
    r = d.send(wrap(d.node_key, "deployment.set_unlock", {"unlock_secret": SECRET}, job=1, nonce=d.nonce()))
    assert r["status"] == "ok", r
    return d


READ_SEQ = [0]


def posture(d):
    READ_SEQ[0] += 1
    return d.send(wrap(d.node_key, "status.get", job=None, rid="read-posture-%d" % READ_SEQ[0]))["result"]["posture"]   # Hardened: locked to the cable


def next_job(d):
    return d.i.request("deployment.get")["result"]["members"][0]["mark"] + 1


def set_and_reboot(d, want):
    r = d.send(wrap(d.node_key, "posture.set", {"posture": want}, job=next_job(d)))
    assert r["status"] == "ok" and r["result"] == {"posture": want}, r
    assert d.n.wait_exit(10) == 75, "a change reboots the device"
    d.n.start()
    d.attach()
    return r


@pytest.mark.parametrize("kind", KINDS)
def test_a_signed_request_raises_and_lowers_an_enrolled_device(nodes, kind):
    d = enrolled(nodes, kind, "ps" + kind[:2])
    assert "posture.set" in d.i.request("hello")["result"]["capabilities"]
    set_and_reboot(d, "hardened")
    assert posture(d) == "hardened"
    set_and_reboot(d, "normal")
    assert posture(d) == "normal"


@pytest.mark.parametrize("kind", KINDS)
def test_unsigned_is_refused_once_enrolled_and_a_replay_runs_nothing(nodes, kind):
    d = enrolled(nodes, kind, "pu" + kind[:2])
    r = d.i.request("posture.set", {"posture": "hardened"})
    assert r["status"] == "refused", r
    assert posture(d) == "normal"
    w = wrap(d.node_key, "posture.set", {"posture": "hardened"}, job=next_job(d))
    assert d.send(w)["status"] == "ok"
    assert d.n.wait_exit(10) == 75
    d.n.start()
    d.attach()
    r = d.send(w)
    assert r["status"] == "refused" and r["error"] == "replay", "the reboot does not bring the request back: %s" % r
    r = d.send(wrap(d.node_key, "posture.set", {"posture": "hardened"}, job=next_job(d)))
    assert r["status"] == "ok", r
    with pytest.raises(subprocess.TimeoutExpired):   # already there: no reboot
        d.n.wait_exit(3)
    assert posture(d) == "hardened"


def test_raising_without_an_unlock_secret_is_refused_and_a_bad_value_is_invalid(nodes):
    d = boot_companion(nodes, "pn")
    d.provision()
    r = d.send(wrap(d.node_key, "posture.set", {"posture": "hardened"}, job=1))
    assert r["status"] == "refused" and r["error"] == "not-permitted", r
    assert posture(d) == "normal"
    assert d.send(wrap(d.node_key, "posture.set", {"posture": "open"}, job=2))["status"] == "invalid"


def test_standalone_the_cable_changes_it_as_with_the_cli_line(nodes):
    d = boot_companion(nodes, "ps")
    r = d.i.request("posture.set", {"posture": "hardened"})
    assert r["status"] == "ok", r
    assert d.n.wait_exit(10) == 75
    d.n.start()
    d.attach()
    assert d.i.request("status.get")["result"]["posture"] == "hardened", "no record: nothing is locked"


def hardened(nodes, kind, name):
    d = enrolled(nodes, kind, name)
    set_and_reboot(d, "hardened")
    return d


READS_ALL_ROLES = ["status.get", "config.schema", "job.list", "update.status", "channel.list", "identity.rotation"]
READS_COMPANION = ["contact.list", "inbox.list", "message.list"]


@pytest.mark.parametrize("kind", KINDS)
def test_a_locked_cable_in_hardened_reads_only_what_identifies_the_device(nodes, kind):
    d = hardened(nodes, kind, "lk" + kind[:2])
    h = d.i.request("hello")
    assert h["status"] == "ok" and h["result"]["node_key"] == d.node_key, h
    assert d.i.request("deployment.get")["status"] == "ok", "the stored set is open to any caller"
    ops = READS_ALL_ROLES + (READS_COMPANION if kind == "companion_standalone" else [])
    for op in ops:
        r = d.i.request(op)
        assert (r["status"], r.get("error")) == ("refused", "locked"), (op, r)
    r = d.i.request("config.get", {"keys": ["name"]})
    assert (r["status"], r.get("error")) == ("refused", "locked"), r


@pytest.mark.parametrize("kind", KINDS)
def test_the_local_unlock_opens_the_reads_and_a_signature_needs_no_unlock(nodes, kind):
    d = hardened(nodes, kind, "ul" + kind[:2])
    m = d.i.request("deployment.get")["result"]["members"][0]["mark"] + 1
    r = d.send(wrap(d.node_key, "status.get", job=None, rid="signed-read-%d" % m))
    assert r["status"] == "ok", "a member's signature reads, locked or not: %s" % r
    ch = d.i.request("deployment.unlock_challenge")
    assert ch["status"] == "ok", ch
    r = d.i.request("deployment.unlock", {"response": unlock_answer(SECRET, ch["result"]["challenge"])})
    assert r["status"] == "ok", r
    ops = READS_ALL_ROLES + (READS_COMPANION if kind == "companion_standalone" else [])
    for op in ops:
        assert d.i.request(op)["status"] == "ok", op
    assert d.i.request("config.apply", {"set": {"name": "x"}})["status"] != "ok", "the unlock opens reads, not writes"


def test_normal_is_unchanged_a_cable_reads_with_no_unlock(nodes):
    d = enrolled(nodes, "companion_standalone", "lkn")
    for op in READS_ALL_ROLES + READS_COMPANION:
        assert d.i.request(op)["status"] == "ok", op


def test_a_locked_cable_gets_no_native_reads_on_a_companion(nodes):
    d = hardened(nodes, "companion_standalone", "lkf")
    CMD_APP_START, CMD_GET_CONTACTS, CMD_SYNC_NEXT_MESSAGE, CMD_DEVICE_QUERY, CMD_GET_CHANNEL = 1, 4, 10, 22, 31
    RESP_ERR, RESP_SELF_INFO, RESP_DEVICE_INFO = 1, 5, 13

    def ask(frame):
        d.c._send(frame)
        end = time.time() + 3
        while time.time() < end:
            f = d.c._read_frame(0.3)
            if f and f[0] != 0x8B and f[0] < 0x80:
                return f
        return None

    assert ask(bytes([CMD_DEVICE_QUERY, 3]))[0] == RESP_DEVICE_INFO
    assert ask(bytes([CMD_APP_START, 3]) + bytes(6) + b"t")[0] == RESP_SELF_INFO
    for frame in (bytes([CMD_GET_CONTACTS]), bytes([CMD_SYNC_NEXT_MESSAGE]), bytes([CMD_GET_CHANNEL, 0])):
        assert ask(frame)[0] == RESP_ERR, frame
    ch = d.i.request("deployment.unlock_challenge")
    assert d.i.request("deployment.unlock", {"response": unlock_answer(SECRET, ch["result"]["challenge"])})["status"] == "ok"
    assert ask(bytes([CMD_GET_CHANNEL, 0]))[0] != RESP_ERR, "the unlock opens them"
