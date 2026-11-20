"""The health stream's battery level (docs/contract/TELEMETRY.md): ok, low, critical and unknown from the mean of 60 s of
readings, on a Companion and on a Repeater. The simulated board reads its voltage from "<dir>.batt" (one number, in mV;
0 reads as a board that cannot measure), so a test drives it while the node runs. The node runs 20 times faster than
real time: one 60 s window is 3 s."""
import os
import sys
import time

import pytest

from sim.harness import CliClient, CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402
import mecon_schema  # noqa: E402

RTC = 1792800000
WINDOW_S = 3.2   # 60 s of node time at 20x, and a little over
KINDS = ["companion_standalone", "repeater"]


def set_battery(n, mv):
    tmp = n.dir + ".batt.tmp"
    with open(tmp, "w") as f:
        f.write("%d\n" % mv)
    os.replace(tmp, n.dir + ".batt")


def boot(nodes, kind, name):
    n = nodes(kind, name, seed=7, rtc=RTC)
    set_battery(n, 4000)
    n.start()
    if kind == "repeater":
        cli = CliClient(n)
        return n, mecon_iface.ConsoleClient(cli.sock.sendall, mecon_iface.socket_lines(cli.sock))
    c = CompanionClient(n)
    c.app_start()
    return n, mecon_iface.IfaceClient(c._send, c._read_frame)


def status(i):
    """The data of a fresh health event: the stream is restarted for each look, so it is never an older one queued."""
    i.request("sub.stop", {"stream": "health"})
    time.sleep(0.3)
    while i.read_message(0.1) is not None:
        pass
    r = i.request("sub.start", {"stream": "health", "period_s": 5, "lifetime_s": 600})
    assert r["status"] == "ok", r
    for _ in range(40):
        m = i.read_message(0.5)
        if m and m.get("ev") == "health":
            return m["data"]
    raise AssertionError("no health event")


def level_is(i, want, timeout=12.0):
    """Poll the health event until battery_level is `want`; returns the last answer."""
    end = time.time() + timeout
    s = status(i)
    while s["battery_level"] != want and time.time() < end:
        time.sleep(0.5)
        s = status(i)
    assert s["battery_level"] == want, s
    return s


def hold(i, seconds):
    """Wait `seconds` of wall time; every health event must still say the level it had."""
    first = status(i)["battery_level"]
    end = time.time() + seconds
    while time.time() < end:
        assert status(i)["battery_level"] == first
        time.sleep(0.4)
    return first


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_the_level_walks_ok_low_critical_ok_with_hysteresis(nodes, kind):
    n, i = boot(nodes, kind, "bl")
    s = status(i)
    assert not mecon_schema.check(s, mecon_schema.load("event.health.json"))
    assert s["battery_level_method"] == "mv_mean_60s"
    assert s["battery_mv"] == 4000
    level_is(i, "ok")
    changes_ok = status(i)["battery_level_changes"]
    assert changes_ok == 1                              # unknown to ok
    set_battery(n, 3500)
    level_is(i, "low")
    assert status(i)["battery_level_changes"] == changes_ok + 1
    set_battery(n, 3300)
    level_is(i, "critical")
    assert status(i)["battery_level_changes"] == changes_ok + 2
    set_battery(n, 3450)                                # above critical, below critical + 100: stays critical
    time.sleep(WINDOW_S * 2)
    assert status(i)["battery_level"] == "critical"
    set_battery(n, 3520)                                # critical + 100 reached: low (below low + 100)
    level_is(i, "low")
    set_battery(n, 3620)                                # above low, below low + 100: stays low
    time.sleep(WINDOW_S * 2)
    assert status(i)["battery_level"] == "low"
    set_battery(n, 3700)
    s = level_is(i, "ok")
    assert s["battery_level_changes"] == changes_ok + 4
    assert not mecon_schema.check(s, mecon_schema.load("event.health.json"))


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_a_dip_shorter_than_the_mean_does_not_move_the_level(nodes, kind):
    n, i = boot(nodes, kind, "bd")
    level_is(i, "ok")
    changes = status(i)["battery_level_changes"]
    # a dip of a third of the time, to a critical voltage, in step with no window: a window ends inside one now and then,
    # so a level taken from the last reading would trip; the mean of a window is about 3670 mV
    end = time.time() + 8 * WINDOW_S
    while time.time() < end:
        set_battery(n, 3000)
        time.sleep(0.5)
        set_battery(n, 4000)
        time.sleep(1.0)
    s = status(i)
    assert s["battery_level"] == "ok" and s["battery_level_changes"] == changes, s


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_an_invalid_reading_is_unknown_never_critical(nodes, kind):
    n, i = boot(nodes, kind, "bi")
    level_is(i, "ok")
    set_battery(n, 0)                                   # the board cannot measure
    s = level_is(i, "unknown")
    assert s["battery_mv"] is None
    assert s["battery_level_method"] == "mv_mean_60s"
    time.sleep(WINDOW_S * 2)
    assert status(i)["battery_level"] == "unknown"      # it never becomes critical
    set_battery(n, 5000)                                # out of range: also not a reading
    time.sleep(WINDOW_S * 2)
    assert status(i)["battery_level"] == "unknown"
    set_battery(n, 4000)
    level_is(i, "ok")


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_the_level_is_unknown_before_the_first_window_and_the_source_is_unknown(nodes, kind):
    n, i = boot(nodes, kind, "bu")
    s = status(i)
    assert s["battery_level"] == "unknown" and s["battery_level_changes"] == 0, s
    assert s["power_source"] == "unknown"               # the simulated board senses no power source
    level_is(i, "ok")


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_the_health_stream_carries_the_fields(nodes, kind):
    n, i = boot(nodes, kind, "bh")
    set_battery(n, 3300)
    level_is(i, "critical")
    assert i.request("sub.start", {"stream": "health", "period_s": 5, "lifetime_s": 60})["status"] == "ok"
    got = None
    for _ in range(40):
        m = i.read_message(1.0)
        if m and m.get("ev") == "health":
            got = m
            break
    assert got, "no health event"
    d = got["data"]
    assert not mecon_schema.check(d, mecon_schema.load("event.health.json")), d
    assert d["battery_mv"] == 3300 and d["battery_level"] == "critical"
    assert d["battery_level_method"] == "mv_mean_60s" and d["power_source"] == "unknown"
    assert d["battery_level_changes"] == 1   # unknown to critical, nothing since
