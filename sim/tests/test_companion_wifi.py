"""The prioritised Wi-Fi controller on a simulated Companion (docs/WIFI.md).

Access points are defined by the test (`WifiEnv`: SSID, password, RSSI, up/down at run time) and the node's
simulated radio logs every transition ("SIMWIFI join|up|fail|lost|leave ssid=..."), which is how the tests count
joins. Profiles are set over the USB console of `companion_wifi_usb` (the controller runs there too, once a profile
exists and the node was rebooted); the TCP-class `companion_wifi` build is then started on the same node directory.
Time runs 20 times faster than real time, so the 60 s rescan and the 120 s hold-down take 3 s and 6 s.
"""
import os
import time

import pytest

from sim import harness
from sim.harness import CompanionClient, WifiEnv, wifi_events

RTC = 1760000000
pytestmark = pytest.mark.timescale(20)


@pytest.fixture
def env(tmp_path):
    made = []

    def make(name="n"):
        e = WifiEnv(str(tmp_path / (name + ".wifi")))
        made.append(e)
        return e
    return make


def _client(node):
    c = CompanionClient(node)
    c.device_query()
    c.app_start()
    return c


def usb_node(nodes, name, env, seed=3, **kw):
    n = nodes("companion_wifi_usb", name, wifi=env, seed=seed, rtc=RTC, **kw).start()
    return n, _client(n)


def configure(c, profiles):
    """profiles: [(ssid, password, priority)] for profile 0, 1, ..."""
    for i, (ssid, pwd, prio) in enumerate(profiles):
        assert "ssid is now" in c.cli("set wifi.profile %d ssid %s" % (i, ssid))
        if pwd is not None:
            assert "pwd" in c.cli("set wifi.profile %d pwd %s" % (i, pwd))
        assert "priority is now" in c.cli("set wifi.profile %d priority %d" % (i, prio))


def reboot(node, c):
    c._send(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"reboot")
    assert node.wait_exit(10) == 75
    node.start()
    return _client(node)


def wait_event(node, kind, ssid, after=0, timeout=20.0):
    end = time.time() + timeout
    while time.time() < end:
        if (kind, ssid) in wifi_events(node)[after:]:
            return True
        time.sleep(0.05)
    return False


def joins(node, ssid, after=0):
    return [e for e in wifi_events(node)[after:] if e == ("join", ssid)]


def status(c):
    return c.cli("get wifi.status")


def wait_status(c, text, timeout=20.0):
    end = time.time() + timeout
    s = ""
    while time.time() < end:
        s = status(c)
        if text in s:
            return s
        time.sleep(0.1)
    raise AssertionError("never saw %r in wifi.status, last: %r" % (text, s))


# ------------------------------------------------------------------ selection

def test_joins_the_highest_priority_visible_profile(nodes, env):
    e = env()
    for s in ("low", "mid", "high"):
        e.set(s, rssi=-60)
    n, c = usb_node(nodes, "sel", e)
    configure(c, [("low", "", 10), ("mid", "", 50), ("high", "", 90)])
    c = reboot(n, c)
    assert wait_event(n, "up", "high")
    assert wifi_events(n) == [("join", "high"), ("up", "high")]       # no detour through lower profiles
    s = wait_status(c, "connected")
    assert "profile=2" in s and "ssid=high" in s and "rssi=-60" in s and "ip=192.0.2.10" in s, s


def test_skips_a_higher_priority_profile_that_is_not_there(nodes, env):
    e = env()
    e.set("low")
    n, c = usb_node(nodes, "skip", e)
    configure(c, [("low", "", 10), ("absent", "", 90)])
    c = reboot(n, c)
    assert wait_event(n, "up", "low")
    assert ("join", "absent") not in wifi_events(n)


# ------------------------------------------------------------------ failover

def test_fails_over_when_the_network_disappears(nodes, env):
    e = env()
    e.set("home"); e.set("phone")
    n, c = usb_node(nodes, "dis", e)
    configure(c, [("home", "", 90), ("phone", "", 10)])
    c = reboot(n, c)
    assert wait_event(n, "up", "home")
    e.down("home")
    assert wait_event(n, "lost", "home")
    assert wait_event(n, "up", "phone")
    assert "ssid=phone" in wait_status(c, "connected")


def test_fails_over_on_a_wrong_password(nodes, env):
    e = env()
    e.set("home", "the-right-pw-fake"); e.set("phone", "phone-pw-fake")
    n, c = usb_node(nodes, "pw", e)
    configure(c, [("home", "the-wrong-pw-fake", 90), ("phone", "phone-pw-fake", 10)])
    c = reboot(n, c)
    assert wait_event(n, "fail", "home")
    assert wait_event(n, "up", "phone")
    assert wifi_events(n)[:3] == [("join", "home"), ("fail", "home"), ("join", "phone")]
    listing = c.cli("get wifi.profiles")
    assert "0: home prio=90 pwd=set last=auth" in listing, listing
    assert "1: phone prio=10 pwd=set last=none" in listing, listing


