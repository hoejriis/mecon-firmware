"""Wi-Fi and the broker on the simulated Repeater and Room server (docs/MQTT.md, docs/WIFI.md): the same settings, the same
device interface and the same trust classes as a Companion, on the same controllers, against real Mosquitto brokers.
Native repeating does not depend on any of it. The console is the cable, so what a broker session may do is read here
against what the console sees."""
import os
import sys
import time

import pytest

from sim.ether import Ether  # noqa: F401
from sim.harness import CompanionClient, MqttEnv, WifiEnv, files_containing, mqtt_events

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_schema  # noqa: E402
from test_companion_mqtt import brokers, _tools  # noqa: F401,E402  (fixtures)
from test_companion_mqtt import PLAIN, TLS, static  # noqa: E402
from sim.mqtt_broker import Broker, ca_commands, make_ca, make_leaf  # noqa: E402
from test_deployment_authority import provision_args, wrap  # noqa: E402
from test_deployment_authority_server import RTC, Srv  # noqa: E402

KINDS = ["repeater_wifi", "room_server_wifi"]
ROLE = {"repeater_wifi": "repeater", "room_server_wifi": "room"}
pytestmark = pytest.mark.timescale(10)


def tls_broker(brokers, name="v", ip="127.0.0.2"):
    return brokers(name, ip, TLS, tls=True)


def verified(b, i=0, prio=50):
    return static(i, b.ip, b.port, prio, tls=True, anchor=b.fingerprint)


def boot(nodes, tmp_path, kind, name, commands, seed=41, hardened=False, **kw):
    """A server with Wi-Fi profile 0 = the simulated access point and these broker commands, rebooted so the links run."""
    wenv = WifiEnv(str(tmp_path / (name + ".wifi")))
    wenv.set("sim")
    n = nodes(kind, name, seed=seed, rtc=RTC, wifi=wenv, mqtt=MqttEnv(str(tmp_path / (name + ".mqtt"))), **kw).start()
    s = Srv(n)
    assert "ssid is now" in s.cli.command("set wifi.profile 0 ssid sim")
    for cmd in commands:
        reply = s.cli.command(cmd)
        assert reply.startswith(">"), (cmd, reply)
    if hardened:
        assert "hardened" in s.cli.command("set posture hardened").lower()
    s.reboot()
    return s


def up(s, timeout=40):
    end = time.time() + timeout
    st = ""
    while time.time() < end:
        st = s.cli.command("get mqtt.status")
        if st.startswith("> connected"):
            time.sleep(2.0)   # past the settle time after the subscription
            return st
        time.sleep(0.3)
    raise AssertionError("no session: " + st)


def dev_of(s):
    return s.i.request("hello")["result"]["device_id"]


def node_id(s):
    return s.node_key[:8]


def schema_keys(s):
    keys, cursor = [], 0
    while cursor is not None:
        page = s.i.request("config.schema", {"cursor": cursor})["result"]
        keys += [k["key"] for k in page["keys"]]
        cursor = page["next"]
    return keys


# ------------------------------------------------------------------ the same operations over the broker

@pytest.mark.parametrize("kind", KINDS)
def test_a_server_answers_the_device_interface_over_a_verified_broker(nodes, tmp_path, brokers, kind):
    b = tls_broker(brokers)
    s = boot(nodes, tmp_path, kind, "ifc", verified(b))
    assert "tls=verified" in up(s)
    dev = dev_of(s)
    assert b.retained_on("mecon/%s/status" % dev) is not None
    r = b.iface_request(dev, {"v": 1, "id": "m1", "op": "hello"})
    assert r["status"] == "ok" and r["result"]["device_id"] == dev and r["result"]["role"] == ROLE[kind], r
    assert not mecon_schema.check(r["result"], mecon_schema.load("hello.result.json"))
    r = b.iface_request(dev, {"v": 1, "id": "m2", "op": "config.apply", "args": {"set": {"name": "ViaBroker"}}})
    assert r["status"] == "ok" and r["result"]["complete"], r
    assert s.name() == "> ViaBroker"
    # the same id over either transport runs once
    again = s.i.request("config.apply", {"set": {"name": "ViaBroker"}}, rid="m2")
    assert again["result"] == r["result"], (r, again)


