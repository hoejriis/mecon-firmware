"""The battery guard's settings, CLI and dry run (docs/contract/OPERATIONS.md, TELEMETRY.md), on a Companion and on a Repeater.
The settings are stored and classify the battery level, and `dry` says what the guard would do; the protective sleep they
arm is test_battery_sleep.py's.
The simulated board reads its voltage from "<dir>.batt" (see test_battery_level.py); the node runs 20 times faster than real
time, so a 60 s window is 3 s."""
import os
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
DEFAULT = "> enabled=off low=3550 critical=3400 margin=100 wake=900"


def boot(nodes, kind, name, mv=4000):
    n = nodes(kind, name, seed=7, rtc=RTC)
    set_battery(n, mv)
    n.start()
    return Dev(n) if kind.startswith("companion") else Srv(n)


def line(d):
    return d.c.cli if isinstance(d, Dev) else d.cli.command


def values(d, key="battery.guard"):
    r = d.i.request("config.get", {"keys": [key]})
    assert r["status"] == "ok", r
    return r["result"]["values"][key]


def apply(d, value, **kw):
    """config.apply of battery.guard from the cable. A Companion's USB cable may need the confirmation a phone does."""
    args = {"set": {"battery.guard": value}}
    args.update(kw)
    return d.i.request("config.apply", args)


