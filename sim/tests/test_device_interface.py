"""The device interface over the Companion's USB frames (docs/contract/DEVICE_INTERFACE.md), on a simulated standalone
Companion, through the reference client in tools/mecon_iface.py."""
import json
import os
import re
import sys

import pytest

from sim import harness
from sim.harness import CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402

RTC = 1792800000


def start(nodes, name="iface", seed=11):
    n = nodes("companion_standalone", name, seed=seed, rtc=RTC).start()
    return n, client(n)


def client(n):
    c = CompanionClient(n)
    c.app_start()
    return c, mecon_iface.IfaceClient(c._send, c._read_frame)


def test_hello_reports_identity_versions_and_capabilities(nodes):
    n, (c, i) = start(nodes)
    r = i.request("hello")
    assert r["status"] == "ok", r
    h = r["result"]
    assert h["interface"] == "1.0"
    assert re.fullmatch(r"[0-9a-f]{32}", h["device_id"]), h
    assert h["node_key"] == c.pub_key.hex(), "the native key is reported beside device_id"
    assert h["role"] == "companion"
    assert h["firmware"].startswith("v1.18.0-mecon"), h
    assert h["upstream"] == "v1.18.0"
    assert h["epoch"] >= 1
    assert set(h["capabilities"]) >= {"hello", "status.get"}


def test_device_id_is_kept_and_epoch_counts_boots(nodes):
    n, (c, i) = start(nodes)
    first = i.request("hello")["result"]
    for k in range(2):
        n.stop()
        n.start()
        c, i = client(n)
        again = i.request("hello")["result"]
        assert again["device_id"] == first["device_id"], "device_id survives a reboot"
        assert again["epoch"] == first["epoch"] + k + 1, "epoch is incremented on every boot"


def test_status_snapshot(nodes):
    n, (c, i) = start(nodes)
    s = i.request("status.get")["result"]
    h = i.request("hello")["result"]
    assert s["device_id"] == h["device_id"] and s["epoch"] == h["epoch"]
    assert isinstance(s["uptime_s"], int) and s["uptime_s"] >= 0
    assert s["heap"] is not None and s["heap"]["free"] > 0
    assert s["contacts"] == {"count": 0, "capacity": s["contacts"]["capacity"]} and s["contacts"]["capacity"] > 0
    assert s["connectivity"] in ("ble", "wifi")
    assert s["posture"] == "normal"
    assert "battery_mv" in s, "present: a number, or null when not measurable"


def test_requests_that_must_run_nothing(nodes):
    n, (c, i) = start(nodes)
    assert i.request("no.such.op")["status"] == "unknown-op"
    assert i.request("hello", v=2)["status"] == "unsupported-version"
    assert i.request("hello", auth={"sig": "x"})["status"] == "refused", "unverifiable authority is refused, not ignored"
    i.send_raw(b'{"v":1,"id":"bad","op":"hello",}')
    assert i.wait_result("bad")["status"] == "invalid"


def test_a_request_split_over_several_frames(nodes):
    n, (c, i) = start(nodes)
    pad = "x" * 600   # > 3 frames; unknown fields are ignored
    r = i.request("hello", args={"pad": pad})
    assert r["status"] == "ok", r
    assert len(json.dumps(i.envelope("hello", {"pad": pad}))) > 3 * mecon_iface.FRAME_PAYLOAD


def test_native_frames_interleave_with_interface_traffic(nodes):
    n, (c, i) = start(nodes)
    # a native request between the frames of an interface request: both are answered, neither is confused
    env = i.envelope("hello", {"pad": "y" * 400})
    frames = mecon_iface.fragment(json.dumps(env).encode(), 9)
    c._send(frames[0])
    native = c.request(bytes([22, 3]))   # CMD_DEVICE_QUERY
    assert native[0] == 13, "the native answer comes back while the interface message is half sent"
    for f in frames[1:]:
        c._send(f)
    assert i.wait_result(env["id"])["status"] == "ok"


# ------------------------------------------------------------------ authorization follows the CLI table, per transport

PIN = 271828


def ble_boot(nodes, name, pin):
    n = nodes("companion_standalone", name, seed=13, rtc=RTC).start()
    c = CompanionClient(n)
    c.app_start()
    assert c.cli("set pin %d" % pin).startswith("> pin is now")
    c.close()
    n.restart()
    return n


