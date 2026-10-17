"""The enforced heap floor (src/helpers/HeapFloor.h) on the standalone Companion.

The simulator's heap is a number the test sets while the node runs (`Node.heap(free, largest)`, the node re-reads
`--heap-file`), so a low episode can be made, held, ended and made again. The check looks once after setup and then
every 60 s of node time; these tests run at time scale 20, so a look comes about every 3 s.

What is verified here: the state change at the floor and at floor + hysteresis, the largest block (not the total) as the
figure, exactly one audit entry per episode, the `heap_low` / `floor` fields of `get mecon.mem`, and the LOW MEM marker on
the Companion's home status line, all end to end through the real application. Not verified: a real allocator, real
fragmentation, or the figure on a real board (those are the hardware sessions).
"""
import re
import time

import pytest

from sim.harness import CompanionClient, audit_entries

RTC = 1760000000
FLOOR, HYST = 20480, 2048
pytestmark = pytest.mark.timescale(20)


def _client(node):
    c = CompanionClient(node)
    c.device_query()
    c.app_start()
    return c


def start(nodes, name, seed=7, **kw):
    n = nodes("companion_standalone", name, seed=seed, rtc=RTC, **kw).start()
    return n, _client(n)


def mem(c):
    """`get mecon.mem` parsed: {"free", "largest", "min_free", "heap_low", "floor"}."""
    text = c.cli("get mecon.mem")
    m = re.fullmatch(r"> heap free=(\d+) largest=(\d+) min_free=(\d+) heap_low=([01]) floor=(\d+)", text)
    assert m, text
    return dict(zip(("free", "largest", "min_free", "heap_low", "floor"), map(int, m.groups())))


def wait_low(c, want, what, timeout=20.0):
    end = time.time() + timeout
    while time.time() < end:
        if mem(c)["heap_low"] == want:
            return
        time.sleep(0.2)
    raise AssertionError("heap_low never became %d (%s): %s" % (want, what, mem(c)))


def low_entries(c):
    return [e for e in audit_entries(c.cli) if " low-mem " in e]


def settle(node, seconds=8.0):
    """Let several looks (one about every 3 s at this time scale) go by."""
    time.sleep(seconds)


def home_settled(node):
    assert node.ui.wait_text("Unread Msg", 20), node.ui.last_text()
    time.sleep(1.0)


def test_a_healthy_heap_reports_no_low_state_and_the_default_floor(nodes):
    n, c = start(nodes, "hf0")
    r = mem(c)
    assert r["heap_low"] == 0 and r["floor"] == FLOOR, r
    assert r["largest"] == 300000, r
    settle(n)
    assert low_entries(c) == []


def test_the_first_look_comes_right_after_setup_not_a_minute_later(nodes):
    """Time scale 1 here: the period is 60 s of real time, so a state seen within a few seconds of the boot is the first
    look after setup, not the second."""
    pre = nodes("companion_standalone", "hf1", seed=7, rtc=RTC, time_scale=1.0)
    pre.heap(300000, 5000)                       # low from the first instant
    pre.start()
    c = _client(pre)
    wait_low(c, 1, "the first look after setup", timeout=10.0)
    assert len(low_entries(c)) == 1


def test_an_episode_starts_below_the_floor_holds_until_the_hysteresis_and_ends(nodes):
    n, c = start(nodes, "hf2")
    n.heap(300000, FLOOR)                        # exactly the floor is not low
    settle(n, 7)
    assert mem(c)["heap_low"] == 0 and low_entries(c) == []
    n.heap(300000, FLOOR - 1)                    # one byte below is
    wait_low(c, 1, "a block one byte under the floor")
    n.heap(300000, FLOOR)                        # back at the floor: still the same episode
    settle(n, 7)
    assert mem(c)["heap_low"] == 1
    n.heap(300000, FLOOR + HYST - 1)             # one byte short of floor + hysteresis
    settle(n, 7)
    assert mem(c)["heap_low"] == 1
    n.heap(300000, FLOOR + HYST)                 # floor + hysteresis ends it
    wait_low(c, 0, "a block at floor + hysteresis")


def test_one_audit_entry_per_episode_however_long_it_lasts(nodes):
    n, c = start(nodes, "hf3")
    n.heap(300000, 1000)
    wait_low(c, 1, "the first episode")
    settle(n, 10)                                # three or more further looks while still low
    e = low_entries(c)
    assert len(e) == 1 and e[0].endswith("low-mem system heap-floor x1"), e
    n.heap(300000, 300000)
    wait_low(c, 0, "the end of the first episode")
    assert low_entries(c)[0].endswith("x1")      # recovering adds nothing
    n.heap(300000, 1000)
    wait_low(c, 1, "the second episode")
    settle(n, 7)
    e = low_entries(c)
    assert len(e) == 1 and e[0].endswith("low-mem system heap-floor x2"), e   # folded: one entry, a count of episodes


def test_the_figure_is_the_largest_block_not_the_total_free_heap(nodes):
    n, c = start(nodes, "hf4")
    n.heap(300000, 1000)                         # plenty free, no usable block
    wait_low(c, 1, "fragmentation")
    r = mem(c)
    assert r["free"] == 300000 and r["largest"] == 1000 and r["heap_low"] == 1, r
    n.heap(15000, 90000)                         # (not a real heap) a small total but a big block
    wait_low(c, 0, "a big block")


def test_the_marker_is_on_the_home_status_line_only_while_low(nodes):
    n, c = start(nodes, "hf5")
    home_settled(n)
    assert "LOW MEM" not in n.ui.last_text(), n.ui.last_text()
    n.heap(300000, 1000)
    t = n.ui.wait_text("LOW MEM", 20)
    assert t, n.ui.last_text()
    assert "BLE LOW MEM" in t.splitlines(), t    # the status line: the mode first, the marker after it
    n.heap(300000, 300000)
    assert n.ui.wait_gone("LOW MEM", 20), n.ui.last_text()
    assert "BLE" in n.ui.last_text().splitlines()


def test_the_marker_replaces_a_status_line_that_would_not_fit_beside_it(nodes):
    n, c = start(nodes, "hf6")
    assert c.cli("set pin 123456").startswith("> pin is now")     # "BLE (default PIN!)" is too long for the marker
    n.restart()
    c = _client(n)
    home_settled(n)
    n.heap(300000, 1000)
    t = n.ui.wait_text("LOW MEM", 20)
    assert t and "LOW MEM" in t.splitlines() and "default" not in t, t
    # the connectivity page still says it
    for _ in range(12):
        if "Mode:" in n.ui.last_text():
            break
        before = n.ui.last_text()
        n.ui.press("click")
        end = time.time() + 15
        while time.time() < end and n.ui.last_text() == before:
            time.sleep(0.05)
    assert n.ui.wait_text("Pin: DEFAULT (unsafe)", 20), n.ui.last_text()


def test_the_low_state_is_not_kept_across_a_boot_but_the_heap_decides_again(nodes):
    n, c = start(nodes, "hf7")
    n.heap(300000, 1000)
    wait_low(c, 1, "an episode")
    n.heap(300000, 300000)                       # the heap recovered, then the device is power-cycled
    n.restart()
    c = _client(n)
    assert mem(c)["heap_low"] == 0
    settle(n, 7)
    assert mem(c)["heap_low"] == 0
