"""The battery guard's protective sleep (src/helpers/BatterySleep.h, docs/contract/OPERATIONS.md) on a Companion and on a
Repeater. The simulated board reads its battery from "<dir>.batt" (test_battery_level.py) and keeps the guard's RTC memory
in "<dir>.rtcmem" (variants/sim/sim_battery.cpp). A protective sleep ends the process with status 77 and records the
seconds it asked for in "<dir>.slept"; the test plays the timer and starts the node again, which boots as a timer wake.
The node runs 20 times faster than real time: a 60 s window is 3 s, the three windows before a sleep 9 s."""
import json
import os
import re
import subprocess
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_schema  # noqa: E402
from test_battery_level import RTC, WINDOW_S, level_is, set_battery, status  # noqa: E402
from test_deployment_authority import Dev, wrap  # noqa: E402
from test_deployment_authority_server import Srv  # noqa: E402
from test_device_ops import next_job  # noqa: E402

KINDS = ["companion_standalone", "repeater"]
SLEPT = 77
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def node(nodes, kind, name, mv=3800, **kw):
    n = nodes(kind, name, seed=7, rtc=RTC, **kw)
    set_battery(n, mv)
    return n


def attach(n, kind):
    return Dev(n) if kind.startswith("companion") else Srv(n)


def boot(nodes, kind, name, mv=3800, **kw):
    n = node(nodes, kind, name, mv, **kw)
    n.start()
    return attach(n, kind)


def line(d):
    return d.c.cli if isinstance(d, Dev) else d.cli.command


def guard(d):
    s = status(d.i)
    assert not mecon_schema.check(s, mecon_schema.load("event.health.json")), s["battery_guard"]
    return s["battery_guard"]


def slept(n):
    p = n.dir.rstrip("/") + ".slept"
    return [int(x) for x in open(p).read().split()] if os.path.exists(p) else []


def asleep(n, timeout=30):
    """Wait for the node to enter a protective sleep (exit 77)."""
    return n.wait_exit(timeout) == SLEPT


def wake(n, cause=None, settle=2.5):
    """Start the node as its wake timer (or `cause`: "button", "brownout") would. None while it runs, else its exit code."""
    if cause:
        with open(n.dir.rstrip("/") + ".wake", "w") as f:
            f.write(cause + "\n")
    try:
        n.start()
    except RuntimeError:
        pass   # it went back to sleep before it published its serial port
    end = time.time() + settle   # the boot guard's three bursts take 10 s of node time, half a second here
    while time.time() < end:
        if n.proc.poll() is not None:
            return n.proc.returncode
        time.sleep(0.05)
    return None


def power_on(n):
    """A power-on: RTC memory is lost."""
    for ext in (".rtcmem", ".wake"):
        p = n.dir.rstrip("/") + ext
        if os.path.exists(p):
            os.unlink(p)


def until(d, want, timeout=20):
    end = time.time() + timeout
    g = guard(d)
    while g["effective"] != want and time.time() < end:
        time.sleep(0.4)
        g = guard(d)
    assert g["effective"] == want, g
    return g


