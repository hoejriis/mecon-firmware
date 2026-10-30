"""The management channel binding on a simulated standalone Companion (docs/contract/MANAGEMENT_CHANNEL.md): bound over
the cable, kept over a reboot, stale after a native change; from a paired phone, which is not the cable, every change
asks to confirm and reverts unless confirmed."""
import itertools
import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402
from sim import harness  # noqa: E402
from test_device_interface import PIN, ble_boot, start as companion_start, client as usb_client  # noqa: E402

# The device keeps a state-changing request's result by its id, so every client here numbers its requests apart: a
# reconnected client starting again at r-1 would be answered from another request's stored result.
_clients = itertools.count(1)


def companion_client(n):
    c, i = usb_client(n)
    i.id_prefix = "u%d" % next(_clients)
    return c, i

KEY = bytes(range(0x10, 0x20))
NEW = bytes(range(0x50, 0x60))


def ble(n):
    b = n.connect_ble(PIN)
    b.app_start()
    return b, mecon_iface.IfaceClient(b._send, b._read_frame, id_prefix="b%d" % next(_clients))


def put(i, slot, name, key, **extra):
    return i.request("channel.put", dict({"slot": slot, "name": name, "secret": key.hex()}, **extra))


def state(i):
    return i.request("mgmt.channel.get")["result"]


def wait_job(i, job, secs=30):
    end = time.time() + secs
    st = None
    while time.time() < end:
        st = i.request("job.get", {"job": job})["result"]["state"]
        if st != "awaiting-confirm":
            break
        time.sleep(0.5)
    return st


def test_the_cable_binds_a_channel_it_survives_a_reboot_and_a_native_change_makes_it_stale(nodes):
    n, (c, i) = companion_start(nodes, "mg1")
    i.id_prefix = "u%d" % next(_clients)
    caps = i.request("hello")["result"]["capabilities"]
    assert {"mgmt.channel.get", "mgmt.channel.bind", "mgmt.channel.rotate", "mgmt.channel.unbind"} <= set(caps), caps
    assert state(i) == {"state": "none"}
    assert put(i, 2, "ops", KEY)["status"] == "ok"
    r = i.request("mgmt.channel.bind", {"slot": 2})
    assert r["status"] == "ok" and r["result"]["state"] == "bound" and r["result"]["generation"] == 1, r
    assert KEY.hex() not in str(r), "never the key"
    assert "job" not in r["result"], "the cable needs no confirmation"
    c.close()
    n.restart()
    c, i = companion_client(n)
    s = state(i)
    assert s["state"] == "bound" and s["slot"] == 2 and s["name"] == "ops", s
    assert c.set_channel(2, "ops", NEW)[0] == harness.RESP_OK   # the app's own frame, behind the binding's back
    assert state(i)["state"] == "stale"
    r = i.request("mgmt.channel.rotate", {"secret": bytes(range(0x70, 0x80)).hex()})
    assert r["status"] == "failed" and r["error"] == "stale", r
    assert c.get_channel(2) == ("ops", NEW), "a stale binding is never moved to a new key"


def test_the_public_channel_cannot_be_bound(nodes):
    n, (c, i) = companion_start(nodes, "mg2")
    i.id_prefix = "u%d" % next(_clients)
    r = i.request("mgmt.channel.bind", {"slot": 0})   # slot 0 is the pre-configured public channel
    assert r["status"] == "failed" and r["error"] == "public-channel", r
    assert state(i) == {"state": "none"}


@pytest.mark.timescale(10)
def test_a_paired_phone_must_confirm_and_an_unconfirmed_bind_reverts_at_its_deadline(nodes):
    n = ble_boot(nodes, "mg3", PIN)
    u, ui = companion_client(n)
    assert put(ui, 2, "ops", KEY)["status"] == "ok"
    u.close()
    b, i = ble(n)
    r = i.request("mgmt.channel.bind", {"slot": 2})
    assert r["status"] == "invalid" and r["error"] == "confirm-required", r
    assert state(i) == {"state": "none"}
    r = i.request("mgmt.channel.bind", {"slot": 2, "confirm": {"within_s": 30}})
    assert r["status"] == "ok" and r["result"]["job_state"] == "awaiting-confirm", r
    assert state(i)["state"] == "bound", "in effect at once, tentatively"
    assert wait_job(i, r["result"]["job"]) == "reverted"
    assert state(i) == {"state": "none"}, "the previous binding (none) is back"


