"""Telemetry streams (docs/contract/TELEMETRY.md) on the simulated Companion (USB frames) and Repeater (console):
discovery in hello, each stream's first event and its schema, the radio counters moving with a transmission, and the
stream table's bounds."""
import os
import struct
import sys
import time

import pytest

from sim import harness
from sim.harness import CliClient, CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402
import mecon_schema  # noqa: E402

RTC = 1792800000
STREAMS = ["health", "radio", "power", "runtime", "inventory"]
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
            if e.get("ev") == stream and not e["data"].get("boot"):   # the runtime boot event is not a snapshot
                i.events.remove(e)
                return e
        m = i.read_message(0.5)
        if m and m.get("ev") == stream and not m["data"].get("boot"):
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



def test_inventory_counts_on_the_companion(nodes):
    n, c, i = companion(nodes, "invc")
    d = check(first_event(i, "inventory"), "inventory")
    h = i.request("status.get")["result"]["contacts"]
    assert d["contacts"] == h, (d, h)
    assert d["channels"]["capacity"] > 0 and 0 <= d["channels"]["count"] <= d["channels"]["capacity"], d


def test_inventory_counts_a_heard_neighbour_on_the_repeater(ether, nodes):
    ether.set_link("invr", "invn")
    r = nodes("repeater", "invr", ether_path=ether.path, rtc=RTC).start()
    nb = nodes("repeater", "invn", ether_path=ether.path, seed=6, rtc=RTC).start()
    assert ether.wait_tx("invr", timeout=120) and ether.wait_tx("invn", timeout=120), "no boot adverts"
    assert ether.wait_quiet(3.0, timeout=120)
    CliClient(nb).command("advert.zerohop")
    assert ether.wait_quiet(3.0, timeout=120)
    cli = CliClient(r)
    i = mecon_iface.ConsoleClient(cli.sock.sendall, mecon_iface.socket_lines(cli.sock))
    d = check(first_event(i, "inventory"), "inventory")
    assert d["neighbours"]["count"] == 1 and d["neighbours"]["capacity"] > 0, d
    assert d["clients"]["count"] == 0 and d["clients"]["capacity"] > 0, d


# ------------------------------------------------------------------ connectivity: BLE and USB link state

def collect(i, stream, seconds):
    """Every event of `stream` that arrives within `seconds` (events of other streams are skipped)."""
    out, end = [], time.time() + seconds
    for e in list(i.events):
        if e.get("ev") == stream:
            out.append(e)
        i.events.remove(e)
    while time.time() < end:
        m = i.read_message(max(0.05, end - time.time()))
        if m and m.get("ev") == stream:
            out.append(m)
    return out


def wait_state(i, pred, timeout=15.0, stream="connectivity"):
    """The first `stream` event whose data satisfies pred."""
    end = time.time() + timeout
    while time.time() < end:
        for e in list(i.events):
            i.events.remove(e)
            if e.get("ev") == stream and pred(e["data"]):
                return e
        m = i.read_message(max(0.05, min(0.5, end - time.time())))
        if m and m.get("ev") == stream and pred(m["data"]):
            return m
    return None


def test_ble_link_changes_are_events(nodes):
    """The simulated BLE transport's connected state (the phone) as the connectivity stream carries it."""
    n, c, i = companion(nodes, "bleev")
    d = check(first_event(i, "connectivity"), "connectivity")
    assert d["ble"] == {"connected": False}, d
    n.ui.phone(True)
    e = wait_state(i, lambda x: x["ble"]["connected"])
    assert e is not None, "no event when the BLE link came up"
    check(e, "connectivity")
    n.ui.phone(False)
    e = wait_state(i, lambda x: not x["ble"]["connected"])
    assert e is not None, "no event when the BLE link went down"


def test_usb_link_changes_are_events(nodes):
    """Subscribed over BLE, the USB host link is seen coming and going; the simulated board's serial port reports its
    client, as a native USB port reports its line state."""
    n = nodes("companion_standalone", "usbev", rtc=RTC, seed=7).start()
    c0 = CompanionClient(n)
    c0.app_start()
    assert c0.cli("set pin 424242").startswith("> pin is now")
    c0.close()
    n.restart()
    b = n.connect_ble(424242)
    b.app_start()
    i = mecon_iface.IfaceClient(b._send, b._read_frame)
    d = check(first_event(i, "connectivity"), "connectivity")
    assert d["usb"] == {"connected": False} and d["ble"] == {"connected": True}, d
    u = CompanionClient(n)
    u.app_start()
    assert wait_state(i, lambda x: x["usb"]["connected"] is True) is not None, "no event when USB attached"
    u.close()
    assert wait_state(i, lambda x: x["usb"]["connected"] is False) is not None, "no event when USB detached"