def sleep_once(nodes, kind, name, **kw):
    """A node armed at 3800 mV, then critical until it sleeps. Returns the node."""
    d = boot(nodes, kind, name, **kw)
    assert line(d)("set battery.guard enabled=on") == "OK"
    set_battery(d.n, 3300)
    assert asleep(d.n), "the guard slept"
    return d.n


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_with_the_guard_off_a_flat_battery_never_sleeps(nodes, kind):
    d = boot(nodes, kind, "bs0", mv=3000)
    level_is(d.i, "critical")
    end = time.time() + 5 * WINDOW_S
    while time.time() < end:
        assert d.n.proc.poll() is None, "the node stayed up"
        g = guard(d)
        assert g["effective"] == "off", g
        assert g["failed_recoveries"] is None and g["sleeps"] is None and g["last_entry"] is None, g
        time.sleep(1)
    assert slept(d.n) == [] and not os.path.exists(d.n.dir.rstrip("/") + ".rtcmem")


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_critical_sleeps_a_wake_below_recovery_sleeps_again_touching_no_file_and_three_bursts_resume(nodes, kind):
    d = boot(nodes, kind, "bs1")
    assert line(d)("set battery.guard enabled=on") == "OK"
    g = until(d, "armed")
    assert g["recover_mv"] == 3650 and g["interval_s"] == 900 and g["sleeps"] == 0 and g["failed_recoveries"] == 0, g
    set_battery(d.n, 3300)
    until(d, "confirming", timeout=8)
    assert asleep(d.n)
    assert slept(d.n) == [900]
    # still low: the wake sleeps again from the boot guard, before the file system is mounted
    calls = d.n.dir.rstrip("/") + ".fscalls"
    d.n.extra += ["--fs-calls", calls]
    set_battery(d.n, 3600)   # a rebound above critical, still under the recovery voltage
    assert wake(d.n) == SLEPT
    assert slept(d.n) == [900, 900]
    assert not os.path.exists(calls), "a wake that goes back to sleep makes no file system call: %s" % (
        open(calls).read() if os.path.exists(calls) else "")
    d.n.extra.remove("--fs-calls")
    d.n.extra.remove(calls)
    set_battery(d.n, 3700)
    assert wake(d.n) is None, "three bursts at or above 3650 mV: it boots"
    d = attach(d.n, kind)
    g = guard(d)
    assert g["effective"] == "probation" or g["effective"] == "blocked:level-unknown", g
    assert (g["last_entry"], g["last_recovery"], g["sleeps"], g["failed_recoveries"]) == ("critical", "voltage", 2, 0), g
    until(d, "probation")


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_a_relapse_in_probation_doubles_the_interval_and_raises_the_bar(nodes, kind):
    n = sleep_once(nodes, kind, "bs2")
    set_battery(n, 3700)
    assert wake(n) is None
    d = attach(n, kind)
    set_battery(n, 3300)
    assert asleep(n), "a relapse within the ten minutes"
    assert slept(n) == [900, 1800]
    set_battery(n, 3680)   # over the first bar, under the raised one
    assert wake(n) == SLEPT
    assert slept(n) == [900, 1800, 1800]
    set_battery(n, 3750)
    assert wake(n) is None
    g = guard(attach(n, kind))
    assert (g["failed_recoveries"], g["interval_s"], g["recover_mv"]) == (1, 1800, 3700), g


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_an_open_job_holds_the_sleep_until_it_is_confirmed(nodes, kind):
    d = boot(nodes, kind, "bs3")
    assert d.provision()["status"] == "ok"
    r = d.send(wrap(d.node_key, "config.apply", {"set": {"battery.guard": "enabled=on"}, "confirm": {"within_s": 600}},
                    job=next_job(d)))
    assert r["status"] == "ok" and r["result"]["state"] == "awaiting-confirm", r
    set_battery(d.n, 3300)
    level_is(d.i, "critical")
    end = time.time() + 4 * WINDOW_S
    while time.time() < end:
        assert d.n.proc.poll() is None, "no sleep while the job is open"
        time.sleep(0.5)
    assert guard(d)["effective"] == "blocked:job-open"
    r = d.send(wrap(d.node_key, "job.confirm", {"job": r["result"]["job"]}, job=next_job(d)))
    assert r["status"] == "ok", r
    assert asleep(d.n, 10), "once the job is over it sleeps"


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_a_timer_that_cannot_be_armed_keeps_the_device_up(nodes, kind):
    d = boot(nodes, kind, "bs4")
    fail = d.n.dir.rstrip("/") + ".timerfail"
    open(fail, "w").close()
    assert line(d)("set battery.guard enabled=on") == "OK"
    set_battery(d.n, 3300)
    g = until(d, "blocked:timer-failed", timeout=20)
    assert g["last_recovery"] == "timer-failed" and g["sleeps"] == 0, g
    time.sleep(WINDOW_S)
    assert d.n.proc.poll() is None and slept(d.n) == []
    os.unlink(fail)
    assert asleep(d.n, 10), "the next window tries again"


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_the_button_gives_a_lease_and_a_power_on_forgets_the_sleep(nodes, kind):
    n = sleep_once(nodes, kind, "bs5")
    assert wake(n, "button") is None, "awake at 3300 mV: the maintenance lease"
    d = attach(n, kind)
    g = guard(d)
    assert g["last_recovery"] == "button", g
    until(d, "blocked:lease")
    time.sleep(4 * WINDOW_S)
    assert n.proc.poll() is None, "the lease holds the sleep off"
    n.stop()
    power_on(n)
    assert wake(n) is None, "a power-on boots whatever the battery"
    g = guard(attach(n, kind))
    assert g["last_entry"] is None and g["sleeps"] == 0, "the block is new: %s" % g


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_a_brownout_reset_below_the_recovery_voltage_sleeps_from_the_boot(nodes, kind):
    d = boot(nodes, kind, "bs6")
    assert line(d)("set battery.guard enabled=on") == "OK"
    until(d, "armed")
    d.n.stop()
    set_battery(d.n, 3600)   # ok to the classifier, under the 3650 mV a brownout asks for
    assert wake(d.n, "brownout") == SLEPT
    assert slept(d.n) == [900]
    set_battery(d.n, 3700)
    assert wake(d.n) is None
    assert guard(attach(d.n, kind))["last_entry"] == "brownout"


