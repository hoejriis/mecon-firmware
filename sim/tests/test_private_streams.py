"""Private telemetry (docs/contract/TELEMETRY.md, class private) on the simulated Companion and Repeater. The owner's rule,
2026-10-28: "All this should go via the broker to the backend". The location stream reaches the cable and a TLS-verified
broker session (unlike logs, which a broker session is refused), and never an untrusted link. The simulated receiver is
driven by "<dir>.gps" (variants/sim/target.h); without it the board has no receiver and the stream is absent."""
import os
import sys
import time

import pytest
from sim.harness import CliClient, CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402
import mecon_schema  # noqa: E402

from test_telemetry_streams import RTC, check, first_event
from test_device_interface import broker, client, mqtt_boot  # noqa: F401  (broker is a fixture)

FIX = "fix 59913900 10752200 23456 9 4\n"   # Oslo, 23.456 m, 9 satellites, fix 4 s old
FIX_DATA = {"fix": True, "lat_e6": 59913900, "lon_e6": 10752200, "alt_m": 23, "sats": 9, "age_s": 4}
NOFIX_DATA = {"fix": False, "lat_e6": None, "lon_e6": None, "alt_m": None, "sats": 2, "age_s": None}


def gps(n, text):
    with open(n.dir + ".gps", "w") as f:
        f.write(text)


def attach(n, kind):
    if kind == "repeater":
        cli = CliClient(n)
        return mecon_iface.ConsoleClient(cli.sock.sendall, mecon_iface.socket_lines(cli.sock))
    c = CompanionClient(n)
    c.app_start()
    return mecon_iface.IfaceClient(c._send, c._read_frame)


@pytest.mark.parametrize("kind", ["companion_standalone", "repeater"])
def test_location_is_absent_without_a_receiver(nodes, kind):
    n = nodes(kind, "noloc", rtc=RTC).start()
    i = attach(n, kind)
    assert "stream.location" not in i.request("hello")["result"]["capabilities"]
    r = i.request("sub.start", {"stream": "location", "lifetime_s": 60})
    assert r["status"] == "invalid" and r["error"] == "unknown-stream", r


@pytest.mark.parametrize("kind", ["companion_standalone", "repeater"])
def test_location_carries_the_fix_and_nulls_without_one(nodes, kind):
    n = nodes(kind, "loc", rtc=RTC)
    gps(n, FIX)
    n.start()
    i = attach(n, kind)
    assert "stream.location" in i.request("hello")["result"]["capabilities"]
    assert check(first_event(i, "location"), "location") == FIX_DATA
    gps(n, "nofix 2\n")
    assert i.request("sub.stop", {"stream": "location"})["status"] == "ok"
    assert check(first_event(i, "location"), "location") == NOFIX_DATA, "no position without a fix"


def test_location_is_refused_on_default_pin_ble(nodes):
    n = nodes("companion_standalone", "locb", seed=61, rtc=RTC)
    gps(n, FIX)
    n.start()
    u = CompanionClient(n)
    u.app_start()
    assert u.cli("set pin 123456").startswith("> pin is now")
    u.close()
    n.restart()
    b = CompanionClient(n, sock=n.connect_ble(123456).sock)
    b.device_query()
    b.app_start()
    bi = mecon_iface.IfaceClient(b._send, b._read_frame)
    r = bi.request("sub.start", {"stream": "location", "lifetime_s": 60})
    assert r["status"] == "refused", r
    assert b"lat_e6" not in r.raw


@pytest.mark.timescale(10)
def test_location_reaches_a_verified_broker_session(nodes, tmp_path, broker):
    """Unlike logs (class posture, refused to a broker session), location is class private, which the owner's rule
    gives to the TLS-verified broker session: the Backend receives it without signing."""
    b = broker()
    n, c, i, dev = mqtt_boot(nodes, tmp_path, "locm", b)
    gps(n, FIX)
    n.stop()   # the receiver is detected at boot
    n.start()
    c, i = client(n)
    end = time.time() + 40
    while time.time() < end and not c.cli("get mqtt.status").startswith("> connected"):
        time.sleep(0.3)
    assert c.cli("get mqtt.status").startswith("> connected")
    time.sleep(2.0)
    r = b.iface_request(dev, {"v": 1, "id": "loc1", "op": "sub.start", "args": {"stream": "location", "period_s": 30, "lifetime_s": 120}})
    assert r["status"] == "ok", r
    got = b.next_message("mecon/%s/ev/location" % dev, timeout=30, count=1)   # the next periodic event
    assert got and got[0]["data"] == FIX_DATA, got
    assert not mecon_schema.check(got[0]["data"], mecon_schema.load("event.location.json"))