# ------------------------------------------------------------------ runtime: heap_low and the boot event

@pytest.mark.timescale(20)
def test_runtime_carries_heap_low_on_the_companion(nodes):
    n, c, i = companion(nodes, "heaplow")
    assert check(first_event(i, "runtime"), "runtime")["heap_low"] is False
    n.heap(10000)   # the largest block under the floor (20480): an episode
    end = time.time() + 30
    seen = None
    while time.time() < end and seen is not True:
        i.request("sub.stop", {"stream": "runtime"})
        d = check(first_event(i, "runtime", timeout=10), "runtime")
        seen = d["heap_low"]
        time.sleep(0.5)
    assert seen is True, "heap_low never became true"
    n.heap(100000)
    end = time.time() + 30
    while time.time() < end and seen is not False:
        i.request("sub.stop", {"stream": "runtime"})
        seen = check(first_event(i, "runtime", timeout=10), "runtime")["heap_low"]
        time.sleep(0.5)
    assert seen is False, "heap_low never ended"


def test_runtime_has_no_heap_low_on_the_repeater(nodes):
    n, cli, i = repeater(nodes, "noheaplow")
    assert "heap_low" not in check(first_event(i, "runtime"), "runtime")


def boot_events(events):
    return [e for e in events if e["data"].get("boot") is True]


def connect_iface(n, kind):
    if kind == "companion_standalone":
        c = CompanionClient(n)
        c.app_start()
        return mecon_iface.IfaceClient(c._send, c._read_frame)
    cli = CliClient(n)
    return mecon_iface.ConsoleClient(cli.sock.sendall, mecon_iface.socket_lines(cli.sock))


@pytest.mark.parametrize("kind", ["companion_standalone", "repeater"])
def test_the_boot_event_comes_once_per_epoch(nodes, kind):
    n = nodes(kind, "bootev", rtc=RTC).start()
    i = connect_iface(n, kind)
    hello = i.request("hello")["result"]
    r = i.request("sub.start", {"stream": "runtime", "period_s": 60, "lifetime_s": 60})
    assert r["status"] == "ok", r
    evs = collect(i, "runtime", 3.0)
    assert len(evs) == 2, evs                                       # the snapshot, then the boot event
    assert "boot" not in evs[0]["data"] and "loop_gap_ms" in evs[0]["data"], evs[0]
    b = check(evs[1], "runtime")
    assert evs[1]["seq"] == evs[0]["seq"] + 1 and evs[1]["epoch"] == hello["epoch"], evs
    assert b["boot"] is True and "loop_gap_ms" not in b, b
    for k in ("firmware", "upstream", "hardware", "role"):
        assert b[k] == hello[k], (k, b, hello)
    assert b["reset_reason"] is None, "the simulated board has no reset reason"
    # a new subscription in the same epoch: a snapshot, no second boot event
    assert i.request("sub.stop", {"stream": "runtime"})["status"] == "ok"
    assert i.request("sub.start", {"stream": "runtime", "period_s": 60, "lifetime_s": 60})["status"] == "ok"
    again = collect(i, "runtime", 3.0)
    assert len(again) == 1 and not boot_events(again), again
    # the next epoch gets its own
    n.restart()
    i = connect_iface(n, kind)
    h2 = i.request("hello")["result"]
    assert h2["epoch"] == hello["epoch"] + 1, h2
    assert i.request("sub.start", {"stream": "runtime", "period_s": 60, "lifetime_s": 60})["status"] == "ok"
    evs = collect(i, "runtime", 3.0)
    assert len(boot_events(evs)) == 1 and boot_events(evs)[0]["epoch"] == h2["epoch"], evs


# ------------------------------------------------------------------ logs: the audit ring as an event stream

def cli_line(text):
    return bytes([harness.CMD_RUN_CLI_COMMAND]) + text.encode()


def logs_companion(nodes, name, hardened=False):
    n, c, i = companion(nodes, name)
    if hardened:
        c.cli("set posture hardened")
    r = i.request("sub.start", {"stream": "logs", "lifetime_s": 120})
    assert r["status"] == "ok" and r["result"] == {"stream": "logs", "period_s": None, "lifetime_s": 120}, r
    return n, c, i


def test_logs_are_discovered_and_take_no_period(nodes):
    n, c, i = companion(nodes, "logsdisc")
    assert "stream.logs" in i.request("hello")["result"]["capabilities"]
    r = i.request("sub.start", {"stream": "logs", "period_s": 60})
    assert r["status"] == "invalid" and r["error"] == "no-period", r
    n2, cli, j = repeater(nodes, "logsdiscr")
    assert "stream.logs" in j.request("hello")["result"]["capabilities"]


