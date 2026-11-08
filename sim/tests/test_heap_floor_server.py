"""The enforced heap floor (src/helpers/HeapFloor.h) on the Repeater and the Room server, as on the Companion (Issue #440).

The simulated boards build as ESP32, where the floor is 20,480 B; the test sets the simulator's heap while the node runs
(`Node.heap(free, largest)`). It checks the low state in `get mecon.mem`, one `low-mem` audit entry per episode, the end
of an episode at floor + hysteresis, and `heap_low` in the runtime stream. Not verified: a real allocator or a real board.
"""
import re
import time

import pytest

from sim.harness import CliClient, audit_entries

RTC = 1760000000
FLOOR, HYST = 20480, 2048
KINDS = ["repeater", "room_server"]
pytestmark = pytest.mark.timescale(20)


def mem(cli):
    text = cli.command("get mecon.mem")
    m = re.search(r"heap free=(\d+) largest=(\d+) min_free=(\d+) heap_low=([01]) floor=(\d+)", text)
    assert m, text
    return dict(zip(("free", "largest", "min_free", "heap_low", "floor"), map(int, m.groups())))


def wait_low(cli, want, what, timeout=20.0):
    end = time.time() + timeout
    while time.time() < end:
        if mem(cli)["heap_low"] == want:
            return
        time.sleep(0.2)
    raise AssertionError("heap_low never became %d (%s): %s" % (want, what, mem(cli)))


def low_entries(cli):
    return [e for e in audit_entries(cli.command) if " low-mem " in e]


@pytest.mark.parametrize("kind", KINDS)
def test_a_server_checks_the_floor_like_the_companion(nodes, kind):
    n = nodes(kind, "hs" + kind[:2], seed=9, rtc=RTC).start()
    cli = CliClient(n)
    r = mem(cli)
    assert r["heap_low"] == 0 and r["floor"] == FLOOR, "the check runs on this role: %s" % r
    n.heap(300000, FLOOR - 1)
    wait_low(cli, 1, "a block one byte under the floor")
    time.sleep(7)
    assert len(low_entries(cli)) == 1, "one audit entry for the episode, however long it lasts"
    n.heap(300000, FLOOR + HYST)
    wait_low(cli, 0, "a block at floor + hysteresis")


@pytest.mark.parametrize("kind", KINDS)
def test_the_runtime_stream_carries_heap_low_on_a_server(nodes, kind):
    import os, sys
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
    from test_deployment_authority_server import Srv
    n = nodes(kind, "hr" + kind[:2], seed=10, rtc=RTC).start()
    d = Srv(n)
    n.heap(300000, FLOOR - 1)
    wait_low(d.cli, 1, "low before subscribing")
    assert d.i.request("sub.start", {"stream": "runtime", "period_s": 60, "lifetime_s": 120})["status"] == "ok"
    end = time.time() + 20
    while time.time() < end:
        m = d.i.read_message(0.5)
        if m and m.get("ev") == "runtime":
            assert m["data"].get("heap_low") is True, m["data"]
            return
    raise AssertionError("no runtime event")