def test_the_settings_survive_a_reboot_and_the_session_comes_back(nodes, tmp_path, brokers):
    b = tls_broker(brokers)
    s = boot(nodes, tmp_path, "repeater_wifi", "per", verified(b))
    up(s)
    s.reboot()
    up(s)
    assert "tls=verified" in s.cli.command("get mqtt.status")


def test_no_profile_starts_no_link_and_the_build_without_wifi_has_no_settings(nodes, tmp_path, brokers):
    n = nodes("repeater_wifi", "nop", seed=42, rtc=RTC, wifi=WifiEnv(str(tmp_path / "nop.wifi")),
              mqtt=MqttEnv(str(tmp_path / "nop.mqtt"))).start()
    s = Srv(n)
    assert not s.cli.command("get mqtt.status").startswith("> connected")
    assert mqtt_events(n) == []
    plain = Srv(nodes("repeater", "nowifi", seed=43, rtc=RTC).start())
    assert "Unknown" in plain.cli.command("get wifi.status") or plain.cli.command("get wifi.status").startswith("??")
    keys = schema_keys(plain)
    assert "owner.info" in keys and not [k for k in keys if k.startswith(("wifi.", "mqtt."))], keys
    caps = plain.i.request("hello")["result"]["capabilities"]
    assert "sub:connectivity" not in caps and "connectivity" not in str(caps), caps


def test_the_schema_of_a_wifi_build_lists_the_link_settings_but_no_mode(nodes, tmp_path, brokers):
    s = boot(nodes, tmp_path, "repeater_wifi", "sch", [])
    keys = schema_keys(s)
    assert {"wifi.enabled", "wifi.profile.{n}.pwd", "mqtt.enabled", "mqtt.profile.{n}.host"} <= set(keys), keys
    assert "connectivity.mode" not in keys, "a server has no BLE to choose between"


# ------------------------------------------------------------------ the trust classes

def test_a_plain_session_reads_nothing_and_changes_nothing(nodes, tmp_path, brokers):
    b = brokers("p", "127.0.0.2", PLAIN)
    s = boot(nodes, tmp_path, "repeater_wifi", "pln", static(0, b.ip, b.port, 50))
    assert "tls=plain" in up(s) or "tls=off" in s.cli.command("get mqtt.status")
    dev = dev_of(s)
    before = s.name()
    r = b.iface_request(dev, {"v": 1, "id": "p1", "op": "config.apply", "args": {"set": {"name": "NotThis"}}})
    assert r is None or r["status"] == "refused", r
    assert s.name() == before
    r = b.iface_request(dev, {"v": 1, "id": "p2", "op": "status.get"})
    assert r is None or r["status"] == "refused", r
    r = b.cli_request(node_id(s), "set name NotThis")
    assert r is None or r["status"] == "refused", r
    assert s.name() == before


def test_a_verified_session_never_gets_what_the_table_keeps_from_it(nodes, tmp_path, brokers):
    """Each line runs as the session, not the console that a server's handlers otherwise assume: the answer is the table's."""
    b = tls_broker(brokers)
    s = boot(nodes, tmp_path, "repeater_wifi", "tab", verified(b))
    up(s)
    nid = node_id(s)
    assert b.cli_request(nid, "get name")["status"] == "ok"
    assert b.cli_request(nid, "set name FromCli")["status"] == "ok"
    assert s.name() == "> FromCli"
    r = b.cli_request(nid, "no such command")
    assert r["status"] == "unknown", r
    for line in ("get prv.key", "set prv.key " + "00" * 64, "set posture hardened", "set posture normal confirm",
                 "get audit", "erase", "set mqtt.profile 0 manage on"):
        r = b.cli_request(nid, line, req_id="x")
        assert r["status"] == "refused", (line, r)
    assert "normal" in s.cli.command("get posture").lower()
    assert s.name() == "> FromCli", "the file system is still there: erase never ran"
    audit = s.cli.command("get audit")
    assert "mqtt" in audit.lower(), "refusals are audited as the broker, not as the cable: " + audit


def test_a_config_write_of_a_refused_class_is_refused_before_anything_is_written(nodes, tmp_path, brokers):
    b = tls_broker(brokers)
    s = boot(nodes, tmp_path, "repeater_wifi", "ref", verified(b))
    up(s)
    dev = dev_of(s)
    r = b.iface_request(dev, {"v": 1, "id": "r1", "op": "deployment.provision", "args": provision_args(s.node_key)})
    assert r["status"] == "refused", r
    assert s.i.request("deployment.get")["result"].get("enrolled") in (False, None)


