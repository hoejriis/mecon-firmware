"""The MQTT controller on a simulated Companion (docs/MQTT.md), against real Mosquitto brokers.

The node is `companion_wifi_usb`: profiles are set over the USB console, the Wi-Fi controller joins the simulated
access point "sim" and the MQTT controller runs after a reboot. Brokers are real Mosquitto processes on their own
loopback addresses (127.0.0.N), plain or TLS with certificates generated here at test time. The independent signal is
the broker's own log (sessions it accepted); the node's log (`SIMMQTT ...`) and `get mqtt.status` are the other side.
DNS names and mDNS responders are the simulated name service of `MqttEnv`, rewritten while the node runs.
Time runs 10 times faster than real time: the 60 s hold-down and rescan take 6 s.
"""
import json
import os
import time

import pytest

from sim import harness, mqtt_broker
from sim.harness import CompanionClient, MqttEnv, WifiEnv, files_containing, mqtt_events
from sim.mqtt_broker import Broker, SniProbe, ca_commands, make_ca, make_cert, make_leaf

RTC = 1760000000
PLAIN, TLS = 18830, 18883
pytestmark = pytest.mark.timescale(10)


@pytest.fixture(autouse=True)
def _tools():
    mqtt_broker.require_tools()


@pytest.fixture
def brokers(tmp_path):
    made = []

    def make(name, ip, port=PLAIN, tls=False, users=None):
        cert = key = fp = None
        if tls:
            cert, key, fp = make_cert(str(tmp_path), name, ip)
        b = Broker(tmp_path, name, ip, port, cert, key, users).start()
        b.fingerprint = fp
        made.append(b)
        return b

    yield make
    for b in made:
        b.stop()


def _client(node):
    c = CompanionClient(node)
    c.device_query()
    c.app_start()
    return c


def reboot(node, c):
    c._send(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"reboot")
    assert node.wait_exit(10) == 75
    node.start()
    return _client(node)


def static(i, host, port, prio, tls=False, anchor=None, user=None, pwd=None):
    cmds = ["set mqtt.profile %d type static" % i, "set mqtt.profile %d host %s" % (i, host),
            "set mqtt.profile %d port %d" % (i, port), "set mqtt.profile %d tls %s" % (i, "on" if tls else "off"),
            "set mqtt.profile %d priority %d" % (i, prio)]
    if anchor:
        cmds.append("set mqtt.profile %d anchor %s" % (i, anchor))
    if user:
        cmds.append("set mqtt.profile %d user %s" % (i, user))
    if pwd:
        cmds.append("set mqtt.profile %d pwd %s" % (i, pwd))
    return cmds


def discovery(i, site, anchor, prio):
    return ["set mqtt.profile %d type discovery" % i, "set mqtt.profile %d site %s" % (i, site),
            "set mqtt.profile %d anchor %s" % (i, anchor), "set mqtt.profile %d priority %d" % (i, prio)]


def boot(nodes, tmp_path, name, commands, hardened=False, menv=None, seed=3):
    """A node with the given MQTT profile commands, Wi-Fi profile 0 = the simulated access point, rebooted so both
    controllers run. Returns (node, client, mqtt_env)."""
    wenv = WifiEnv(str(tmp_path / (name + ".wifi")))
    wenv.set("sim")
    menv = menv or MqttEnv(str(tmp_path / (name + ".mqtt")))
    n = nodes("companion_wifi_usb", name, wifi=wenv, mqtt=menv, seed=seed, rtc=RTC).start()
    c = _client(n)
    assert "ssid is now" in c.cli("set wifi.profile 0 ssid sim")
    for cmd in commands:
        reply = c.cli(cmd)
        assert reply.startswith(">"), (cmd, reply)
    if hardened:   # raised last, so a plain profile stored while Normal is still there
        assert "hardened" in c.cli("set posture hardened").lower()
    c = reboot(n, c)
    return n, c, menv


def wait_status(c, text, timeout=30.0):
    # "profile=<n>" means a session on that profile. Since the connect runs off the main loop, the status also reads
    # "connecting profile=<n>" while an attempt is under way; a bare substring match returned before the session existed.
    if text.startswith("profile="):
        text = "connected " + text
    end = time.time() + timeout
    s = ""
    while time.time() < end:
        s = c.cli("get mqtt.status")
        if text in s:
            return s
        time.sleep(0.1)
    raise AssertionError("never saw %r in mqtt.status, last: %r" % (text, s))


def wait_until(fn, what, timeout=30.0):
    end = time.time() + timeout
    while time.time() < end:
        v = fn()
        if v:
            return v
        time.sleep(0.1)
    raise AssertionError("timed out waiting for " + what)


def last(c, i):
    return c.cli("get mqtt.profile %d" % i).split("last=")[-1].strip()


# ------------------------------------------------------------------ priority and failover

