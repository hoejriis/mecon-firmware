"""Adaptive contact memory on simulated boards (docs/CONTACTS.md).

Software verification only: the simulated file system is a host directory, so nothing here says how fast or how
worn a real flash is, and the heap a board reports at boot is a number the test chooses (--heap-budget).

Storage faults use the SimFS hook: a power cut after N bytes of a named file, or just before a rename/remove.
"""
import os
import struct
import time

import pytest

from sim.harness import (CompanionClient, ERR_TABLE_FULL, PUSH_CONTACT_DELETED, RESP_ERR)

RTC = 1760000000
DAY = 86400
STOCK = struct.Struct("<32s32sBBBIBI64sIii")   # the 152-byte record of the stock /contacts3 file
assert STOCK.size == 152


def key(n):
    return bytes([n & 255, (n >> 8) & 255]) + bytes([0x40]) * 29 + bytes([0x77])


def stock_file(contacts):
    """contacts: list of dicts as returned by get_contacts, or built by contact()."""
    out = b""
    for c in contacts:
        out += STOCK.pack(c["key"], c["name"].encode(), c["type"], c["flags"], 0, c.get("sync_since", 0), c["path_len"],
                          c["adv"], c["path"].ljust(64, b"\0"), c["lastmod"], c["lat"], c["lon"])
    return out


def contact(n, flags=0, path_len=0xFF, lastmod=None, ctype=1):
    return {"key": key(n), "name": "contact-%d" % n, "type": ctype, "flags": flags, "path_len": path_len,
            "path": (bytes(range(1, 1 + path_len)) if path_len != 0xFF else b"").ljust(64, b"\0"), "adv": RTC - 5000 + n, "lat": n * 1000,
            "lon": -n * 1000, "lastmod": lastmod if lastmod is not None else RTC - 3000 + n, "sync_since": n}


def start(nodes, name="a", extra=None, kind="companion", fault=None, fault_before=None, seed=5):
    n = nodes(kind, name, seed=seed, rtc=RTC, extra=extra, fault=fault, fault_before=fault_before)
    return n


def connect(node):
    c = CompanionClient(node)
    c.device_query()
    c.app_start()
    return c


def by_key(contacts):
    return {c["key"]: c for c in contacts}


def p(node, path):
    return os.path.join(node.dir, path.lstrip("/"))


def reboot_and_wait(node, client):
    client.request_reboot()
    assert node.wait_exit(15) == 75


# ------------------------------------------------------------------------------------------------ migration

def test_stock_contacts_file_migrates_unchanged(nodes):
    want = [contact(i, flags=1 if i % 9 == 0 else 0, path_len=(i % 5) if i % 3 == 0 else 0xFF) for i in range(60)]
    node = start(nodes)
    with open(p(node, "/contacts3"), "wb") as f:
        f.write(stock_file(want))
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    assert count == 60 and len(got) == 60
    g = by_key(got)
    for w in want:
        r = g[w["key"]]
        for field in ("name", "type", "flags", "path_len", "path", "adv", "lat", "lon", "lastmod"):
            assert r[field] == w[field], (w["name"], field)
    assert not os.path.exists(p(node, "/contacts3")), "the stock file stays behind after a complete migration"
    assert os.path.exists(p(node, "/ct0"))
    line = c.contacts_line()
    assert line["mode"] == "adaptive" and line["store"][0] == 60
    # and the same after a restart, now from the store alone
    c.close()
    node.restart()
    c = connect(node)
    count, got2 = c.get_contacts()
    assert count == 60 and by_key(got2) == g


def test_migration_cut_before_the_old_file_goes_finishes_without_duplicates(nodes):
    want = [contact(i) for i in range(40)]
    node = start(nodes, fault_before="remove:/contacts3")
    with open(p(node, "/contacts3"), "wb") as f:
        f.write(stock_file(want))
    with pytest.raises(RuntimeError):
        node.start()                                  # the node dies during boot, buckets written, old file still there
    assert node.proc.wait(5) == 99
    assert os.path.exists(p(node, "/contacts3"))
    node.fault_before = None
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    assert count == 40 and len(got) == 40 and len({x["key"] for x in got}) == 40
    assert not os.path.exists(p(node, "/contacts3"))


