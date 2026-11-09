"""device.reset {"scope"} (docs/contract/OPERATIONS.md section 3.5) on the Companion and on the Repeater and Room server
console. Each scope removes exactly what it names: what it keeps is read back after the reboot that follows. Its class is
the destructive one, which the unsigned cable keeps on an enrolled device and nothing signed or brokered ever gets."""
import os
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from test_deployment_authority import RTC, Dev, broker, mqtt_boot, wrap  # noqa: E402,F401 (broker: the fixture)
from test_deployment_authority_server import Srv  # noqa: E402
from test_device_ops import next_job, shaped  # noqa: E402

KINDS = ["companion_standalone", "repeater", "room_server"]
CHAN_KEY = bytes(range(16))


def boot(nodes, kind, name, seed=51):
    n = nodes(kind, name, seed=seed, rtc=RTC).start()
    return Dev(n) if kind.startswith("companion") else Srv(n)


def line(d):
    return d.c.cli if isinstance(d, Dev) else d.cli.command


def reset(d, scope):
    """device.reset on the cable; after an ok, the reboot it promised, and the link again."""
    r = d.i.request("device.reset", {"scope": scope})
    if r["status"] == "ok":
        assert shaped("device.reset", r["result"]) == {"scope": scope, "reboot": True}, r
        assert d.n.wait_exit(10) == 75, "the answer came first, then the reboot"
        d.n.start()
        d.attach()
    return r


def channels(d):
    return {c["slot"]: c["name"] for c in d.i.request("channel.list")["result"]["channels"]}


@pytest.mark.parametrize("kind", KINDS)
def test_radio_goes_back_to_the_builds_default_and_nothing_else(nodes, kind):
    default = line(boot(nodes, kind, "rd0" + kind[:2], seed=50))("get radio")
    d = boot(nodes, kind, "rd" + kind[:2])
    assert line(d)("set radio 869.525,250,7,5").startswith("OK")
    assert line(d)("set name keepme").startswith("OK")
    d.reboot()
    changed, key = line(d)("get radio"), d.node_key
    assert changed != default, (changed, default)
    assert reset(d, "radio")["status"] == "ok"
    assert line(d)("get radio") == default
    assert "keepme" in d.name() and d.node_key == key, "the name and the identity stay"


@pytest.mark.parametrize("kind", ["companion_standalone", "repeater_wifi"])   # the builds with Wi-Fi and a broker
def test_network_removes_every_stored_profile_and_keeps_the_rest(nodes, kind):
    d = boot(nodes, kind, "rn" + kind[:2])
    keys = {"wifi.profile.1.ssid": "home", "mqtt.profile.1.host": "broker.example"}
    r = d.i.request("config.apply", {"set": dict(keys, name="keepnet", **{"mqtt.profile.1.type": "static"})})
    assert r["status"] == "ok", r
    before = d.i.request("config.get", {"keys": list(keys)})["result"]["values"]
    assert all(before[k] for k in keys), "set, so that empty afterwards means removed: %s" % before
    key = d.node_key
    assert reset(d, "network")["status"] == "ok"
    got = d.i.request("config.get", {"keys": list(keys)})["result"]["values"]
    assert all(not got[k] for k in keys), got
    assert "keepnet" in d.name() and d.node_key == key


def test_contacts_removes_the_table_and_keeps_the_channels(nodes):
    d = boot(nodes, "companion_standalone", "rct")
    assert d.c.add_contact(bytes([7] * 32), "someone")[0] == 0
    assert d.i.request("channel.put", {"slot": 2, "name": "ops", "secret": CHAN_KEY.hex()})["status"] == "ok"
    assert d.i.request("device.reboot")["status"] == "ok"   # it saves the contact, so its absence later is the reset's
    assert d.n.wait_exit(10) == 75
    d.n.start()
    d.attach()
    assert d.i.request("contact.list")["result"]["count"] == 1
    key = d.node_key
    assert reset(d, "contacts")["status"] == "ok"
    assert d.i.request("contact.list")["result"]["count"] == 0
    assert channels(d).get(2) == "ops" and d.node_key == key, "the channels and the identity stay"


def test_channels_keeps_only_the_public_one_and_the_contacts(nodes):
    d = boot(nodes, "companion_standalone", "rch")
    assert d.c.add_contact(bytes([7] * 32), "someone")[0] == 0
    assert d.i.request("channel.put", {"slot": 0, "name": "notpublic", "secret": CHAN_KEY.hex()})["status"] == "ok"
    assert d.i.request("channel.put", {"slot": 2, "name": "ops", "secret": bytes(range(16, 32)).hex()})["status"] == "ok"
    assert reset(d, "channels")["status"] == "ok"
    assert channels(d) == {0: "Public"}, channels(d)
    assert d.i.request("contact.list")["result"]["count"] == 1, "the contacts stay"


@pytest.mark.parametrize("kind", ["companion_standalone", "repeater"])
def test_factory_is_the_native_reset(nodes, kind):
    # The simulator derives a new identity from the node's seed, so the key it makes after the erase is the same one:
    # what the erase removed is read from the settings instead.
    default = line(boot(nodes, kind, "rf0" + kind[:2], seed=50))("get radio")
    d = boot(nodes, kind, "rf" + kind[:2])
    assert line(d)("set name gone").startswith("OK")
    assert line(d)("set radio 869.525,250,7,5").startswith("OK")
    if isinstance(d, Dev):
        assert d.c.add_contact(bytes([7] * 32), "someone")[0] == 0
    assert reset(d, "factory")["status"] == "ok"
    assert "gone" not in d.name() and line(d)("get radio") == default, "nothing stays"
    if isinstance(d, Dev):
        assert d.i.request("contact.list")["result"]["count"] == 0


