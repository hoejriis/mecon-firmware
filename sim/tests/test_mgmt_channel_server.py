"""The management channel on the simulated Repeater and Room server (docs/contract/MANAGEMENT_CHANNEL.md section 6): one
channel slot, the same channel.* and mgmt.channel.* operations as a Companion, the same confirm-or-revert for anyone but
the cable, the key never read back, and native repeating left as it was."""
import os
import sys
import time

import pytest

from sim.ether import Ether  # noqa: F401
from sim.harness import CompanionClient

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from test_deployment_authority import wrap  # noqa: E402
from test_deployment_authority_server import RTC, boot  # noqa: E402

KINDS = ["repeater", "room_server"]
KEY = bytes(range(0x10, 0x20))
NEW = bytes(range(0x50, 0x60))
PUBLIC = bytes.fromhex("8b3387e9c5cdea6ac9e5edbaa115cd72")


def put(d, name, key, slot=0):
    return d.i.request("channel.put", {"slot": slot, "name": name, "secret": key.hex()})


def state(d):
    return d.i.request("mgmt.channel.get")["result"]


def wait_job(d, job, secs=30):
    end = time.time() + secs
    st = None
    while time.time() < end:
        st = d.i.request("job.get", {"job": job})["result"]["state"]
        if st != "awaiting-confirm":
            break
        time.sleep(0.5)
    return st


def enrolled(nodes, kind, name, seed):
    """Bound over the cable, then enrolled: from here a change is signed, and a signed caller is not the cable."""
    d = boot(nodes, kind, name, seed)
    assert put(d, "ops", KEY)["status"] == "ok"
    assert d.i.request("mgmt.channel.bind", {"slot": 0})["status"] == "ok"
    assert d.provision()["status"] == "ok"
    return d


@pytest.mark.parametrize("kind", KINDS)
def test_the_cable_binds_the_one_slot_and_it_survives_a_reboot(nodes, kind):
    d = boot(nodes, kind, "mb" + kind[:2], 81)
    caps, cursor = [], None
    while True:
        h = d.i.request("hello", {} if cursor is None else {"cursor": cursor})["result"]
        caps += h["capabilities"]
        cursor = h.get("next")
        if cursor is None:
            break
    assert {"channel.put", "mgmt.channel.get", "mgmt.channel.bind", "mgmt.channel.rotate", "mgmt.channel.unbind"} <= set(caps)
    assert state(d) == {"state": "none"}
    r = d.i.request("mgmt.channel.bind", {"slot": 0})
    assert r["status"] == "failed", "an empty slot cannot be bound: %s" % r
    r = put(d, "ops", KEY)
    assert r["status"] == "ok", r
    assert put(d, "two", NEW, slot=1)["status"] != "ok", "there is one slot"
    r = d.i.request("mgmt.channel.bind", {"slot": 0})
    assert r["status"] == "ok" and r["result"]["state"] == "bound" and r["result"]["generation"] == 1, r
    assert "job" not in r["result"], "the cable needs no confirmation"
    d.reboot()
    s = state(d)
    assert s["state"] == "bound" and s["slot"] == 0 and s["name"] == "ops" and s["key_bits"] == 128, s
    l = d.i.request("channel.list")["result"]
    assert [c["name"] for c in l["channels"]] == ["ops"], l
    for r in (s, l, d.i.request("channel.get", {"slot": 0})["result"]):
        assert KEY.hex() not in str(r), "never the key"
    assert KEY in open(os.path.join(d.n.dir, "mgmt_slot"), "rb").read(), "it is on flash, as a native channel's key is"


@pytest.mark.parametrize("kind", KINDS)
def test_the_public_channel_cannot_be_bound(nodes, kind):
    d = boot(nodes, kind, "mp" + kind[:2], 82)
    assert put(d, "public", PUBLIC)["status"] == "ok"
    r = d.i.request("mgmt.channel.bind", {"slot": 0})
    assert r["status"] == "failed" and r["error"] == "public-channel", r
    assert state(d) == {"state": "none"}


