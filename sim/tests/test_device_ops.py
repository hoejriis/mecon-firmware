"""identity.get, clock.get, advert.send and device.reboot (docs/contract/OPERATIONS.md section 5.1) on the Companion and on
the Repeater and Room server console. identity.get and clock.get are plain reads; advert.send is a write, sent through the
native self-advert path, and seen here on the ether; device.reboot is of the reboot class, which an unsigned local caller
keeps on an enrolled device and an unsigned remote one does not (AUTHORITY.md section 9)."""
import os
import subprocess
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from test_deployment_authority import RTC, broker, mqtt_boot, wrap  # noqa: E402,F401 (broker: the fixture)
from test_lora_management import target  # noqa: E402
from test_provenance import verifies  # noqa: E402
from test_companion_mqtt import brokers, _tools  # noqa: F401,E402  (fixtures)

KINDS = ["companion_standalone", "repeater", "room_server"]
ADVERT = 4


def shaped(op, result):
    import mecon_schema
    problems = mecon_schema.check(result, mecon_schema.load(op + ".result.json"))
    assert not problems, (op, problems, result)
    return result


def next_job(d):
    return d.i.request("deployment.get")["result"]["members"][0]["mark"] + 1


def adverts(ether, name, since):
    """The self adverts `name` put on the air after `since` (a count of its earlier transmissions): (route type, ...)."""
    return [t.data[0] & 0x03 for t in ether.tx_of(name)[since:] if t.data and (t.data[0] >> 2) & 0x0F == ADVERT]


def wait_adverts(ether, name, since, n=1, timeout=5.0):
    """The adverts after `since`, once `n` have come, plus 2 s for any that should not have."""
    end = time.time() + timeout
    while time.time() < end and len(adverts(ether, name, since)) < n:
        time.sleep(0.1)
    time.sleep(2.0)
    return adverts(ether, name, since)


def settled(ether, name):
    """Waits out what a node sends by itself after boot: in the simulator its first transmission comes about 30 s after
    boot (a Repeater's and Room server's own advert among them), and later ones within a second. Then 3 s of quiet."""
    end = time.time() + 60.0
    while time.time() < end and not ether.tx_of(name):
        time.sleep(0.2)
    n, quiet = len(ether.tx_of(name)), time.time()
    while time.time() - quiet < 3.0:
        time.sleep(0.2)
        if len(ether.tx_of(name)) != n:
            n, quiet = len(ether.tx_of(name)), time.time()
    return n


@pytest.mark.parametrize("kind", KINDS)
def test_identity_and_clock_are_plain_reads_that_agree_with_hello(ether, nodes, kind):
    d = target(nodes, ether, kind, "ic" + kind[:2])
    h = d.i.request("hello")["result"]
    caps = h["capabilities"]
    for op in ("identity.get", "clock.get", "advert.send", "device.reboot"):
        assert op in caps or "next" in h, (op, caps)
    r = d.i.request("identity.get")
    assert r["status"] == "ok", r
    shaped("identity.get", r["result"])
    assert r["result"]["device_id"] == h["device_id"] and r["result"]["node_key"] == h["node_key"], r
    assert r["result"]["device_id_sig"] == h["device_id_sig"], "signed as hello signs it"
    t = shaped("clock.get", d.i.request("clock.get")["result"])["epoch"]
    assert isinstance(t, int) and RTC <= t < RTC + 600, t
    assert d.provision()["status"] == "ok"
    # enrolled: both stay plain reads, unsigned, and a signed read needs no job id
    assert d.i.request("identity.get")["result"]["node_key"] == h["node_key"]
    assert d.i.request("clock.get")["status"] == "ok"
    s = d.send(wrap(d.node_key, "clock.get", job=None, rid="cg1"))
    assert s["status"] == "ok" and verifies(s.raw, d.node_key, "result"), s
    s = d.send(wrap(d.node_key, "identity.get", job=None, rid="ig1"))
    assert s["status"] == "ok" and s["result"]["node_key"] == h["node_key"], s


