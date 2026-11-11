"""The remaining radio preferences as interface settings keys (#489; DEVICE_INTERFACE.md section 5): af, int.thresh, cad,
radio.rxgain, rxdelay, txdelay, direct.txdelay, agc.reset.interval, path.hash.mode and multi.acks. They come from the
shared radio CLI (CommonRadioPrefs), so the Companion and the Repeater have the same keys, each with its CLI line's range.
The unsigned cable applies them at once; any other caller must confirm the ones that change how the radio listens or sends."""
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from test_device_interface import RTC, client  # noqa: E402
from test_device_interface_server import start as server_start  # noqa: E402

# key: (an in-range value, how it reads back, out-of-range values, needs a confirmation from callers other than the cable)
KEYS = {
    "af": ("2.5", 2.5, ["9.5", "-1", "x"], True),
    "int.thresh": (14, 14, [256, -1], True),
    "cad": (True, True, [], True),
    "radio.rxgain": (True, True, [], True),
    "rxdelay": ("4.5", 4.5, ["20.5", "-0.5"], True),
    "txdelay": ("1.5", 1.5, ["2.5"], True),
    "direct.txdelay": ("0.8", 0.8, ["2.1"], True),
    "agc.reset.interval": (40, 40, [1021, -4], False),
    "path.hash.mode": (2, 2, [3, -1], True),
    "multi.acks": (1, 1, [2], False),
}


def boot(nodes, kind, name):
    if kind != "companion_standalone":
        return server_start(nodes, kind, name)[1]   # the console: mecon> lines
    n = nodes(kind, name, seed=11, rtc=RTC).start()
    return client(n)


def schema(i):
    keys, cursor = {}, 0
    while cursor is not None:
        r = i.request("config.schema", {"cursor": cursor})
        assert r["status"] == "ok", r
        keys.update({k["key"]: k for k in r["result"]["keys"]})
        cursor = r["result"]["next"]
    return keys


def same(got, want):
    return abs(float(got) - want) < 1e-6 if isinstance(want, float) else got == want


@pytest.mark.parametrize("kind", ["companion_standalone", "repeater"])
def test_every_radio_preference_reads_writes_and_keeps_its_range(nodes, kind):
    c, i = boot(nodes, kind, "rp" + kind[:2])
    listed = schema(i)
    for key, (good, back, bad, confirm) in KEYS.items():
        assert key in listed, "%s is not in config.schema" % key
        assert bool(listed[key].get("confirm")) == confirm, (key, listed[key])
        r = i.request("config.get", {"keys": [key]})
        assert r["status"] == "ok" and key in r["result"]["values"], (key, r)
        r = i.request("config.apply", {"set": {key: good}})
        assert r["status"] == "ok", (key, r)
        v = i.request("config.get", {"keys": [key]})["result"]["values"][key]
        assert same(v, back), "%s read back %r after writing %r" % (key, v, good)
        for b in bad:
            r = i.request("config.apply", {"set": {key: b}})
            assert r["status"] == "invalid", "%s=%r was accepted: %s" % (key, b, r)
        v = i.request("config.get", {"keys": [key]})["result"]["values"][key]
        assert same(v, back), "%s changed after a refused write: %r" % (key, v)


def test_the_cli_and_the_interface_read_the_same_store(nodes):
    c, i = boot(nodes, "companion_standalone", "rpcli")
    assert i.request("config.apply", {"set": {"af": "3"}})["status"] == "ok"
    assert c.cli("get af").startswith("> 3")
    assert c.cli("set path.hash.mode 1").startswith("OK")
    assert i.request("config.get", {"keys": ["path.hash.mode"]})["result"]["values"]["path.hash.mode"] == 1
