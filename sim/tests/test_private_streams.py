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