@pytest.mark.parametrize("kind", KINDS)
def test_advert_send_goes_on_the_air_zero_hop_or_flooded(ether, nodes, kind):
    name = "av" + kind[:2]
    d = target(nodes, ether, kind, name)
    d.i.request("advert.send")   # wakes the radio: the first send of a boot waits ~30 s in the simulator
    before = settled(ether, name)
    r = d.i.request("advert.send")
    assert r["status"] == "ok" and shaped("advert.send", r["result"]) == {"flood": False}, r
    assert wait_adverts(ether, name, before) == [2], "zero hop: a direct route with no path"
    before = len(ether.tx_of(name))
    r = d.i.request("advert.send", {"flood": True})
    assert r["status"] == "ok" and r["result"] == {"flood": True}, r
    got = wait_adverts(ether, name, before)
    assert len(got) == 1 and got[0] in (0, 1), "flooded: %s" % got
    assert d.i.request("advert.send", {"flood": "yes"})["status"] == "invalid"
    # enrolled: a write, so unsigned is refused and sends nothing; signed sends
    assert d.provision()["status"] == "ok"
    before = len(ether.tx_of(name))
    r = d.i.request("advert.send", {"flood": True})
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r
    assert wait_adverts(ether, name, before, timeout=3.0) == [], "nothing was sent"
    r = d.send(wrap(d.node_key, "advert.send", {"flood": False}, job=next_job(d)))
    assert r["status"] == "ok", r
    assert wait_adverts(ether, name, before) == [2]


@pytest.mark.parametrize("kind", KINDS)
def test_device_reboot_answers_then_reboots_and_the_unsigned_cable_keeps_it(ether, nodes, kind):
    d = target(nodes, ether, kind, "rb" + kind[:2])
    r = d.i.request("device.reboot")
    assert r["status"] == "ok" and shaped("device.reboot", r["result"]) == {}, r
    assert d.n.wait_exit(10) == 75, "the answer came first, then the reboot"
    d.n.start()
    d.attach()
    assert d.provision()["status"] == "ok"
    r = d.i.request("device.reboot")
    assert r["status"] == "ok", "the unsigned cable keeps a reboot on an enrolled device: %s" % r
    assert d.n.wait_exit(10) == 75
    d.n.start()
    d.attach()
    w = wrap(d.node_key, "device.reboot", job=next_job(d))
    assert d.send(w)["status"] == "ok"
    assert d.n.wait_exit(10) == 75
    d.n.start()
    d.attach()
    r = d.send(w)
    assert r["status"] == "refused" and r["error"] == "replay", "the reboot does not bring the request back: %s" % r
    with pytest.raises(subprocess.TimeoutExpired):
        d.n.wait_exit(3)


def test_an_unsigned_reboot_over_mqtt_is_refused_once_enrolled(nodes, tmp_path, broker):
    b = broker()
    d, dev = mqtt_boot(nodes, tmp_path, "rbm", b)
    assert d.provision()["status"] == "ok"
    r = b.iface_request(dev, {"v": 1, "id": "r1", "op": "device.reboot"})
    assert r["status"] == "refused", r
    with pytest.raises(subprocess.TimeoutExpired):
        d.n.wait_exit(3)
    r = b.iface_request(dev, wrap(d.node_key, "device.reboot", job=next_job(d)))
    assert r["status"] == "ok", "a member's signature may, over the same session: %s" % r
    assert d.n.wait_exit(10) == 75


@pytest.mark.timescale(10)
@pytest.mark.parametrize("kind", ["repeater_wifi", "room_server_wifi"])
def test_an_unsigned_reboot_over_a_servers_broker_is_refused_once_enrolled(nodes, tmp_path, brokers, kind):
    from test_server_mqtt import boot as srv_boot, dev_of, tls_broker, up, verified
    b = tls_broker(brokers)
    s = srv_boot(nodes, tmp_path, kind, "rbs" + kind[:2], verified(b))
    up(s)
    dev = dev_of(s)
    assert s.provision()["status"] == "ok"
    r = b.iface_request(dev, {"v": 1, "id": "r1", "op": "device.reboot"})
    assert r["status"] == "refused", r
    with pytest.raises(subprocess.TimeoutExpired):
        s.n.wait_exit(3)
    r = b.iface_request(dev, wrap(s.node_key, "device.reboot", job=next_job(s)))
    assert r["status"] == "ok", "a member's signature may, over the same session: %s" % r
    assert s.n.wait_exit(10) == 75


