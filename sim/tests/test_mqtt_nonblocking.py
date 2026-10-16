"""The MQTT connect must not block the main loop (docs/MQTT.md, "The connect runs on a worker").

A black-holing broker accepts the TCP connection and never answers the TLS handshake or the CONNACK; an unreachable
address never answers the SYN. A blocking connect stalls the node's main loop for the whole attempt (about 6 s each),
so mesh packets and the USB console wait. The sim node logs every main-loop gap above 50 ms in wall-clock time
(`SIMLOOP gap_ms=N`), so these tests run at time scale 1 and read the longest stall from the log.
"""
import time

import pytest

from sim import mqtt_broker
from sim.harness import CompanionClient, MqttEnv, WifiEnv, loop_gaps, mqtt_events, node_log
from sim.mqtt_broker import Broker, BlackHole, make_cert
from sim.tests.test_companion_mqtt import PLAIN, TLS, RTC, static, wait_status, wait_until, last

SECRET = bytes(range(16))
MAX_GAP_MS = 200   # the main loop may be late by this much while a connect is in flight; it used to be ~6000 per attempt

# the unreachable address is TEST-NET-2 (RFC 5737): never routed
CASES = [("plain-black-hole", "127.0.0.7", False), ("tls-black-hole", "127.0.0.7", True),
         ("unreachable", "198.51.100.1", False)]


@pytest.fixture(autouse=True)
def _tools():
    mqtt_broker.require_tools()


@pytest.fixture
def holes():
    made = []

    def make(ip, port):
        h = BlackHole(ip, port).start()
        made.append(h)
        return h

    yield make
    for h in made:
        h.stop()


def _node(nodes, tmp_path, name, host, tls, ether=None, kind="companion_wifi_usb", seed=3, extra_cmds=()):
    wenv = WifiEnv(str(tmp_path / (name + ".wifi")))
    wenv.set("sim")
    menv = MqttEnv(str(tmp_path / (name + ".mqtt")))
    kw = dict(wifi=wenv, mqtt=menv, seed=seed, rtc=RTC)
    if ether:
        kw["ether_path"] = ether.path
    n = nodes(kind, name, **kw).start()
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    assert "ssid is now" in c.cli("set wifi.profile 0 ssid sim")
    for cmd in static(0, host, TLS if tls else PLAIN, 90, tls=tls) + list(extra_cmds):
        assert c.cli(cmd).startswith(">"), cmd
    return n, c


def _reboot(n, c):
    from sim import harness
    c._send(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"reboot")
    assert n.wait_exit(10) == 75
    n.start()
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    return c


def _opens(n):
    return sum(1 for e in mqtt_events(n) if e.startswith("open "))


@pytest.mark.parametrize("label,ip,tls", CASES, ids=[c[0] for c in CASES])
def test_a_connect_attempt_does_not_stall_the_main_loop(nodes, tmp_path, holes, label, ip, tls):
    if ip == "127.0.0.7":
        holes(ip, TLS if tls else PLAIN)
    n, c = _node(nodes, tmp_path, "gap", ip, tls)
    c = _reboot(n, c)
    wait_until(lambda: _opens(n) >= 1, "the first connect attempt", 20)
    # the console answers while the attempt is in flight, and says so
    t0 = time.time()
    s = c.cli("get mqtt.status", timeout=30)
    took = time.time() - t0
    # let the attempt run to its end (the transport and CONNACK timeouts are 6 s each)
    wait_until(lambda: last(c, 0) in ("tcp", "tls", "timeout"), "the attempt to fail", 30)
    gaps = loop_gaps(n)
    print("%s: longest main-loop gap %d ms; console answered in %.2f s: %s" % (label, max(gaps or [0]), took, s))
    assert "connecting" in s, s
    assert took < 1.0, "the console waited %.1f s for the connect" % took
    assert max(gaps or [0]) < MAX_GAP_MS, "main loop stalled %s ms during the connect attempt" % gaps