def test_write_only_settings_never_come_back_over_any_transport(nodes, tmp_path, brokers):
    b = tls_broker(brokers)
    s = boot(nodes, tmp_path, "repeater_wifi", "sec", verified(b) + ["set mqtt.profile 0 user alice", "set mqtt.profile 0 pwd hunter2secret",
                                                                     "set wifi.profile 1 ssid other", "set wifi.profile 1 pwd wifisecret77"])
    # the broker has no such user configured: it allows anonymous ones, so the session comes up all the same
    up(s)
    dev = dev_of(s)
    for text in (s.cli.command("get mqtt.profile 0"), s.cli.command("get wifi.profile 1"), s.cli.command("get wifi.pwd"),
                 str(b.iface_request(dev, {"v": 1, "id": "g1", "op": "config.get",
                                           "args": {"keys": ["mqtt.profile.0.pwd", "wifi.profile.1.pwd"]}})),
                 str(b.cli_request(node_id(s), "get wifi.profile 1"))):
        assert "hunter2secret" not in text and "wifisecret77" not in text, text


def test_a_setting_applied_over_the_broker_needs_a_confirmation_and_comes_back_without_one(nodes, tmp_path, brokers):
    """A broker's own change can cut its own path: confirm-or-revert, the stored password back as it was, the job told on the broker."""
    b = tls_broker(brokers)
    s = boot(nodes, tmp_path, "repeater_wifi", "rev", verified(b) + static(1, "127.0.0.9", b.port, 1, tls=True, anchor=b.fingerprint, pwd="firstsecret"))
    up(s)
    dev = dev_of(s)
    r = b.iface_request(dev, {"v": 1, "id": "c1", "op": "config.apply", "args": {"set": {"mqtt.profile.1.priority": 7}}})
    assert r["status"] == "invalid" and r["error"] == "confirm-required", r
    r = b.iface_request(dev, {"v": 1, "id": "c2", "op": "config.apply",
                              "args": {"set": {"mqtt.profile.1.pwd": "secondsecret"}, "confirm": {"within_s": 30}}})
    assert r["status"] == "ok" and r["result"]["state"] == "awaiting-confirm", r
    job = r["result"]["job"]
    evs = b.next_message("mecon/%s/ev/job" % dev, timeout=40, count=1)   # 30 s at ten times speed
    assert evs and evs[0]["data"]["job"] == job, evs
    end = time.time() + 15
    st = None
    while time.time() < end:
        st = s.i.request("job.get", {"job": job})["result"]["state"]
        if st != "awaiting-confirm":
            break
        time.sleep(0.5)
    assert st == "reverted", st
    # the stored password is the old one again, and the new one is nowhere on the file system
    assert files_containing(s.n, "firstsecret"), "the old password is back"
    assert not files_containing(s.n, "secondsecret"), "the tentative password is gone"


# ------------------------------------------------------------------ a Deployment, over the broker

def test_signed_logs_over_a_verified_broker_on_a_repeater(nodes, tmp_path, brokers):
    """Plain sub.start refused, signed with no nonce refused, signed with this session's nonce allowed, and an entry arrives."""
    b = tls_broker(brokers)
    s = boot(nodes, tmp_path, "repeater_wifi", "log", verified(b))
    up(s)
    dev = dev_of(s)
    assert s.provision()["status"] == "ok"
    args = {"stream": "logs", "lifetime_s": 120}
    r = b.iface_request(dev, {"v": 1, "id": "l1", "op": "sub.start", "args": args})
    assert r["status"] == "refused", r
    r = b.iface_request(dev, wrap(s.node_key, "sub.start", args, job=11))
    assert r["status"] == "refused", r
    assert r.get("error") == "bad_channel_nonce", r
    h = b.iface_request(dev, {"v": 1, "id": "h1", "op": "hello"})
    nonce = h["result"]["channel_nonce"]
    assert nonce != s.nonce(), "the console and the session are two links, with a nonce each"
    r = b.iface_request(dev, wrap(s.node_key, "sub.start", args, job=12, nonce=nonce))
    assert r["status"] == "ok", r
    # an audited act: raised on the cable, where it is allowed
    got = []
    import threading
    t = threading.Thread(target=lambda: got.extend(b.next_message("mecon/%s/ev/logs" % dev, timeout=25, count=1)))
    t.start()
    time.sleep(1.5)
    s.cli.command("set posture hardened")
    t.join()
    assert got and got[0]["ev"] == "logs", got