def test_connects_to_the_highest_priority_broker_and_failover_follows(nodes, tmp_path, brokers):
    a = brokers("a", "127.0.0.2")
    b = brokers("b", "127.0.0.3")
    # the better broker has the higher profile number, so a controller that took the first profile would fail here
    n, c, _ = boot(nodes, tmp_path, "pri", static(0, "127.0.0.3", PLAIN, 50) + static(1, "127.0.0.2", PLAIN, 90))
    s = wait_status(c, "connected")
    assert "profile=1" in s and "broker=127.0.0.2:%d" % PLAIN in s and "tls=plain" in s, s
    assert a.client_connects() == 1 and b.client_connects() == 0       # the lower-priority broker was never touched
    a.stop()                                                            # the broker goes away
    s = wait_status(c, "profile=0")
    assert "connected" in s and "broker=127.0.0.3:%d" % PLAIN in s, s
    assert b.client_connects() == 1
    assert last(c, 1) in ("dropped", "tcp"), last(c, 1)


def test_status_topic_is_published_and_retained(nodes, tmp_path, brokers):
    a = brokers("a", "127.0.0.2")
    n, c, _ = boot(nodes, tmp_path, "topic", static(0, "127.0.0.2", PLAIN, 90))
    wait_status(c, "connected")
    payload = wait_until(lambda: a.retained_status(2), "the retained status")
    assert json.loads(payload) == {"online": True, "profile": 0, "tls": "plain"}, payload


def test_returns_to_the_better_broker_after_the_hold_down_without_flapping(nodes, tmp_path, brokers):
    b = brokers("b", "127.0.0.3")
    n, c, _ = boot(nodes, tmp_path, "hold", static(0, "127.0.0.3", PLAIN, 50) + static(1, "127.0.0.2", PLAIN, 90))
    wait_status(c, "profile=0")                       # the better broker is not there yet
    t_backup = time.time()
    a = brokers("a", "127.0.0.2")                     # now it is
    time.sleep(3.0)                                   # 30 s of node time: inside the 60 s hold-down
    assert "profile=0" in c.cli("get mqtt.status") and a.client_connects() == 0
    wait_status(c, "profile=1", timeout=20)
    assert time.time() - t_backup >= 5.0              # node time >= 50 s: not before the session was a hold-down old
    time.sleep(12.0)                                  # two further hold-downs: no more switching
    assert "profile=1" in c.cli("get mqtt.status")
    assert a.client_connects() == 1 and b.client_connects() == 1
    opens = [e for e in mqtt_events(n) if e.startswith("open ")]
    assert [e for e in opens if "127.0.0.3" in e] == ["open addr=127.0.0.3:%d tls=0" % PLAIN]   # the backup was opened once
    assert [e for e in opens if "127.0.0.2" in e].count("open addr=127.0.0.2:%d tls=0" % PLAIN) <= 2   # the refused try and the return


# ------------------------------------------------------------------ DNS

def test_a_host_name_is_resolved_again_on_every_attempt(nodes, tmp_path, brokers):
    a = brokers("a", "127.0.0.2")
    b = brokers("b", "127.0.0.3")
    menv = MqttEnv(str(tmp_path / "dns.mqtt"))
    menv.host("broker.test", "127.0.0.2")
    n, c, _ = boot(nodes, tmp_path, "dns", static(0, "broker.test", PLAIN, 90), menv=menv)
    assert "broker=127.0.0.2" in wait_status(c, "connected")
    menv.host("broker.test", "127.0.0.3")             # dynamic DNS: the name moves
    a.stop()
    s = wait_status(c, "broker=127.0.0.3", timeout=30)
    assert "connected" in s and b.client_connects() == 1
    resolved = [e for e in mqtt_events(n) if e.startswith("resolve ")]
    assert resolved[0] == "resolve name=broker.test addr=127.0.0.2"
    assert "resolve name=broker.test addr=127.0.0.3" in resolved


def test_an_unresolvable_name_fails_over_and_is_reported(nodes, tmp_path, brokers):
    b = brokers("b", "127.0.0.3")
    n, c, _ = boot(nodes, tmp_path, "nodns", static(0, "127.0.0.3", PLAIN, 50) + static(1, "gone.test", PLAIN, 90))
    wait_status(c, "profile=0")
    assert last(c, 1) == "dns"


# ------------------------------------------------------------------ discovery

def test_discovery_accepts_a_matching_site_id_with_a_matching_certificate(nodes, tmp_path, brokers):
    a = brokers("a", "127.0.0.2")
    t = brokers("t", "127.0.0.4", TLS, tls=True)
    menv = MqttEnv(str(tmp_path / "d1.mqtt"))
    menv.advertise("alpha", "127.0.0.4", TLS)
    n, c, _ = boot(nodes, tmp_path, "d1", discovery(0, "alpha", t.fingerprint, 90) + static(1, "127.0.0.2", PLAIN, 10),
                   menv=menv)
    s = wait_status(c, "connected")
    assert "profile=0" in s and "broker=127.0.0.4:%d" % TLS in s and "tls=verified" in s, s
    assert t.client_connects() == 1 and a.client_connects() == 0       # the browse came before any fallback connect
    payload = wait_until(lambda: t.retained_status(2), "the retained status")
    assert json.loads(payload) == {"online": True, "profile": 0, "tls": "verified"}, payload