def test_a_stock_file_beside_a_filled_store_is_merged_not_duplicated(nodes):
    node = start(nodes).start()
    c = connect(node)
    for i in range(10):
        assert c.add_contact_full(key(i), "new-%d" % i, lastmod=RTC)[0] == 0
    reboot_and_wait(node, c)
    old = [contact(i) for i in range(5, 25)]          # 5..9 are already in the store under a newer name
    with open(p(node, "/contacts3"), "wb") as f:
        f.write(stock_file(old))
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    g = by_key(got)
    assert count == 25 and len(got) == 25
    assert g[key(7)]["name"] == "new-7", "the store's own version wins"
    assert g[key(20)]["name"] == "contact-20"
    assert not os.path.exists(p(node, "/contacts3"))


# ------------------------------------------------------------------------------------------------ sync, cache

def test_app_sync_returns_every_stored_contact_beyond_the_cache(nodes):
    node = start(nodes, extra=["--heap-budget", "50000"]).start()      # tiny heap: the cache is at its floor
    c = connect(node)
    for i in range(200):
        assert c.add_contact_full(key(i), "c%d" % i, lastmod=RTC + i)[0] == 0, i
    line = c.contacts_line()
    assert line["store"][0] == 200
    assert line["cache"][1] <= 24 and line["cache"][0] <= line["cache"][1]
    count, got = c.get_contacts()
    assert count == 200 and len(got) == 200
    assert {x["key"] for x in got} == {key(i) for i in range(200)}
    # the same through a restart, then the app's incremental sync with a 'since' filter
    reboot_and_wait(node, c)
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    assert count == 200 and len(got) == 200
    count, newer = c.get_contacts(since=RTC + 189)
    assert count == 200 and len(newer) == 10


def test_capacity_and_cache_follow_the_heap_the_board_reports(nodes):
    big = start(nodes, "big", extra=["--heap-budget", "400000"]).start()
    small = start(nodes, "small", extra=["--heap-budget", "50000"], seed=6).start()
    lb, ls = connect(big).contacts_line(), connect(small).contacts_line()
    assert lb["cache"][1] == 96 and lb["store"][1] == 1000
    assert ls["cache"][1] < lb["cache"][1] and ls["cache"][1] >= 16
    assert ls["store"][1] >= 350, "never below the stock 350 while the file system has room"
    assert ls["ram"] < lb["ram"]


def test_ram_use_is_below_the_stock_array_at_equal_or_higher_capacity(nodes):
    node = start(nodes).start()
    line = connect(node).contacts_line()
    stock_array = (350 + 8) * 184                      # ContactInfo is 184 bytes, 358 slots in the stock build
    assert line["store"][1] >= 350
    assert line["ram"] < stock_array * 0.65, (line["ram"], stock_array)


def test_device_query_reports_half_the_capacity_capped_at_one_byte(nodes):
    big = start(nodes, "big").start()
    f = CompanionClient(big).device_query()
    assert f[2] == 255, "a 1000-record store must not wrap the one-byte field"
    tiny = tight_node(nodes, "tiny").start()
    c = connect(tiny)
    cap = c.contacts_line()["store"][1]
    assert c.device_query()[2] == cap // 2


