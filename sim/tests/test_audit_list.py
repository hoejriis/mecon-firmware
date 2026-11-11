"""audit.list {"cursor"} (#491; AuditOps.h): the audit ring's history over the interface, newest first, the entries and order
of the CLI's `get audit`, in the logs stream's names. Its class is posture, so a caller that may read but not reach the
posture class (an MQTT session to a TLS-verified broker, docs/CLI.md) is refused."""
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from sim import harness  # noqa: E402
from test_deployment_authority import broker, mqtt_boot  # noqa: E402,F401
from test_telemetry_streams import cli_line, companion  # noqa: E402
from test_device_interface_server import start as server_start  # noqa: E402


def entries(i, cursor=None):
    r = i.request("audit.list", {"cursor": cursor} if cursor is not None else None)
    assert r["status"] == "ok", r
    return r["result"]


def test_the_history_newest_first_as_the_cli_has_it(nodes):
    n, c, i = companion(nodes, "audl")
    c._send(cli_line("set posture hardened"))
    c._send(bytes([harness.CMD_EXPORT_PRIVATE_KEY]))   # Hardened: refused, and audited
    c._send(bytes([harness.CMD_EXPORT_PRIVATE_KEY]))   # the same refusal again folds: one entry, count 2
    res = entries(i)
    got = [(e["event"], e["caller"], e["what"], e["count"]) for e in res["entries"]]
    assert got[:2] == [("refused", "cable", "key-export", 2), ("posture", "cable", "hardened", 1)], got
    assert res["total"] == len(res["entries"]) and res["next"] is None, res
    assert all(set(e) == {"ts", "event", "caller", "what", "count"} for e in res["entries"]), res
    cli = c.cli("get audit")
    assert cli.startswith("> %d entries" % res["total"]), cli
    assert "key-export" in cli.split("\n")[1] and "x2" in cli.split("\n")[1], "the CLI's newest is the same entry: %s" % cli
    if res["total"] > 1:
        page = entries(i, 1)
        assert page["entries"][0] == res["entries"][1], "the cursor counts from the newest"
    assert i.request("audit.list", {"cursor": -1})["status"] == "invalid"


@pytest.mark.timescale(10)
def test_a_verified_broker_session_reads_but_not_the_audit(nodes, tmp_path, broker):
    b = broker()
    d, dev = mqtt_boot(nodes, tmp_path, "audm", b)
    assert b.iface_request(dev, {"v": 1, "id": "h1", "op": "hello"})["status"] == "ok", "it may read"
    r = b.iface_request(dev, {"v": 1, "id": "a1", "op": "audit.list"})
    assert r["status"] == "refused", "the posture class is not a broker session's: %s" % r
    assert d.i.request("audit.list")["status"] == "ok", "the cable may"


def test_the_repeater_console_has_it_too(nodes):
    n, (c, i) = server_start(nodes, "repeater", "audr")
    res = entries(i)
    assert res["total"] >= 0 and res["next"] is None, res