@pytest.mark.parametrize("kind", KINDS)
def test_rotate_and_unbind_with_readback(nodes, kind):
    d = boot(nodes, kind, "mr" + kind[:2], 83)
    assert put(d, "ops", KEY)["status"] == "ok"
    h0 = state(d) and d.i.request("channel.get", {"slot": 0})["result"]["hash"]
    assert d.i.request("mgmt.channel.bind", {"slot": 0})["status"] == "ok"
    r = d.i.request("mgmt.channel.rotate", {"secret": NEW.hex()})
    assert r["status"] == "ok" and r["result"]["generation"] == 2 and r["result"]["name"] == "ops", r
    assert r["result"]["hash"] != h0, "the slot holds the new key"
    d.reboot()
    assert state(d)["generation"] == 2
    assert NEW in open(os.path.join(d.n.dir, "mgmt_slot"), "rb").read()
    r = d.i.request("mgmt.channel.unbind")
    assert r["result"] == {"state": "none", "removed": False}, r
    assert d.i.request("channel.get", {"slot": 0})["result"]["name"] == "ops", "the channel stays unless asked"
    assert d.i.request("mgmt.channel.bind", {"slot": 0})["status"] == "ok"
    r = d.i.request("mgmt.channel.unbind", {"remove": True})
    assert r["result"] == {"state": "none", "removed": True}, r
    assert not os.path.exists(os.path.join(d.n.dir, "mgmt_slot")), "the key has left the flash"


@pytest.mark.parametrize("kind", KINDS)
def test_a_damaged_slot_makes_the_binding_stale_and_nothing_moves(nodes, kind):
    d = boot(nodes, kind, "md" + kind[:2], 84)
    assert put(d, "ops", KEY)["status"] == "ok"
    assert d.i.request("mgmt.channel.bind", {"slot": 0})["status"] == "ok"
    p = os.path.join(d.n.dir, "mgmt_slot")
    d.n.stop()
    b = bytearray(open(p, "rb").read())
    b[10] ^= 0xFF
    open(p, "wb").write(bytes(b))
    d.n.start()
    d.attach()
    assert state(d)["state"] == "stale"
    r = d.i.request("mgmt.channel.rotate", {"secret": NEW.hex()})
    assert r["status"] == "failed" and r["error"] == "stale", r


@pytest.mark.timescale(10)
@pytest.mark.parametrize("kind", KINDS)
def test_a_signed_change_must_confirm_and_reverts_unconfirmed(nodes, kind):
    d = enrolled(nodes, kind, "ms" + kind[:2], 85)
    key = d.node_key
    r = d.i.request("mgmt.channel.rotate", {"secret": NEW.hex()})
    assert r["status"] == "refused" and r["error"] == "unauthenticated", "once enrolled the cable alone does not change it: %s" % r
    r = d.send(wrap(key, "mgmt.channel.rotate", {"secret": NEW.hex()}, job=1))
    assert r["status"] == "invalid" and r["error"] == "confirm-required", r
    r = d.send(wrap(key, "mgmt.channel.rotate", {"secret": NEW.hex(), "confirm": {"within_s": 30}}, job=2))
    assert r["status"] == "ok" and r["result"]["job_state"] == "awaiting-confirm", r
    assert NEW in open(os.path.join(d.n.dir, "mgmt_slot"), "rb").read(), "in effect at once, tentatively"
    reads = d.i.request("status.get")["result"]["lora"]["binding_reads"]
    assert wait_job(d, r["result"]["job"]) == "reverted"
    assert KEY in open(os.path.join(d.n.dir, "mgmt_slot"), "rb").read(), "the old key is back"
    time.sleep(0.2)
    assert d.i.request("status.get")["result"]["lora"]["binding_reads"] > reads, "no request made this change, and the link must still see it"
    s = state(d)
    assert s["state"] == "bound" and s["generation"] == 1, s


