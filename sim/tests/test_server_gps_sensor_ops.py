"""GPS and sensor settings over the device interface on the Repeater and Room server (#493; OPERATIONS.md 5.1):
sensor.list/get/set on every build, gps.* on a build with GPS (ENV_INCLUDE_GPS, the simulated Wi-Fi builds here, as the
Heltec builds), each running its CLI line as the caller. The simulated sensor manager has a "gps" setting on a GPS build
and twelve plain ones, sim.s0 to sim.s11 (variants/sim/target.h)."""
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from test_deployment_authority import wrap  # noqa: E402
from test_device_ops import next_job, shaped  # noqa: E402
from test_lora_management import target  # noqa: E402

GPS_KINDS = ["repeater_wifi", "room_server_wifi"]
GPS_OPS = ("gps.get", "gps.set", "gps.sync", "gps.setloc", "gps.advert.set")


def caps(d):
    out, cursor = [], None
    while True:
        h = d.i.request("hello", {"cursor": cursor} if cursor else None)["result"]
        out += h["capabilities"]
        cursor = h.get("next")
        if not cursor:
            return out


@pytest.mark.parametrize("kind", ["repeater", "room_server"])
def test_sensor_settings_list_in_pages_get_and_set(ether, nodes, kind):
    d = target(nodes, ether, kind, "sn" + kind[:2])
    first = shaped("sensor.list", d.i.request("sensor.list")["result"])
    assert first["total"] == 12 and first["next"] is not None and len(first["settings"]) == first["next"], first
    assert first["settings"][0] == {"name": "sim.s0", "value": "value0"}, first
    rest = shaped("sensor.list", d.i.request("sensor.list", {"cursor": first["next"]})["result"])
    names = [s["name"] for s in first["settings"] + rest["settings"]]
    assert names == ["sim.s%d" % i for i in range(12)] and rest["next"] is None, (names, rest)
    assert d.i.request("sensor.list", {"cursor": 12})["status"] == "invalid", "past the end"
    r = d.i.request("sensor.get", {"name": "sim.s3"})
    assert r["status"] == "ok" and shaped("sensor.get", r["result"]) == {"name": "sim.s3", "value": "value3"}, r
    r = d.i.request("sensor.get", {"name": "nope"})
    assert r["status"] == "failed" and r["error"] == "unknown-setting", r
    r = d.i.request("sensor.set", {"name": "sim.s3", "value": "42"})
    assert r["status"] == "ok" and shaped("sensor.set", r["result"]) == {"name": "sim.s3", "value": "42"}, r
    assert d.cli.command("sensor get sim.s3") == "> 42", "the native setting changed"
    assert d.i.request("sensor.set", {"name": "nope", "value": "1"})["error"] == "unknown-setting"
    r = d.i.request("sensor.set", {"name": "sim.s3", "value": "4 2"})
    assert r["status"] == "invalid" and r["error"] == "bad-value", r
    assert d.provision()["status"] == "ok"
    assert d.i.request("sensor.get", {"name": "sim.s3"})["status"] == "ok", "a read, kept unsigned once enrolled"
    r = d.i.request("sensor.set", {"name": "sim.s3", "value": "7"})
    assert r["status"] == "refused", "a write: %s" % r
    r = d.send(wrap(d.node_key, "sensor.set", {"name": "sim.s3", "value": "7"}, job=next_job(d)))
    assert r["status"] == "ok", r


def test_a_build_without_gps_offers_none_of_it(ether, nodes):
    d = target(nodes, ether, "repeater", "nogps")
    offered = caps(d)
    assert not [op for op in GPS_OPS if op in offered], offered
    assert d.i.request("gps.get")["status"] == "unknown-op"
    assert "sensor.list" in offered


@pytest.mark.parametrize("kind", GPS_KINDS)
def test_gps_on_a_board_without_a_receiver(ether, nodes, kind):
    d = target(nodes, ether, kind, "gn" + kind[:2] + kind[-4:])
    assert all(op in caps(d) for op in GPS_OPS)
    res = shaped("gps.get", d.i.request("gps.get")["result"])
    assert res["receiver"] is False and res["powered"] is None and res["fix"] is None and res["diag"] is None, res
    assert res["advert"] in ("none", "share", "prefs"), "the policy is a setting, receiver or not: %s" % res
    r = d.i.request("gps.sync")
    assert r["status"] == "failed" and r["error"] == "no-gps", r
    assert d.i.request("gps.set", {"on": True})["status"] == "ok", "the toggle is a setting, receiver or not, as in the CLI"
    assert d.i.request("gps.setloc")["status"] == "ok", "as the CLI's: it saves the sensor manager's position, whatever it is"


@pytest.mark.parametrize("kind", GPS_KINDS)
def test_gps_with_a_fix_switches_shares_and_saves(ether, nodes, kind):
    d = target(nodes, ether, kind, "gf" + kind[:2] + kind[-4:])
    with open(d.n.dir + ".gps", "w") as f:
        f.write("fix 55676100 12568300 15000 9 2\n")
    res = shaped("gps.get", d.i.request("gps.get")["result"])
    assert res["receiver"] is True and res["powered"] is True and res["fix"] is True and res["sats"] == 9, res
    assert res["active"] is False and res["diag"].startswith("req:0"), res
    r = d.i.request("gps.set", {"on": True})
    assert r["status"] == "ok" and shaped("gps.set", r["result"]) == {"on": True}, r
    res = d.i.request("gps.get")["result"]
    assert res["active"] is True and res["diag"].startswith("req:1"), res
    assert d.cli.command("sensor get gps") == "> 1"
    with open(d.n.dir + ".gps", "w") as f:
        f.write("nofix 3\n")
    res = d.i.request("gps.get")["result"]
    assert res["fix"] is False and res["sats"] == 3 and res["powered"] is True, res
    with open(d.n.dir + ".gps", "w") as f:
        f.write("fix 55676100 12568300 15000 9 2\n")
    for p in ("share", "prefs", "none"):
        r = d.i.request("gps.advert.set", {"policy": p})
        assert r["status"] == "ok" and r["result"] == {"policy": p}, r
        assert d.i.request("gps.get")["result"]["advert"] == p
    assert d.i.request("gps.advert.set", {"policy": "all"})["status"] == "invalid"
    assert d.i.request("gps.sync")["status"] == "ok"
    r = d.i.request("gps.setloc")
    assert r["status"] == "ok", r
    shaped("gps.setloc", r["result"])
    assert r["result"] == {"lat": d.cli.command("get lat")[2:], "lon": d.cli.command("get lon")[2:]}, r
    assert d.i.request("gps.set", {"on": False})["status"] == "ok"
    assert d.i.request("gps.get")["result"]["active"] is False
    assert d.provision()["status"] == "ok"
    res = d.i.request("gps.get")["result"]
    assert res["fix"] is True, "a read, kept unsigned once enrolled: %s" % res
    assert res["diag"] is None and res["advert"] is None, "lines of the write class, which the CLI gate refuses it: %s" % res
    for op, args in (("gps.set", {"on": True}), ("gps.sync", None), ("gps.setloc", None), ("gps.advert.set", {"policy": "share"})):
        r = d.i.request(op, args)
        assert r["status"] == "refused", (op, r)
    r = d.send(wrap(d.node_key, "gps.set", {"on": True}, job=next_job(d)))
    assert r["status"] == "ok", r
