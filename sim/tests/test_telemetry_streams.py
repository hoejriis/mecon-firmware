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
EVENT_STREAMS = ["observations"]


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
    r = i.request("sub.start", {"stream": stream, "period_s": 5 if stream == "radio" else 3600, "lifetime_s": 60})
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
    for s in STREAMS + EVENT_STREAMS:
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
        assert isinstance(d["loop_gap_ms"]["max"], int) and d["loop_gap_ms"]["max"] >= 0, d   # measured natively
    if stream == "radio":
        assert d["duty"]["limit_bp"] > 0, d


def test_radio_counts_a_transmission_companion(nodes):
    n, c, i = companion(nodes, "radc")
    before = check(first_event(i, "radio"), "radio")["tx"]
    c.request(bytes([7, 1]))   # CMD_SEND_SELF_ADVERT, flood
    after = before
    end = time.time() + 30
    while time.time() < end and after == before:
        m = i.read_message(1.0)
        if m and m.get("ev") == "radio":
            after = check(m, "radio")["tx"]
    assert after["flood"] == before["flood"] + 1 and after["direct"] == before["direct"], (before, after)


def test_radio_counts_a_transmission_repeater(nodes):
    n, cli, i = repeater(nodes, "radr")
    before = check(first_event(i, "radio"), "radio")["tx"]
    cli.command("advert.zerohop")   # zero-hop adverts go out direct (the sim counts two packets)
    after = before
    end = time.time() + 30
    while time.time() < end and after == before:
        m = i.read_message(1.0)
        if m and m.get("ev") == "radio":
            after = check(m, "radio")["tx"]
    assert after["direct"] > before["direct"] and after["flood"] == before["flood"], (before, after)


@pytest.mark.parametrize("make", [companion, repeater], ids=["companion", "repeater"])
def test_the_stream_table_bounds_requests(nodes, make):
    n, c, i = make(nodes, "bounds")
    r = i.request("sub.start", {"stream": "screen"})
    assert r["status"] == "invalid" and r["error"] == "unknown-stream", r
    r = i.request("sub.start", {"stream": "power", "period_s": 5})   # power's minimum is 30 s
    assert r["status"] == "invalid", r
    r = i.request("sub.stop", {"stream": "radio"})
    assert r["status"] == "ok" and r["result"] == {"stream": "radio", "active": False}, r


def test_a_new_subscription_after_a_stop_gets_an_event_at_once(nodes):
    """The first event of a stream that has run before is not held back to its old schedule."""
    n, c, i = companion(nodes, "resub")
    check(first_event(i, "power"), "power")          # period 3600 s: the next one is an hour away
    assert i.request("sub.stop", {"stream": "power"})["status"] == "ok"
    check(first_event(i, "power", timeout=10), "power")


def wait_event(i, stream, timeout):
    end = time.time() + timeout
    while time.time() < end:
        m = i.read_message(1.0)
        if m and m.get("ev") == stream:
            return m
    return None


@pytest.mark.parametrize("kind", ["companion_standalone", "repeater"])
def test_observations_carry_each_received_packet_as_heard(ether, nodes, kind):
    """A Repeater's advert, heard by the subscribed node: the event carries exactly the bytes the channel carried."""
    ether.set_link("obsrx", "obstx")
    rx = nodes(kind, "obsrx", ether_path=ether.path, rtc=RTC).start()
    tx = nodes("repeater", "obstx", ether_path=ether.path, seed=5, rtc=RTC).start()
    assert ether.wait_tx("obstx", timeout=120), "no boot advert from the sender"   # a Companion sends none
    assert ether.wait_quiet(3.0, timeout=120)
    if kind == "repeater":
        cli = CliClient(rx)
        i = mecon_iface.ConsoleClient(cli.sock.sendall, mecon_iface.socket_lines(cli.sock))
    else:
        c = CompanionClient(rx)
        c.app_start()
        i = mecon_iface.IfaceClient(c._send, c._read_frame)
    r = i.request("sub.start", {"stream": "observations", "lifetime_s": 120})
    assert r["status"] == "ok" and r["result"]["period_s"] is None, r
    sent = len(ether.tx_of("obstx"))
    CliClient(tx).command("advert.zerohop")
    e = wait_event(i, "observations", 60)
    assert e is not None, "no observation"
    d = check(e, "observations")
    heard = [t for t in ether.tx_of("obstx")[sent:]]
    assert heard, "the advert never went on the air"
    assert d["raw"] in [t.data.hex() for t in heard], (d["raw"][:40], [t.data.hex()[:40] for t in heard])
    assert d["len"] == len(bytes.fromhex(d["raw"])) and d["dropped"] == 0, d


def test_an_event_stream_takes_no_period(nodes):
    n, c, i = companion(nodes, "noper")
    r = i.request("sub.start", {"stream": "observations", "period_s": 5})
    assert r["status"] == "invalid" and r["error"] == "no-period", r



def sensors_file(nodes_factory_node):
    return nodes_factory_node.dir + ".sensors"


@pytest.mark.parametrize("kind", ["companion_standalone", "repeater"])
def test_environment_carries_the_attached_sensors(nodes, kind):
    n = nodes(kind, "env", rtc=RTC)
    with open(n.dir + ".sensors", "w") as f:   # what the simulated board's sensors read
        f.write("1 temperature 21.5\n2 humidity 40\n3 pressure 1013.2\n")
    n.start()
    if kind == "repeater":
        cli = CliClient(n)
        i = mecon_iface.ConsoleClient(cli.sock.sendall, mecon_iface.socket_lines(cli.sock))
    else:
        c = CompanionClient(n)
        c.app_start()
        i = mecon_iface.IfaceClient(c._send, c._read_frame)
    assert "stream.environment" in i.request("hello")["result"]["capabilities"]
    d = check(first_event(i, "environment"), "environment")
    got = {(r["channel"], r["quantity"]): (r["value"], r["unit"]) for r in d["readings"]}
    assert got == {(1, "temperature"): (21.5, "C"), (2, "humidity"): (40.0, "%"), (3, "pressure"): (1013.2, "hPa")}, d
    assert "mcu_temp_c" not in d, "the simulated board reads no MCU temperature"


@pytest.mark.parametrize("make", [companion, repeater], ids=["companion", "repeater"])
def test_environment_is_absent_without_sensors(nodes, make):
    n, c, i = make(nodes, "noenv")
    assert "stream.environment" not in i.request("hello")["result"]["capabilities"]
    r = i.request("sub.start", {"stream": "environment"})
    assert r["status"] == "invalid" and r["error"] == "unknown-stream", r