def dry(d, value):
    return d.i.request("config.apply", {"set": {"battery.guard": value}, "dry": True})


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_defaults_and_the_cli_round_trip(nodes, kind):
    d = boot(nodes, kind, "bg1")
    assert line(d)("get battery.guard") == DEFAULT
    assert values(d) == DEFAULT[2:], "config.get gives the same value as the CLI"
    assert line(d)("set battery.guard enabled=on low=3600 critical=3300 margin=150 wake=1200") == "OK"
    assert line(d)("get battery.guard") == "> enabled=on low=3600 critical=3300 margin=150 wake=1200"
    assert values(d) == "enabled=on low=3600 critical=3300 margin=150 wake=1200"
    assert line(d)("set battery.guard enabled=off") == "OK", "a field left out keeps its stored value"
    assert line(d)("get battery.guard") == "> enabled=off low=3600 critical=3300 margin=150 wake=1200"
    d.reboot()
    assert line(d)("get battery.guard") == "> enabled=off low=3600 critical=3300 margin=150 wake=1200", "saved"


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_the_interface_checks_the_whole_value_and_refuses_it_all_or_changes_it_all(nodes, kind):
    d = boot(nodes, kind, "bg2")
    for bad, why in (("low=3400", "low-not-above-critical"), ("critical=3500", "low-not-above-critical"),
                     ("margin=10", "margin-out-of-range"), ("wake=10", "wake-out-of-range"),
                     ("low=2000", "low-out-of-range"), ("critical=2000", "critical-out-of-range")):
        r = apply(d, bad)
        assert r["status"] == "invalid" and r["error"] == "%s:battery.guard" % why, (bad, r)
        assert line(d)("set battery.guard " + bad) == "Err - " + why, bad
    for bad in ("junk", "low=", "enabled=yes", "low=3600 low=3700"):
        r = apply(d, bad)
        assert r["status"] == "invalid" and r["error"] == "bad-format:battery.guard", (bad, r)
    assert values(d) == DEFAULT[2:], "nothing changed"
    # the pair a stored value alone would refuse: critical above the stored low - 100, with a new low in the same request
    r = apply(d, "critical=3500 low=3700")
    assert r["status"] == "ok" and r["result"]["applied"] == {"battery.guard": "ok"}, r
    assert values(d) == "enabled=off low=3700 critical=3500 margin=100 wake=900"
    r = apply(d, "low=3300 critical=3000")
    assert r["status"] == "ok", r
    assert values(d) == "enabled=off low=3300 critical=3000 margin=100 wake=900"
    # a request that names the key twice with a bad one changes nothing of the good one
    r = d.i.request("config.apply", {"set": {"tx": 5, "battery.guard": "low=3000 critical=3300"}})
    assert r["status"] == "invalid", r
    assert values(d) == "enabled=off low=3300 critical=3000 margin=100 wake=900"


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_the_classifier_uses_the_stored_thresholds(nodes, kind):
    d = boot(nodes, kind, "bg3", mv=3500)
    assert level_is(d.i, "low")["battery_mv"] == 3500          # the default low is 3550
    assert apply(d, "low=3400 critical=3200")["status"] == "ok"   # 3500 is now low + margin: ok, without waiting a window
    level_is(d.i, "ok", timeout=5)
    set_battery(d.n, 3350)
    level_is(d.i, "low")
    set_battery(d.n, 3100)
    level_is(d.i, "critical")


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_arming_at_critical_is_refused_would_sleep_now(nodes, kind):
    d = boot(nodes, kind, "bg4", mv=3300)
    level_is(d.i, "critical")
    r = apply(d, "enabled=on")
    assert r["status"] == "refused" and r["error"] == "would-sleep-now:battery.guard", r
    assert line(d)("set battery.guard enabled=on") == "Err - would-sleep-now"
    assert values(d) == DEFAULT[2:], "refused, so nothing is stored"
    assert apply(d, "enabled=off low=3600")["status"] == "ok", "turning it off, and thresholds while off, are not refused"
    set_battery(d.n, 3700)
    level_is(d.i, "ok")
    assert apply(d, "enabled=on")["status"] == "ok"
    # thresholds that would put the healthy device at critical now are refused while it is on
    r = apply(d, "critical=3800 low=3900")
    assert r["status"] == "refused" and r["error"] == "would-sleep-now:battery.guard", r
    assert values(d) == "enabled=on low=3600 critical=3400 margin=100 wake=900"


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_the_dry_run_answers_and_changes_nothing(nodes, kind):
    d = boot(nodes, kind, "bg5", mv=3500)
    level_is(d.i, "low")
    r = dry(d, "enabled=on")
    assert r["status"] == "ok", r
    assert r["result"] == {"dry": True, "battery_guard": {"accepted": True, "level": "low", "would_sleep": False,
                                                          "reason": "level-low"}}, r
    r = dry(d, "enabled=on low=3500 critical=3490")
    assert r["result"]["battery_guard"] == {"accepted": False, "refusal": "low-not-above-critical", "level": "low",
                                            "would_sleep": False, "reason": "guard-off"}, r
    r = dry(d, "enabled=on critical=3600 low=3700")
    assert r["result"]["battery_guard"] == {"accepted": False, "refusal": "would-sleep-now", "level": "critical",
                                            "would_sleep": True, "reason": "level-critical"}, r
    assert dry(d, "junk")["status"] == "invalid"
    assert d.i.request("config.apply", {"set": {"battery.guard": "enabled=on", "tx": 5}, "dry": True})["status"] == "invalid"
    assert d.i.request("config.apply", {"set": {"tx": 5}, "dry": True})["status"] == "invalid", "only for this key"
    assert d.i.request("config.apply", {"set": {"battery.guard": "enabled=on"}, "dry": True,
                                        "confirm": {"within_s": 30}})["status"] == "invalid"
    assert line(d)("get battery.guard.dry enabled=on") == "> accepted=yes level=low would_sleep=no reason=level-low"
    assert values(d) == DEFAULT[2:] and level_is(d.i, "low")["battery_guard"]["enabled"] is False
    d.reboot()
    assert line(d)("get battery.guard") == DEFAULT, "nothing was stored by any of it"


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_once_enrolled_an_unsigned_write_is_refused_and_a_signed_one_confirms(nodes, kind):
    d = boot(nodes, kind, "bg6")
    assert d.provision()["status"] == "ok"
    r = apply(d, "enabled=on")
    assert r["status"] == "refused", "a write: the unsigned cable keeps reads only once enrolled: %s" % r
    assert line(d)("set battery.guard enabled=on") != "OK"
    assert values(d) == DEFAULT[2:]
    assert dry(d, "enabled=on")["status"] == "refused", "config.apply is a write operation, dry or not"
    assert line(d)("get battery.guard.dry enabled=on").startswith("> accepted=yes "), "the CLI form is a read, open to readers"
    r = d.send(wrap(d.node_key, "config.apply", {"set": {"battery.guard": "enabled=on"}, "dry": True}, job=next_job(d)))
    assert r["status"] == "ok" and r["result"]["dry"] is True and r["result"]["battery_guard"]["accepted"] is True, r
    assert values(d) == DEFAULT[2:], "a signed dry run needs no confirmation and writes nothing"
    args = {"set": {"battery.guard": "enabled=on low=3600"}}
    r = d.send(wrap(d.node_key, "config.apply", args, job=next_job(d)))
    assert r["status"] == "invalid" and r["error"] == "confirm-required", "a remote caller must ask to confirm: %s" % r
    args["confirm"] = {"within_s": 60}
    r = d.send(wrap(d.node_key, "config.apply", args, job=next_job(d)))
    assert r["status"] == "ok" and r["result"]["state"] == "awaiting-confirm", r
    assert values(d) == "enabled=on low=3600 critical=3400 margin=100 wake=900", "tentatively applied"
    r = d.send(wrap(d.node_key, "job.confirm", {"job": r["result"]["job"]}, job=next_job(d)))
    assert r["status"] == "ok", r
    assert values(d) == "enabled=on low=3600 critical=3400 margin=100 wake=900"


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_an_unconfirmed_change_goes_back(nodes, kind):
    d = boot(nodes, kind, "bg7")
    assert d.provision()["status"] == "ok"
    args = {"set": {"battery.guard": "low=3700 critical=3500"}, "confirm": {"within_s": 30}}
    r = d.send(wrap(d.node_key, "config.apply", args, job=next_job(d)))
    assert r["status"] == "ok", r
    assert values(d) == "enabled=off low=3700 critical=3500 margin=100 wake=900"
    r = d.send(wrap(d.node_key, "job.cancel", {"job": r["result"]["job"]}, job=next_job(d)))
    assert r["status"] == "ok", r
    assert values(d) == DEFAULT[2:], "the revert put the whole stored value back"


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_the_health_stream_carries_battery_guard(nodes, kind):
    d = boot(nodes, kind, "bg8")
    s = status(d.i)
    g = s["battery_guard"]
    assert (g["enabled"], g["supported"], g["effective"]) == (False, True, "off"), s
    assert (g["last_entry"], g["last_recovery"], g["failed_recoveries"], g["sleeps"]) == (None, None, None, None), \
        "no state while off: null, not zero: %s" % g
    assert not mecon_schema.check(s, mecon_schema.load("event.health.json"))
    assert line(d)("set battery.guard enabled=on") == "OK"
    time.sleep(WINDOW_S)
    s = status(d.i)
    g = s["battery_guard"]
    assert (g["enabled"], g["effective"], g["failed_recoveries"], g["sleeps"]) == (True, "armed", 0, 0), g
    assert not mecon_schema.check(s, mecon_schema.load("event.health.json"))
    set_battery(d.n, 3300)
    level_is(d.i, "critical")
    s = status(d.i)
    assert s["battery_guard"]["effective"] == "confirming", "critical: three windows before it sleeps (test_battery_sleep.py)"
    assert d.i.request("status.get")["status"] == "ok"
    assert "battery_guard" not in d.i.request("status.get")["result"], "status.get must fit one signed LoRa reply"


@pytest.mark.timescale(20)
def test_the_room_server_has_no_such_key(nodes):
    d = Srv(nodes("room_server", "bg9", seed=7, rtc=RTC).start())
    keys, cursor = [], 0
    while cursor is not None:
        r = d.i.request("config.schema", {"cursor": cursor})
        keys += [k["key"] for k in r["result"]["keys"]]
        cursor = r["result"]["next"]
    assert "battery.guard" not in keys and "owner.info" in keys, keys
    assert apply(d, "enabled=on")["status"] != "ok"
    assert d.cli.command("set battery.guard enabled=on") != "OK"


@pytest.mark.timescale(20)
@pytest.mark.parametrize("kind", KINDS)
def test_a_radio_reset_keeps_the_guard_and_a_factory_reset_clears_it(nodes, kind):
    from test_device_reset import reset
    d = boot(nodes, kind, "bgr")
    assert line(d)("set battery.guard enabled=on low=3600 critical=3300") == "OK"
    assert reset(d, "radio")["status"] == "ok"
    assert line(d)("get battery.guard") == "> enabled=on low=3600 critical=3300 margin=100 wake=900"
    assert reset(d, "factory")["status"] == "ok"
    assert line(d)("get battery.guard") == DEFAULT
