"""posture.set (docs/contract/OPERATIONS.md, AUTHORITY.md sections 9 and 11): the signed way to change the posture of an
enrolled device, on the Companion, Repeater and Room server."""
import subprocess

import pytest

from test_deployment_authority import SECRET, boot_with_secret, boot as boot_companion, wrap
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


def posture(d):
    return d.i.request("status.get")["result"]["posture"]


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
    assert posture(d) == "hardened"
