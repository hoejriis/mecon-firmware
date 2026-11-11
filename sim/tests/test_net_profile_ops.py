"""net.profile.clear and mqtt.profile.anchor (#490; NetProfileOps.h): the network-profile lifecycle over the interface, run
as the caller's own CLI lines. A caller other than the cable may not clear or re-anchor the profile its session runs on."""
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import mqtt_broker  # noqa: E402
from test_deployment_authority import broker, mqtt_boot, wrap  # noqa: E402,F401
from test_device_interface import RTC, client  # noqa: E402
from test_device_ops import next_job  # noqa: E402


def standalone(nodes, name):
    n = nodes("companion_standalone", name, seed=11, rtc=RTC).start()
    return client(n)


def test_the_cable_clears_a_profile_of_either_kind(nodes):
    c, i = standalone(nodes, "npc")
    assert c.cli("set wifi.profile 2 ssid lab").startswith(">")
    for cmd in ("set mqtt.profile 3 type static", "set mqtt.profile 3 host broker.test"):
        assert c.cli(cmd).startswith(">"), cmd
    r = i.request("net.profile.clear", {"kind": "wifi", "profile": 2})
    assert r["status"] == "ok" and r["result"] == {"kind": "wifi", "profile": 2}, r
    assert "lab" not in c.cli("get wifi.profile 2"), "the profile is gone"
    r = i.request("net.profile.clear", {"kind": "mqtt", "profile": 3})
    assert r["status"] == "ok", r
    assert "broker.test" not in c.cli("get mqtt.profile 3")
    for bad in ({"kind": "wifi", "profile": 10}, {"kind": "mqtt", "profile": 6}, {"kind": "tcp", "profile": 0},
                {"kind": "wifi"}, {"profile": 1}):
        assert i.request("net.profile.clear", bad)["status"] == "invalid", bad


def test_the_cable_sets_and_clears_both_anchors(nodes, tmp_path):
    mqtt_broker.require_tools()
    c, i = standalone(nodes, "npa")
    for cmd in ("set mqtt.profile 1 type static", "set mqtt.profile 1 host broker.test", "set mqtt.profile 1 tls on"):
        assert c.cli(cmd).startswith(">"), cmd
    fp = "ab" * 32
    r = i.request("mqtt.profile.anchor", {"profile": 1, "fingerprint": fp})
    assert r["status"] == "ok" and r["result"] == {"profile": 1, "anchor": "fingerprint"}, r
    assert "anchor=" in c.cli("get mqtt.profile 1") and "anchor=unset" not in c.cli("get mqtt.profile 1")
    cert, _ = mqtt_broker.make_ca(str(tmp_path), "npca")
    b64 = mqtt_broker.ca_b64(cert)
    r = i.request("mqtt.profile.anchor", {"profile": 1, "ca": b64}, timeout=15)
    assert r["status"] == "ok" and r["result"]["anchor"] == "ca", r
    assert "anchor=ca" in c.cli("get mqtt.profile 1"), c.cli("get mqtt.profile 1")
    damaged = b64[:len(b64) // 2]
    r = i.request("mqtt.profile.anchor", {"profile": 1, "ca": damaged}, timeout=15)
    assert r["status"] == "invalid", "a truncated certificate is refused at ca end: %s" % r
    assert "anchor=ca" in c.cli("get mqtt.profile 1"), "and the stored CA is unchanged"
    r = i.request("mqtt.profile.anchor", {"profile": 1, "clear": True})
    assert r["status"] == "ok" and r["result"]["anchor"] == "unset", r
    assert "anchor=unset" in c.cli("get mqtt.profile 1")
    for bad in ({"profile": 1}, {"profile": 1, "clear": True, "fingerprint": fp}, {"profile": 6, "clear": True},
                {"profile": 1, "clear": False}, {"profile": 1, "ca": "a\\nb"}):
        assert i.request("mqtt.profile.anchor", bad)["status"] == "invalid", bad


@pytest.mark.timescale(10)
def test_a_remote_caller_may_not_cut_its_own_session(nodes, tmp_path, broker):
    b = broker()
    d, dev = mqtt_boot(nodes, tmp_path, "npm", b)
    assert d.provision()["status"] == "ok"
    signed = lambda op, args: b.iface_request(dev, wrap(d.node_key, op, args, job=next_job(d)), timeout=10)  # noqa: E731
    r = signed("net.profile.clear", {"kind": "mqtt", "profile": 0})
    assert r["status"] == "refused" and r["error"] == "in-use", "the broker profile it is on: %s" % r
    r = signed("mqtt.profile.anchor", {"profile": 0, "clear": True})
    assert r["status"] == "refused" and r["error"] == "in-use", r
    r = signed("net.profile.clear", {"kind": "wifi", "profile": 0})
    assert r["status"] == "refused" and r["error"] == "in-use", "the Wi-Fi profile it is on: %s" % r
    r = signed("net.profile.clear", {"kind": "mqtt", "profile": 4})
    assert r["status"] == "ok", "a profile it is not on: %s" % r
    assert d.c.cli("get mqtt.status").startswith("> connected"), "the session survived"
    r = d.i.request("net.profile.clear", {"kind": "mqtt", "profile": 4})
    assert r["status"] == "refused", "the unsigned cable writes nothing once enrolled: %s" % r


def test_hello_lists_both_where_the_build_has_a_network(nodes):
    c, i = standalone(nodes, "nph")
    caps = []
    cursor = 0
    while cursor is not None:
        r = i.request("hello", {"cursor": cursor} if cursor else None)["result"]
        caps += r["capabilities"]
        cursor = r.get("next")
    assert "net.profile.clear" in caps and "mqtt.profile.anchor" in caps
    n = nodes("companion_iface_usb", "nphu", seed=11, rtc=RTC).start()
    c2, i2 = client(n)
    caps = []
    cursor = 0
    while cursor is not None:
        r = i2.request("hello", {"cursor": cursor} if cursor else None)["result"]
        caps += r["capabilities"]
        cursor = r.get("next")
    assert "net.profile.clear" not in caps and "mqtt.profile.anchor" not in caps, "a build without a network has neither"
    assert i2.request("net.profile.clear", {"kind": "wifi", "profile": 0})["status"] != "ok"
