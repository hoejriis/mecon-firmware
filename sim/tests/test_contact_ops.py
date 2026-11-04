"""contact.list / get / put / remove / protect over the device interface on simulated Companions
(docs/contract/OPERATIONS.md section 5.1): the native contact table is the one store, so a contact the interface wrote
is the contact a stock app frame reads and the other way round, a repeat is the same state, a full table behaves as it
does for the app, and a caller the CLI table does not trust writes nothing."""
import os
import sys
import time

from sim.harness import (CompanionClient, ERR_TABLE_FULL, PUSH_CONTACT_DELETED, RESP_ERR, RESP_OK)

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402

RTC = 1792800000
DAY = 86400
GRANTS = 0x1E   # telemetry (base, location, environment) and the remote CLI


def key(n):
    return bytes([n & 255, (n >> 8) & 255]) + bytes([0x40]) * 29 + bytes([0x77])


def client(n):
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    return c, mecon_iface.IfaceClient(c._send, c._read_frame)


def put(i, k, name, ctype="chat", **more):
    args = {"key": k.hex(), "name": name, "type": ctype}
    args.update(more)
    return i.request("contact.put", args)


def native(c):
    return {x["key"]: x for x in c.get_contacts()[1]}


def walk(i):
    """contact.list to its end: every page's keys, and the page count."""
    keys, cursor, pages = [], None, 0
    while True:
        r = i.request("contact.list", {} if cursor is None else {"cursor": cursor})
        assert r["status"] == "ok", r
        pages += 1
        keys += [x["key"] for x in r["result"]["contacts"]]
        cursor = r["result"]["next"]
        if cursor is None:
            return keys, pages


def tight(nodes, name):
    """A node whose file system leaves room for only a few dozen contacts (as test_contacts.tight_node)."""
    return nodes("companion_standalone", name, seed=21, rtc=RTC,
                 extra=["--fs-size", str(8192 + 3000 + 16 * 200), "--heap-budget", "50000"])


def deleted(c, i):
    """The keys of the contact-deleted pushes so far: the interface client keeps the native frames it skipped."""
    out = [bytes(f[1:33]) for f in i.native if f and f[0] == PUSH_CONTACT_DELETED]
    out += [bytes(f[1:33]) for f in c.pushes if f[0] == PUSH_CONTACT_DELETED]
    c.wait_push(PUSH_CONTACT_DELETED, timeout=0.3)
    del i.native[:]
    del c.pushes[:]
    return out


def test_a_contact_put_through_the_interface_is_the_contact_an_app_frame_reads_and_survives_a_reboot(nodes):
    n = nodes("companion_standalone", "ct1", seed=21, rtc=RTC).start()
    c, i = client(n)
    h = i.request("hello")["result"]
    assert {"contact.list", "contact.get", "contact.put", "contact.remove", "contact.protect"} <= set(h["capabilities"]), h
    r = put(i, key(1), "Ada", "repeater", lat_e6=55000000, lon_e6=12000000)
    assert r["status"] == "ok" and r["result"]["created"] is True and r["result"]["changed"] is True, r
    assert r["result"]["type"] == "repeater" and r["result"]["fav"] is False and r["result"]["hops"] == -1, r
    nat = native(c)[key(1)]
    assert (nat["name"], nat["type"], nat["flags"], nat["path_len"], nat["lat"], nat["lon"]) == \
        ("Ada", 2, 0, 0xFF, 55000000, 12000000), nat
    assert c.add_contact_full(key(2), "Bo", lastmod=RTC)[0] == RESP_OK
    g = i.request("contact.get", {"key": key(2).hex()})
    assert g["status"] == "ok" and g["result"]["name"] == "Bo" and g["result"]["type"] == "chat", g
    p = i.request("contact.protect", {"key": key(1).hex(), "protected": True})
    assert p["status"] == "ok" and p["result"]["fav"] is True and p["result"]["changed"] is True, p
    assert native(c)[key(1)]["flags"] == 1, "the native favourite flag"
    for res in (r, g, p, i.request("contact.list")):
        assert "secret" not in str(res) and "path" not in str(res), res
    c.request_reboot()
    assert n.wait_exit(15) == 75
    c.close()
    n.start()
    c, i = client(n)
    nat = native(c)
    assert nat[key(1)]["name"] == "Ada" and nat[key(1)]["flags"] == 1 and nat[key(2)]["name"] == "Bo", nat
    assert i.request("contact.get", {"key": key(1).hex()})["result"]["fav"] is True
    r = i.request("contact.remove", {"key": key(1).hex()})
    assert r["status"] == "ok" and r["result"]["removed"] is True, r
    assert key(1) not in native(c) and key(2) in native(c)