def test_an_enrolled_server_takes_a_signed_change_over_the_broker_and_refuses_an_unsigned_one(nodes, tmp_path, brokers):
    b = tls_broker(brokers)
    s = boot(nodes, tmp_path, "repeater_wifi", "enr", verified(b))
    up(s)
    dev = dev_of(s)
    assert s.provision()["status"] == "ok"
    before = s.name()
    r = b.iface_request(dev, {"v": 1, "id": "u1", "op": "config.apply", "args": {"set": {"name": "Unsigned"}}})
    assert r["status"] == "refused", r
    assert s.name() == before
    r = b.iface_request(dev, wrap(s.node_key, "config.apply", {"set": {"name": "Signed"}}, job=21))
    assert r["status"] == "ok" and r["result"]["complete"], r
    assert s.name() == "> Signed"


def test_the_deployment_broker_roles_order_the_brokers_on_a_server(nodes, tmp_path, brokers):
    a = tls_broker(brokers, "a", "127.0.0.2")
    c = tls_broker(brokers, "c", "127.0.0.3")
    s = boot(nodes, tmp_path, "repeater_wifi", "rol", verified(a, 0, 90) + verified(c, 1, 10))
    assert "broker=127.0.0.2" in up(s)
    args = provision_args(s.node_key)
    args["deployment"]["broker_roles"] = [{"profile": 1, "role": "primary"}]
    r = s.i.request("deployment.provision", args)
    assert r["status"] == "ok" and r["result"]["broker_roles"] == [{"profile": 1, "role": "primary"}], r
    s.reboot()
    assert "broker=127.0.0.3" in up(s)


# ------------------------------------------------------------------ Hardened

def test_hardened_keeps_a_verified_broker_and_starts_no_wifi_for_a_plain_one(nodes, tmp_path, brokers):
    p = brokers("p", "127.0.0.2", PLAIN)
    s = boot(nodes, tmp_path, "repeater_wifi", "hp", static(0, p.ip, p.port, 50), hardened=True)
    time.sleep(3)
    assert not s.cli.command("get mqtt.status").startswith("> connected")
    assert p.connections() == [] or not p.client_connects()
    assert "connected" not in s.cli.command("get wifi.status").replace("not connected", "").replace("disconnected", "")
    assert "net" in s.cli.command("get audit").lower()
    v = tls_broker(brokers, "v", "127.0.0.3")
    s2 = boot(nodes, tmp_path, "repeater_wifi", "hv", verified(v), hardened=True, seed=44)
    up(s2)
    dev = dev_of(s2)
    assert v.iface_request(dev, {"v": 1, "id": "h1", "op": "status.get"})["status"] == "ok"
    r = v.iface_request(dev, {"v": 1, "id": "h2", "op": "config.apply", "args": {"set": {"name": "NotInHardened"}}})
    assert r["status"] == "refused", r


# ------------------------------------------------------------------ telemetry

def test_connectivity_and_health_reach_the_broker_and_a_subscription_lapses_with_the_session(nodes, tmp_path, brokers):
    b = tls_broker(brokers)
    s = boot(nodes, tmp_path, "repeater_wifi", "tel", verified(b))
    up(s)
    dev = dev_of(s)
    r = b.iface_request(dev, {"v": 1, "id": "t1", "op": "sub.start", "args": {"stream": "connectivity", "lifetime_s": 120}})
    assert r["status"] == "ok", r
    ev = b.next_message("mecon/%s/ev/connectivity" % dev, timeout=20, count=1)
    assert ev and ev[0]["data"]["wifi"]["state"] == "connected" and ev[0]["data"]["mqtt"]["state"] == "connected", ev
    assert ev[0]["data"]["mqtt"]["tls"] == "verified", ev
    # the console is not subscribed: it stays quiet, the two transports hold a set each
    assert not [e for e in s.i.events if e.get("ev") == "connectivity"], s.i.events
    nonce = b.iface_request(dev, {"v": 1, "id": "t2", "op": "hello"})["result"].get("channel_nonce")
    assert nonce is None   # not enrolled
    r = s.i.request("sub.start", {"stream": "health", "period_s": 5, "lifetime_s": 120})
    assert r["status"] == "ok"
    assert b.next_message("mecon/%s/ev/health" % dev, timeout=8, count=1) == [], "the console's subscription is the console's"
    r = b.iface_request(dev, {"v": 1, "id": "t3", "op": "sub.start", "args": {"stream": "health", "period_s": 5, "lifetime_s": 120}})
    assert r["status"] == "ok", r
    assert b.next_message("mecon/%s/ev/health" % dev, timeout=20, count=1)
    b.stop()
    end = time.time() + 30
    while time.time() < end and s.cli.command("get mqtt.status").startswith("> connected"):
        time.sleep(0.3)
    b.start()
    up(s)
    assert b.next_message("mecon/%s/ev/health" % dev, timeout=8, count=1) == [], "a subscription made over the broker lapses with the session"


