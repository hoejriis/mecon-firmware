"""Uptime does not wrap with millis() after 49.7 days: the interface's uptime_s and the Repeater's stats keep counting
past 2^32 ms. --millis-start starts the node's millis() just before the wrap."""
import json
import os
import sys
import time

from sim.harness import CliClient, CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402

WRAP = 2 ** 32
START = WRAP - 8000        # 8 s before millis() wraps


def test_the_interface_uptime_keeps_counting_across_the_millis_wrap(nodes):
    n = nodes("companion_standalone", "upc", seed=11, extra=["--millis-start", str(START)]).start()
    c = CompanionClient(n)
    c.app_start()
    i = mecon_iface.IfaceClient(c._send, c._read_frame)
    before = i.request("status.get")["result"]["uptime_s"]
    time.sleep(11)                                                   # millis() wraps meanwhile
    after = i.request("status.get")["result"]["uptime_s"]
    assert before >= START // 1000, before
    assert after >= WRAP // 1000 and after > before, (before, after)  # stock: millis()/1000 back near 0


def test_the_repeater_stats_uptime_keeps_counting_across_the_millis_wrap(nodes):
    n = nodes("repeater", "upr", seed=5, extra=["--millis-start", str(START)]).start()
    cli = CliClient(n)
    before = json.loads(cli.command("stats-core"))["uptime_secs"]
    time.sleep(11)
    after = json.loads(cli.command("stats-core"))["uptime_secs"]
    cli.close()
    assert after >= WRAP // 1000 and after > before, (before, after)
