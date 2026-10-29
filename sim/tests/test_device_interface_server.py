"""The device interface on the Repeater and Room server console (docs/contract/DEVICE_INTERFACE.md section 7): one
"mecon> " line per request, results and events back as "mecon> " lines, through the reference client in
tools/mecon_iface.py. Simulated boards."""
import json
import os
import re
import sys

import pytest

from sim.harness import CliClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402
import mecon_schema  # noqa: E402

FIXTURES = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "contract", "fixtures")
ROLES = [("repeater", "repeater"), ("room_server", "room")]


def start(nodes, kind, name):
    n = nodes(kind, name).start()
    return n, client(n)


def client(n):
    cli = CliClient(n)
    return cli, mecon_iface.ConsoleClient(cli.sock.sendall, mecon_iface.socket_lines(cli.sock))


def ok(answer, schema):
    assert answer["status"] == "ok", answer
    problems = mecon_schema.check(answer["result"], mecon_schema.load(schema))
    assert not problems, problems
    return answer["result"]


@pytest.mark.parametrize("kind,role", ROLES)
def test_hello_names_the_role(nodes, kind, role):
    n, (cli, i) = start(nodes, kind, "h" + role)
    h = ok(i.request("hello"), "hello.result.json")
    assert h["role"] == role
    assert re.fullmatch(r"[0-9a-f]{32}", h["device_id"]), h
    assert h["epoch"] >= 1


def test_device_id_is_kept_and_epoch_counts_boots(nodes):
    n, (cli, i) = start(nodes, "repeater", "boots")
    first = ok(i.request("hello"), "hello.result.json")
    n.stop()
    n.start()
    cli, i = client(n)
    again = ok(i.request("hello"), "hello.result.json")
    assert again["device_id"] == first["device_id"]
    assert again["epoch"] == first["epoch"] + 1


def test_status_has_no_contact_list(nodes):
    n, (cli, i) = start(nodes, "repeater", "st")
    s = ok(i.request("status.get"), "status.get.result.json")
    assert "contacts" not in s
    assert s["posture"] == "normal"
    assert s["heap"] is not None and s["heap"]["free"] > 0


def test_the_stock_console_still_works_beside_it(nodes):
    n, (cli, i) = start(nodes, "repeater", "stock")
    assert cli.command("get name") == "> Sim Repeater"
    assert ok(i.request("hello"), "hello.result.json")["role"] == "repeater"
    assert cli.command("get name") == "> Sim Repeater", "the console is back to the CLI after an interface line"


@pytest.mark.parametrize("kind,role", ROLES)
def test_schema_lists_the_server_keys(nodes, kind, role):
    n, (cli, i) = start(nodes, kind, "sc" + role)
    keys, cursor = [], 0
    while cursor is not None:
        r = ok(i.request("config.schema", {"cursor": cursor}), "config.schema.result.json")
        keys += [k["key"] for k in r["keys"]]
        cursor = r["next"]
    assert {"name", "radio", "lat", "lon", "repeat", "advert.interval", "loop.detect"} <= set(keys), keys
    assert not any(k.startswith("wifi.") for k in keys)


@pytest.mark.parametrize("kind,role", ROLES)
def test_get_reads_what_the_cli_reads(nodes, kind, role):
    n, (cli, i) = start(nodes, kind, "g" + role)
    keys = ["name", "tx", "radio", "lat", "lon", "repeat", "advert.interval", "flood.advert.interval", "flood.max",
            "loop.detect", "allow.read.only", "owner.info"]
    r = ok(i.request("config.get", {"keys": keys}), "config.get.result.json")
    assert "errors" not in r, r
    v = r["values"]
    assert v["name"] == cli.command("get name")[2:]
    assert v["radio"] == cli.command("get radio")[2:]
    assert v["repeat"] == (cli.command("get repeat") == "> on")
    assert v["loop.detect"] == cli.command("get loop.detect")[2:]