def test_discovery_rejects_the_right_site_id_with_the_wrong_certificate(nodes, tmp_path, brokers):
    a = brokers("a", "127.0.0.2")
    real = brokers("real", "127.0.0.4", TLS, tls=True)         # the enrolled broker's certificate ...
    rogue = brokers("rogue", "127.0.0.5", TLS, tls=True)       # ... and another one on the same LAN
    menv = MqttEnv(str(tmp_path / "d2.mqtt"))
    menv.advertise("alpha", "127.0.0.5", TLS)                  # the rogue answers mDNS with the right site ID
    n, c, _ = boot(nodes, tmp_path, "d2", discovery(0, "alpha", real.fingerprint, 90) + static(1, "127.0.0.2", PLAIN, 10),
                   menv=menv)
    wait_status(c, "profile=1")                                # fell back to the static broker
    assert last(c, 0) == "pin"
    assert rogue.client_connects() == 0                        # the broker's own log: no MQTT session, no credentials sent
    assert "open-fail pin" in mqtt_events(n)
    assert real.client_connects() == 0


def test_a_rogue_does_not_shut_out_the_real_broker_that_carries_the_same_site_id(nodes, tmp_path, brokers):
    real = brokers("real", "127.0.0.4", TLS, tls=True)
    rogue = brokers("rogue", "127.0.0.5", TLS, tls=True)
    menv = MqttEnv(str(tmp_path / "d3.mqtt"))
    menv.advertise("alpha", "127.0.0.5", TLS)
    menv.advertise("alpha", "127.0.0.4", TLS)
    n, c, _ = boot(nodes, tmp_path, "d3", discovery(0, "alpha", real.fingerprint, 90), menv=menv)
    s = wait_status(c, "connected")
    assert "broker=127.0.0.4:%d" % TLS in s and "tls=verified" in s, s
    assert real.client_connects() == 1 and rogue.client_connects() == 0


def test_discovery_ignores_a_responder_with_another_site_id(nodes, tmp_path, brokers):
    a = brokers("a", "127.0.0.2")
    t = brokers("t", "127.0.0.4", TLS, tls=True)
    menv = MqttEnv(str(tmp_path / "d4.mqtt"))
    menv.advertise("beta", "127.0.0.4", TLS)                   # right certificate, wrong site
    n, c, _ = boot(nodes, tmp_path, "d4", discovery(0, "alpha", t.fingerprint, 90) + static(1, "127.0.0.2", PLAIN, 10),
                   menv=menv)
    wait_status(c, "profile=1")
    time.sleep(1.0)
    assert t.connections() == 0                                # not even a TCP connection
    assert not [e for e in mqtt_events(n) if "127.0.0.4" in e]


def test_a_discovered_broker_is_never_used_over_plain_mqtt(nodes, tmp_path, brokers):
    a = brokers("a", "127.0.0.2")
    plain = brokers("plain", "127.0.0.6", PLAIN)               # advertises _mqtt._tcp with the right site ID
    menv = MqttEnv(str(tmp_path / "d5.mqtt"))
    menv.advertise("alpha", "127.0.0.6", PLAIN, secure=False)
    anchor = "ab" * 32
    n, c, _ = boot(nodes, tmp_path, "d5", discovery(0, "alpha", anchor, 90) + static(1, "127.0.0.2", PLAIN, 10), menv=menv)
    wait_status(c, "profile=1")
    time.sleep(1.0)
    assert plain.connections() == 0
    assert last(c, 0) == "plain"
    assert not [e for e in mqtt_events(n) if "127.0.0.6" in e]


def test_discovery_browses_only_while_not_on_the_best_profile(nodes, tmp_path, brokers):
    a = brokers("a", "127.0.0.2")
    anchor = "cd" * 32
    # (a) the static broker is the best profile: no browse at all
    n, c, _ = boot(nodes, tmp_path, "b1", static(0, "127.0.0.2", PLAIN, 90) + discovery(1, "alpha", anchor, 10))
    wait_status(c, "connected")
    time.sleep(8.0)                                            # 80 s of node time: more than one 60 s rescan
    assert not [e for e in mqtt_events(n) if e == "browse"]
    n.stop()
    # (b) a better discovery profile exists but nothing answers: the device browses at the rescan interval
    n2, c2, _ = boot(nodes, tmp_path, "b2", static(0, "127.0.0.2", PLAIN, 10) + discovery(1, "alpha", anchor, 90))
    wait_status(c2, "connected")
    time.sleep(14.0)                                           # 140 s of node time
    assert 2 <= len([e for e in mqtt_events(n2) if e == "browse"]) <= 4