@pytest.mark.timescale(10)
def test_a_confirmed_rotation_stays_and_a_reboot_while_pending_reverts_it(nodes):
    n = ble_boot(nodes, "mg4", PIN)
    u, ui = companion_client(n)
    assert put(ui, 2, "ops", KEY)["status"] == "ok"
    assert ui.request("mgmt.channel.bind", {"slot": 2})["status"] == "ok"
    u.close()
    b, i = ble(n)
    r = i.request("mgmt.channel.rotate", {"secret": NEW.hex(), "confirm": {"within_s": 60}})
    assert r["status"] == "ok" and r["result"]["generation"] == 2, r
    c = i.request("job.confirm", {"job": r["result"]["job"]})
    assert c["status"] == "ok" and c["result"]["state"] == "confirmed", c
    b.close()
    u, ui = companion_client(n)
    assert u.get_channel(2) == ("ops", NEW)
    u.close()
    b, i = ble(n)
    other = bytes(range(0x30, 0x40))
    job = i.request("mgmt.channel.rotate", {"secret": other.hex(), "confirm": {}})["result"]["job"]
    b.close()
    n.restart()   # the change took effect in the boot before: this is the second boot while it is pending
    u, ui = companion_client(n)
    assert u.get_channel(2) == ("ops", NEW), "the unconfirmed rotation was reverted at boot"
    s = state(ui)
    assert s["state"] == "bound" and s["generation"] == 2, s
    assert ui.request("job.get", {"job": job})["result"]["state"] == "reverted"


@pytest.mark.timescale(10)
def test_a_phone_changing_the_bound_slot_through_channel_put_must_confirm(nodes):
    n = ble_boot(nodes, "mg5", PIN)
    u, ui = companion_client(n)
    assert put(ui, 2, "ops", KEY)["status"] == "ok"
    assert ui.request("mgmt.channel.bind", {"slot": 2})["status"] == "ok"
    u.close()
    b, i = ble(n)
    r = put(i, 2, "ops", NEW)
    assert r["status"] == "invalid" and r["error"] == "confirm-required", r
    r = put(i, 2, "ops", NEW, confirm={"within_s": 30})
    assert r["status"] == "ok" and r["result"]["job_state"] == "awaiting-confirm", r
    assert i.request("job.get", {"job": r["result"]["job"]})["result"]["op"] == "channel.put"
    assert wait_job(i, r["result"]["job"]) == "reverted"
    b.close()
    u, ui = companion_client(n)
    assert u.get_channel(2) == ("ops", KEY), "the old key is back"
    assert state(ui)["state"] == "bound"


@pytest.mark.timescale(10)
def test_a_revert_never_overwrites_an_app_change_made_while_it_waited(nodes):
    n = ble_boot(nodes, "mg6", PIN)
    u, ui = companion_client(n)
    assert put(ui, 2, "ops", KEY)["status"] == "ok"
    assert ui.request("mgmt.channel.bind", {"slot": 2})["status"] == "ok"
    u.close()
    b, i = ble(n)
    r = i.request("mgmt.channel.rotate", {"secret": NEW.hex(), "confirm": {"within_s": 30}})
    assert r["status"] == "ok" and r["result"]["job_state"] == "awaiting-confirm", r
    job = r["result"]["job"]
    b.close()
    u, ui = companion_client(n)
    app = bytes(range(0x70, 0x80))
    assert u.set_channel(2, "app", app)[0] == harness.RESP_OK   # the app's own change, inside the window
    assert wait_job(ui, job) == "failed"
    assert u.get_channel(2) == ("app", app), "the app's key stays"
    assert state(ui)["state"] == "stale"
