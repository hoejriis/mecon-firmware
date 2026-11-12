"""Region and scope changes (Issue #421): a signed change resent under the same job id changes nothing a second time, and a
private scope's key appears nowhere a manager reads besides the one place it is given (the request)."""
import base64
import json
import os
import sys
import time

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402
from sim.harness import CompanionClient  # noqa: E402
from test_deployment_authority import PIN, RTC, Dev, bare, broker, mqtt_boot, wrap  # noqa: E402,F401
from test_deployment_authority_server import boot  # noqa: E402
from test_regions import listed  # noqa: E402
from test_scope import native as _native  # noqa: E402

KINDS = ["repeater", "room_server"]


def native(d):
    """Frame 64's scope; a job event that arrives first on the cable is not the answer, so ask again."""
    for _ in range(6):
        try:
            return _native(d)
        except AssertionError:
            time.sleep(0.3)
    return _native(d)
KEY_A = bytes(range(0x40, 0x50))
KEY_B = bytes(range(0xA0, 0xB0))


def confirmed(d, op, args, job):
    """A signed change that confirms: the change at `job`, its job.confirm at job + 1. Returns the change's result."""
    r = d.send(wrap(d.node_key, op, dict(args, confirm={"within_s": 60}), job=job))
    assert r["status"] == "ok" and r["result"]["job_state"] == "awaiting-confirm", r
    c = d.send(wrap(d.node_key, "job.confirm", {"job": r["result"]["job"]}, job=job + 1))
    assert c["status"] == "ok" and c["result"]["state"] == "confirmed", c
    return r


@pytest.mark.parametrize("kind", KINDS)
def test_a_region_change_resent_under_its_job_id_changes_nothing_twice(nodes, kind):
    d = boot(nodes, kind, "rr" + kind[:2], 97)
    assert d.provision()["status"] == "ok"
    put = wrap(d.node_key, "region.put", {"name": "alpha", "confirm": {"within_s": 60}}, job=1)
    r = d.send(put)
    assert r["status"] == "ok" and r["result"]["job_state"] == "awaiting-confirm", r
    again = d.send(put)   # the same bytes while the job is open: the stored answer, not a second job
    assert bare(again) == bare(r), (r, again)
    assert d.send(wrap(d.node_key, "job.confirm", {"job": r["result"]["job"]}, job=2))["result"]["state"] == "confirmed"
    jobs = d.send(wrap(d.node_key, "job.list", job=None))["result"]["jobs"]
    assert len([j for j in jobs if j["op"] == "region.put"]) == 1, "one job, however often it was sent: %s" % jobs
    # Later changes move the mark past job 1; the old put, sent again, must not bring "alpha" back.
    confirmed(d, "region.remove", {"name": "alpha"}, job=3)
    assert "alpha" not in listed(d)[1]
    stale = d.send(put)
    assert stale["status"] == "refused" and stale["error"] == "replay", stale
    assert "alpha" not in listed(d)[1], "the replayed put changed nothing"
    # The same holds for a flood flag and for a reboot in between (the mark is on flash).
    confirmed(d, "region.put", {"name": "beta"}, job=5)
    flood = wrap(d.node_key, "region.flood", {"name": "beta", "flood": "deny", "confirm": {"within_s": 60}}, job=7)
    r = d.send(flood)
    assert r["status"] == "ok", r
    assert d.send(wrap(d.node_key, "job.confirm", {"job": r["result"]["job"]}, job=8))["status"] == "ok"
    confirmed(d, "region.flood", {"name": "beta", "flood": "allow"}, job=9)
    d.reboot()
    stale = d.send(flood)
    assert stale["status"] == "refused" and stale["error"] == "replay", stale
    assert listed(d)[1]["beta"]["flood"] == "allow", "the replayed flood flag changed nothing"


def test_a_scope_change_resent_under_its_job_id_changes_nothing_twice(nodes):
    d = Dev(nodes("companion_standalone", "rsc", seed=103, rtc=RTC).start())
    assert d.provision()["status"] == "ok"
    st = wrap(d.node_key, "scope.default.set", {"name": "$ops", "key": KEY_A.hex(), "confirm": {"within_s": 60}}, job=1)
    r = d.send(st)
    assert r["status"] == "ok" and r["result"]["job_state"] == "awaiting-confirm", r
    assert bare(d.send(st)) == bare(r), "the same bytes get the stored answer"
    assert d.send(wrap(d.node_key, "job.confirm", {"job": r["result"]["job"]}, job=2))["result"]["state"] == "confirmed"
    jobs = d.send(wrap(d.node_key, "job.list", job=None))["result"]["jobs"]
    assert len([j for j in jobs if j["op"] == "scope.default.set"]) == 1, jobs
    confirmed(d, "scope.default.clear", {}, job=3)
    assert native(d) is None
    stale = d.send(st)
    assert stale["status"] == "refused" and stale["error"] == "replay", stale
    assert native(d) is None, "the replayed set did not bring the scope back"
    d.reboot()
    stale = d.send(st)
    assert stale["status"] == "refused" and stale["error"] == "replay", "the mark survives a reboot: %s" % stale
    assert native(d) is None