def test_a_responder_that_appears_later_is_taken_after_the_hold_down(nodes, tmp_path, brokers):
    a = brokers("a", "127.0.0.2")
    t = brokers("t", "127.0.0.4", TLS, tls=True)
    menv = MqttEnv(str(tmp_path / "d6.mqtt"))
    n, c, _ = boot(nodes, tmp_path, "d6", discovery(0, "alpha", t.fingerprint, 90) + static(1, "127.0.0.2", PLAIN, 10),
                   menv=menv)
    wait_status(c, "profile=1")

    def browses():
        return sum(1 for e in mqtt_events(n) if e == "browse")

    def next_browse(after, timeout):
        """Wall-clock time at which a browse beyond the `after`-th started (polled every 20 ms)."""
        end = time.time() + timeout
        while time.time() < end:
            if browses() > after:
                return time.time()
            time.sleep(0.02)
        raise AssertionError("no browse started")

    # Advertise only after a browse has finished (it takes 200 ms of node time), so the first browse that can see the
    # responder is the next one, and the time it started is known from the node's own log. The hold-down is then
    # measured from that browse, not from the advertisement: the old bound (9 s from the advertisement) depended on
    # where in the rescan period the advertisement happened to fall, and failed when a browse came right after it.
    next_browse(browses(), 40)
    time.sleep(0.5)
    seen = browses()
    menv.advertise("alpha", "127.0.0.4", TLS)
    first_sight = next_browse(seen, 40)
    s = wait_status(c, "profile=0", timeout=40)
    took = time.time() - first_sight
    hold = 6.0   # 60 s of node time at time scale 10
    assert took >= hold - 0.15, "taken %.2f s after it was first seen: not held down for %.1f s" % (took, hold)
    assert "tls=verified" in s
    assert a.client_connects() == 1 and t.client_connects() == 1       # one switch, no flapping
    time.sleep(8.0)
    assert "profile=0" in c.cli("get mqtt.status") and t.client_connects() == 1


# ------------------------------------------------------------------ credentials

def test_credentials_are_used_and_a_wrong_password_fails_over(nodes, tmp_path, brokers):
    secure = brokers("secure", "127.0.0.2", users={"alice": "pw-marker-s3cret"})
    open_ = brokers("open", "127.0.0.3")
    n, c, _ = boot(nodes, tmp_path, "cred",
                   static(0, "127.0.0.2", PLAIN, 90, user="alice", pwd="wrong-marker-xx") + static(1, "127.0.0.3", PLAIN, 50))
    wait_status(c, "profile=1")
    assert last(c, 0) == "auth"
    assert secure.client_connects() == 0
    assert "updated" in c.cli("set mqtt.profile 0 pwd pw-marker-s3cret")       # an edit applies at once, no reboot
    wait_status(c, "profile=0", timeout=6)             # well inside the 120 s (12 s here) the refused password cooled the profile for
    assert secure.client_connects() == 1


def test_credentials_are_nowhere_but_the_prefs_file(nodes, tmp_path, brokers):
    secure = brokers("secure", "127.0.0.2", users={"alice-marker": "pw-marker-s3cret"})
    n, c, _ = boot(nodes, tmp_path, "sec",
                   static(0, "127.0.0.2", PLAIN, 90, user="alice-marker", pwd="pw-marker-s3cret"))
    wait_status(c, "connected")
    replies = [c.cli("get mqtt.profile 0"), c.cli("get mqtt.profiles"), c.cli("get mqtt.status"),
               c.cli("set mqtt.profile 0 pwd pw-marker-s3cret"), c.cli("set mqtt.profile 0 user alice-marker"),
               c.cli("get mqtt.enabled")]
    for r in replies:
        assert "marker" not in r, r
    time.sleep(2.0)
    for secret in ("pw-marker-s3cret", "alice-marker"):
        hits = [os.path.basename(h) for h in files_containing(n, secret)]
        assert hits == ["prefs.json"], (secret, hits)       # the node's directory and log: only the prefs file holds it
    n.stop()
    hits = [os.path.basename(h) for h in files_containing(n, "pw-marker-s3cret")]
    assert hits == ["prefs.json"]


# ------------------------------------------------------------------ posture

def test_hardened_refuses_plain_mqtt_at_the_command(nodes, tmp_path, brokers):
    n, c, _ = boot(nodes, tmp_path, "hc", ["set mqtt.profile 0 type static", "set mqtt.profile 0 host 127.0.0.2"])
    assert c.cli("set mqtt.profile 0 tls off").startswith(">")
    assert "hardened" in c.cli("set posture hardened").lower()
    assert c.cli("set mqtt.profile 0 tls off").startswith("ERR")
    assert c.cli("set mqtt.profile 0 tls on").startswith(">")


def test_hardened_never_connects_a_stored_plain_profile(nodes, tmp_path, brokers):
    plain = brokers("plain", "127.0.0.2")
    tls = brokers("tls", "127.0.0.4", TLS, tls=True)
    n, c, _ = boot(nodes, tmp_path, "hs",
                   static(0, "127.0.0.2", PLAIN, 90) + static(1, "127.0.0.4", TLS, 50, tls=True, anchor="00" * 32)[:-1]
                   + ["set mqtt.profile 1 anchor %s" % tls.fingerprint], hardened=True)
    s = wait_status(c, "connected")
    assert "profile=1" in s and "tls=verified" in s, s
    time.sleep(1.0)
    assert plain.connections() == 0                                # no TCP connection to the plain broker, ever
    assert last(c, 0) == "posture"