def test_a_standalone_companion_may_be_rebooted_by_its_verified_broker(nodes, tmp_path, broker):
    # the reboot class, not the posture class: a verified broker session may reboot a device with no record (the table),
    # though it may not change the posture
    b = broker()
    d, dev = mqtt_boot(nodes, tmp_path, "rbw", b)
    r = b.iface_request(dev, {"v": 1, "id": "r1", "op": "device.reboot"})
    assert r["status"] == "ok", r
    assert d.n.wait_exit(10) == 75


@pytest.mark.timescale(10)
def test_a_standalone_server_may_be_rebooted_by_its_verified_broker(nodes, tmp_path, brokers):
    from test_server_mqtt import boot as srv_boot, dev_of, tls_broker, up, verified
    b = tls_broker(brokers)
    s = srv_boot(nodes, tmp_path, "repeater_wifi", "rbw2", verified(b))
    up(s)
    r = b.iface_request(dev_of(s), {"v": 1, "id": "r1", "op": "device.reboot"})
    assert r["status"] == "ok", r
    assert s.n.wait_exit(10) == 75


def test_region_list_is_a_plain_read_once_enrolled(nodes):
    from test_deployment_authority_server import boot as srv_boot
    s = srv_boot(nodes, "repeater", "rlp")
    assert s.provision()["status"] == "ok"
    r = s.send(wrap(s.node_key, "region.list", job=None, rid="rl1"))
    assert r["status"] == "ok", "a signed region.list needs no job id: %s" % r


@pytest.mark.timescale(10)
def test_the_connectivity_event_times_the_broker_writes(nodes, tmp_path, brokers):
    import mecon_schema
    from test_server_mqtt import boot as srv_boot, tls_broker, up, verified
    b = tls_broker(brokers)
    s = srv_boot(nodes, tmp_path, "repeater_wifi", "wt", verified(b))
    up(s)
    assert s.i.request("sub.start", {"stream": "connectivity", "period_s": 10, "lifetime_s": 60})["status"] == "ok"
    import time as _t
    end, ev = _t.time() + 20, None
    while _t.time() < end and ev is None:
        m = s.i.read_message(0.5)
        if m and m.get("ev") == "connectivity" and m["data"].get("mqtt", {}).get("state") == "connected":
            ev = m["data"]["mqtt"]
    assert ev and isinstance(ev["write_ms_max"], int) and ev["slow_writes"] == 0, ev
    assert not mecon_schema.check(m["data"], mecon_schema.load("event.connectivity.json"))


@pytest.mark.parametrize("kind", KINDS)
def test_clock_set_moves_the_clock_forward_only(ether, nodes, kind):
    d = target(nodes, ether, kind, "cs" + kind[:2])
    now = d.i.request("clock.get")["result"]["epoch"]
    r = d.i.request("clock.set", {"epoch": now + 3600})
    assert r["status"] == "ok" and now + 3600 <= r["result"]["epoch"] < now + 3700, r
    import mecon_schema
    assert not mecon_schema.check(r["result"], mecon_schema.load("clock.set.result.json")), r
    r = d.i.request("clock.set", {"epoch": now})
    assert r["status"] == "failed" and r["error"] == "backwards", "never back: %s" % r
    assert d.i.request("clock.get")["result"]["epoch"] >= now + 3600
    assert d.i.request("clock.set", {"epoch": "soon"})["status"] == "invalid"
    assert d.provision()["status"] == "ok"
    r = d.i.request("clock.set", {"epoch": now + 7200})
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r
    r = d.send(wrap(d.node_key, "clock.set", {"epoch": now + 7200}, job=next_job(d)))
    assert r["status"] == "ok" and r["result"]["epoch"] >= now + 7200, r