@pytest.mark.parametrize("kind", KINDS)
def test_scopes_that_are_not_this_operations_say_where_to_go(nodes, kind):
    d = boot(nodes, kind, "rs" + kind[:2])
    for scope, status, err in (("deployment", "invalid", "use-deployment.reset"), ("identity", "invalid", "use-identity.rotate"),
                               ("everything", "invalid", "bad-scope")):
        r = d.i.request("device.reset", {"scope": scope})
        assert r["status"] == status and r["error"] == err, (scope, r)
    assert d.i.request("device.reset")["error"] == "bad-scope"
    if not isinstance(d, Dev):
        for scope in ("contacts", "channels", "bonds"):
            r = d.i.request("device.reset", {"scope": scope})
            assert r["status"] == "failed" and r["error"] == "not-on-this-device", (scope, r)
    with pytest.raises(subprocess.TimeoutExpired):
        d.n.wait_exit(2)


@pytest.mark.parametrize("kind", KINDS)
def test_enrolled_the_cable_keeps_it_and_a_signature_never_gets_it(nodes, kind):
    d = boot(nodes, kind, "re" + kind[:2])
    assert d.provision()["status"] == "ok"
    r = d.send(wrap(d.node_key, "device.reset", {"scope": "radio"}, job=next_job(d)))
    assert r["status"] == "refused", "no remote erase, whatever the signature: %s" % r
    with pytest.raises(subprocess.TimeoutExpired):
        d.n.wait_exit(2)
    assert reset(d, "radio")["status"] == "ok", "the unsigned cable keeps it"


def test_a_broker_session_never_gets_it(nodes, tmp_path, broker):
    b = broker()
    d, dev = mqtt_boot(nodes, tmp_path, "rbr", b)
    r = b.iface_request(dev, {"v": 1, "id": "r1", "op": "device.reset", "args": {"scope": "radio"}})
    assert r["status"] == "refused", r
    with pytest.raises(subprocess.TimeoutExpired):
        d.n.wait_exit(3)


def test_a_contact_added_just_before_a_reboot_is_kept(nodes):
    # The native reboot saves a contact change still waiting for its lazy save; the interface's reboots do too.
    d = boot(nodes, "companion_standalone", "rbk")
    assert d.c.add_contact(bytes([7] * 32), "someone")[0] == 0
    assert d.i.request("device.reboot")["status"] == "ok"
    assert d.n.wait_exit(10) == 75
    d.n.start()
    d.attach()
    assert d.i.request("contact.list")["result"]["count"] == 1


def test_paired_ble_may_on_a_standalone_device_and_not_once_enrolled(nodes):
    from test_jobs import PIN, ble, ble_boot
    n = ble_boot(nodes, "rbl", PIN)
    b, i = ble(n)
    r = i.request("device.reset", {"scope": "radio"})
    assert r["status"] == "ok", "the table's paired row, with no record: %s" % r
    assert n.wait_exit(10) == 75
    n.start()
    d = Dev(n)
    assert d.provision()["status"] == "ok"
    b, i = ble(n)
    r = i.request("device.reset", {"scope": "radio"})
    assert r["status"] == "refused", "enrolled, only the cable: %s" % r
    with pytest.raises(subprocess.TimeoutExpired):
        n.wait_exit(2)


def bonds(i):
    r = i.request("ble.bonds")
    assert r["status"] == "ok", r
    return shaped("ble.bonds", r["result"])


def test_bonds_are_counted_and_cleared_from_the_cable_only_at_the_next_ble_start(nodes):
    from test_jobs import PIN, ble, ble_boot
    n = ble_boot(nodes, "rbo", PIN)
    d = Dev(n)
    assert bonds(d.i) == {"count": 0, "clear_pending": False}
    b, i = ble(n)   # a pairing makes a bond
    assert bonds(d.i)["count"] == 1
    r = i.request("device.reset", {"scope": "bonds"})
    assert r["status"] == "refused" and r["error"] == "usb-only", "not over BLE, though the class allows it here: %s" % r
    b.close()
    assert reset(d, "bonds")["status"] == "ok"
    assert bonds(d.i) == {"count": 0, "clear_pending": False}, "cleared at the BLE start, and checked"


def test_a_bond_clear_waits_for_a_ble_start(nodes):
    from test_jobs import PIN, ble, ble_boot
    n = ble_boot(nodes, "rbw", PIN)
    d = Dev(n)
    b, i = ble(n)
    b.close()
    assert bonds(d.i)["count"] == 1
    assert d.c.cli("set connectivity.mode wifi").startswith("OK")   # Wi-Fi mode: the BLE stack never starts
    assert n.wait_exit(10) == 75
    n.start()
    d.attach()
    assert reset(d, "bonds")["status"] == "ok"
    assert bonds(d.i) == {"count": None, "clear_pending": True}, "kept until a BLE start"
    assert d.c.cli("set connectivity.mode ble").startswith("OK")
    assert n.wait_exit(10) == 75
    n.start()
    d.attach()
    assert bonds(d.i) == {"count": 0, "clear_pending": False}