@pytest.mark.timescale(10)
def test_a_paired_ble_phone_gets_hello(nodes):
    n = ble_boot(nodes, "bp", PIN)
    b = n.connect_ble(PIN)
    b.app_start()
    r = mecon_iface.IfaceClient(b._send, b._read_frame).request("hello")
    assert r["status"] == "ok", r


@pytest.mark.timescale(10)
def test_the_default_pin_is_refused_like_its_cli_line(nodes):
    n = ble_boot(nodes, "bd", 123456)
    b = n.connect_ble(123456)
    b.app_start()
    r = mecon_iface.IfaceClient(b._send, b._read_frame).request("hello")
    assert r == {"v": 1, "id": r["id"], "status": "refused"}, r
    f = b.request(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"get name")
    assert f == bytes([1, 7]), "the CLI frame is refused on the same link (ERR not permitted): the same table decides"


@pytest.mark.timescale(10)
def test_a_result_goes_only_to_the_transport_that_asked(nodes):
    n = ble_boot(nodes, "bx", PIN)
    b = n.connect_ble(PIN)
    b.app_start()
    u = CompanionClient(n)
    u.app_start()
    ib = mecon_iface.IfaceClient(b._send, b._read_frame, id_prefix="ble")
    iu = mecon_iface.IfaceClient(u._send, u._read_frame, id_prefix="usb")
    assert ib.request("hello")["status"] == "ok"
    assert iu.read_message(timeout=1.5) is None, "the USB client sees nothing of the BLE request"
    assert iu.request("status.get")["status"] == "ok"
    assert ib.read_message(timeout=1.5) is None, "and the BLE client nothing of the USB one"


# ------------------------------------------------------------------ the published fixtures and schemas

import mecon_schema  # noqa: E402

FIXTURES = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "contract", "fixtures")


def fixture(name):
    with open(os.path.join(FIXTURES, name)) as f:
        return json.load(f)


def expand(req):
    s = json.dumps(req, separators=(",", ":")).replace("PAD600", "x" * 600)
    return s.encode()


def assert_case(i, case, answer):
    exp = case["expect"]
    assert answer["status"] == exp["status"], (case["name"], answer)
    assert not mecon_schema.check(answer, mecon_schema.load("result.json")), (case["name"], answer)
    if "id" in exp:
        assert answer["id"] == exp["id"], (case["name"], answer)
    if "error" in exp:
        assert answer.get("error") == exp["error"], (case["name"], answer)
    if "schema" in exp:
        problems = mecon_schema.check(answer["result"], mecon_schema.load(exp["schema"]))
        assert not problems, (case["name"], problems)


def test_every_request_fixture_against_the_device(nodes):
    n, (c, i) = start(nodes, "fx")
    for case in fixture("requests.json")["cases"]:
        if "raw" in case:
            i.send_raw(case["raw"].encode())
            answer = i.wait_result(case["expect"].get("id", ""))
        else:
            assert not mecon_schema.check(case["request"], mecon_schema.load("request.json")) or case["request"]["v"] != 1 \
                or "auth" in case["request"], case["name"]
            i.send_raw(expand(case["request"]))
            answer = i.wait_result(case["request"]["id"])
        assert_case(i, case, answer)


@pytest.mark.timescale(10)
def test_the_transport_fixtures(nodes):
    n = ble_boot(nodes, "fxb", 123456)
    b = n.connect_ble(123456)
    b.app_start()
    i = mecon_iface.IfaceClient(b._send, b._read_frame)
    for case in fixture("requests.json")["transport_cases"]:
        assert case["transport"] == "ble-default-pin"
        i.send_raw(expand(case["request"]))
        assert_case(i, case, i.wait_result(case["request"]["id"]))


def test_the_framing_fixture(nodes):
    fx = fixture("framing.json")
    frames = [bytes.fromhex(h) for h in fx["frames_hex"]]
    assert mecon_iface.fragment(fx["message_utf8"].encode(), fx["xid"]) == frames, "the reference client frames it so"
    n, (c, i) = start(nodes, "fxf")
    for f in reversed(frames):   # any order
        c._send(f)
    assert i.wait_result("fx-frames")["status"] == "ok"


# ------------------------------------------------------------------ settings: config.schema / get / apply

def ok_result(answer, schema):
    assert answer["status"] == "ok", answer
    problems = mecon_schema.check(answer["result"], mecon_schema.load(schema))
    assert not problems, problems
    return answer["result"]