def test_a_put_keeps_the_grants_and_the_path_of_a_contact_and_cannot_grant(nodes):
    n = nodes("companion_standalone", "ct2", seed=22, rtc=RTC).start()
    c, i = client(n)
    path = bytes([9, 8, 7])
    assert c.add_contact_full(key(1), "Cy", flags=GRANTS | 1, out_path_len=3, path=path, lastmod=RTC)[0] == RESP_OK
    r = i.request("contact.put", {"key": key(1).hex(), "name": "Cy renamed"})
    assert r["status"] == "ok" and r["result"]["created"] is False and r["result"]["changed"] is True, r
    nat = native(c)[key(1)]
    assert nat["name"] == "Cy renamed" and nat["flags"] == GRANTS | 1, "the grants stay the contact's own"
    assert nat["path_len"] == 3 and nat["path"][:3] == path, "the known path stays"
    r = put(i, key(2), "Dee", flags=GRANTS)
    assert r["status"] == "ok" and native(c)[key(2)]["flags"] == 0, "a put names no flags, so a new contact has none"
    assert i.request("contact.protect", {"key": key(1).hex(), "protected": True})["result"]["fav"] is True
    assert i.request("contact.protect", {"key": key(1).hex(), "protected": False})["result"]["fav"] is False
    assert native(c)[key(1)]["flags"] == GRANTS, "protect touches the favourite bit and no grant"
    assert i.request("contact.put", {"key": key(3).hex(), "name": "x", "type": "none"})["status"] == "invalid"
    assert key(3) not in native(c), "type 0 is the anonymous slot's and is refused as the native frame refuses it"


def test_a_repeated_request_is_the_same_state_and_the_same_answer(nodes):
    n = nodes("companion_standalone", "ct3", seed=23, rtc=RTC).start()
    c, i = client(n)
    first = i.request("contact.put", {"key": key(1).hex(), "name": "Ed", "type": "room"}, rid="same-1")
    mod = native(c)[key(1)]["lastmod"]
    again = i.request("contact.put", {"key": key(1).hex(), "name": "Ed", "type": "room"}, rid="same-1")
    assert again["result"] == first["result"], "a request id repeated returns the stored result (the dispatcher's own)"
    fresh = put(i, key(1), "Ed", "room")   # a new request id: the same put again
    assert fresh["status"] == "ok" and fresh["result"]["created"] is False and fresh["result"]["changed"] is False, fresh
    assert native(c)[key(1)]["lastmod"] == mod, "an unchanged put does not touch the contact"
    assert len(native(c)) == 1
    for _ in range(2):
        r = i.request("contact.remove", {"key": key(1).hex()})
        assert r["status"] == "ok", r
    assert r["result"]["removed"] is False and native(c) == {}
    for want in (True, True):
        r = i.request("contact.protect", {"key": key(9).hex(), "protected": want})
        assert r["status"] == "failed" and r["error"] == "not-found", r
    assert put(i, key(4), "Fy")["status"] == "ok"
    a = i.request("contact.protect", {"key": key(4).hex(), "protected": True})["result"]
    b = i.request("contact.protect", {"key": key(4).hex(), "protected": True})["result"]
    assert (a["changed"], b["changed"], b["fav"]) == (True, False, True)


