"""MQTT over WebSocket (docs/MQTT.md, "WebSocket") on a simulated Companion, against a real Mosquitto reached through
sim/ws_front.py: a WebSocket front, plain or TLS, that forwards MQTT to the broker's plain listener, as a reverse proxy
serves a broker as WSS. The front is an independent RFC 6455 implementation; it records what the device sent and can
misbehave on purpose. The broker's own log is the independent signal that a session reached it."""
import json
import time

import pytest

from sim import mqtt_broker
from sim.harness import MqttEnv, WifiEnv
from sim.mqtt_broker import Broker, ca_commands, make_ca, make_cert, make_leaf
from sim.ws_front import WsFront
from test_companion_mqtt import boot, last, static, wait_status, wait_until
from test_deployment_authority import RTC as DEP_RTC, Dev, apply_name, bare, wrap

PLAIN, WS, WSS = 18830, 18080, 18443
pytestmark = pytest.mark.timescale(10)


@pytest.fixture
def stack(tmp_path):
    """make(front_ip, tls=None | (cert, key), **front) -> (broker, front): a plain broker on 127.0.0.2 behind a front."""
    made = []
    mqtt_broker.require_tools()

    def make(front_ip="127.0.0.3", port=WS, tls=None, **kw):
        b = Broker(tmp_path, "b%d" % len(made), "127.0.0.2", PLAIN).start()
        f = WsFront(front_ip, port, ("127.0.0.2", PLAIN), cert=tls[0] if tls else None, key=tls[1] if tls else None,
                    **kw).start()
        made.append((b, f))
        return b, f

    yield make
    for b, f in made:
        f.stop()
        b.stop()


def ws(i, path=None):
    return ["set mqtt.profile %d transport ws" % i] + (["set mqtt.profile %d path %s" % (i, path)] if path else [])


def test_mqtt_over_plain_websocket_connects_and_the_profile_survives_a_reboot(nodes, tmp_path, stack):
    b, f = stack()
    n, c, _ = boot(nodes, tmp_path, "wsp", static(0, "127.0.0.3", WS, 90) + ws(0))   # boot() reboots after the commands
    s = wait_status(c, "profile=0")
    assert "broker=127.0.0.3:%d" % WS in s and "tls=plain" in s, s
    assert c.cli("get mqtt.profile 0").startswith("> 0: static 127.0.0.3:%d tls=off transport=ws anchor=" % WS)
    assert c.cli("get mqtt.profile 0 path") == "> /mqtt"
    line, h = f.requests[0]
    assert line == "GET /mqtt HTTP/1.1", line
    assert h["host"] == "127.0.0.3:%d" % WS and h["sec-websocket-protocol"] == "mqtt" and h["sec-websocket-version"] == "13", h
    assert h["upgrade"].lower() == "websocket" and "upgrade" in h["connection"].lower(), h
    wait_until(lambda: b.client_connects() == 1, "the broker to log the session")
    assert json.loads(wait_until(lambda: b.retained_status(2), "the retained status"))["tls"] == "plain"
    assert f.client_frames > 0 and f.unmasked == 0, "every frame from a client is masked"


def test_wss_with_a_ca_anchor_is_verified_by_name_and_answers_over_the_broker(nodes, tmp_path, stack):
    ca = make_ca(str(tmp_path), "ca")
    leaf = make_leaf(str(tmp_path), "front", ca, dns="broker.test")
    b, f = stack(port=WSS, tls=leaf[:2], path="/ws/mqtt")
    menv = MqttEnv(str(tmp_path / "wss.mqtt"))
    menv.host("broker.test", "127.0.0.3")
    cmds = static(0, "broker.test", WSS, 90, tls=True) + ca_commands(0, ca[0]) + ws(0, "/ws/mqtt")
    n, c, _ = boot(nodes, tmp_path, "wss", cmds, menv=menv)
    s = wait_status(c, "profile=0")
    assert "tls=verified" in s, s
    assert f.sni and set(f.sni) == {"broker.test"}, f.sni
    line, h = f.requests[-1]
    assert line == "GET /ws/mqtt HTTP/1.1" and h["host"] == "broker.test:%d" % WSS, (line, h)
    nid = c.pub_key[:4].hex()
    r = b.cli_request(nid, "get mecon.cli.who")
    assert r is not None and r["reply"] == "> transport=mqtt auth=tls-verified remote=1 posture=normal principal=mqtt", r
    assert f.unmasked == 0


def test_the_upgrade_is_sent_only_after_the_anchor_passes(nodes, tmp_path, stack):
    cert, key, _ = make_cert(str(tmp_path), "front", "127.0.0.3")
    _, _, other = make_cert(str(tmp_path), "other", "127.0.0.3")
    b, f = stack(port=WSS, tls=(cert, key))
    n, c, _ = boot(nodes, tmp_path, "wpin", static(0, "127.0.0.3", WSS, 90, tls=True, anchor=other) + ws(0))
    wait_until(lambda: last(c, 0) == "pin", "the wrong certificate to be refused")
    assert f.requests == [], "nothing of the upgrade reached a broker that failed the anchor"
    assert b.client_connects() == 0


@pytest.mark.parametrize("fault", ["path", "bad_accept", "no_protocol"])
def test_an_upgrade_the_server_does_not_complete_properly_is_refused(nodes, tmp_path, stack, fault):
    b, f = stack(path="/other" if fault == "path" else "/mqtt", mode="ok" if fault == "path" else fault)
    n, c, _ = boot(nodes, tmp_path, "wf" + fault[:3], static(0, "127.0.0.3", WS, 90) + ws(0))
    wait_until(lambda: last(c, 0) == "ws", "the upgrade to be refused")
    assert f.requests, "the request was sent"
    time.sleep(1)
    assert b.client_connects() == 0, "no MQTT session started"


