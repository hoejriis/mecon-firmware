"""What the heap was doing at its lowest (src/helpers/HeapWatch.h; #513), end to end on the standalone Companion: the
lowest largest block, with what was running and when, in `get mecon.heaplow` and in the runtime stream's `heap_lows`.

The simulator's heap is a number the test sets (`Node.heap(free, largest)`), re-read at most every 100 ms, and it tracks
no min_free, so `min_free` reads `none` and null here. Which name a low is put down to depends on which look sees the new
number first; the native unit test (test/test_heap_watch) pins that logic, so here the name is only checked to be one
the firmware gives."""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from test_heap_floor import start  # noqa: E402
from test_telemetry_streams import check, first_event  # noqa: E402

import mecon_iface  # noqa: E402

NAMES = re.compile(r"^(idle|request|mqtt\.(connect|request|publish)|stream(\.[a-z]+)?|[a-z]+(\.[a-z_]+)+)$")


def heaplow(c):
    text = c.cli("get mecon.heaplow")
    m = re.fullmatch(r"> largest=(none|(\d+) by=(\S+) at=(\d+)s) min_free=(none|.*)", text)
    assert m, text
    return text, (None if m.group(1) == "none" else (int(m.group(2)), m.group(3), int(m.group(4)))), m.group(5)


def test_the_lowest_largest_block_is_named_and_kept(nodes):
    n, c = start(nodes, "hw1")
    n.heap(300000, 120000)
    time.sleep(1.5)
    _, low, minf = heaplow(c)
    assert low and low[0] == 120000 and NAMES.match(low[1]), low
    assert minf == "none", "the simulator tracks no min_free"
    n.heap(300000, 90000)                          # lower: a new low, named
    c.cli("get mecon.mem")
    time.sleep(1.5)
    _, low2, _ = heaplow(c)
    assert low2[0] == 90000 and NAMES.match(low2[1]) and low2[2] >= low[2], low2
    n.heap(300000, 150000)                         # higher again: the low and its name stay
    time.sleep(1.5)
    _, low3, _ = heaplow(c)
    assert low3 == low2, (low2, low3)


def test_the_runtime_stream_carries_both_lows(nodes):
    n, c = start(nodes, "hw2")
    n.heap(300000, 64000)
    time.sleep(1.5)
    i = mecon_iface.IfaceClient(c._send, c._read_frame)
    d = check(first_event(i, "runtime"), "runtime")   # the event schema, heap_lows included
    lows = d["heap_lows"]
    assert set(lows) == {"largest", "min_free"}, lows
    assert lows["largest"]["bytes"] == 64000 and NAMES.match(lows["largest"]["by"]), lows
    assert lows["min_free"] is None, "untracked in the simulator: null, not zero"
    assert d["heap_low"] is False