def test_raising_the_posture_ends_a_plain_session(nodes, tmp_path, brokers):
    plain = brokers("plain", "127.0.0.2")
    tls = brokers("tls", "127.0.0.4", TLS, tls=True)
    n, c, _ = boot(nodes, tmp_path, "hr",
                   static(0, "127.0.0.2", PLAIN, 90) + static(1, "127.0.0.4", TLS, 50, tls=True)
                   + ["set mqtt.profile 1 anchor %s" % tls.fingerprint])
    assert "profile=0" in wait_status(c, "connected")
    assert plain.client_connects() == 1
    assert "hardened" in c.cli("set posture hardened").lower()
    s = wait_status(c, "profile=1")
    assert "tls=verified" in s
    assert plain.client_connects() == 1                            # not reconnected
    assert last(c, 0) == "posture"


# ------------------------------------------------------------------ persistence and control

def test_profiles_persist_across_a_reboot_and_can_be_disabled(nodes, tmp_path, brokers):
    a = brokers("a", "127.0.0.2")
    n, c, _ = boot(nodes, tmp_path, "per", static(0, "127.0.0.2", PLAIN, 90, user="u1", pwd="p1")
                   + static(3, "broker.example", 8884, 20, tls=True))
    wait_status(c, "connected")
    n.stop()
    n.start()
    c = _client(n)
    listing = c.cli("get mqtt.profiles")
    assert "0: static 127.0.0.2:%d tls=off anchor=unset prio=90 user=set pwd=set" % PLAIN in listing, listing
    nxt = c.cli("get mqtt.profile 3")
    assert "3: static broker.example:8884 tls=on" in nxt and "prio=20" in nxt, nxt
    wait_status(c, "connected")
    assert c.cli("set mqtt.enabled 0").startswith("> mqtt.enabled is now 0")
    assert c.cli("get mqtt.status") == "> off"
    time.sleep(2.0)
    count = a.client_connects()
    assert c.cli("set mqtt.enabled 1").startswith("> mqtt.enabled is now 1")
    wait_status(c, "connected")
    assert a.client_connects() == count + 1


def test_a_tls_profile_without_an_anchor_is_encrypted_but_reported_unverified_and_a_wrong_anchor_is_refused(nodes, tmp_path, brokers):
    plain = brokers("plain", "127.0.0.2")
    t = brokers("t", "127.0.0.4", TLS, tls=True)
    # profile 2 pins the wrong certificate: refused; profile 1 pins nothing: used, but not verified
    n, c, _ = boot(nodes, tmp_path, "unv", static(0, "127.0.0.2", PLAIN, 10)
                   + static(2, "127.0.0.4", TLS, 90, tls=True, anchor="11" * 32) + static(1, "127.0.0.4", TLS, 50, tls=True))
    s = wait_status(c, "connected")
    assert "profile=1" in s and "tls=unverified" in s, s
    assert last(c, 2) == "pin"
    assert t.client_connects() == 1 and plain.client_connects() == 0
    payload = wait_until(lambda: t.retained_status(2), "the retained status")
    assert json.loads(payload) == {"online": True, "profile": 1, "tls": "unverified"}, payload


def test_a_discovery_profile_is_tls_even_if_the_slot_was_plain_before(nodes, tmp_path, brokers):
    t = brokers("t", "127.0.0.4", TLS, tls=True)
    menv = MqttEnv(str(tmp_path / "d7.mqtt"))
    menv.advertise("alpha", "127.0.0.4", TLS)
    cmds = static(0, "127.0.0.4", TLS, 90) + ["set mqtt.profile 0 type discovery", "set mqtt.profile 0 site alpha",
                                              "set mqtt.profile 0 anchor %s" % t.fingerprint]
    n, c, _ = boot(nodes, tmp_path, "d7", cmds, menv=menv)       # tls was switched off on the static profile first
    assert "tls=verified" in wait_status(c, "connected")