@pytest.mark.timescale(10)
def test_favourites_stay_resident_and_reachable_under_a_small_heap(ether, nodes):
    a = nodes("companion", "ca", ether_path=ether.path, seed=11, rtc=RTC, extra=["--heap-budget", "50000"]).start()
    b = nodes("companion", "cb", ether_path=ether.path, seed=22, rtc=RTC).start()
    d = nodes("companion", "cd", ether_path=ether.path, seed=33, rtc=RTC).start()
    assert ether.wait_for_nodes(["ca", "cb", "cd"])
    ether.set_link("ca", "cb")
    ether.set_link("ca", "cd")
    ca, cb, cd = connect(a), connect(b), connect(d)
    # A knows the plain contact D first, B (a favourite) second, then 60 strangers that push D out of the cache
    assert ca.add_contact_full(cd.pub_key, "dora", lastmod=RTC)[0] == 0
    assert ca.add_contact_full(cb.pub_key, "bob", flags=1, lastmod=RTC)[0] == 0
    for i in range(60):
        assert ca.add_contact_full(key(i), "x%d" % i, lastmod=RTC - 100 + i)[0] == 0
    cb.add_contact_full(ca.pub_key, "alice")
    cd.add_contact_full(ca.pub_key, "alice")
    before = ca.contacts_line()
    assert before["cache"][1] <= 24 and before["pinned"] >= 1
    cb.send_direct(ca.pub_key, "from the favourite", RTC + 5)
    msgs = ca.sync_messages(timeout=90, want=1)
    assert [m["text"] for m in msgs] == ["from the favourite"]
    mid = ca.contacts_line()
    assert mid["loads"] == before["loads"], "a favourite's message needed the file system: it was not resident"
    cd.send_direct(ca.pub_key, "from the plain contact", RTC + 6)
    msgs = ca.sync_messages(timeout=90, want=1)
    assert [m["text"] for m in msgs] == ["from the plain contact"], "a contact that is only in the store must still be reachable"
    after = ca.contacts_line()
    assert after["loads"] > mid["loads"]


# ------------------------------------------------------------------------------------------------ eviction

def tight_node(nodes, name="t", extra_room=16):
    """A node whose file system leaves room for only a few dozen contacts."""
    return start(nodes, name, extra=["--fs-size", str(8192 + 3000 + extra_room * 200), "--heap-budget", "50000"])


def fill(c, count, start_n=0, flags=0, lastmod_of=lambda n: RTC - 1000 + n):
    for n in range(start_n, start_n + count):
        r = c.add_contact_full(key(n), "c%d" % n, flags=flags, lastmod=lastmod_of(n))
        assert r[0] == 0, (n, r)


def deleted_keys(c):
    out = []
    while True:
        f = c.wait_push(PUSH_CONTACT_DELETED, timeout=0.3)
        if f is None:
            return out
        out.append(bytes(f[1:33]))


def test_a_full_store_evicts_the_least_meaningful_and_never_a_favourite(nodes):
    node = tight_node(nodes).start()
    c = connect(node)
    cap = c.contacts_line()["store"][1]
    assert 8 <= cap <= 80, cap
    assert c.set_overwrite_oldest(True)[0] == 0
    fav = [1000, 1001, 1002]
    fill(c, 3, 1000, flags=1, lastmod_of=lambda n: RTC - 50000)           # very old favourites
    c.add_contact_full(key(2000), "messaged", lastmod=RTC - 40000)        # the oldest of all, but we write to it
    c.send_direct(key(2000), "hello", RTC)
    fillers = list(range(cap - 4))
    fill(c, len(fillers), 0)
    assert c.contacts_line()["store"][0] == cap
    while c.pushes:
        c.pushes.pop()
    newcomers = list(range(5000, 5006))
    fill(c, len(newcomers), 5000, lastmod_of=lambda n: RTC)
    gone = deleted_keys(c)
    assert gone == [key(n) for n in fillers[:len(newcomers)]], "the overwrite event names the advert-only contacts, oldest first"
    count, got = c.get_contacts()
    keys = {x["key"] for x in got}
    assert count == cap
    assert all(key(n) in keys for n in fav + [2000]), "a favourite or a contact with message history was evicted"
    assert all(key(n) in keys for n in newcomers)
    line = c.contacts_line()
    assert line["evicted"] == len(newcomers)


def test_a_contact_with_message_history_outlives_an_advert_only_one(nodes):
    node = tight_node(nodes, "m").start()
    c = connect(node)
    cap = c.contacts_line()["store"][1]
    c.set_overwrite_oldest(True)
    c.add_contact_full(key(1), "friend", lastmod=RTC - 20 * DAY)
    c.send_direct(key(1), "hi", RTC)
    fill(c, cap - 1, 100, lastmod_of=lambda n: RTC - 100)
    for k in range(3 * cap):                          # keep strangers coming
        assert c.add_contact_full(key(9000 + k), "s%d" % k, lastmod=RTC)[0] == 0
    count, got = c.get_contacts()
    assert key(1) in {x["key"] for x in got}


