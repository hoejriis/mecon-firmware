"""The unified command-line engine on a simulated Companion (docs/CLI.md).

One node, three ways in: the USB cable (CMD_RUN_CLI_COMMAND), the LoRa remote CLI of a flagged contact, and the
command line over MQTT against a real Mosquitto. The engine's table decides who may run which class of command, so
the same line gives the same reply where all of them may run it and the defined refusal where they may not; every
refusal is an audit entry. MQTT management is accepted only on a TLS session whose broker certificate matched its
pinned anchor, and in Hardened it is read-only unless the broker profile is flagged for management.
"""
import json
import os
import time

import pytest

from sim import harness
from sim.harness import CompanionClient, MqttEnv, WifiEnv, audit_entries, files_containing
from sim.mqtt_broker import Broker, make_cert
import sim.mqtt_broker as mqtt_broker

RTC = 1760000000
PLAIN, TLS = 18830, 18883
REMOTE_CLI_FLAG = 0x10
pytestmark = pytest.mark.timescale(10)


@pytest.fixture(autouse=True)
def _tools():
    mqtt_broker.require_tools()


@pytest.fixture
def brokers(tmp_path):
    made = []

    def make(name, ip, port=PLAIN, tls=False):
        cert = key = fp = None
        if tls:
            cert, key, fp = make_cert(str(tmp_path), name, ip)
        b = Broker(tmp_path, name, ip, port, cert, key).start()
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


def wait_status(c, text, timeout=30.0):
    end = time.time() + timeout
    s = ""
    while time.time() < end:
        s = c.cli("get mqtt.status")
        if text in s:
            return s
        time.sleep(0.1)
    raise AssertionError("never saw %r in mqtt.status, last: %r" % (text, s))


def profile(host, port, tls=True, anchor=None, manage=False, user=None, pwd=None):
    cmds = ["set mqtt.profile 0 type static", "set mqtt.profile 0 host %s" % host, "set mqtt.profile 0 port %d" % port,
            "set mqtt.profile 0 tls %s" % ("on" if tls else "off")]
    if anchor:
        cmds.append("set mqtt.profile 0 anchor %s" % anchor)
    if manage:
        cmds.append("set mqtt.profile 0 manage on")
    if user:
        cmds.append("set mqtt.profile 0 user %s" % user)
    if pwd:
        cmds.append("set mqtt.profile 0 pwd %s" % pwd)
    return cmds


def boot(nodes, tmp_path, name, commands, ether=None, seed=3):
    """A Companion with the given MQTT profile commands, rebooted so the Wi-Fi and MQTT controllers run.
    Returns (node, client, node id as the MQTT topics spell it)."""
    wenv = WifiEnv(str(tmp_path / (name + ".wifi")))
    wenv.set("sim")
    menv = MqttEnv(str(tmp_path / (name + ".mqtt")))
    kw = {"ether_path": ether.path} if ether else {}
    n = nodes("companion_wifi_usb", name, wifi=wenv, mqtt=menv, seed=seed, rtc=RTC, **kw).start()
    c = _client(n)
    assert "ssid is now" in c.cli("set wifi.profile 0 ssid sim")
    for cmd in commands:
        reply = c.cli(cmd)
        assert reply.startswith(">"), (cmd, reply)
    c._send(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"reboot")
    assert n.wait_exit(10) == 75
    n.start()
    c = _client(n)
    return n, c, c.pub_key[:4].hex()


def audit(c):
    return audit_entries(c.cli)


def mq(broker, node_id, command, **kw):
    r = broker.cli_request(node_id, command, **kw)
    assert r is not None, "no response to %r" % command
    return r


# ------------------------------------------------------------------ the same command, three transports

