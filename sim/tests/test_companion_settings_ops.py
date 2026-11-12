"""Companion settings over the device interface (#494; docs/contract/OPERATIONS.md 5.1): ble.pin.set, the tz.offset key,
clock.back and contact.capacity, each with the class and the callers of its CLI line."""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from sim.harness import audit_entries  # noqa: E402
from test_clock_back import DAY, RTC, ahead, client, now  # noqa: E402
from test_deployment_authority import Dev, wrap  # noqa: E402
from test_device_ops import next_job, shaped  # noqa: E402
from test_jobs import PIN, ble_boot  # noqa: E402

import mecon_iface  # noqa: E402 (test_clock_back put tools/ on the path)

NEW_PIN, LATER_PIN, DEFAULT_PIN = 654321, 222222, 123456


def paired(n, pin):
    b = n.connect_ble(pin)
    b.app_start()
    return b, mecon_iface.IfaceClient(b._send, b._read_frame)


def test_the_cable_sets_the_pin_it_is_used_from_the_next_start_and_never_answered_from_the_cache(nodes):
    n = ble_boot(nodes, "bps", PIN)
    d = Dev(n)
    r = d.i.request("ble.pin.set", {"pin": NEW_PIN}, rid="s1")
    assert r["status"] == "ok" and shaped("ble.pin.set", r["result"]) == {"reboot_needed": True}, r
    assert d.i.request("ble.pin.get")["result"] == {"pin": PIN}, "the stack keeps its PIN until it starts again"
    for bad in ({"pin": 12345}, {"pin": 1000000}, {"pin": -1}, {"pin": "654321"}, {}):
        assert d.i.request("ble.pin.set", bad)["status"] == "invalid", bad
    r = d.i.request("ble.pin.set", {"pin": LATER_PIN}, rid="s1")
    assert r["status"] == "ok", "the same id runs again, it is not served from the cache: %s" % r
    d.c.close()
    n.restart()
    b, i = paired(n, LATER_PIN)
    assert i.request("hello")["status"] == "ok", "the second request's PIN is the one in use"
    r = i.request("ble.pin.set", {"pin": NEW_PIN})
    assert r["status"] == "ok", "a phone paired with its own PIN may write, as `set pin` from it: %s" % r
    b.close()


def test_a_phone_paired_with_the_published_default_cannot_set_it(nodes):
    n = ble_boot(nodes, "bpd", PIN)
    c, _ = client(n)
    assert c.cli("set pin %d" % DEFAULT_PIN).startswith("> pin is now")
    c.close()
    n.restart()
    b, i = paired(n, DEFAULT_PIN)
    r = i.request("ble.pin.set", {"pin": NEW_PIN})
    assert r["status"] == "refused", r
    b.close()
    n.restart()
    c, i = client(n)
    assert i.request("ble.pin.get")["result"] == {"pin": DEFAULT_PIN}, "nothing was written"


def test_tz_offset_is_a_key_and_the_cli_no_longer_wraps(nodes):
    n = nodes("companion_standalone", "tzo", seed=61, rtc=RTC).start()
    c, i = client(n)
    r = i.request("config.apply", {"set": {"tz.offset": -5}})
    assert r["status"] == "ok", r
    assert i.request("config.get", {"keys": ["tz.offset"]})["result"]["values"] == {"tz.offset": -5}
    assert c.cli("get tz.offset") == "> -5"
    for bad in (15, -13, "2"):
        r = i.request("config.apply", {"set": {"tz.offset": bad}})
        assert r["status"] == "invalid", (bad, r)
    keys = [k["key"] for k in i.request("config.schema")["result"]["keys"]]
    assert "tz.offset" in keys, keys
    assert c.cli("set tz.offset 250").startswith("Error"), "250 must not wrap to -6"
    assert c.cli("get tz.offset") == "> -5"


def test_clock_back_from_the_cable_goes_back_says_how_far_and_is_audited(nodes):
    n = nodes("companion_standalone", "cbi", seed=62, rtc=RTC).start()
    c, i = client(n)
    ahead(c, DAY)
    r = i.request("clock.back", {"epoch": RTC + 10})
    assert r["status"] == "ok" and shaped("clock.back", r["result"])["epoch"] == RTC + 10, r
    assert DAY - 100 < r["result"]["back_s"] <= DAY, r
    assert now(c) < RTC + 100
    assert any("clock-back cable" in e for e in audit_entries(c.cli)), audit_entries(c.cli)
    was = now(c)
    for epoch in (was + 1000, 1000):
        r = i.request("clock.back", {"epoch": epoch})
        assert r["status"] == "failed" and r["error"] == "not-earlier", (epoch, r)
    assert i.request("clock.back", {})["status"] == "invalid"
    assert abs(now(c) - was) < 30


def test_clock_back_is_the_cables_alone_not_a_phones_or_a_signers(nodes):
    n = ble_boot(nodes, "cbs", PIN)
    b, i = paired(n, PIN)
    was = now(b)
    r = i.request("clock.back", {"epoch": was - 100})
    assert r["status"] == "refused" and r["error"] == "cable-only", "the destructive class lets this phone by: %s" % r
    assert now(b) >= was
    b.close()
    d = Dev(n)
    assert d.provision()["status"] == "ok"
    was = now(d.c)
    r = d.send(wrap(d.node_key, "clock.back", {"epoch": was - 100}, job=next_job(d)))
    assert r["status"] == "refused", r
    assert now(d.c) >= was
    assert d.i.request("clock.back", {"epoch": was - 100})["status"] == "ok", "the cable still may, enrolled"


def test_contact_capacity_gives_the_cli_figures(nodes):
    n = nodes("companion_standalone", "ccap", seed=63, rtc=RTC).start()
    c, i = client(n)
    r = i.request("contact.capacity")
    assert r["status"] == "ok", r
    res = shaped("contact.capacity", r["result"])
    line = c.cli("get contacts.capacity")
    m = re.match(r"> (\S+) store (\d+)/(\d+) cache (\d+)/(\d+) pinned (\d+) fav (\d+) .* ram (\d+)", line)
    assert m, line
    assert (res["store"], res["used"], res["capacity"], res["cache_used"], res["cache_slots"], res["pinned"],
            res["favourites"], res["ram_bytes"]) == (m[1], *map(int, m.groups()[1:])), (res, line)


def test_once_enrolled_only_a_signature_sets_the_pin(nodes):
    n = ble_boot(nodes, "bpe", PIN)
    d = Dev(n)
    assert d.provision()["status"] == "ok"
    r = d.i.request("ble.pin.set", {"pin": NEW_PIN})
    assert r["status"] == "refused", "a write, so the unsigned cable keeps reads only: %s" % r
    r = d.send(wrap(d.node_key, "ble.pin.set", {"pin": NEW_PIN}, job=next_job(d)))
    assert r["status"] == "ok" and r["result"] == {"reboot_needed": True}, r