# ------------------------------------------------------------------ return, without flapping

def test_returns_to_the_better_network_after_the_hold_down_and_only_once(nodes, env):
    e = env()
    e.set("home", up=False); e.set("phone")
    n, c = usb_node(nodes, "ret", e)
    configure(c, [("home", "", 90), ("phone", "", 10)])
    c = reboot(n, c)
    assert wait_event(n, "up", "phone")
    mark = len(wifi_events(n))
    e.up("home")
    # 2 s real = 40 s node time: home is seen by now, but not for the 120 s hold-down
    time.sleep(2)
    assert not joins(n, "home", mark), "switched up before the hold-down had passed"
    assert wait_event(n, "up", "home", after=mark, timeout=20)
    time.sleep(8)    # 160 s of node time more: no ping-pong
    after = wifi_events(n)[mark:]
    assert ("join", "home") in after and ("up", "home") in after, after
    assert len(joins(n, "home", mark)) == 1 and len(joins(n, "phone", mark)) == 0, after


def test_a_failed_switch_up_does_not_flap(nodes, env):
    e = env()
    e.set("home", "right-fake", up=False); e.set("phone")
    n, c = usb_node(nodes, "nf", e)
    configure(c, [("home", "wrong-fake", 90), ("phone", "", 10)])
    c = reboot(n, c)
    assert wait_event(n, "up", "phone")
    e.up("home")
    time.sleep(12)    # 240 s of node time: the upgrade is tried, fails on the password, and is not hammered
    assert 1 <= len(joins(n, "home")) <= 2, wifi_events(n)
    assert "ssid=phone" in wait_status(c, "connected")


# ------------------------------------------------------------------ nothing reachable

def test_keeps_trying_with_backoff_and_joins_when_the_network_appears(nodes, env):
    e = env()
    n, c = usb_node(nodes, "bo", e)
    configure(c, [("late", "", 50)])
    c = reboot(n, c)
    time.sleep(3)
    assert "up ssid=late" not in str(wifi_events(n))
    assert "connected" not in status(c)
    e.set("late")
    assert wait_event(n, "up", "late", timeout=30)


# ------------------------------------------------------------------ live edits

def test_editing_the_profile_in_use_applies_without_a_reboot(nodes, env):
    e = env()
    e.set("one"); e.set("two")
    n, c = usb_node(nodes, "live", e)
    configure(c, [("one", "", 50)])
    c = reboot(n, c)
    assert wait_event(n, "up", "one")
    assert "ssid is now two" in c.cli("set wifi.profile 0 ssid two")
    assert wait_event(n, "up", "two")
    assert "ssid=two" in wait_status(c, "connected")


# ------------------------------------------------------------------ persistence

def _profiles_listing(c):
    out, cmd = [], "get wifi.profiles"
    for _ in range(12):
        r = c.cli(cmd)
        out += [ln for ln in r.splitlines() if ln and not ln.startswith(">") and not ln.startswith("...")]
        nxt = [ln for ln in r.splitlines() if ln.startswith("... next:")]
        if not nxt:
            break
        cmd = "get wifi.profiles " + nxt[0].split(":")[1]
    return out


def test_ten_profiles_persist_across_a_reboot(nodes, env):
    e = env()
    n, c = usb_node(nodes, "ten", e)
    configure(c, [("network-number-%d" % i, "pw-fake-%d" % i, 100 - i) for i in range(10)])
    before = _profiles_listing(c)
    assert len(before) == 10, before
    c = reboot(n, c)
    assert _profiles_listing(c) == before
    assert c.cli("get wifi.profile 9") == "> 9: network-number-9 prio=91 pwd=set last=none"
    n.stop()
    n.start()           # and again, from a clean stop
    assert _profiles_listing(_client(n)) == before


@pytest.mark.parametrize("cut", [1, 60, 400, "tail"])
def test_a_power_cut_while_saving_keeps_the_ten_profiles(nodes, env, cut):
    e = env()
    n, c = usb_node(nodes, "cut%s" % cut, e)
    configure(c, [("network-number-%d" % i, "pw-fake-%d" % i, 100 - i) for i in range(10)])
    before = _profiles_listing(c)
    full = os.path.getsize(os.path.join(n.dir, "prefs.json"))
    n.stop()
    n.fault = "/prefs.json.tmp:%d" % (full - 5 if cut == "tail" else cut)
    n.start()
    c = _client(n)
    c._send(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"set wifi.profile 4 ssid changed-during-the-cut")
    assert n.wait_exit(10) == 99, "the save never reached the injected fault"
    n.fault = None
    n.start()
    assert _profiles_listing(_client(n)) == before       # all ten, the old value of profile 4


# ------------------------------------------------------------------ passwords

