"""Region scopes on the managed interface (OPERATIONS.md, "Region scopes"): the native region tree of a Repeater and a Room
server through region.list, region.put, region.remove, region.flood, region.home and region.default. The operations act on
the same RegionMap the console's `region` commands use, add the checks the console does not make, save every change, and
make a signed caller confirm."""
import os
import sys
import time

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from test_deployment_authority import wrap  # noqa: E402
from test_deployment_authority_server import RTC, boot  # noqa: E402

KINDS = ["repeater", "room_server"]


def listed(d):
    """The whole tree, all pages."""
    r = d.i.request("region.list")
    assert r["status"] == "ok", r
    first = r["result"]
    regions = list(first["regions"])
    nxt = first["next"]
    while nxt is not None:
        page = d.i.request("region.list", {"cursor": nxt})["result"]
        regions += page["regions"]
        nxt = page["next"]
    return first, {e["name"]: e for e in regions}


def capabilities(d):
    """hello's capability list, every page (DEVICE_INTERFACE.md: cursor in, next out)."""
    caps, cursor = [], 0
    for _ in range(8):
        r = d.i.request("hello", {"cursor": cursor} if cursor else None)
        assert r["status"] == "ok", r
        caps += r["result"]["capabilities"]
        cursor = r["result"].get("next")
        if cursor is None:
            return caps
    raise AssertionError("hello never ended its pages")


def wait_job(d, job, secs=30):
    end = time.time() + secs
    st = None
    while time.time() < end:
        st = d.i.request("job.get", {"job": job})["result"]["state"]
        if st != "awaiting-confirm":
            break
        time.sleep(0.5)
    return st


@pytest.mark.parametrize("kind", KINDS)
def test_a_fresh_server_lists_only_the_wildcard(nodes, kind):
    d = boot(nodes, kind, "rg" + kind[:2], 90)
    first, regions = listed(d)
    assert first["capacity"] == 32 and first["wildcard"] == {"flood": "allow"}, first
    assert first["home"] is None and first["default"] is None and regions == {}, first
    caps = capabilities(d)
    assert {"region.list", "region.put", "region.remove", "region.flood", "region.home", "region.default"} <= set(caps), caps


@pytest.mark.parametrize("kind", KINDS)
def test_put_builds_the_native_tree_and_rejects_bad_names_parents_and_cycles(nodes, kind):
    d = boot(nodes, kind, "rp" + kind[:2], 91)
    r = d.i.request("region.put", {"name": "alpha"})
    assert r["status"] == "ok" and r["result"] == {"name": "alpha", "parent": "*", "flood": "allow", "kind": "hashtag"}, r
    r = d.i.request("region.put", {"name": "beta", "parent": "alpha", "flood": "deny"})
    assert r["status"] == "ok" and r["result"]["parent"] == "alpha" and r["result"]["flood"] == "deny", r
    assert d.i.request("region.put", {"name": "$ops"})["result"]["kind"] == "private"
    _, regions = listed(d)
    assert set(regions) == {"alpha", "beta", "$ops"}, regions
    tree = d.cli.command("region")
    assert "alpha" in tree and "beta" in tree, "the console reads the same native map: %s" % tree
    # The checks the console does not make.
    r = d.i.request("region.put", {"name": "alpha", "parent": "beta"})
    assert (r["status"], r["error"]) == ("invalid", "cycle"), "moving a region under its own child closes a loop: %s" % r
    r = d.i.request("region.put", {"name": "alpha", "parent": "alpha"})
    assert (r["status"], r["error"]) == ("invalid", "cycle"), r
    r = d.i.request("region.put", {"name": "gamma", "parent": "nope"})
    assert (r["status"], r["error"]) == ("failed", "not-found"), r
    r = d.i.request("region.put", {"name": "a" * 31})
    assert (r["status"], r["error"]) == ("invalid", "name-too-long"), "the native field holds 30 bytes, and truncating would rename it: %s" % r
    assert d.i.request("region.put", {"name": "a" * 30})["status"] == "ok"
    r = d.i.request("region.put", {"name": "with space"})
    assert (r["status"], r["error"]) == ("invalid", "bad-char"), r
    r = d.i.request("region.put", {"name": "alph"})
    assert r["status"] == "ok" and "alph" in listed(d)[1], "exact names: a prefix of alpha is a new region, not alpha"


@pytest.mark.parametrize("kind", KINDS)
def test_flood_home_default_and_the_references_remove_refuses(nodes, kind):
    d = boot(nodes, kind, "rf" + kind[:2], 92)
    for name, parent in (("north", "*"), ("city", "north")):
        assert d.i.request("region.put", {"name": name, "parent": parent})["status"] == "ok"
    assert d.i.request("region.flood", {"name": "city", "flood": "deny"})["status"] == "ok"
    assert d.i.request("region.flood", {"name": "*", "flood": "deny"})["status"] == "ok"
    first, regions = listed(d)
    assert regions["city"]["flood"] == "deny" and first["wildcard"]["flood"] == "deny", (first, regions)
    assert d.i.request("region.home", {"name": "north"})["status"] == "ok"
    assert d.i.request("region.default", {"name": "city"})["status"] == "ok"
    first, regions = listed(d)
    assert first["home"] == "north" and first["default"] == "city", first
    assert regions["city"]["flood"] == "allow", "the default region forwards floods, as the console's `region default` makes it"
    r = d.i.request("region.remove", {"name": "north"})
    assert (r["status"], r["error"]) == ("busy", "not-empty"), r
    r = d.i.request("region.remove", {"name": "city"})
    assert (r["status"], r["error"]) == ("busy", "referenced"), "the console leaves a default pointing at nothing: %s" % r
    assert d.i.request("region.default", {"name": None})["status"] == "ok"
    assert d.i.request("region.remove", {"name": "city"})["status"] == "ok"
    r = d.i.request("region.remove", {"name": "north"})
    assert (r["status"], r["error"]) == ("busy", "referenced"), "north is the home: %s" % r
    assert d.i.request("region.home", {"name": None})["status"] == "ok"
    assert d.i.request("region.remove", {"name": "north"})["status"] == "ok"
    r = d.i.request("region.remove", {"name": "north"})
    assert (r["status"], r["error"]) == ("failed", "not-found"), r