def sweep(i, secs):
    """Every event that arrives within `secs`, as JSON text."""
    out, end = [], time.time() + secs
    for e in list(i.events):
        out.append(json.dumps(e))
        i.events.remove(e)
    while time.time() < end:
        m = i.read_message(max(0.05, end - time.time()))
        if m:
            out.append(json.dumps(m))
    return out


def test_a_private_scope_key_is_in_no_log_event_audit_entry_or_job_record(nodes):
    d = Dev(nodes("companion_standalone", "rka", seed=104, rtc=RTC).start())
    assert d.provision()["status"] == "ok"
    key = d.node_key
    seen = []

    def record(r):
        seen.append(json.dumps(r))
        assert r["status"] == "ok", r
        return r

    streams = ["logs", "health", "radio", "power", "runtime", "inventory", "connectivity"]
    started = []
    for s in streams:   # every stream the device has; a name it lacks is skipped
        r = d.send(wrap(key, "sub.start", {"stream": s, "lifetime_s": 120, **({} if s in ("logs", "connectivity", "runtime") else {"period_s": 30})},
                              job=None, nonce=d.nonce()))
        if r["status"] == "ok":
            started.append(s)
    assert "logs" in started and "runtime" in started, started
    n = 10
    for k, secret in enumerate((KEY_A, KEY_B)):
        r = record(d.send(wrap(key, "scope.default.set", {"name": "$ops%d" % k, "key": secret.hex(), "confirm": {"within_s": 60}}, job=n)))
        assert r["status"] == "ok", r
        record(d.i.request("job.get", {"job": r["result"]["job"]}))
        record(d.send(wrap(key, "job.confirm", {"job": r["result"]["job"]}, job=n + 1)))
        n += 2
    record(d.i.request("scope.default.get"))
    record(d.i.request("job.list"))
    record(d.i.request("status.get"))
    cursor = None
    while True:
        a = record(d.i.request("audit.list", {"cursor": cursor} if cursor else None))
        assert a["status"] == "ok", a
        cursor = a["result"]["next"]
        if cursor is None:
            break
    seen += sweep(d.i, 4.0)
    text = "\n".join(seen)
    assert '"ev"' in text, "events were collected"
    for secret in (KEY_A, KEY_B):
        for form in (secret.hex(), secret.hex().upper(), base64.b64encode(secret).decode(),
                     base64.urlsafe_b64encode(secret).decode(), base64.b64encode(secret).decode().rstrip("=")):
            assert form not in text, "the key left the device as %s" % form[:4]
    assert native(d)[1] == KEY_B, "the key itself is in the prefs, where the native frame 64 reads it"


def scope_through(send, d, name, secret):
    """A signed, confirmed private scope set through `send`, resent once under the same job id, then confirmed."""
    w = wrap(d.node_key, "scope.default.set", {"name": name, "key": secret.hex(), "confirm": {"within_s": 60}}, job=1)
    r = send(w)
    assert r["status"] == "ok" and r["result"]["job_state"] == "awaiting-confirm", r
    assert bare(send(w)) == bare(r), "a resend gets the stored answer and opens no second job"
    c = send(wrap(d.node_key, "job.confirm", {"job": r["result"]["job"]}, job=2))
    assert c["status"] == "ok" and c["result"]["state"] == "confirmed", c
    assert native(d) == (name, secret), "the prefs hold the scope the request carried"
    assert secret.hex() not in json.dumps([r, c]), "no key in an answer"
    assert d.i.request("scope.default.get")["result"] == {"name": name, "kind": "private"}


def test_a_scope_change_over_paired_ble_runs_as_it_does_on_the_cable(nodes):
    n = nodes("companion_standalone", "rsb", seed=105, rtc=RTC).start()
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

    scope_through(over_ble, d, "$ble", KEY_B)
    stale = over_ble(wrap(d.node_key, "scope.default.set", {"name": "$old", "key": KEY_A.hex(), "confirm": {}}, job=1))
    assert stale["status"] == "refused" and stale["error"] == "replay", stale
    assert native(d)[0] == "$ble", "the replay changed nothing"
    b.close()


@pytest.mark.timescale(10)
def test_a_scope_change_over_a_verified_mqtt_session_runs_as_it_does_on_the_cable(nodes, tmp_path, broker):
    b = broker()
    d, dev = mqtt_boot(nodes, tmp_path, "rsm", b)
    assert d.provision()["status"] == "ok"
    scope_through(lambda w: b.iface_request(dev, w), d, "$mqtt", KEY_B)
    stale = b.iface_request(dev, wrap(d.node_key, "scope.default.set", {"name": "$old", "key": KEY_A.hex(), "confirm": {}}, job=1))
    assert stale["status"] == "refused" and stale["error"] == "replay", stale
    assert native(d)[0] == "$mqtt", "the replay changed nothing"