def test_hardened_keeps_the_link_for_mqtt_but_never_serves_the_tcp_port(nodes, tmp_path, brokers):
    t = brokers("t", "127.0.0.4", TLS, tls=True)
    wenv = WifiEnv(str(tmp_path / "hm.wifi"))
    wenv.set("sim")
    menv = MqttEnv(str(tmp_path / "hm.mqtt"))
    # profiles and posture are set over USB (companion_wifi_usb), then the TCP-class build starts on the same directory
    u = nodes("companion_wifi_usb", "hm", wifi=wenv, mqtt=menv, seed=3, rtc=RTC).start()
    c = _client(u)
    for cmd in ["set wifi.profile 0 ssid sim"] + static(0, "127.0.0.4", TLS, 90, tls=True, anchor=t.fingerprint):
        assert c.cli(cmd).startswith(">"), cmd
    assert "hardened" in c.cli("set posture hardened").lower()
    u.stop()
    h = nodes("companion_wifi", "hm", wifi=wenv, mqtt=menv, seed=3, rtc=RTC).start()
    wait_until(lambda: t.client_connects() == 1, "the MQTT session in Hardened")
    hc = CompanionClient(h)
    with pytest.raises(TimeoutError):                              # the TCP companion port is not served
        hc.request(bytes([harness.CMD_DEVICE_QUERY, 3]), timeout=3)
    # positive control: the same build in Normal serves it
    n2 = nodes("companion_wifi", "hn", wifi=wenv, mqtt=menv, seed=4, rtc=RTC).start()
    assert CompanionClient(n2).device_query()[0] != harness.RESP_ERR


def test_the_commands_work_while_the_controller_is_not_running(nodes, tmp_path):
    # no Wi-Fi profile: Wi-Fi never starts, so neither does MQTT; the profiles can still be edited and listed
    n = nodes("companion_wifi_usb", "idle", wifi=WifiEnv(str(tmp_path / "idle.wifi")), mqtt=MqttEnv(str(tmp_path / "idle.mqtt")),
              seed=3, rtc=RTC).start()
    c = _client(n)
    assert c.cli("get mqtt.status") == "> off"
    assert c.cli("get mqtt.profiles") == "> (no profiles)"
    assert c.cli("set mqtt.profile 0 type static").startswith(">")
    assert c.cli("set mqtt.profile 0 host broker.example").startswith(">")
    assert "last=none" in c.cli("get mqtt.profile 0")
    assert c.cli("get mqtt.status") == "> off"
    assert c.cli("get mqtt.enabled") == "> 1"


# ------------------------------------------------------------------ CA certificate anchors and SNI

@pytest.fixture
def ca_brokers(tmp_path):
    """TLS brokers whose certificate is signed by a CA made here: make(name, ip, ca, dns=None) -> Broker with
    .fingerprint. renew() starts a new broker on the same address with a fresh certificate under the same CA."""
    made = []

    def make(name, ip, ca, dns=None, port=TLS, cert_ip=None):
        cert, key, fp = make_leaf(str(tmp_path), name, ca, dns=dns, ip=cert_ip)
        b = Broker(tmp_path, name, ip, port, cert, key, cafile=ca[0]).start()
        b.fingerprint = fp
        made.append(b)
        return b

    yield make
    for b in made:
        b.stop()


def ca_static(i, host, prio, ca):
    return static(i, host, TLS, prio, tls=True) + ca_commands(i, ca[0])


def test_a_ca_anchor_survives_the_broker_certificate_being_renewed(nodes, tmp_path, ca_brokers):
    ca = make_ca(str(tmp_path), "ca")
    b1 = ca_brokers("b1", "127.0.0.4", ca, dns="broker.test")
    menv = MqttEnv(str(tmp_path / "ren.mqtt"))
    menv.host("broker.test", "127.0.0.4")
    n, c, _ = boot(nodes, tmp_path, "ren", ca_static(0, "broker.test", 90, ca), menv=menv)
    s = wait_status(c, "connected")
    assert "profile=0" in s and "tls=verified" in s, s
    assert "anchor=ca" in c.cli("get mqtt.profile 0")
    assert b1.client_connects() == 1
    assert json.loads(wait_until(lambda: b1.retained_status(2), "the retained status"))["tls"] == "verified"
    b1.stop()                                              # the certificate is renewed: a new key under the same CA
    b2 = ca_brokers("b2", "127.0.0.4", ca, dns="broker.test")
    assert b2.fingerprint != b1.fingerprint                # a leaf pin would have to be reconfigured here
    wait_until(lambda: b2.client_connects() == 1, "the reconnect to the renewed broker", timeout=60)
    s = wait_status(c, "connected")
    assert "profile=0" in s and "tls=verified" in s, s
    assert last(c, 0) in ("none", "dropped"), last(c, 0)   # never refused: the renewal needed no reconfiguration


def test_a_leaf_fingerprint_is_refused_after_the_certificate_is_renewed(nodes, tmp_path, ca_brokers):
    # the contrast with the test above: a fingerprint anchor does not survive a renewal (unchanged behaviour)
    ca = make_ca(str(tmp_path), "ca")
    b1 = ca_brokers("b1", "127.0.0.4", ca, dns="broker.test")
    menv = MqttEnv(str(tmp_path / "fpren.mqtt"))
    menv.host("broker.test", "127.0.0.4")
    n, c, _ = boot(nodes, tmp_path, "fpren", static(0, "broker.test", TLS, 90, tls=True, anchor=b1.fingerprint), menv=menv)
    assert "tls=verified" in wait_status(c, "connected")
    assert "anchor=fingerprint" in c.cli("get mqtt.profile 0")
    b1.stop()
    b2 = ca_brokers("b2", "127.0.0.4", ca, dns="broker.test")
    wait_until(lambda: last(c, 0) == "pin", "the renewed certificate to be refused", timeout=60)
    assert b2.client_connects() == 0