@pytest.mark.timescale(20)
def test_an_update_is_refused_while_the_battery_is_not_ok(nodes):
    d = boot(nodes, "companion_standalone", "bs7", mv=3500)
    level_is(d.i, "low")
    offer = {"manifest_sha256": "ab" * 32, "source": {"push": True}, "confirm": {"within_s": 120}}
    r = d.i.request("update.offer", offer)
    assert r.get("error") == "manifest-not-staged", "guard off: as before: %s" % r
    assert line(d)("set battery.guard enabled=on") == "OK"
    r = d.i.request("update.offer", offer)
    assert (r["status"], r.get("error")) == ("refused", "battery-low"), r
    set_battery(d.n, 3800)
    level_is(d.i, "ok")
    r = d.i.request("update.offer", offer)
    assert r.get("error") == "manifest-not-staged", r


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_the_bench_offset_is_the_cables_and_survives_the_sleep(nodes, kind):
    d = boot(nodes, kind, "bs8", mv=3800)
    assert line(d)("battery test offset 3000").startswith("Err"), "0 to 2000"
    assert line(d)("battery test offset 500").startswith("OK - battery test offset 500 mV")
    level_is(d.i, "critical")   # 3800 - 500
    assert guard(d)["test_offset_mv"] == 500
    assert status(d.i)["battery_mv"] == 3800, "the reading the board gives is reported as it is"
    assert line(d)("set battery.guard enabled=on") != "OK", "would-sleep-now: the guard sees 3300"
    assert line(d)("battery test offset 0").startswith("OK")
    level_is(d.i, "ok")
    assert line(d)("set battery.guard enabled=on") == "OK"
    assert line(d)("battery test offset 600").startswith("OK")
    assert asleep(d.n), "the offset alone takes it to sleep"
    set_battery(d.n, 4200)   # 3600 to the guard: under the recovery voltage
    assert wake(d.n) == SLEPT, "the offset is still there after the sleep"
    set_battery(d.n, 4400)
    assert wake(d.n) is None
    d = attach(d.n, kind)
    assert line(d)("battery test offset") == "> 600"


@pytest.mark.timescale(20)
def test_the_bench_offset_is_refused_over_a_paired_link(nodes):
    n = node(nodes, "companion_standalone", "bs9")
    n.start()
    d = Dev(n)
    assert d.c.cli("set pin 654321").startswith("> pin is now")
    d.reboot()
    b = n.connect_ble(654321)
    b.app_start()
    assert not b.cli("battery test offset 500").startswith("OK")
    b.close()
    assert d.c.cli("battery test offset") == "> 0"


def test_no_release_build_defines_the_bench_offset():
    """MECON_BATTERY_TEST_OFFSET is a bench flag: only the simulated boards set it, never a firmware environment, and the
    release workflow passes no extra build flags that could add it."""
    found = []
    for d, _, files in os.walk(ROOT):
        if "/.pio" in d or "/.git" in d or "/node_modules" in d:
            continue
        for name in files:
            if name.endswith(".ini"):
                p = os.path.join(d, name)
                for i, text in enumerate(open(p, encoding="utf-8", errors="replace").read().splitlines(), 1):
                    if re.search(r"^\s*-D\s*MECON_BATTERY_TEST_OFFSET\b", text):
                        found.append((os.path.relpath(p, ROOT), i))
    assert [f for f, _ in found] == ["variants/sim/platformio.ini"], found
    releases = json.load(open(os.path.join(ROOT, "ci", "release-variants.json")))["variants"]
    assert releases and all(not v["variant"].startswith("sim_") for v in releases)
    for wf in ("signed-release.yml", "firmware-builder.yml", "target-boards.yml", "all-boards.yml"):
        p = os.path.join(ROOT, ".github", "workflows", wf)
        if os.path.exists(p):
            text = open(p, encoding="utf-8").read()
            assert "MECON_BATTERY_TEST_OFFSET" not in text and "PLATFORMIO_BUILD_FLAGS" not in text, wf