@pytest.mark.parametrize("kind", KINDS)
def test_a_full_table_is_busy_and_a_managed_change_survives_a_reboot(nodes, kind):
    d = boot(nodes, kind, "rt" + kind[:2], 93)
    for i in range(32):
        assert d.i.request("region.put", {"name": "r%02d" % i})["status"] == "ok", i
    r = d.i.request("region.put", {"name": "one-more"})
    assert (r["status"], r["error"]) == ("busy", "full"), r
    assert d.i.request("region.put", {"name": "r05", "flood": "deny"})["status"] == "ok", "an existing name is not a new entry"
    assert d.i.request("region.remove", {"name": "r31"})["status"] == "ok"
    d.cli.command("region remove r30")   # the console's change stays in RAM until `region save`
    d.reboot()
    _, regions = listed(d)
    assert len(regions) == 31 and "r31" not in regions and regions["r05"]["flood"] == "deny", "every managed change was saved"
    assert "r30" in regions, "the console's unsaved change did not persist, as in stock"


@pytest.mark.timescale(10)
@pytest.mark.parametrize("kind", KINDS)
def test_a_signed_change_must_confirm_reverts_unconfirmed_and_stays_confirmed(nodes, kind):
    d = boot(nodes, kind, "rs" + kind[:2], 94)
    assert d.i.request("region.put", {"name": "keep"})["status"] == "ok"
    assert d.provision()["status"] == "ok"
    key = d.node_key
    r = d.i.request("region.put", {"name": "cable"})
    assert r["status"] == "refused", "once enrolled the unsigned cable changes nothing: %s" % r
    r = d.send(wrap(key, "region.put", {"name": "new"}, job=1))
    assert (r["status"], r.get("error")) == ("invalid", "confirm-required"), r
    r = d.send(wrap(key, "region.put", {"name": "new", "confirm": {"within_s": 30}}, job=2))
    assert r["status"] == "ok" and r["result"]["job_state"] == "awaiting-confirm", r
    assert "new" in listed(d)[1], "in effect at once, tentatively"
    assert wait_job(d, r["result"]["job"]) == "reverted"
    _, regions = listed(d)
    assert "new" not in regions and "keep" in regions, "unconfirmed, the old tree is back: %s" % regions
    r = d.send(wrap(key, "region.default", {"name": "keep", "confirm": {"within_s": 60}}, job=3))
    assert r["status"] == "ok", r
    c = d.send(wrap(key, "job.confirm", {"job": r["result"]["job"]}, job=4))
    assert c["status"] == "ok" and c["result"]["state"] == "confirmed", c
    assert not os.path.exists(os.path.join(d.n.dir, "region_snap")), "the copy of the old map goes with the confirmation"
    d.reboot()
    assert listed(d)[0]["default"] == "keep", "the confirmed default survives a reboot"


@pytest.mark.timescale(10)
@pytest.mark.parametrize("kind", KINDS)
def test_a_reboot_while_a_change_is_pending_reverts_it_at_boot(nodes, kind):
    d = boot(nodes, kind, "rb" + kind[:2], 95)
    assert d.provision()["status"] == "ok"
    r = d.send(wrap(d.node_key, "region.put", {"name": "maybe", "confirm": {}}, job=1))
    assert r["status"] == "ok", r
    job = r["result"]["job"]
    d.n.restart()   # the change took effect in the boot before: this is the second boot while it is pending
    d.attach()
    assert "maybe" not in listed(d)[1], "the unconfirmed change was reverted at boot"
    assert d.i.request("job.get", {"job": job})["result"]["state"] == "reverted"


@pytest.mark.parametrize("kind", KINDS)
def test_an_enrolled_servers_signed_hello_pages_its_capabilities(nodes, kind):
    """The device-id signature, the nonce and the result's own signature leave less room on an enrolled server: the list
    goes on a second page rather than past the 1,536-byte result (it answered too-large-to-sign before it was paged)."""
    d = boot(nodes, kind, "rh" + kind[:2], 96)
    plain = capabilities(d)
    assert d.provision()["status"] == "ok"
    first = d.i.request("hello")
    assert first["status"] == "ok", first
    signed = capabilities(d)
    assert sorted(signed) == sorted(plain) and len(signed) == len(set(signed)), "every capability once, across the pages"
    assert "hello" in first["result"]["capabilities"], "hello and the streams come first"