def test_a_ca_anchor_rejects_a_certificate_from_another_ca(nodes, tmp_path, brokers, ca_brokers):
    ca = make_ca(str(tmp_path), "ca")
    other = make_ca(str(tmp_path), "other")
    plain = brokers("plain", "127.0.0.2")
    rogue = ca_brokers("rogue", "127.0.0.4", other, dns="broker.test")      # right name, wrong CA
    menv = MqttEnv(str(tmp_path / "oca.mqtt"))
    menv.host("broker.test", "127.0.0.4")
    n, c, _ = boot(nodes, tmp_path, "oca", ca_static(1, "broker.test", 90, ca) + static(0, "127.0.0.2", PLAIN, 10),
                   menv=menv)
    wait_status(c, "profile=0")                            # fell back to the plain broker
    assert last(c, 1) == "pin"
    assert rogue.client_connects() == 0                    # the broker's own log: no MQTT session, no credentials sent
    assert "open-fail pin" in mqtt_events(n)


def test_a_ca_anchor_rejects_a_wrong_host_name(nodes, tmp_path, brokers, ca_brokers):
    ca = make_ca(str(tmp_path), "ca")
    plain = brokers("plain", "127.0.0.2")
    b = ca_brokers("b", "127.0.0.4", ca, dns="broker.test")                # the certificate names broker.test ...
    menv = MqttEnv(str(tmp_path / "hn.mqtt"))
    menv.host("other.test", "127.0.0.4")                   # ... but the profile reaches it as other.test
    n, c, _ = boot(nodes, tmp_path, "hn", ca_static(1, "other.test", 90, ca) + static(0, "127.0.0.2", PLAIN, 10), menv=menv)
    wait_status(c, "profile=0")
    assert last(c, 1) == "pin"
    assert b.client_connects() == 0
    # the same broker under its right name is accepted by the same CA (the chain was never the problem)
    menv.host("broker.test", "127.0.0.4")
    assert c.cli("set mqtt.profile 1 host broker.test").startswith(">")
    s = wait_status(c, "profile=1")
    assert "tls=verified" in s and b.client_connects() == 1, s


def test_a_ca_anchor_on_an_address_literal_checks_the_chain_only(nodes, tmp_path, ca_brokers):
    ca = make_ca(str(tmp_path), "ca")
    b = ca_brokers("b", "127.0.0.4", ca, dns="not-the-address.test")       # no name that matches an address
    n, c, _ = boot(nodes, tmp_path, "lit", ca_static(0, "127.0.0.4", 90, ca))
    s = wait_status(c, "connected")
    assert "tls=verified" in s and b.client_connects() == 1, s
    assert "name=-" in " ".join(mqtt_events(n))            # no name was presented or checked


def test_a_discovery_ca_anchor_checks_the_chain_and_a_rogue_ca_is_refused(nodes, tmp_path, brokers, ca_brokers):
    ca = make_ca(str(tmp_path), "ca")
    other = make_ca(str(tmp_path), "other")
    plain = brokers("plain", "127.0.0.2")
    rogue = ca_brokers("rogue", "127.0.0.5", other, dns="broker.test")
    real = ca_brokers("real", "127.0.0.4", ca, dns="broker.test")
    menv = MqttEnv(str(tmp_path / "dca.mqtt"))
    menv.advertise("alpha", "127.0.0.5", TLS)              # the rogue answers first with the right site ID
    menv.advertise("alpha", "127.0.0.4", TLS)
    cmds = ["set mqtt.profile 0 type discovery", "set mqtt.profile 0 site alpha", "set mqtt.profile 0 priority 90"]
    n, c, _ = boot(nodes, tmp_path, "dca", cmds + ca_commands(0, ca[0]) + static(1, "127.0.0.2", PLAIN, 10), menv=menv)
    s = wait_status(c, "connected")
    assert "profile=0" in s and "broker=127.0.0.4:%d" % TLS in s and "tls=verified" in s, s
    assert real.client_connects() == 1 and rogue.client_connects() == 0
    assert plain.client_connects() == 0
    assert "anchor=ca" in c.cli("get mqtt.profile 0")


def test_the_server_name_is_sent_with_every_tls_profile_that_has_a_host_name(nodes, tmp_path):
    probe = SniProbe(tmp_path, "127.0.0.6", TLS).start()
    try:
        menv = MqttEnv(str(tmp_path / "sni.mqtt"))
        menv.host("sni.test", "127.0.0.6")
        # unanchored, and fingerprint-anchored (which must still pass the pin before anything else): the name goes out
        cmds = static(0, "sni.test", TLS, 90, tls=True)
        n, c, _ = boot(nodes, tmp_path, "sni", cmds, menv=menv)
        wait_until(lambda: "sni.test" in probe.names, "a ClientHello with the server name")
        c.cli("set mqtt.profile 0 anchor %s" % probe.fingerprint)
        before = len(probe.names)
        wait_until(lambda: len(probe.names) > before and probe.names[-1] == "sni.test", "the pinned profile's ClientHello")
        # an address literal has no name to send
        c.cli("set mqtt.profile 0 host 127.0.0.6")
        before_names = len(probe.names)
        # a client that sends no server name reaches the callback with None; an attempt still in flight from before the
        # edit may add one more "sni.test", so wait for the None rather than counting entries
        wait_until(lambda: None in probe.names[before_names:], "a connection to the address literal without a server name")
        assert "127.0.0.6" not in probe.names               # and never the address text posing as a name
    finally:
        probe.stop()