def test_a_store_of_only_favourites_refuses_a_new_favourite_and_says_so(nodes):
    node = tight_node(nodes, "f").start()
    c = connect(node)
    cap = c.contacts_line()["store"][1]
    c.set_overwrite_oldest(True)
    fill(c, cap, 0, flags=1)
    while c.pushes:
        c.pushes.pop()
    r = c.add_contact_full(key(7777), "one too many", flags=1, lastmod=RTC)
    assert r[0] == RESP_ERR and r[1] == ERR_TABLE_FULL, r
    r = c.add_contact_full(key(7778), "plain too", flags=0, lastmod=RTC)
    assert r[0] == RESP_ERR and r[1] == ERR_TABLE_FULL, r
    assert deleted_keys(c) == []
    line = c.contacts_line()
    assert line["refused_fav"] == 2 and line["store"][0] == cap and line["fav"] == cap
    count, got = c.get_contacts()
    assert count == cap and {x["key"] for x in got} == {key(n) for n in range(cap)}


def test_without_the_overwrite_setting_a_full_store_refuses_and_keeps_everything(nodes):
    node = tight_node(nodes, "n").start()
    c = connect(node)
    cap = c.contacts_line()["store"][1]
    fill(c, cap, 0)
    r = c.add_contact_full(key(8888), "extra", lastmod=RTC)
    assert r[0] == RESP_ERR and r[1] == ERR_TABLE_FULL
    assert c.contacts_line()["refused_full"] == 1
    assert c.get_contacts()[0] == cap


@pytest.mark.timescale(10)
def test_a_contact_that_wrote_to_us_outlives_advert_only_ones(ether, nodes):
    a = nodes("companion", "ca", ether_path=ether.path, seed=11, rtc=RTC,
              extra=["--fs-size", str(8192 + 3000 + 16 * 200), "--heap-budget", "50000"]).start()
    b = nodes("companion", "cb", ether_path=ether.path, seed=22, rtc=RTC).start()
    assert ether.wait_for_nodes(["ca", "cb"])
    ether.set_link("ca", "cb")
    ca, cb = connect(a), connect(b)
    cap = ca.contacts_line()["store"][1]
    ca.set_overwrite_oldest(True)
    assert ca.add_contact_full(cb.pub_key, "bob", lastmod=RTC - 20 * DAY)[0] == 0      # the oldest contact by far
    fill(ca, cap - 1, 100, lastmod_of=lambda n: RTC - 100)
    cb.add_contact_full(ca.pub_key, "alice")
    cb.send_direct(ca.pub_key, "it is me", RTC + 5)
    assert [m["text"] for m in ca.sync_messages(timeout=90, want=1)] == ["it is me"]
    for k in range(2 * cap):
        assert ca.add_contact_full(key(9000 + k), "s%d" % k, lastmod=RTC + 10)[0] == 0
    count, got = ca.get_contacts()
    assert cb.pub_key in {x["key"] for x in got}, "a contact that wrote to us was evicted before advert-only ones"


# ------------------------------------------------------------------------------------------------ power cuts

def _two_buckets_saved(node):
    c = connect(node)
    for i in range(12):
        assert c.add_contact_full(key(i), "before-%d" % i, lastmod=RTC + i)[0] == 0
    reboot_and_wait(node, c)
    return c


def _change_and_save(node):
    c = connect(node)
    assert c.add_contact_full(key(3), "AFTER-3", lastmod=RTC + 50)[0] == 0
    c.request_reboot()


def _names(node):
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    return count, {x["key"]: x["name"] for x in got}


def _live_file(node):
    return "/ct0" if os.path.exists(p(node, "/ct0")) else "/contacts3"


@pytest.mark.parametrize("cut", [0, 1, 160, 700, 1279])
def test_power_cut_in_the_temp_file_keeps_the_previous_record(nodes, cut):
    node = start(nodes)
    node.start()
    _two_buckets_saved(node)
    live = _live_file(node)
    size = os.path.getsize(p(node, live))
    node.fault = "%s.tmp:%d" % (live, cut)
    node.start()
    _change_and_save(node)
    assert node.wait_exit(15) == 99, "the save never reached the injected fault (written in place, or not at all)"
    node.fault = None
    count, names = _names(node)
    assert count == 12
    assert names[key(3)] == "before-3", "the previous version of the record must survive a cut at any byte"
    assert names[key(4)] == "before-4"
    assert os.path.getsize(p(node, live)) == size


