"""The Sensor role on the simulated board: the real example's mesh and CLI, with no sensor hardware (Issue #249).

It shares CommonCLI with the Repeater and the Room server, and the duty-cycle window and regulatory floor in the
Dispatcher. It has no device interface and no posture (docs/CLI_REFERENCE.md)."""
import re

from sim.harness import CliClient

RTC = 1760000000
BAND = re.compile(r"^> band (\S+?)(?: MHz)?, limit (\d+\.\d\d)%, floor (\S+), user (\d+\.\d\d)%, used (\d+) ms \((\d+\.\d\d)%\)$")


def start(nodes, name):
    n = nodes("sensor", name, seed=5, rtc=RTC).start()
    return n, CliClient(n)


def test_a_sensor_boots_and_keeps_its_settings(nodes):
    n, c = start(nodes, "sensor_boot")
    assert c.command("get name").startswith("> ")
    assert c.command("set name Lab sensor").startswith("OK")
    c.close()
    n.restart()
    c = CliClient(n)
    assert c.command("get name") == "> Lab sensor"
    assert c.command("get role") == "> sensor"


def test_get_dutycycle_reports_the_band_and_floor_like_the_other_roles(nodes):
    n, c = start(nodes, "sensor_duty")
    assert c.command("set radio 868.1,125,10,5").startswith("OK")
    c.close()
    n.restart()
    c = CliClient(n)
    m = BAND.match(c.command("get dutycycle"))
    assert m and m.group(1) == "868.000-868.600" and m.group(2) == "1.00" and m.group(3) == "1.00%", m
    assert c.command("set radio 915.0,250,10,5").startswith("OK")
    c.close()
    n.restart()
    c = CliClient(n)
    m = BAND.match(c.command("get dutycycle"))
    assert m and m.group(1) == "none" and m.group(3) == "none", m


def test_a_sensor_has_no_device_interface_and_no_posture(nodes):
    n, c = start(nodes, "sensor_gaps")
    assert c.command("get posture").startswith("??")
    reply = c.command('mecon> {"v":1,"id":"a","op":"hello"}', timeout=2.0)
    assert '"status":"ok"' not in (reply or "")
