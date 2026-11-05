"""The clock counts as set only once a real source has set it (RTCClock::isSet). A clock that runs on from a seed or from
a contact's time reads as a plausible date, so nothing may state it as the time: no rx_ts, ts null, audit entries at 0.
Simulated boards; `rtc_unset` makes the starting value a seed, as on a board nothing has set the time of."""
import os
import struct
import sys
import time

import pytest

from sim.ether import Ether  # noqa: F401
from sim.harness import CliClient, CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import mecon_iface  # noqa: E402
from test_device_interface import next_event  # noqa: E402
from test_inbox import CONTACT_WRITE_S, RTC, listed, pair, wait_received  # noqa: E402

CMD_GET_DEVICE_TIME, CMD_SET_DEVICE_TIME, RESP_CURR_TIME = 5, 6, 9


def start(nodes, name, **kw):
    n = nodes("companion_standalone", name, seed=51, rtc=RTC, **kw).start()
    return (n,) + client(n)


def client(n):
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    return c, mecon_iface.IfaceClient(c._send, c._read_frame)


def set_clock(c, secs):
    assert c.request(bytes([CMD_SET_DEVICE_TIME]) + struct.pack("<I", secs))[0] == 0


def now(c):
    f = c.request(bytes([CMD_GET_DEVICE_TIME]))
    assert f[0] == RESP_CURR_TIME, f
    return struct.unpack_from("<I", f, 1)[0]


def test_a_message_has_no_received_time_until_the_clock_is_set(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether, reader_kw={"rtc_unset": True})
    assert listed(ir)["messages"] == [], "the first read makes the slots"
    assert ir.request("sub.start", {"stream": "observations", "lifetime_s": 120})["status"] == "ok"
    assert cs.send_direct(cr.pub_key, "before", RTC + 5)[0] != 1
    wait_received(ir, 1)
    heard = [e for e in ir.events if e.get("ev") == "observations"]
    assert heard and all(e["ts"] is None for e in heard), heard
    m = listed(ir)["messages"][0]
    assert "rx_ts" not in m and m["ts"] == RTC + 5 and m["age_s"] >= 0, m
    told = [e for e in ir.events if e.get("ev") == "inbox"]
    assert told and all(e["ts"] is None for e in told), told

    set_clock(cr, RTC + 100)
    ir.events.clear()
    assert cs.send_direct(cr.pub_key, "after", RTC + 6)[0] != 1
    wait_received(ir, 2)
    m = listed(ir)["messages"][-1]
    assert m["text"] == "after" and m["rx_ts"] >= RTC + 100, m
    told = [e for e in ir.events if e.get("ev") == "inbox"]
    assert told and all(e["ts"] >= RTC + 100 for e in told), told


def test_a_stream_event_has_no_time_until_the_clock_is_set(nodes):
    n, c, i = start(nodes, "stream", rtc_unset=True)
    assert i.request("sub.start", {"stream": "health", "period_s": 5, "lifetime_s": 120})["status"] == "ok"
    e = next_event(i, "health", 15)
    assert e is not None and e["ts"] is None, e
    set_clock(c, RTC + 100)
    i.events.clear()
    e = next_event(i, "health", 15)
    assert e is not None and e["ts"] >= RTC + 100, e


def test_an_inventory_event_has_no_time_until_the_clock_is_set(nodes):
    n, c, i = start(nodes, "inv", rtc_unset=True)
    assert i.request("sub.start", {"stream": "inventory", "lifetime_s": 120})["status"] == "ok"
    e = next_event(i, "inventory", 15)
    assert e is not None and e["ts"] is None, e


def test_an_audit_entry_keeps_no_guessed_time(nodes):
    n, c, i = start(nodes, "audit", rtc_unset=True)
    assert i.request("sub.start", {"stream": "logs", "lifetime_s": 120})["status"] == "ok"
    assert "hardened" in c.cli("set posture hardened")
    e = next_event(i, "logs", 15)
    assert e is not None and e["ts"] is None and e["data"]["ts"] is None, e
    assert "\n0 " in c.cli("get audit"), "the entry was recorded with no time"

    set_clock(c, RTC + 100)
    assert "normal" in c.cli("set posture normal confirm")
    e = next_event(i, "logs", 15)
    assert e is not None and e["ts"] >= RTC + 100 and e["data"]["ts"] >= RTC + 100, e


def test_a_job_event_has_no_time_until_the_clock_is_set(nodes):
    n, c, i = start(nodes, "job", rtc_unset=True)
    tx = i.request("config.get", {"keys": ["tx"]})["result"]["values"]["tx"]
    r = i.request("config.apply", {"set": {"tx": tx - 1}, "confirm": {"within_s": 60}})
    assert r["status"] == "ok", r
    e = next_event(i, "job", 10) or next(x for x in i.events if x.get("ev") == "job")
    assert e["ts"] is None, e
    set_clock(c, RTC + 100)
    i.events.clear()
    assert i.request("job.confirm", {"job": r["result"]["job"]})["status"] == "ok"
    told = [x for x in i.events if x.get("ev") == "job"] or [next_event(i, "job", 10)]
    assert told[0] is not None and told[0]["ts"] >= RTC + 100, told


@pytest.mark.parametrize("kind", ["repeater", "room_server"])
def test_a_job_event_on_a_server_role_has_no_time_until_the_clock_is_set(nodes, kind):
    n = nodes(kind, "job" + kind, rtc=RTC, rtc_unset=True).start()
    cli = CliClient(n)
    i = mecon_iface.ConsoleClient(cli.sock.sendall, mecon_iface.socket_lines(cli.sock))
    tx = i.request("config.get", {"keys": ["tx"]})["result"]["values"]["tx"]
    r = i.request("config.apply", {"set": {"tx": tx - 1}, "confirm": {"within_s": 60}})
    assert r["status"] == "ok", r
    e = next((x for x in i.events if x.get("ev") == "job"), None) or next_event(i, "job", 10)
    assert e is not None and e["ts"] is None, e


def test_a_contacts_time_runs_the_clock_on_but_does_not_set_it(nodes):
    n, c, i = start(nodes, "guess", rtc_unset=True)
    assert c.add_contact_full(bytes(range(32)), "future", lastmod=RTC + 100000)[0] == 0
    time.sleep(CONTACT_WRITE_S)
    n.stop()
    n.start()
    c, i = client(n)
    assert now(c) >= RTC + 100000, "stock behaviour: it starts from the newest contact"
    assert i.request("sub.start", {"stream": "health", "period_s": 5, "lifetime_s": 120})["status"] == "ok"
    e = next_event(i, "health", 15)
    assert e is not None and e["ts"] is None, e


def test_a_set_clock_is_not_replaced_by_a_contacts_time(nodes):
    n, c, i = start(nodes, "kept")
    assert c.add_contact_full(bytes(range(32)), "future", lastmod=RTC + 100000)[0] == 0
    time.sleep(CONTACT_WRITE_S)
    n.stop()
    n.start()
    c, i = client(n)
    assert now(c) < RTC + 100000, "a real time stays"