def test_the_mesh_keeps_delivering_during_a_connect_attempt(ether, nodes, tmp_path, holes):
    holes("127.0.0.7", PLAIN)
    n, a = _node(nodes, tmp_path, "relay", "127.0.0.7", False, ether=ether)
    cb = nodes("companion", "peer", ether_path=ether.path, seed=22, rtc=RTC).start()
    a = _reboot(n, a)
    assert ether.wait_for_nodes(["relay", "peer"])
    ether.set_link("relay", "peer")
    b = CompanionClient(cb)
    b.device_query()
    b.app_start()
    assert a.add_contact(b.pub_key, "bob")[0] == 0
    assert b.add_contact(a.pub_key, "alice")[0] == 0
    # the first transmission after a boot waits for the duty-cycle window; send one now, away from the attempt under test
    assert b.send_direct(a.pub_key, "prime", RTC + 10)[0] == 6
    assert a.sync_messages(timeout=60, want=1), "the prime message never arrived"
    before = _opens(n)
    wait_until(lambda: _opens(n) > before, "the next connect attempt", 40)
    assert b.send_direct(a.pub_key, "during", RTC + 11)[0] == 6
    t0 = time.time()
    assert a.wait_push(0x83, timeout=3.0) is not None, "no message delivered while the connect was in flight"
    print("delivered %.2f s into the attempt" % (time.time() - t0))
    msgs = a.sync_messages(timeout=5, want=1)
    assert [m["text"] for m in msgs] == ["during"], msgs
    assert max(loop_gaps(n) or [0]) < MAX_GAP_MS


# ------------------------------------------------------------------ cancellation (ASan + UBSan build)

def _count(n, what):
    return sum(1 for e in mqtt_events(n) if e == what)


def test_a_profile_edit_during_a_pending_connect_is_applied_and_leaks_nothing(nodes, tmp_path, holes):
    holes("127.0.0.7", PLAIN)
    good = Broker(tmp_path, "good", "127.0.0.2", PLAIN).start()
    try:
        n, c = _node(nodes, tmp_path, "edit", "127.0.0.7", False, kind="companion_wifi_usb_asan")
        c = _reboot(n, c)
        wait_until(lambda: _opens(n) >= 1, "the connect attempt", 30)
        assert "connecting" in c.cli("get mqtt.status")
        assert c.cli("set mqtt.profile 0 host 127.0.0.2").startswith(">")   # the profile being connected is edited
        s = wait_status(c, "connected", timeout=40)
        assert "broker=127.0.0.2:%d" % PLAIN in s, s
        assert good.client_connects() == 1
        # one transport open: the session. Every abandoned attempt closed its own.
        assert _opens(n) - _count(n, "close") == 1, mqtt_events(n)
        assert n.sanitizer_report() == "", n.sanitizer_report()
        assert n.proc.poll() is None
    finally:
        good.stop()


def test_raising_the_posture_during_a_pending_connect_cancels_it(nodes, tmp_path, holes):
    holes("127.0.0.7", PLAIN)
    cert, key, _ = make_cert(str(tmp_path), "tls", "127.0.0.4")
    tls = Broker(tmp_path, "tls", "127.0.0.4", TLS, cert, key).start()   # profile 1: TLS without an anchor (unverified)
    try:
        n, c = _node(nodes, tmp_path, "post", "127.0.0.7", False, kind="companion_wifi_usb_asan",
                     extra_cmds=static(1, "127.0.0.4", TLS, 50, tls=True))
        c = _reboot(n, c)
        wait_until(lambda: _opens(n) >= 1, "the connect attempt", 30)
        assert "hardened" in c.cli("set posture hardened").lower()
        s = wait_status(c, "connected", timeout=40)
        assert "profile=1" in s, s                                   # the plain profile is forbidden, the TLS one is used
        assert last(c, 0) == "posture"
        assert tls.client_connects() == 1
        assert _opens(n) - _count(n, "close") == 1, mqtt_events(n)
        assert n.sanitizer_report() == "", n.sanitizer_report()
        assert n.proc.poll() is None
    finally:
        tls.stop()