def _lora_pair(ether, nodes, v_node):
    remote = nodes("companion", "lr", seed=32, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes([v_node.name, "lr"])
    ether.set_link(v_node.name, "lr")
    r = CompanionClient(remote)
    r.device_query()
    r.app_start()
    return r


def _remote(r, v, text):
    assert r.send_cli_command(v.pub_key, text)[0] == 6
    msgs = r.sync_messages(timeout=60, want=1)
    assert msgs, "no reply to remote command %r" % text
    return msgs[0]["text"]


def test_the_same_command_over_usb_lora_and_mqtt(ether, nodes, tmp_path, brokers):
    t = brokers("t", "127.0.0.2", TLS, tls=True)
    n, c, nid = boot(nodes, tmp_path, "uni", profile("127.0.0.2", TLS, anchor=t.fingerprint), ether=ether)
    r = _lora_pair(ether, nodes, n)
    assert c.add_contact(r.pub_key, "remote", flags=REMOTE_CLI_FLAG)[0] == 0
    assert r.add_contact(c.pub_key, "victim")[0] == 0
    assert "tls=verified" in wait_status(c, "connected")
    assert t.retained_status(5)

    # allowed for all three: identical replies
    for line in ("get name", "ver", "board", "get posture", "get wifi.pwd", "get radio", "get mqtt.enabled"):
        usb = c.cli(line)
        lora = _remote(r, c, line)
        resp = mq(t, nid, line)
        assert resp["status"] == "ok" and resp["ok"] is True and resp["id"] == "t1", (line, resp)
        assert usb == lora == resp["reply"], (line, usb, lora, resp)
    assert c.cli("get wifi.pwd") == "> wifi.pwd is write-only"

    # MECON's own commands: how each caller is seen, and a dry run of the table
    assert c.cli("get mecon.cli.who") == "> transport=usb auth=cable remote=0 posture=normal principal=cable"
    assert _remote(r, c, "get mecon.cli.who") == "> transport=lora auth=contact-flag remote=1 posture=normal principal=remote-cli"
    assert mq(t, nid, "get mecon.cli.who")["reply"] == "> transport=mqtt auth=tls-verified remote=1 posture=normal principal=mqtt"
    assert c.cli("get mecon.cli.check set name x") == "> class=write allowed"
    assert _remote(r, c, "get mecon.cli.check set name x") == "> class=write remote-limited"
    assert mq(t, nid, "get mecon.cli.check set name x")["reply"] == "> class=write allowed"
    assert _remote(r, c, "get mecon.cli.check get audit") == "> class=posture not-permitted"

    # a settings change: the cable and a verified broker may, a flagged contact may not
    assert "not permitted remotely" in _remote(r, c, "set name ViaLora")
    assert c.cli("get name") != "> ViaLora"
    resp = mq(t, nid, "set name ViaMqtt", req_id="w1")
    assert (resp["status"], resp["reply"], resp["id"]) == ("ok", "OK", "w1"), resp
    assert c.cli("get name") == "> ViaMqtt"
    assert c.cli("set name ViaUsb") == "OK"

    # the audit log, the posture and the broker-management flag are local matters: refused remotely, the same way
    for line in ("get audit", "set posture hardened", "set connectivity.mode wifi", "set mqtt.profile 0 manage on"):
        lora = _remote(r, c, line)
        resp = mq(t, nid, line)
        assert lora == "ERR: not permitted", (line, lora)
        assert resp["status"] == "refused" and resp["ok"] is False and resp["reply"] == lora, (line, resp)
    assert c.cli("get posture") == "> normal"
    assert "manage=on" not in c.cli("get mqtt.profile 0")
    log = audit(c)
    for want in ("refused remote-cli audit x1", "refused mqtt audit x1", "refused remote-cli posture x1",
                 "refused mqtt posture x1", "refused remote-cli connectivity-mode x1",
                 "refused mqtt connectivity-mode x1", "refused remote-cli mgmt-flag x1", "refused mqtt mgmt-flag x1",
                 "refused remote-cli remote-write x1"):
        assert any(ln.endswith(want) for ln in log), (want, log)


def test_mqtt_cannot_do_what_only_a_local_caller_may(nodes, tmp_path, brokers):
    t = brokers("t", "127.0.0.2", TLS, tls=True)
    n, c, nid = boot(nodes, tmp_path, "loc", profile("127.0.0.2", TLS, anchor=t.fingerprint))
    wait_status(c, "tls=verified")
    for line in ("get audit 0", "set posture normal confirm", "set posture   hardened", "set posture", "set mqtt.profile 0 manage on"):
        resp = mq(t, nid, line)
        assert (resp["status"], resp["reply"]) == ("refused", "ERR: not permitted"), (line, resp)
    assert c.cli("get posture") == "> normal"
    # an unknown command is not a refusal, and still gets an answer
    resp = mq(t, nid, "nonsense command")
    assert resp["status"] == "unknown" and resp["ok"] is False, resp
    log = audit(c)
    assert any(ln.endswith("refused mqtt posture x3") for ln in log), log
    assert any(ln.endswith("refused mqtt audit x1") for ln in log), log
    assert any(ln.endswith("refused mqtt mgmt-flag x1") for ln in log), log
    assert "manage=on" not in c.cli("get mqtt.profile 0")


# ------------------------------------------------------------------ session trust

def test_mqtt_is_refused_on_plain_and_on_unverified_tls_and_audited(nodes, tmp_path, brokers):
    plain = brokers("plain", "127.0.0.2")
    unverified = brokers("unv", "127.0.0.4", TLS, tls=True)
    for name, broker, cmds in (("pl", plain, profile("127.0.0.2", PLAIN, tls=False)),
                               ("un", unverified, profile("127.0.0.4", TLS, tls=True))):   # TLS without an anchor
        n, c, nid = boot(nodes, tmp_path, name, cmds)
        s = wait_status(c, "connected")
        assert ("tls=plain" if name == "pl" else "tls=unverified") in s, s
        assert broker.retained_status(5)
        for line in ("get name", "set name Hijacked", "reboot"):
            resp = mq(broker, nid, line)
            assert resp["status"] == "refused" and resp["ok"] is False, (name, line, resp)
            assert resp["reply"] == "ERR: management needs a TLS-verified broker", resp
        assert n.proc.poll() is None                    # the reboot was not run
        assert c.cli("get name") != "> Hijacked"
        entry = [ln for ln in audit(c) if "mqtt-session" in ln]
        assert entry and entry[0].endswith("refused mqtt mqtt-session x3"), entry
        n.stop()


def test_a_verified_session_needs_the_right_certificate(nodes, tmp_path, brokers):
    real = brokers("real", "127.0.0.2", TLS, tls=True)
    other = brokers("other", "127.0.0.4", TLS, tls=True)
    # the anchor of the other broker: the real one is not verified, so the node never opens a session with it at all
    n, c, nid = boot(nodes, tmp_path, "pin", profile("127.0.0.2", TLS, anchor=other.fingerprint))
    time.sleep(3.0)
    assert "connected" not in c.cli("get mqtt.status")
    assert real.client_connects() == 0


# ------------------------------------------------------------------ Hardened

def test_hardened_mqtt_is_read_only_unless_the_profile_is_flagged_for_management(nodes, tmp_path, brokers):
    t = brokers("t", "127.0.0.2", TLS, tls=True)
    n, c, nid = boot(nodes, tmp_path, "hrd", profile("127.0.0.2", TLS, anchor=t.fingerprint))
    wait_status(c, "tls=verified")
    assert c.cli("set posture hardened") == "OK - posture hardened"

    assert mq(t, nid, "get name")["status"] == "ok"                       # reads work
    assert mq(t, nid, "get posture")["reply"] == "> hardened"
    resp = mq(t, nid, "set name Nope")                                     # a write does not
    assert (resp["status"], resp["reply"]) == ("refused", "ERR: management is off for this broker profile"), resp
    assert c.cli("get name") != "> Nope"
    assert any(ln.endswith("refused mqtt remote-write x1") for ln in audit(c))

    assert "may be managed" in c.cli("set mqtt.profile 0 manage on")       # the owner flags the broker, locally
    assert "manage=on" in c.cli("get mqtt.profile 0")
    resp = mq(t, nid, "set name Allowed")                                  # now it may
    assert (resp["status"], resp["reply"]) == ("ok", "OK"), resp
    assert c.cli("get name") == "> Allowed"
    # but never the posture, the flag, the key or a factory reset, flagged or not
    for line in ("set posture normal confirm", "set mqtt.profile 0 manage off", "get audit"):
        assert mq(t, nid, line)["status"] == "refused", line
    assert c.cli("get posture") == "> hardened"
    assert "manage=on" in c.cli("get mqtt.profile 0")

    assert "read-only" in c.cli("set mqtt.profile 0 manage off")           # unflagged again
    assert mq(t, nid, "set name Again")["status"] == "refused"
    assert c.cli("set posture normal confirm") == "OK - posture normal"
    assert mq(t, nid, "set name BackInNormal")["status"] == "ok"           # Normal: a verified broker manages


def test_the_management_flag_survives_a_reboot_and_is_off_by_default(nodes, tmp_path, brokers):
    t = brokers("t", "127.0.0.2", TLS, tls=True)
    n, c, nid = boot(nodes, tmp_path, "flag", profile("127.0.0.2", TLS, anchor=t.fingerprint))
    assert "manage" not in c.cli("get mqtt.profile 0")
    assert c.cli("set mqtt.profile 0 manage on").startswith(">")
    assert c.cli("set mqtt.profile 0 manage maybe").startswith("ERR")
    n.restart()
    c = _client(n)
    assert "manage=on" in c.cli("get mqtt.profile 0")
    assert c.cli("set mqtt.profile 0 clear").startswith(">")
    assert "manage" not in c.cli("get mqtt.profile 0")                      # clearing the profile clears the flag


# ------------------------------------------------------------------ the exchange itself

def test_malformed_requests_get_an_answer_and_run_nothing(nodes, tmp_path, brokers):
    t = brokers("t", "127.0.0.2", TLS, tls=True)
    n, c, nid = boot(nodes, tmp_path, "bad", profile("127.0.0.2", TLS, anchor=t.fingerprint))
    wait_status(c, "tls=verified")
    for raw in ("not json", "{}", '{"id":"1"}', '{"cmd":"reboot"}', '{"id":"1","cmd":"get name\\nreboot"}',
                '{"id":"1","id":"2","cmd":"reboot"}', '{"id":"has space","cmd":"reboot"}'):
        resp = mq(t, nid, None, raw=raw)
        assert resp["status"] == "bad-request" and resp["id"] == "" and resp["ok"] is False, (raw, resp)
    assert n.proc.poll() is None
    resp = mq(t, nid, "get name", req_id="A.b_c:9-x")                      # the correlation id comes back as sent
    assert resp["id"] == "A.b_c:9-x"


def test_a_retained_request_is_not_replayed_to_a_reconnecting_node(nodes, tmp_path, brokers):
    t = brokers("t", "127.0.0.2", TLS, tls=True)
    n, c, nid = boot(nodes, tmp_path, "ret", profile("127.0.0.2", TLS, anchor=t.fingerprint))
    wait_status(c, "tls=verified")
    time.sleep(1.0)                                                         # past the settle time after the subscription
    # a retained request: the live node runs it once ...
    t.publish("mecon/%s/cli/req" % nid, json.dumps({"id": "r1", "cmd": "set name Replay1"}), retain=True)
    deadline = time.time() + 10
    while time.time() < deadline and c.cli("get name") != "> Replay1":
        time.sleep(0.2)
    assert c.cli("get name") == "> Replay1"
    assert c.cli("set name Changed") == "OK"
    # ... and the broker hands it to the node again when it reconnects: it must not run a second time
    n.restart()
    c = _client(n)
    wait_status(c, "tls=verified")
    assert t.client_connects() >= 2                                         # it did reconnect (and was offered the message)
    time.sleep(3.0)
    assert c.cli("get name") == "> Changed", "the retained request ran again after a reconnect"
    assert mq(t, nid, "get name")["reply"] == "> Changed"                   # live requests still work after the settle time


def test_get_wifi_pwd_is_answered_write_only_by_the_engine(nodes):
    n = nodes("companion_wifi_usb", "wo", seed=9, rtc=RTC).start()
    c = _client(n)
    assert "updated" in c.cli("set wifi.pwd marker-s3cret")
    assert c.cli("get wifi.pwd") == "> wifi.pwd is write-only"


def test_no_secret_is_in_any_mqtt_response(nodes, tmp_path, brokers):
    t = brokers("t", "127.0.0.2", TLS, tls=True)
    cmds = profile("127.0.0.2", TLS, anchor=t.fingerprint, user="user-marker", pwd="pw-marker-s3cret")
    n, c, nid = boot(nodes, tmp_path, "sec", cmds + ["set wifi.profile 0 pwd wifi-marker-s3cret"])
    wait_status(c, "tls=verified")
    for line in ("get mqtt.profile 0", "get mqtt.profiles", "get mqtt.status", "get wifi.pwd", "get wifi.profile 0",
                 "get wifi.profiles", "set mqtt.profile 0 pwd pw-marker-s3cret", "set wifi.pwd wifi-marker-s3cret"):
        resp = mq(t, nid, line)
        assert "marker" not in json.dumps(resp), (line, resp)
    for secret in ("pw-marker-s3cret", "user-marker", "wifi-marker-s3cret"):
        hits = {os.path.basename(h) for h in files_containing(n, secret)}
        assert hits <= {"prefs.json"}, (secret, hits)


def test_reboot_over_mqtt_runs_in_normal_and_the_node_comes_back(nodes, tmp_path, brokers):
    t = brokers("t", "127.0.0.2", TLS, tls=True)
    n, c, nid = boot(nodes, tmp_path, "rb", profile("127.0.0.2", TLS, anchor=t.fingerprint))
    wait_status(c, "tls=verified")
    time.sleep(1.0)                                                         # past the settle time after the subscription
    t.publish("mecon/%s/cli/req" % nid, json.dumps({"id": "rb1", "cmd": "reboot"}))
    assert n.wait_exit(15) == 75                                            # no response: the device is gone first
    n.start()
    c = _client(n)
    wait_status(c, "tls=verified")                                          # and it is back on its broker


def test_mqtt_cannot_power_the_device_off_but_the_console_can(nodes, tmp_path, brokers):
    t = brokers("t", "127.0.0.2", TLS, tls=True)
    n, c, nid = boot(nodes, tmp_path, "pwr", profile("127.0.0.2", TLS, anchor=t.fingerprint, manage=True))
    wait_status(c, "tls=verified")
    for line in ("poweroff", "shutdown"):
        resp = mq(t, nid, line)
        assert (resp["status"], resp["reply"]) == ("refused", "ERR: not permitted"), resp
    assert n.proc.poll() is None
    assert any(ln.endswith("refused mqtt cli x1") or "refused mqtt cli" in ln for ln in audit(c)), audit(c)
    c._send(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"poweroff")
    assert n.wait_exit(10) == 0