def test_apply_writes_through_the_cli_and_survives_a_reboot(nodes):
    n, (cli, i) = start(nodes, "repeater", "ap")
    r = i.request("config.apply", {"set": {"name": "Hilltop", "repeat": False, "lat": "56.1572", "loop.detect": "strict"}})
    assert r["status"] == "ok", r
    assert cli.command("get repeat") == "> off", "false reaches the CLI as off"
    n.stop()
    n.start()
    cli, i = client(n)
    v = ok(i.request("config.get", {"keys": ["name", "repeat", "lat", "loop.detect"]}), "config.get.result.json")["values"]
    assert v["name"] == "Hilltop" and v["repeat"] is False and v["loop.detect"] == "strict"
    assert v["lat"].startswith("56.157"), v


def test_a_bad_value_writes_nothing(nodes):
    n, (cli, i) = start(nodes, "repeater", "bad")
    r = i.request("config.apply", {"set": {"name": "Ok", "lat": "north"}})
    assert r["status"] == "invalid" and r["error"] == "bad-format:lat", r
    assert cli.command("get name") == "> Sim Repeater"


def test_a_line_too_long_is_refused_not_truncated(nodes):
    n, (cli, i) = start(nodes, "repeater", "long")
    i.send_raw(json.dumps({"v": 1, "id": "big", "op": "hello", "pad": "x" * 2000}))
    r = i.wait_result("")
    assert r["status"] == "busy" and r["error"] == "line-too-long", r
    assert ok(i.request("hello"), "hello.result.json")["role"] == "repeater", "the next line is read normally"


def test_health_events_come_back_as_console_lines(nodes):
    n, (cli, i) = start(nodes, "repeater", "ev")
    assert i.request("sub.start", {"stream": "health", "period_s": 5, "lifetime_s": 30})["status"] == "ok"
    seen = []
    for _ in range(40):
        m = i.read_message(1.0)
        if m and m.get("ev") == "health":
            seen.append(m)
        if len(seen) >= 2:
            break
    assert len(seen) >= 2, seen
    assert seen[1]["seq"] == seen[0]["seq"] + 1
    for e in seen:
        assert not mecon_schema.check(e["data"], mecon_schema.load("event.health.json")), e
    r = i.request("sub.stop", {"stream": "health"})
    assert r["status"] == "ok" and r["result"]["active"] is False, r
    late = [m for m in (i.read_message(1.0) for _ in range(12)) if m and m.get("ev") == "health"]
    assert not late, "no event after sub.stop"


def test_every_request_fixture_against_the_repeater(nodes):
    with open(os.path.join(FIXTURES, "requests.json")) as f:
        cases = json.load(f)["cases"]
    n, (cli, i) = start(nodes, "repeater", "fx")
    for case in cases:
        if "repeater" not in case.get("roles", ["repeater"]):
            continue
        if "raw" in case:
            if "\n" in case["raw"] or "\r" in case["raw"]:
                continue   # a line break inside a raw case cannot be one console line
            i.send_raw(case["raw"])
            answer = i.wait_result(case["expect"].get("id", ""))
        else:
            text = json.dumps(case["request"], separators=(",", ":")).replace("PAD600", "x" * 600)
            i.send_raw(text)
            answer = i.wait_result(case["request"]["id"])
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


def test_the_hardware_runner_passes_against_the_simulated_console(nodes):
    """tools/mecon_iface_hw.py in its console mode, end to end, so the tool a bench run uses is itself tested."""
    import subprocess
    n = nodes("repeater", "hw").start()
    tool = os.path.join(os.path.dirname(__file__), "..", "..", "tools", "mecon_iface_hw.py")
    r = subprocess.run([sys.executable, tool, "console", "socket://127.0.0.1:%d" % n.port], capture_output=True,
                       text=True, timeout=180)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "role repeater:" in r.stdout
    last = r.stdout.strip().splitlines()[-1]
    a, b = re.match(r"(\d+) of (\d+) passed", last).groups()
    assert a == b, r.stdout


@pytest.mark.parametrize("kind,role", ROLES)
def test_get_device_id_on_the_console_matches_hello(nodes, kind, role):
    n, (cli, i) = start(nodes, kind, "did" + role)
    h = ok(i.request("hello"), "hello.result.json")
    assert cli.command("get device.id").split(" ")[:2] == [">", h["device_id"]]   # the signature: test_device_id_binding.py