def test_a_fingerprint_anchor_does_not_check_the_host_name(nodes, tmp_path, ca_brokers):
    ca = make_ca(str(tmp_path), "ca")
    b = ca_brokers("b", "127.0.0.4", ca, dns="broker.test")
    menv = MqttEnv(str(tmp_path / "fpn.mqtt"))
    menv.host("another.test", "127.0.0.4")
    n, c, _ = boot(nodes, tmp_path, "fpn", static(0, "another.test", TLS, 90, tls=True, anchor=b.fingerprint), menv=menv)
    assert "tls=verified" in wait_status(c, "connected")   # a leaf pin is the trust decision; unchanged
    assert b.client_connects() == 1


def test_the_largest_practical_ca_fits_and_is_accepted(nodes, tmp_path, ca_brokers):
    ca = make_ca(str(tmp_path), "bigca", bits=4096)         # an RSA-4096 root, like the large public roots
    b = ca_brokers("b", "127.0.0.4", ca, dns="broker.test")
    menv = MqttEnv(str(tmp_path / "big.mqtt"))
    menv.host("broker.test", "127.0.0.4")
    cmds = ca_commands(0, ca[0])
    print("RSA-4096 CA: %d commands" % len(cmds))
    n, c, _ = boot(nodes, tmp_path, "big", static(0, "broker.test", TLS, 90, tls=True) + cmds, menv=menv)
    assert "tls=verified" in wait_status(c, "connected")
    assert b.client_connects() == 1


def test_a_damaged_ca_is_refused_at_ca_end_and_the_profile_keeps_its_anchor(nodes, tmp_path, ca_brokers):
    ca = make_ca(str(tmp_path), "ca")
    b = ca_brokers("b", "127.0.0.4", ca, dns="broker.test")
    menv = MqttEnv(str(tmp_path / "dmg.mqtt"))
    menv.host("broker.test", "127.0.0.4")
    cmds = ca_commands(0, ca[0])
    damaged = cmds[:-3] + [cmds[-1]]                        # two pieces lost before ca end
    n, c, _ = boot(nodes, tmp_path, "dmg", static(0, "broker.test", TLS, 90, tls=True, anchor=b.fingerprint), menv=menv)
    for cmd in damaged[:-1]:
        assert c.cli(cmd).startswith(">")
    assert c.cli(damaged[-1]).startswith("ERR: ca is not one complete")
    assert "anchor=fingerprint" in c.cli("get mqtt.profile 0")
    assert "tls=verified" in wait_status(c, "connected")


@pytest.mark.parametrize("damage", ["flipped", "first_piece_missing"])
def test_a_damaged_stored_ca_fails_closed_and_never_connects_unverified(nodes, tmp_path, ca_brokers, damage):
    ca = make_ca(str(tmp_path), "ca")
    b = ca_brokers("b", "127.0.0.4", ca, dns="broker.test")
    menv = MqttEnv(str(tmp_path / ("dmgp-%s.mqtt" % damage)))
    menv.host("broker.test", "127.0.0.4")
    name = "dmgp" + damage[:3]
    n, c, _ = boot(nodes, tmp_path, name, ca_static(0, "broker.test", 90, ca), menv=menv)
    assert "tls=verified" in wait_status(c, "connected")
    sessions, tcp = b.client_connects(), b.connections()
    n.stop()
    path = os.path.join(n.dir, "prefs.json")
    text = open(path).read()
    if damage == "flipped":
        k = text.index("c001:\"") + 6
        text = text[:k] + "*" + text[k + 1:]            # a character that is not base64
    else:
        k = text.index("c000:")
        e = text.index('"', text.index('"', k) + 1)
        text = text[:k] + text[e + 2:]                  # the first piece is gone
    open(path, "w").write(text)
    n.start()
    c = _client(n)
    wait_until(lambda: last(c, 0) == "pin", "the damaged anchor to be refused")
    time.sleep(1.0)
    s = c.cli("get mqtt.status")
    assert "tls=unverified" not in s and "connected" not in s, s
    assert "anchor=ca" in c.cli("get mqtt.profile 0")        # still a pinned profile, not an unanchored one
    assert b.client_connects() == sessions                    # the broker's own log: no new session ...
    assert b.connections() == tcp                             # ... and not even a TCP connection: closed before the network
    assert any("mqtt-session" in e for e in harness.audit_entries(c.cli)), "no refusal in the audit log"