@pytest.mark.timescale(10)
@pytest.mark.parametrize("kind", KINDS)
def test_a_confirmed_rotation_stays_and_a_reboot_while_pending_reverts(nodes, kind):
    d = enrolled(nodes, kind, "mc" + kind[:2], 86)
    key = d.node_key
    r = d.send(wrap(key, "mgmt.channel.rotate", {"secret": NEW.hex(), "confirm": {"within_s": 60}}, job=1))
    assert r["status"] == "ok" and r["result"]["generation"] == 2, r
    c = d.send(wrap(key, "job.confirm", {"job": r["result"]["job"]}, job=2))
    assert c["status"] == "ok" and c["result"]["state"] == "confirmed", c
    assert not os.path.exists(os.path.join(d.n.dir, "mgmt_snap")), "the snapshot held the old key, and it has gone"
    other = bytes(range(0x30, 0x40))
    r = d.send(wrap(key, "mgmt.channel.rotate", {"secret": other.hex(), "confirm": {}}, job=3))
    assert r["status"] == "ok", r
    job = r["result"]["job"]
    d.n.restart()   # the change took effect in the boot before: this is the second boot while it is pending
    d.attach()
    assert NEW in open(os.path.join(d.n.dir, "mgmt_slot"), "rb").read(), "the unconfirmed rotation was reverted at boot"
    s = state(d)
    assert s["state"] == "bound" and s["generation"] == 2, s
    assert d.i.request("job.get", {"job": job})["result"]["state"] == "reverted"


@pytest.mark.timescale(10)
@pytest.mark.parametrize("kind", KINDS)
def test_a_put_of_the_bound_slot_must_confirm(nodes, kind):
    d = enrolled(nodes, kind, "mg" + kind[:2], 90)
    key = d.node_key
    args = {"slot": 0, "name": "ops", "secret": NEW.hex()}
    r = d.send(wrap(key, "channel.put", args, job=1))
    assert r["status"] == "invalid" and r["error"] == "confirm-required", r
    assert KEY in open(os.path.join(d.n.dir, "mgmt_slot"), "rb").read()
    r = d.send(wrap(key, "channel.put", dict(args, confirm={"within_s": 30}), job=2))
    assert r["status"] == "ok" and r["result"]["job_state"] == "awaiting-confirm", r
    assert wait_job(d, r["result"]["job"]) == "reverted"
    assert KEY in open(os.path.join(d.n.dir, "mgmt_slot"), "rb").read()


def test_repeating_goes_on_with_a_bound_channel(nodes, ether):
    """A and B hear only the repeater: the message arrives only if it forwards, bound or not."""
    r = boot(nodes, "repeater", "mfr", 87, ether_path=ether.path)
    assert put(r, "ops", KEY)["status"] == "ok"
    assert r.i.request("mgmt.channel.bind", {"slot": 0})["status"] == "ok"
    na = nodes("companion_standalone", "mfa", ether_path=ether.path, seed=88, rtc=RTC).start()
    nb = nodes("companion_standalone", "mfb", ether_path=ether.path, seed=89, rtc=RTC).start()
    a, b = CompanionClient(na), CompanionClient(nb)
    for c in (a, b):
        c.device_query()
        c.app_start()
    assert ether.wait_for_nodes(["mfr", "mfa", "mfb"])
    ether.set_link("mfa", "mfr")
    ether.set_link("mfb", "mfr")
    assert a.add_contact(b.pub_key, "b")[0] == 0
    assert b.add_contact(a.pub_key, "a")[0] == 0
    a.send_direct(b.pub_key, "through the repeater", RTC + 5)
    got = b.sync_messages(timeout=75, want=1)
    assert [m["text"] for m in got] == ["through the repeater"], got
    assert [t for t in ether.tx_log if t.src == "mfr"], "the repeater carried it"