@pytest.mark.parametrize("op,expect", [
    ("rename:/ct0.tmp", "before-3"),     # temp file complete, nothing swapped yet
    ("remove:/ct0", "before-3"),         # verified copy exists, old file not removed yet
    ("rename:/ct0.new", "AFTER-3"),      # old file removed: the boot must finish the swap
])
def test_power_cut_between_write_and_rename(nodes, op, expect):
    node = start(nodes)
    node.start()
    _two_buckets_saved(node)
    assert _live_file(node) == "/ct0"
    node.fault_before = op
    node.start()
    _change_and_save(node)
    assert node.wait_exit(15) == 99
    node.fault_before = None
    count, names = _names(node)
    assert count == 12 and names[key(3)] == expect
    assert all(names[key(i)] == "before-%d" % i for i in range(12) if i != 3)


def test_contacts_are_never_written_in_place(nodes):
    node = start(nodes)
    node.start()
    _two_buckets_saved(node)
    live = _live_file(node)
    node.fault = "%s:40" % live                      # armed on the live file itself: it must never fire
    node.start()
    _change_and_save(node)
    assert node.wait_exit(15) == 75, "a contact save wrote the live file in place"
    node.fault = None
    count, names = _names(node)
    assert count == 12 and names[key(3)] == "AFTER-3"
    for leftover in ("%s.tmp" % live, "%s.new" % live):
        assert not os.path.exists(p(node, leftover)), leftover


def test_a_damaged_bucket_loses_only_what_is_in_it(nodes):
    node = start(nodes)
    node.start()
    _two_buckets_saved(node)
    with open(p(node, "/ct0"), "r+b") as f:             # flip a byte inside the record of contact 2
        f.seek(2 * 160 + 40)
        b = f.read(1)
        f.seek(2 * 160 + 40)
        f.write(bytes([b[0] ^ 0xFF]))
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    keys = {x["key"] for x in got}
    assert count == 11 and key(2) not in keys and key(3) in keys and key(9) in keys
    assert c.contacts_line()["damaged"] == 1


# ------------------------------------------------------------------------------------------------ the opt-out

def test_the_stock_array_build_is_ram_only_and_atomic_and_evicts_the_oldest(nodes):
    node = start(nodes, "s", kind="companion_stockcontacts").start()
    c = connect(node)
    line = c.contacts_line()
    assert line["mode"] == "ram-only" and line["store"][1] == 350 and line["ram"] == 0
    for i in range(12):
        assert c.add_contact_full(key(i), "before-%d" % i, lastmod=RTC + i)[0] == 0
    reboot_and_wait(node, c)
    assert os.path.exists(p(node, "/contacts3")) and not os.path.exists(p(node, "/ct0"))
    node.fault = "/contacts3.tmp:100"
    node.start()
    _change_and_save(node)
    assert node.wait_exit(15) == 99
    node.fault = None
    count, names = _names(node)
    assert count == 12 and names[key(3)] == "before-3", "the stock-array build saves /contacts3 atomically too"

    # stock eviction: the oldest lastmod goes, whatever message history it has
    c = connect(node)
    c.set_overwrite_oldest(True)
    c.send_direct(key(0), "hello", RTC + 100)
    for i in range(12, 350):
        assert c.add_contact_full(key(i), "f%d" % i, lastmod=RTC + 1000 + i)[0] == 0
    assert c.contacts_line()["store"][0] == 350
    assert c.add_contact_full(key(999), "new", lastmod=RTC + 5000)[0] == 0
    gone = deleted_keys(c)
    assert gone == [key(0)]


def test_the_stock_array_build_migrates_nothing_and_reads_a_stock_file(nodes):
    want = [contact(i) for i in range(30)]
    node = start(nodes, "s2", kind="companion_stockcontacts")
    with open(p(node, "/contacts3"), "wb") as f:
        f.write(stock_file(want))
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    assert count == 30 and by_key(got)[key(5)]["name"] == "contact-5"
    assert os.path.exists(p(node, "/contacts3"))
