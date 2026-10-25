"""Telemetry streams (docs/contract/TELEMETRY.md) on the simulated Companion (USB frames) and Repeater (console):
discovery in hello, each stream's first event and its schema, the radio counters moving with a transmission, and the
stream table's bounds."""
import os
import sys
import time

import pytest

from sim.harness import CliClient, CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402
import mecon_schema  # noqa: E402

RTC = 1792800000
STREAMS = ["health", "radio", "power", "runtime"]


def companion(nodes, name):
    n = nodes("companion_standalone", name, rtc=RTC).start()
    c = CompanionClient(n)
    c.app_start()
    return n, c, mecon_iface.IfaceClient(c._send, c._read_frame)


def repeater(nodes, name):
    n = nodes("repeater", name).start()
    cli = CliClient(n)
    return n, cli, mecon_iface.ConsoleClient(cli.sock.sendall, mecon_iface.socket_lines(cli.sock))


def first_event(i, stream, timeout=15.0):
    """Starts the stream and returns its first event, which a new subscription gets at once."""
    r = i.request("sub.start", {"stream": stream, "period_s": 3600 if stream != "radio" else 5, "lifetime_s": 60})
    assert r["status"] == "ok" and r["result"]["stream"] == stream, r
    end = time.time() + timeout
    while time.time() < end:
        for e in list(i.events):
            if e.get("ev") == stream:
                i.events.remove(e)
                return e
        m = i.read_message(0.5)
        if m and m.get("ev") == stream:
            return m
        if m and "ev" in m:
            i.events.append(m)
    raise AssertionError("no %s event" % stream)


def check(e, stream):
    assert not mecon_schema.check(e, mecon_schema.load("event.json")), e
    problems = mecon_schema.check(e["data"], mecon_schema.load("event.%s.json" % stream))
    assert not problems, (stream, problems, e)
    return e["data"]


@pytest.mark.parametrize("make", [companion, repeater], ids=["companion", "repeater"])
def test_hello_lists_every_stream(nodes, make):
    n, c, i = make(nodes, "caps")
    caps = i.request("hello")["result"]["capabilities"]
    for s in STREAMS:
        assert "stream." + s in caps, caps


@pytest.mark.parametrize("make", [companion, repeater], ids=["companion", "repeater"])
@pytest.mark.parametrize("stream", STREAMS)
def test_each_stream_sends_its_first_event_at_once(nodes, make, stream):
    n, c, i = make(nodes, "first" + stream[:3])
    d = check(first_event(i, stream), stream)
    if stream == "power":
        assert d["battery_mv"] == 4000, d             # the simulated board's reading
        assert d["external"] is None and d["charging"] is None, "a board that cannot tell says null, not false"
    if stream == "runtime":
        assert isinstance(d["boot_ms"]["setup"], int), d
        assert d["reset_reason"] is None and d["abnormal_reset"] is None, "the simulated board has no reset reason"
    if stream == "radio":
        assert d["duty"]["limit_bp"] > 0, d


def test_radio_counts_a_transmission_companion(nodes):
    n, c, i = companion(nodes, "radc")
    before = check(first_event(i, "radio"), "radio")["tx"]["flood"]
    c.request(bytes([7, 1]))   # CMD_SEND_SELF_ADVERT, flood
    after = before
    end = time.time() + 30
    while time.time() < end and after == before:
        m = i.read_message(1.0)
        if m and m.get("ev") == "radio":
            after = check(m, "radio")["tx"]["flood"]
    assert after == before + 1, (before, after)


def test_radio_counts_a_transmission_repeater(nodes):
    n, cli, i = repeater(nodes, "radr")
    before = check(first_event(i, "radio"), "radio")["tx"]["flood"]
    assert cli.command("advert").startswith("OK"), "a flood advert"
    after = before
    end = time.time() + 30
    while time.time() < end and after == before:
        m = i.read_message(1.0)
        if m and m.get("ev") == "radio":
            after = check(m, "radio")["tx"]["flood"]
    assert after == before + 1, (before, after)


@pytest.mark.parametrize("make", [companion, repeater], ids=["companion", "repeater"])
def test_the_stream_table_bounds_requests(nodes, make):
    n, c, i = make(nodes, "bounds")
    r = i.request("sub.start", {"stream": "screen"})
    assert r["status"] == "invalid" and r["error"] == "unknown-stream", r
    r = i.request("sub.start", {"stream": "power", "period_s": 5})   # power's minimum is 30 s
    assert r["status"] == "invalid", r
    r = i.request("sub.stop", {"stream": "radio"})
    assert r["status"] == "ok" and r["result"] == {"stream": "radio", "active": False}, r
