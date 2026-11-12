"""A Companion's default flood scope on the managed interface (OPERATIONS.md, "Region scopes"): scope.default.get, .set and
.clear on the same prefs the native frames 63 and 64 use. A hashtag scope's key is derived as the firmware's own default is,
a private one's is given and never read back, and a signed caller must confirm."""
import hashlib
import os
import sys
import time

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from test_deployment_authority import RTC, Dev, wrap  # noqa: E402
from test_regions import capabilities  # noqa: E402

CMD_GET_DEFAULT_FLOOD_SCOPE, RESP_CODE_DEFAULT_FLOOD_SCOPE = 64, 28
PRIVATE = bytes(range(0x40, 0x50))


def native(d):
    """The default scope as frame 64 reads it: (name, key), or None."""
    f = d.c.request(bytes([CMD_GET_DEFAULT_FLOOD_SCOPE]))
    assert f[0] == RESP_CODE_DEFAULT_FLOOD_SCOPE, f
    if len(f) < 1 + 31 + 16:
        return None
    return f[1:32].split(b"\0")[0].decode(), bytes(f[32:48])


def wait_job(d, job, secs=30):
    end = time.time() + secs
    st = None
    while time.time() < end:
        st = d.i.request("job.get", {"job": job})["result"]["state"]
        if st != "awaiting-confirm":
            break
        time.sleep(0.5)
    return st


def companion(nodes, name, seed):
    return Dev(nodes("companion_standalone", name, seed=seed, rtc=RTC).start())


def shaped(op, r):
    import mecon_schema
    assert r["status"] == "ok", r
    problems = mecon_schema.check(r["result"], mecon_schema.load(op + ".result.json"))
    assert not problems, (op, problems, r)
    return r


def test_set_get_clear_on_the_native_prefs(nodes):
    d = companion(nodes, "sc", 100)
    r = shaped("scope.default.get", d.i.request("scope.default.get"))
    assert r["status"] == "ok" and r["result"] == {"name": None, "kind": None}, r
    assert native(d) is None
    caps = capabilities(d)   # every page: the list pages once it outgrows one
    assert {"scope.default.get", "scope.default.set", "scope.default.clear"} <= set(caps), caps
    r = shaped("scope.default.set", d.i.request("scope.default.set", {"name": "#city"}))
    assert r["status"] == "ok" and r["result"] == {"name": "city", "kind": "hashtag"}, r
    assert native(d) == ("city", hashlib.sha256(b"#city").digest()[:16]), "the key the firmware's own default derives"
    r = d.i.request("scope.default.set", {"name": "$ops", "key": PRIVATE.hex()})
    assert r["status"] == "ok" and r["result"] == {"name": "$ops", "kind": "private"}, r
    assert native(d) == ("$ops", PRIVATE)
    r = shaped("scope.default.get", d.i.request("scope.default.get"))
    assert r["result"] == {"name": "$ops", "kind": "private"} and PRIVATE.hex() not in str(r), "the key is never read back: %s" % r
    d.reboot()
    assert native(d) == ("$ops", PRIVATE), "saved in the prefs"
    shaped("scope.default.clear", d.i.request("scope.default.clear"))
    assert native(d) is None and d.i.request("scope.default.get")["result"]["name"] is None


def test_names_and_keys_are_checked(nodes):
    d = companion(nodes, "sv", 101)
    for args, err in (({"name": "$ops"}, "key-required"), ({"name": "a" * 31}, "name-too-long"),
                      ({"name": "with space"}, "bad-char"), ({"name": "$ops", "key": "00" * 16}, "bad-key"),
                      ({"name": "$ops", "key": "ab" * 15}, "bad-key"), ({"name": "#"}, "bad-name")):
        r = d.i.request("scope.default.set", args)
        assert (r["status"], r.get("error")) == ("invalid", err), (args, r)
    assert native(d) is None, "nothing was written"
    assert d.i.request("scope.default.set", {"name": "a" * 30})["status"] == "ok"


@pytest.mark.timescale(10)
def test_a_signed_change_must_confirm_reverts_unconfirmed_and_stays_confirmed(nodes):
    d = companion(nodes, "ss", 102)
    assert d.i.request("scope.default.set", {"name": "home"})["status"] == "ok"
    assert d.provision()["status"] == "ok"
    key = d.node_key
    r = d.i.request("scope.default.set", {"name": "cable"})
    assert r["status"] == "refused", "once enrolled the unsigned cable changes nothing: %s" % r
    r = d.send(wrap(key, "scope.default.set", {"name": "away"}, job=1))
    assert (r["status"], r.get("error")) == ("invalid", "confirm-required"), r
    r = d.send(wrap(key, "scope.default.set", {"name": "away", "confirm": {"within_s": 30}}, job=2))
    assert r["status"] == "ok" and r["result"]["job_state"] == "awaiting-confirm", r
    assert native(d)[0] == "away", "in effect at once, tentatively"
    assert wait_job(d, r["result"]["job"]) == "reverted"
    assert native(d) == ("home", hashlib.sha256(b"#home").digest()[:16]), "unconfirmed, the previous scope is back"
    r = d.send(wrap(key, "scope.default.clear", {"confirm": {"within_s": 60}}, job=3))
    assert r["status"] == "ok", r
    c = d.send(wrap(key, "job.confirm", {"job": r["result"]["job"]}, job=4))
    assert c["status"] == "ok" and c["result"]["state"] == "confirmed", c
    d.reboot()
    assert native(d) is None, "the confirmed clear survives a reboot"