def test_a_broker_event_goes_out_while_a_console_line_is_being_typed(nodes, tmp_path, brokers):
    b = tls_broker(brokers)
    s = boot(nodes, tmp_path, "repeater_wifi", "typ", verified(b))
    up(s)
    dev = dev_of(s)
    r = b.iface_request(dev, {"v": 1, "id": "t1", "op": "sub.start", "args": {"stream": "health", "period_s": 5, "lifetime_s": 120}})
    assert r["status"] == "ok", r
    s.cli.sock.sendall(b"mecon> {\"v\":1,")   # a request left half typed
    assert b.next_message("mecon/%s/ev/health" % dev, timeout=20, count=1)
    s.cli.sock.sendall(b"\"id\":\"late\",\"op\":\"hello\"}\r\n")
    assert s.i.wait_result("late", 8)["status"] == "ok"


# ------------------------------------------------------------------ repeating

def test_repeating_goes_on_with_a_session_up_and_with_the_broker_gone(nodes, ether, tmp_path, brokers):
    """A and B hear only the repeater: a message arrives only if it forwards, with or without a broker."""
    b = tls_broker(brokers)
    s = boot(nodes, tmp_path, "repeater_wifi", "fr", verified(b), ether_path=ether.path)
    up(s)
    na = nodes("companion_standalone", "fa", ether_path=ether.path, seed=88, rtc=RTC).start()
    nb = nodes("companion_standalone", "fb", ether_path=ether.path, seed=89, rtc=RTC).start()
    x, y = CompanionClient(na), CompanionClient(nb)
    for c in (x, y):
        c.device_query()
        c.app_start()
    assert ether.wait_for_nodes(["fr", "fa", "fb"])
    ether.set_link("fa", "fr")
    ether.set_link("fb", "fr")
    assert x.add_contact(y.pub_key, "y")[0] == 0
    assert y.add_contact(x.pub_key, "x")[0] == 0
    x.send_direct(y.pub_key, "with a session", RTC + 5)
    got = y.sync_messages(timeout=75, want=1)
    assert [m["text"] for m in got] == ["with a session"], got
    assert [t for t in ether.tx_log if t.src == "fr"], "the repeater carried it"
    b.stop()
    end = time.time() + 30
    while time.time() < end and s.cli.command("get mqtt.status").startswith("> connected"):
        time.sleep(0.3)
    x.send_direct(y.pub_key, "with the broker gone", RTC + 6)
    got = y.sync_messages(timeout=75, want=1)
    assert [m["text"] for m in got] == ["with the broker gone"], got


def test_the_clock_decides_whether_a_servers_anchor_judges_dates(nodes, tmp_path):
    """As on a Companion: a server whose clock no real source has set cannot judge a certificate's dates."""
    ca = make_ca(str(tmp_path), "ca")
    cert, key, _ = make_leaf(str(tmp_path), "old", ca, ip="127.0.0.2", days=-1)
    b = Broker(tmp_path, "old", "127.0.0.2", TLS, cert, key, cafile=ca[0]).start()
    try:
        cmds = static(0, b.ip, b.port, 50, tls=True) + ca_commands(0, ca[0])
        s = boot(nodes, tmp_path, "repeater_wifi", "dunset", cmds, rtc_unset=True)
        assert "tls=verified" in up(s), "a clock never set cannot judge the date"
        s = boot(nodes, tmp_path, "repeater_wifi", "dset", cmds, seed=42)
        time.sleep(8.0)
        assert not s.cli.command("get mqtt.status").startswith("> connected"), "a set clock refuses it"
        assert "last=pin" in s.cli.command("get mqtt.profile 0")
    finally:
        b.stop()