def test_a_list_walk_does_not_skip_or_repeat_what_stays_while_contacts_come_and_go(nodes):
    n = nodes("companion_standalone", "ct4", seed=24, rtc=RTC).start()
    c, i = client(n)
    for k in range(20):
        assert put(i, key(k), "c%d" % k)["status"] == "ok"
    r = i.request("contact.list")["result"]
    assert r["count"] == 20 and len(r["contacts"]) <= 6 and r["next"] is not None, r
    seen = [x["key"] for x in r["contacts"]]
    cursor = r["next"]
    assert c.add_contact_full(key(100), "late", lastmod=RTC)[0] == RESP_OK                     # added while paging
    assert i.request("contact.remove", {"key": key(0).hex()})["result"]["removed"] is True     # removed behind the cursor
    assert i.request("contact.remove", {"key": key(19).hex()})["result"]["removed"] is True    # removed ahead of it
    while cursor is not None:
        r = i.request("contact.list", {"cursor": cursor})["result"]
        seen += [x["key"] for x in r["contacts"]]
        cursor = r["next"]
    assert len(seen) == len(set(seen)), "a contact listed twice"
    stayed = {key(k).hex() for k in range(1, 19)}
    assert stayed <= set(seen), sorted(stayed - set(seen))
    assert key(19).hex() not in seen[6:], "a contact removed before its page is not listed"


def test_a_full_table_evicts_or_refuses_as_the_app_does_and_a_protected_contact_is_never_evicted(nodes):
    n = tight(nodes, "ct5").start()
    c, i = client(n)
    cap = c.contacts_line()["store"][1]
    assert 8 <= cap <= 80, cap
    assert c.set_overwrite_oldest(True)[0] == RESP_OK
    for k in range(cap):    # the oldest first: key(0) would go first
        assert c.add_contact_full(key(k), "c%d" % k, lastmod=RTC - 1000 + k)[0] == RESP_OK
    assert i.request("contact.protect", {"key": key(0).hex(), "protected": True})["status"] == "ok"
    deleted(c, i)
    r = put(i, key(500), "newcomer")
    assert r["status"] == "ok" and r["result"]["created"] is True, r
    assert deleted(c, i) == [key(1)], "the oldest unprotected contact goes, with the native overwrite event"
    nat = native(c)
    assert key(0) in nat and key(500) in nat and key(1) not in nat and len(nat) == cap
    # only protected contacts left: nothing may be evicted, and the app's answer is the interface's
    for k in list(nat):
        assert i.request("contact.protect", {"key": k.hex(), "protected": True})["status"] == "ok"
    deleted(c, i)
    r = put(i, key(501), "one too many")
    assert r["status"] == "failed" and r["error"] == "table-full", r
    assert c.add_contact_full(key(502), "app too", lastmod=RTC)[:2] == bytes([RESP_ERR, ERR_TABLE_FULL])[:2]
    assert deleted(c, i) == [] and len(native(c)) == cap
    # an update of a stored contact still works at capacity, and unprotecting makes room again
    assert put(i, key(500), "renamed")["result"]["changed"] is True
    assert i.request("contact.protect", {"key": key(500).hex(), "protected": False})["status"] == "ok"
    assert put(i, key(501), "now fits")["status"] == "ok"
    assert key(500) not in native(c), "the one unprotected contact made the room"


def test_without_the_overwrite_setting_a_full_table_refuses_a_new_contact(nodes):
    n = tight(nodes, "ct6").start()
    c, i = client(n)
    cap = c.contacts_line()["store"][1]
    for k in range(cap):
        assert put(i, key(k), "c%d" % k)["status"] == "ok", k
    r = put(i, key(900), "extra")
    assert r["status"] == "failed" and r["error"] == "table-full", r
    assert len(native(c)) == cap and key(900) not in native(c)
    keys, pages = walk(i)
    assert len(keys) == cap and len(set(keys)) == cap and pages > 1