def test_fragmented_messages_with_pings_between_them(nodes, tmp_path, stack):
    cert, key, fp = make_cert(str(tmp_path), "front", "127.0.0.3")
    b, f = stack(port=WSS, tls=(cert, key), mode="fragment")
    n, c, _ = boot(nodes, tmp_path, "wfrag", static(0, "127.0.0.3", WSS, 90, tls=True, anchor=fp) + ws(0))
    assert "tls=verified" in wait_status(c, "profile=0")
    nid = c.pub_key[:4].hex()
    for k in range(3):
        r = b.cli_request(nid, "get mecon.cli.who", req_id="f%d" % k)
        assert r is not None and r["id"] == "f%d" % k and "auth=tls-verified" in r["reply"], r
    wait_until(lambda: len(f.pongs) >= len(f.pings_sent) > 0, "every ping to be answered")
    assert f.pongs[:len(f.pings_sent)] == f.pings_sent[:len(f.pongs)], "each pong carries its ping's payload"
    assert f.unmasked == 0


def test_transport_and_path_at_the_command_line(nodes, tmp_path):
    n, c, _ = boot(nodes, tmp_path, "wcli", static(0, "127.0.0.3", 0, 90, tls=True))
    assert c.cli("get mqtt.profile 0").startswith("> 0: static 127.0.0.3:8883 tls=on anchor=")
    assert c.cli("set mqtt.profile 0 transport ws").startswith(">")
    assert c.cli("get mqtt.profile 0").startswith("> 0: static 127.0.0.3:443 tls=on transport=ws anchor=")
    assert c.cli("set mqtt.profile 0 tls off").startswith(">")
    assert c.cli("get mqtt.profile 0").startswith("> 0: static 127.0.0.3:80 tls=off transport=ws anchor=")
    for bad in ("mqtt", "/a b", "/" + "x" * 63, "/q?x=1", "/frag#x"):
        assert c.cli("set mqtt.profile 0 path " + bad).startswith("ERR"), bad
    assert c.cli("set mqtt.profile 0 path /" + "x" * 62).startswith(">")
    assert c.cli("get mqtt.profile 0 path") == "> /" + "x" * 62
    assert c.cli("set mqtt.profile 0 transport http").startswith("ERR")
    assert c.cli("set mqtt.profile 0 transport tcp").startswith(">")
    assert c.cli("get mqtt.profile 0").startswith("> 0: static 127.0.0.3:1883 tls=off anchor="), "the path stays, unused"
    assert c.cli("get mqtt.profile 0 path") == "> /" + "x" * 62
    assert c.cli("set mqtt.profile 1 type discovery").startswith(">")
    assert c.cli("set mqtt.profile 1 transport ws").startswith("ERR"), "a discovery profile is always MQTT over TLS"


def test_an_unsigned_and_a_signed_request_over_wss(nodes, tmp_path, stack):
    """The acceptance case of the WebSocket transport: on a WSS session that the anchor verified, an unsigned read is
    answered and, once the device is provisioned over the cable, an unsigned write is refused and a signed one runs."""
    cert, key, fp = make_cert(str(tmp_path), "front", "127.0.0.3")
    b, f = stack(port=WSS, tls=(cert, key))
    wenv = WifiEnv(str(tmp_path / "wdep.wifi"))
    wenv.set("sim")
    n = nodes("companion_standalone", "wdep", seed=17, rtc=DEP_RTC, wifi=wenv, mqtt=MqttEnv(str(tmp_path / "wdep.mqtt"))).start()
    d = Dev(n)
    dev = d.i.request("hello")["result"]["device_id"]
    for cmd in ["set wifi.profile 0 ssid sim"] + static(0, "127.0.0.3", WSS, 90, tls=True, anchor=fp) + ws(0):
        assert d.c.cli(cmd).startswith(">"), cmd
    g = d.i.request("config.get", {"keys": ["mqtt.profile.0.transport", "mqtt.profile.0.path", "mqtt.profile.1.transport",
                                            "mqtt.profile.1.path"]})["result"]["values"]
    assert g == {"mqtt.profile.0.transport": "ws", "mqtt.profile.0.path": "/mqtt", "mqtt.profile.1.transport": None,
                 "mqtt.profile.1.path": "/mqtt"}, "an unset profile's transport is null, like its host; a path has its default: %s" % g
    assert d.c.cli("set connectivity.mode wifi").startswith("OK")
    assert n.wait_exit(10) == 75
    n.start()
    d.attach()
    wait_until(lambda: d.c.cli("get mqtt.status").startswith("> connected"), "the WSS session", timeout=40)
    assert "tls=verified" in d.c.cli("get mqtt.status")
    time.sleep(2.0)
    h = b.iface_request(dev, {"v": 1, "id": "h1", "op": "hello"})
    assert h is not None and h["status"] == "ok" and h["result"]["device_id"] == dev, h
    assert d.provision()["status"] == "ok"   # over the cable; from now on a write needs the authority
    r = b.iface_request(dev, {"v": 1, "id": "w1", "op": "config.apply", "args": apply_name("ViaWss")})
    assert (bare(r)["status"], bare(r)["error"]) == ("refused", "unauthenticated"), r
    assert d.name() != "> ViaWss"
    r = b.iface_request(dev, wrap(d.node_key, "config.apply", apply_name("ViaWss"), job=1))
    assert r["status"] == "ok", r
    assert d.name() == "> ViaWss"
    assert f.upgrades >= 1 and f.unmasked == 0