def test_the_schema_lists_every_key_over_its_pages(nodes):
    n, (c, i) = start(nodes, "cs")
    keys, cursor = [], 0
    while cursor is not None:
        page = ok_result(i.request("config.schema", {"cursor": cursor}), "config.schema.result.json")
        keys += page["keys"]
        cursor = page["next"]
    names = [k["key"] for k in keys]
    assert len(names) == len(set(names))
    assert {"name", "tx", "wifi.profile.{n}.ssid", "wifi.profile.{n}.pwd", "mqtt.profile.{n}.host"} <= set(names)
    pwd = next(k for k in keys if k["key"] == "wifi.profile.{n}.pwd")
    assert pwd["write_only"] and pwd["index"] == [0, 9]
    assert "lat" not in names, "a key exists only where the CLI has a line for it"


def test_get_reads_the_same_values_as_the_cli(nodes):
    n, (c, i) = start(nodes, "cg")
    assert c.cli("set wifi.profile 2 ssid lab net").startswith(">")
    assert c.cli("set wifi.profile 2 pwd TopSecret-fake").startswith(">")
    v = ok_result(i.request("config.get", {"keys": ["name", "tx", "wifi.profile.2.ssid", "wifi.profile.2.pwd",
                                                    "wifi.profile.4.priority"]}), "config.get.result.json")["values"]
    assert v["name"] == c.cli("get name")[2:]
    assert v["tx"] == int(c.cli("get tx")[2:])
    assert v["wifi.profile.2.ssid"] == "lab net" and v["wifi.profile.2.pwd"] == "set"
    assert v["wifi.profile.4.priority"] is None, "an unset profile has no priority"
    assert "TopSecret" not in json.dumps(v)


def test_apply_writes_through_the_cli_and_survives_a_reboot(nodes):
    n, (c, i) = start(nodes, "ca")
    r = ok_result(i.request("config.apply", {"set": {"name": "Lab node", "wifi.profile.1.ssid": "office",
                                                     "wifi.profile.1.pwd": "pw-fake-1", "wifi.profile.1.priority": 60}}),
                  "config.apply.result.json")
    assert r["complete"] and set(r["applied"].values()) == {"ok"}
    assert "pw-fake-1" not in json.dumps(r)
    assert c.cli("get name") == "> Lab node"
    assert c.cli("get wifi.profile 1") == "> 1: office prio=60 pwd=set last=none"
    n.stop()
    n.start()
    c, i = client(n)
    v = i.request("config.get", {"keys": ["name", "wifi.profile.1.ssid", "wifi.profile.1.pwd"]})["result"]["values"]
    assert v == {"name": "Lab node", "wifi.profile.1.ssid": "office", "wifi.profile.1.pwd": "set"}


def test_apply_writes_nothing_when_one_key_fails_its_check(nodes):
    n, (c, i) = start(nodes, "cx")
    before = c.cli("get name")
    r = i.request("config.apply", {"set": {"name": "Changed", "tx": 99}})
    assert r["status"] == "invalid" and r["error"] == "out-of-range:tx", r
    assert c.cli("get name") == before


def test_a_retried_apply_runs_once(nodes):
    n, (c, i) = start(nodes, "cr")
    first = i.request("config.apply", {"set": {"name": "First"}}, rid="apply-1")
    assert c.cli("set name Meanwhile") == "OK"
    again = i.request("config.apply", {"set": {"name": "First"}}, rid="apply-1")
    assert again == first, "the stored result, not a second run"
    assert c.cli("get name") == "> Meanwhile"


@pytest.mark.timescale(10)
def test_settings_over_ble_follow_the_cli_table(nodes):
    n = ble_boot(nodes, "cb", PIN)
    b = n.connect_ble(PIN)
    b.app_start()
    i = mecon_iface.IfaceClient(b._send, b._read_frame)
    assert i.request("config.apply", {"set": {"name": "Blephone"}})["status"] == "ok", "a paired phone may configure"
    b.close()
    n2 = ble_boot(nodes, "cd", 123456)
    d = n2.connect_ble(123456)
    d.app_start()
    j = mecon_iface.IfaceClient(d._send, d._read_frame)
    assert j.request("config.get", {"keys": ["name"]})["status"] == "refused"
    assert j.request("config.apply", {"set": {"name": "Nope"}})["status"] == "refused"