def test_a_caller_the_cli_table_does_not_trust_writes_nothing(nodes):
    n = nodes("companion_standalone", "ct7", seed=25, rtc=RTC).start()
    c, i = client(n)
    assert put(i, key(1), "kept")["status"] == "ok"
    assert c.cli("set pin 123456").startswith("> pin is now")   # the published default PIN: an untrusted link
    c.request_reboot()   # saves the contact the lazy write has not reached yet
    assert n.wait_exit(15) == 75
    c.close()
    n.start()
    b = n.connect_ble(123456)
    b.app_start()
    bi = mecon_iface.IfaceClient(b._send, b._read_frame)
    assert put(bi, key(2), "intruder")["status"] == "refused"
    assert bi.request("contact.list")["status"] == "refused", "a read of the roster is the CLI's read class: not for this link"
    assert bi.request("contact.remove", {"key": key(1).hex()})["status"] == "refused"
    assert bi.request("contact.protect", {"key": key(1).hex(), "protected": True})["status"] == "refused"
    b.close()
    u, ui = client(n)
    nat = native(u)
    assert set(nat) == {key(1)} and nat[key(1)]["flags"] == 0 and nat[key(1)]["name"] == "kept", nat


def test_every_change_is_written_by_the_lazy_save_without_a_reboot(nodes):
    # A power cut after the lazy write: the change must have been scheduled, not left for a reboot to find.
    n = nodes("companion_standalone", "ct8", seed=26, rtc=RTC).start()
    c, i = client(n)
    for k in range(3):
        assert put(i, key(k), "c%d" % k)["status"] == "ok"
    bucket = os.path.join(n.dir, "ct0")
    assert _wait(lambda: os.path.exists(bucket), 20), "the lazy save never wrote the first bucket"
    time.sleep(0.5)
    c.close()
    n.proc.kill()
    n.proc.wait()
    n.start()
    c, i = client(n)
    assert set(native(c)) == {key(0), key(1), key(2)}
    # one kind of change per power cut: a bucket rewritten for one change would carry the others along
    assert i.request("contact.protect", {"key": key(1).hex(), "protected": True})["status"] == "ok"
    c, i = _cut(n, c)
    assert native(c)[key(1)]["flags"] == 1, "protect"
    assert i.request("contact.remove", {"key": key(2).hex()})["status"] == "ok"
    c, i = _cut(n, c)
    assert set(native(c)) == {key(0), key(1)}, "remove"
    assert put(i, key(0), "renamed")["status"] == "ok"
    c, i = _cut(n, c)
    assert native(c)[key(0)]["name"] == "renamed", "update"


def _cut(n, c):
    """Waits out the lazy save (5 s plus a pass per changed bucket), then loses power and boots again."""
    time.sleep(7)
    c.close()
    n.proc.kill()
    n.proc.wait()
    n.start()
    return client(n)


def _wait(cond, secs):
    end = time.time() + secs
    while time.time() < end and not cond():
        time.sleep(0.2)
    return cond()


def test_hello_with_a_nonce_on_an_enrolled_device_pages_its_capabilities_instead_of_failing(nodes):
    # The five contact.* names took the signed, nonce-bearing hello of a build with every stream past the 1,536-byte
    # result: it answered busy too-large-to-sign. The list is paged, as the contract says, only when it must be.
    from sim.tests.test_deployment_authority import boot
    d = boot(nodes, "hp")
    full = d.i.request("hello")["result"]["capabilities"]
    assert "next" not in d.i.request("hello")["result"], "a hello that fits is one page"
    d.provision()
    plain = d.i.request("hello")["result"]
    assert plain["capabilities"] == full and "next" not in plain
    r = d.i.request("hello", {"nonce": "n" * 32})
    assert r["status"] == "ok", r
    got, page = list(r["result"]["capabilities"]), r["result"]
    assert "hello" in got and "contact.protect" in got and page["next"] == len(got), page
    for _ in range(5):
        if "next" not in page:
            break
        page = d.i.request("hello", {"nonce": "n" * 32, "cursor": page["next"]})["result"]
        got += page["capabilities"]
    assert "next" not in page, "the pages never ended"
    assert got == full, (got, full)
    assert d.i.request("hello", {"cursor": 999})["status"] == "invalid"