def test_logs_send_a_posture_change_and_a_refusal_with_names(nodes):
    n, c, i = logs_companion(nodes, "logsc")
    c._send(cli_line("set posture hardened"))
    c._send(bytes([harness.CMD_EXPORT_PRIVATE_KEY]))                  # Hardened: refused on every transport
    evs = collect(i, "logs", 4.0)
    got = [(check(e, "logs")["event"], e["data"]["caller"], e["data"]["what"], e["data"]["count"]) for e in evs]
    assert got == [("posture", "cable", "hardened", 1), ("refused", "cable", "key-export", 1)], got
    assert evs[1]["seq"] == evs[0]["seq"] + 1, "one sequence for the stream"
    assert all(e["data"]["dropped"] == 0 and e["data"]["ts"] >= RTC for e in evs), evs
    # the same refusal again folds in the ring (one entry, count 2) and is sent again with the running count
    c._send(bytes([harness.CMD_EXPORT_PRIVATE_KEY]))
    again = collect(i, "logs", 3.0)
    assert [(e["data"]["event"], e["data"]["count"]) for e in again] == [("refused", 2)], again


def test_logs_on_the_repeater_console(nodes):
    n, cli, i = repeater(nodes, "logsr")
    assert i.request("sub.start", {"stream": "logs", "lifetime_s": 120})["status"] == "ok"
    cli.sock.sendall(b"set posture hardened\r")
    evs = collect(i, "logs", 4.0)
    got = [(check(e, "logs")["event"], e["data"]["caller"], e["data"]["what"]) for e in evs]
    assert ("posture", "cable", "hardened") in got, got


def test_logs_rate_is_limited_and_drops_are_counted(nodes):
    n, c, i = logs_companion(nodes, "logsrate", hardened=True)
    collect(i, "logs", 1.0)                                           # the posture change
    burst = 40
    c.sock.sendall(b"".join(b"<" + struct.pack("<H", 1) + bytes([harness.CMD_EXPORT_PRIVATE_KEY]) for _ in range(burst)))
    first = collect(i, "logs", 2.5)
    assert 1 <= len(first) <= 8, "at most 5 a second (a burst of 5): %d" % len(first)
    time.sleep(1.5)                                                   # the bucket refills
    c._send(bytes([harness.CMD_EXPORT_PRIVATE_KEY]))
    last = collect(i, "logs", 3.0)
    assert len(last) == 1, last
    allv = first + last
    assert sum(e["data"]["dropped"] for e in allv) + len(allv) == burst + 1, [e["data"] for e in allv]
    assert last[0]["data"]["dropped"] > 0 and last[0]["data"]["count"] == burst + 1, last


def test_logs_carry_no_secret(nodes):
    n, c, i = logs_companion(nodes, "logsec")
    secrets = ["hunter2-wifi-SECRET", "mqtt-pass-SECRET99"]
    c._send(cli_line("set wifi.profile 0 ssid net"))
    c._send(cli_line("set wifi.profile 0 pwd " + secrets[0]))
    c._send(cli_line("set mqtt.profile 0 type static"))
    c._send(cli_line("set mqtt.profile 0 pwd " + secrets[1]))
    c._send(cli_line("set posture hardened"))
    c._send(bytes([harness.CMD_EXPORT_PRIVATE_KEY]))
    evs = collect(i, "logs", 4.0)
    assert len(evs) >= 2, evs
    replies = [f[1:].decode(errors="replace") for f in i.native if f and f[0] == harness.RESP_CLI_REPLY]
    assert len(replies) >= 4 and not any(r.startswith("ERR") for r in replies), replies   # the passwords were set
    text = repr(evs)
    for s in secrets:
        assert s not in text and s[:8] not in text, (s, evs)
    for e in evs:
        assert set(e["data"]) == {"ts", "event", "caller", "what", "count", "dropped"}, e


def test_logs_are_refused_to_a_caller_without_the_posture_class(nodes):
    """A BLE link that paired with the published default PIN is not trusted: the table gives it no class at all."""
    n = nodes("companion_standalone", "logsble", rtc=RTC).start()
    c0 = CompanionClient(n)
    c0.app_start()
    assert c0.cli("set pin 123456").startswith("> pin is now")
    c0.close()
    n.restart()
    b = n.connect_ble(123456)
    b.app_start()
    i = mecon_iface.IfaceClient(b._send, b._read_frame)
    r = i.request("sub.start", {"stream": "logs", "lifetime_s": 60})
    assert r["status"] == "refused", r
    r = i.request("sub.start", {"stream": "health"})
    assert r["status"] == "refused", r