def test_passwords_are_nowhere_but_the_prefs_file(nodes, env):
    pws = ["Pw-%d-fake-secret-%d" % (i, 1000 + i) for i in range(4)]
    e = env()
    for i in range(4):
        e.set("net%d" % i, pws[i], up=(i == 2))
    n, c = usb_node(nodes, "secret", e)
    replies = []
    for i in range(4):
        replies.append(c.cli("set wifi.profile %d ssid net%d" % (i, i)))
        replies.append(c.cli("set wifi.profile %d pwd %s" % (i, pws[i])))
        replies.append(c.cli("set wifi.profile %d priority %d" % (i, 50 + i)))
    c = reboot(n, c)
    assert wait_event(n, "up", "net2")
    replies.append(status(c))
    replies += _profiles_listing(c)
    replies += [c.cli("get wifi.profile %d" % i) for i in range(10)]
    replies += [c.cli(q) for q in ("get wifi.pwd", "get wifi.ssid", "get wifi.enabled", "get wifi.ip")]
    replies.append(c.cli("set wifi.pwd " + pws[0] + "x"))      # even the stock setter's reply
    replies.append(c.cli("set wifi.profile 7 pwd " + "z" * 64))   # a rejected password too
    for p in pws + [pws[0] + "x"]:
        assert not any(p in r for r in replies), p
        hits = harness.files_containing(n, p)
        assert hits in ([os.path.join(n.dir, "prefs.json")], []), (p, hits)    # the node log is among the places searched
    assert harness.files_containing(n, pws[1]) == [os.path.join(n.dir, "prefs.json")]
    for root, _, files in os.walk(n.dir):
        for f in files:
            assert f not in ("prefs.json.tmp", "prefs.json.new"), "a leftover copy of the prefs: " + f


# ------------------------------------------------------------------ stock commands and files

def test_stock_wifi_commands_still_work(nodes, env):
    e = env()
    e.set("stocknet", "stock-pw-fake")
    n, c = usb_node(nodes, "stock", e)
    assert c.cli("set wifi.ssid stocknet") .startswith("> wifi.ssid is now stocknet")
    assert c.cli("set wifi.pwd stock-pw-fake") == "> wifi.pwd updated"
    assert c.cli("get wifi.ssid") == "> stocknet"
    assert c.cli("get wifi.pwd") == "> wifi.pwd is write-only"
    assert c.cli("get wifi.profile 0") == "> 0: stocknet prio=50 pwd=set last=none"
    c = reboot(n, c)
    assert wait_event(n, "up", "stocknet")
    assert c.cli("get wifi.enabled") == "> 1"
    assert c.cli("set wifi.clear") == "> wifi config cleared"
    assert c.cli("get wifi.ssid") == "> (not set)"


def test_a_stock_format_prefs_file_loads(nodes, env):
    e = env()
    e.set("stocknet", "stock-pw-fake")
    n = nodes("companion_wifi_usb", "stockfile", wifi=e, seed=8, rtc=RTC)
    with open(os.path.join(n.dir, "prefs.json"), "w") as f:
        f.write('{name:"Stock",wifi:{ssid:"stocknet",pwd:"stock-pw-fake",enabled:1}}')   # what stock 1.18 writes
    n.start()
    c = _client(n)
    assert wait_event(n, "up", "stocknet")
    assert c.cli("get wifi.profile 0") == "> 0: stocknet prio=50 pwd=set last=none"
    assert c.cli("get name") == "> Stock"


# ------------------------------------------------------------------ TCP companion on top of the controller

def test_the_tcp_companion_server_serves_only_while_the_controller_has_a_link(nodes, env):
    e = env()
    e.set("lan", up=False)
    u, c = usb_node(nodes, "tcp", e)
    configure(c, [("lan", "", 50)])
    u.stop()
    t = nodes("companion_wifi", "tcp", wifi=e, seed=3, rtc=RTC).start()      # same node directory, TCP transport class
    tc = CompanionClient(t)
    tc._send(bytes([harness.CMD_DEVICE_QUERY, 3]))
    assert tc._read_frame(2.0) is None                  # no network: nothing is served
    e.up("lan")
    f = tc._read_frame(20.0)                           # the controller joins, the queued query is answered
    assert f is not None and f[0] != harness.RESP_ERR
    assert wait_event(t, "up", "lan")
    # and the link is the controller's: take it away and the server goes quiet again
    e.down("lan")
    assert wait_event(t, "lost", "lan")
    tc._send(bytes([harness.CMD_DEVICE_QUERY, 3]))
    assert tc._read_frame(2.0) is None


def test_hardened_starts_no_wifi_at_all(nodes, env):
    e = env()
    e.set("lan")
    n, c = usb_node(nodes, "hard", e)
    configure(c, [("lan", "", 50)])
    assert c.cli("set posture hardened") == "OK - posture hardened"
    c = reboot(n, c)
    time.sleep(2)
    assert wifi_events(n) == []
    assert c.cli("get wifi.status") == "> off"
