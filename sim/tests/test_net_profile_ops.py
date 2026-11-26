"""net.profile.clear and mqtt.profile.anchor (#490; NetProfileOps.h): the network-profile lifecycle over the interface, run
as the caller's own CLI lines. A caller other than the cable may not clear or re-anchor the profile its session runs on."""
import base64
import hashlib
import json
import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import mqtt_broker  # noqa: E402
from test_deployment_authority import TLS_PORT, broker, mqtt_boot, wrap  # noqa: E402,F401
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


MQTT_REQUEST_BOUND = 1551   # what a request over a broker may be (UPDATES.md): the client buffer less the header and topic


def _status_has(c, *parts):
    s = c.cli("get mqtt.status")
    return s if all(p in s for p in parts) else None


def _wait(fn, what, timeout=60.0):
    end = time.time() + timeout
    while time.time() < end:
        v = fn()
        if v:
            return v
        time.sleep(0.3)
    raise AssertionError("timed out waiting for " + what)


def test_a_signed_piece_of_768_characters_fits_the_request_bound_over_a_broker(nodes, tmp_path, broker):
    """The recommended piece (docs/MQTT.md): the longest signed, enveloped ca_add, with a request id of 31 characters and
    a job number of 10 digits, is under the bound a request over a broker has."""
    b = broker()
    d, dev = mqtt_boot(nodes, tmp_path, "npb", b)
    piece = "A" * 768
    w = wrap(d.node_key, "mqtt.profile.anchor", {"profile": 5, "ca_add": {"offset": 1152, "data": piece}}, job=2 ** 31, rid="r" * 31)
    assert len(json.dumps(w)) < MQTT_REQUEST_BOUND, len(json.dumps(w))
    w = wrap(d.node_key, "mqtt.profile.anchor", {"profile": 5, "ca_add": {"offset": 1152, "data": "A" * 1024}}, job=2 ** 31, rid="r" * 31)
    assert len(json.dumps(w)) >= MQTT_REQUEST_BOUND, "the op's own 1,024-character bound is above what a signed envelope allows"


@pytest.mark.timescale(10)
def test_a_ca_installed_in_pieces_over_the_broker_is_trusted_and_a_stranger_is_refused(nodes, tmp_path, broker):
    """An enrolled board on a broker is given a CA anchor in pieces over that same broker, then loses the broker and
    connects `tls: verified` to one whose certificate chains to the CA, and refuses one that does not."""
    b = broker()
    ca = mqtt_broker.make_ca(str(tmp_path), "pca")
    other = mqtt_broker.make_ca(str(tmp_path), "pother")
    made = []
    for name, ip, signer in (("good", "127.0.0.4", ca), ("rogue", "127.0.0.5", other)):
        cert, key, fp = mqtt_broker.make_leaf(str(tmp_path), name, signer, ip=ip)
        made.append(mqtt_broker.Broker(tmp_path, name, ip, TLS_PORT, cert, key, cafile=signer[0]).start())
    good, rogue = made
    try:
        d, dev = mqtt_boot(nodes, tmp_path, "npp", b)
        # Profile 1 (the broker whose certificate chains to the CA) and profile 2 (a stranger's), both better than
        # nothing and worse than profile 0, which the session runs on. Set from the cable, before enrolment.
        for i, host, prio in ((1, "127.0.0.4", 40), (2, "127.0.0.5", 45)):
            for cmd in ("set mqtt.profile %d type static" % i, "set mqtt.profile %d host %s" % (i, host),
                        "set mqtt.profile %d port %d" % (i, TLS_PORT), "set mqtt.profile %d tls on" % i,
                        "set mqtt.profile %d priority %d" % (i, prio)):
                assert d.c.cli(cmd).startswith(">"), cmd
        assert d.provision()["status"] == "ok"
        signed = lambda args: b.iface_request(dev, wrap(d.node_key, "mqtt.profile.anchor", args, job=next_job(d)), timeout=10)  # noqa: E731

        text = mqtt_broker.ca_b64(ca[0])
        der = base64.b64decode(text)
        sha = hashlib.sha256(der).hexdigest()
        piece = 256
        pieces = [(at, text[at:at + piece]) for at in range(0, len(text), piece)]
        assert len(pieces) >= 3, "the CA is cut into several pieces"

        # profile 1: in order, with a gap and a repeat on the way, both refused with nothing appended
        r = signed({"profile": 1, "ca_begin": {"size": len(der), "sha256": sha}})
        assert r["status"] == "ok" and r["result"] == {"profile": 1, "staged": 0}, r
        at, data = pieces[0]
        r = signed({"profile": 1, "ca_add": {"offset": at, "data": data}})
        assert r["status"] == "ok" and r["result"]["staged"] == len(data), r
        at2, data2 = pieces[2]
        r = signed({"profile": 1, "ca_add": {"offset": at2, "data": data2}})
        assert r["status"] == "invalid", "a gap: %s" % r
        r = signed({"profile": 1, "ca_add": {"offset": 0, "data": data}})
        assert r["status"] == "invalid", "a repeat: %s" % r
        assert "anchor=ca" not in d.c.cli("get mqtt.profile 1"), "nothing is installed before the end"
        for at, data in pieces[1:]:
            r = signed({"profile": 1, "ca_add": {"offset": at, "data": data}})
            assert r["status"] == "ok", r
        r = signed({"profile": 1, "ca_end": True})
        assert r["status"] == "ok" and r["result"] == {"profile": 1, "anchor": "ca"}, r
        assert "anchor=ca" in d.c.cli("get mqtt.profile 1"), d.c.cli("get mqtt.profile 1")

        # profile 2: the same CA, after an end whose hash does not match the text is refused and changes nothing
        wrong = ("0" if sha[0] != "0" else "1") + sha[1:]
        assert signed({"profile": 2, "ca_begin": {"size": len(der), "sha256": wrong}})["status"] == "ok"
        for at, data in pieces:
            assert signed({"profile": 2, "ca_add": {"offset": at, "data": data}})["status"] == "ok"
        r = signed({"profile": 2, "ca_end": True})
        assert r["status"] == "invalid", "a hash that does not match: %s" % r
        assert "anchor=ca" not in d.c.cli("get mqtt.profile 2")
        assert signed({"profile": 2, "ca_begin": {"size": len(der), "sha256": sha}})["status"] == "ok"
        for at, data in pieces:
            assert signed({"profile": 2, "ca_add": {"offset": at, "data": data}})["status"] == "ok"
        assert signed({"profile": 2, "ca_end": True})["status"] == "ok"
        assert "anchor=ca" in d.c.cli("get mqtt.profile 2")

        # the profile the session runs on may not be touched by any step
        for args in ({"profile": 0, "ca_begin": {"size": len(der), "sha256": sha}}, {"profile": 0, "ca_add": {"offset": 0, "data": "AAAA"}},
                     {"profile": 0, "ca_end": True}):
            r = signed(args)
            assert r["status"] == "refused" and r["error"] == "in-use", r

        # lose the broker the session is on: the board tries the stranger first (45), refuses it, and takes the
        # broker that chains to the CA (40), verified
        b.stop()
        s = _wait(lambda: _status_has(d.c, "connected", "profile=1", "tls=verified"), "a verified session on profile 1")
        assert "127.0.0.4" in s, s
        assert "last=pin" in d.c.cli("get mqtt.profile 2"), d.c.cli("get mqtt.profile 2")
        assert rogue.client_connects() == 0, "the stranger saw no MQTT session"
        assert good.client_connects() >= 1
    finally:
        for x in made:
            x.stop()