# ------------------------------------------------------------------ inventory entries (Companion)

SECRET = bytes(range(0x40, 0x50))   # a channel secret that must never appear in any event


def snapshot(i, timeout=30.0):
    """sub.start inventory and collect one snapshot's pages until last; returns (pages, entries)."""
    r = i.request("sub.start", {"stream": "inventory", "lifetime_s": 300})
    assert r["status"] == "ok", r
    pages, end = [], time.time() + timeout
    while time.time() < end:
        m = i.read_message(0.5)
        if not m or m.get("ev") != "inventory":
            continue
        pages.append(m)
        if m["data"].get("last"):
            break
    assert pages and pages[-1]["data"]["last"], pages
    snap = pages[0]["data"]["snapshot"]
    assert [p["data"]["part"] for p in pages] == list(range(1, len(pages) + 1))
    assert all(p["data"]["snapshot"] == snap for p in pages)
    entries = [e for p in pages for e in p["data"]["entries"]]
    return pages, entries


def test_inventory_snapshot_carries_every_contact_and_channel_once_and_no_secret(nodes):
    n = nodes("companion_standalone", "inv", seed=63, rtc=RTC).start()
    c = CompanionClient(n)
    c.app_start()
    keys = [bytes([k]) + bytes(range(1, 32)) for k in range(1, 31)]   # 30 contacts: several pages
    for j, k in enumerate(keys):
        c.add_contact(k, "C%02d \"q\"" % j)
    assert c.set_channel(1, "Team", SECRET)[0] == 0
    i = mecon_iface.IfaceClient(c._send, c._read_frame)
    pages, entries = snapshot(i)
    assert len(pages) > 1, "30 contacts do not fit one page"
    for p in pages:
        assert len(p.raw) <= 1536, len(p.raw)   # under the link queue's and the MQTT client's limits
        check(p, "inventory")
        assert SECRET.hex().encode() not in p.raw and SECRET.hex().upper().encode() not in p.raw
        assert p["data"]["contacts"]["count"] == 30
    got = sorted(e["key"] for e in entries if e["kind"] == "contact")
    assert got == sorted(k.hex() for k in keys), "every contact exactly once"
    names = {e["name"] for e in entries if e["kind"] == "contact"}
    assert "C07 \"q\"" in names
    chans = [e for e in entries if e["kind"] == "channel"]
    assert {"kind": "channel", "index": 1, "name": "Team"} in chans
    assert all(set(e) == {"kind", "index", "name"} for e in chans), "a channel entry carries no secret"


def test_inventory_snapshot_over_a_verified_broker_session(nodes, tmp_path, broker):
    b = broker()
    n, c, i, dev = mqtt_boot(nodes, tmp_path, "invm", b)
    c.add_contact(bytes(range(32)), "Bob")
    r = b.iface_request(dev, {"v": 1, "id": "inv1", "op": "sub.start", "args": {"stream": "inventory", "period_s": 300}})
    assert r["status"] == "ok", r
    # the first snapshot is sent at once, before this subscriber is listening: restart it so the pages are seen
    got = []
    for attempt in range(3):
        assert b.iface_request(dev, {"v": 1, "id": "inv-s%d" % attempt, "op": "sub.stop", "args": {"stream": "inventory"}})["status"] == "ok"
        import threading
        res = {}
        t = threading.Thread(target=lambda: res.update(m=b.next_message("mecon/%s/ev/inventory" % dev, timeout=15, count=1)))
        t.start()
        time.sleep(1.0)
        assert b.iface_request(dev, {"v": 1, "id": "inv-r%d" % attempt, "op": "sub.start", "args": {"stream": "inventory", "period_s": 300}})["status"] == "ok"
        t.join()
        got = res.get("m") or []
        if got:
            break
    assert got, "no inventory page over MQTT"
    d = got[0]["data"]
    assert d["part"] == 1 and d["contacts"]["count"] == 1
    assert any(e.get("key") == bytes(range(32)).hex() for e in d["entries"]), d
