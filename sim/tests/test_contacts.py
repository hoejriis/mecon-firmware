"""Adaptive contact memory on simulated boards (docs/CONTACTS.md).

Software verification only: the simulated file system is a host directory, so nothing here says how fast or how
worn a real flash is, and the heap a board reports at boot is a number the test chooses (--heap-budget).

Storage faults use the SimFS hook: a power cut after N bytes of a named file, or just before a rename/remove.
"""
import os
import struct
import time

import pytest

from sim.harness import (node_log, CompanionClient, ERR_TABLE_FULL, PUSH_CONTACT_DELETED, RESP_ERR)

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


@pytest.mark.lockstep()
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
    # cb's path return (direct, to ca) follows ca's flood PATH; cd cannot hear cb, so a send now collides with it at ca
    assert ether.wait_quiet(), "the channel never went quiet"
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


@pytest.mark.lockstep()
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
    for k in range(2 * cap):                          # strangers newer than anything the node's clock has shown
        assert ca.add_contact_full(key(9000 + k), "s%d" % k, lastmod=RTC + 1000000)[0] == 0
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


# ------------------------------------------------------------------------------------------------ boot file system work
# A real SPIFFS is flat and scans its file table on every lookup, so a boot that probes every possible bucket took
# 87-170 s on a Heltec V3 (the simulated file system is a host directory, where the same calls are instant). These
# tests count the file system calls of a boot: they must follow the files present, never the store's capacity.

def _counted(nodes, tmp_path, name, extra=None):
    calls = str(tmp_path / (name + ".calls"))
    return start(nodes, name, extra=["--fs-calls", calls, "--heap-budget", "400000"] + (extra or [])), calls


def _calls_after_boot(node, calls):
    c = connect(node)                                   # the node answers only once its boot (and the store) is done
    c.device_query()
    with open(calls) as f:
        got = {k: int(v) for k, v in (kv.split("=") for kv in f.read().split())}
    got["all"] = sum(v for k, v in got.items() if k not in ("missing", "create"))   # both re-count calls already counted
    print("FSCALLS", os.path.basename(calls), got, "files", len(os.listdir(node.dir)))
    return c, got


def _files_present(node):
    return len(os.listdir(node.dir))


def test_boot_file_system_calls_do_not_grow_with_the_capacity_empty_store(nodes, tmp_path):
    node, calls = _counted(nodes, tmp_path, "empty")
    node.start()
    c, got = _calls_after_boot(node, calls)
    files = _files_present(node)
    assert got["exists"] <= 30 and got["all"] <= 60 + 4 * files, "boot used %r file system calls for %d files" % (got, files)


def test_boot_file_system_calls_follow_the_files_present_with_350_contacts(nodes, tmp_path):
    node, calls = _counted(nodes, tmp_path, "full")
    node.start()
    c = connect(node)
    for i in range(350):
        assert c.add_contact_full(key(i), "c-%d" % i, lastmod=RTC + i)[0] == 0
    reboot_and_wait(node, c)
    assert len([f for f in os.listdir(node.dir) if f.startswith("ct")]) >= 43
    node.start()
    c, got = _calls_after_boot(node, calls)
    files = _files_present(node)
    assert got["exists"] <= 30 and got["all"] <= 60 + 4 * files, "boot used %r file system calls for %d files" % (got, files)
    count, _ = c.get_contacts()
    assert count == 350, "every contact is still loaded"


def test_boot_looks_up_no_file_that_is_not_there(nodes, tmp_path):
    # On SPIFFS a lookup that finds nothing scans the whole file table: about 0.9 s per lookup on a Heltec V4, and a
    # steady-state boot used to make 15 of them (the .tmp/.new leftovers recover() checks for, and data files not yet
    # written). One listing at boot (helpers/BootPresence.h) now answers those: none is looked up.
    node, calls = _counted(nodes, tmp_path, "nolookup")
    node.start()
    c, first = _calls_after_boot(node, calls)
    assert first["missing"] == 0, "first boot looked up missing files: %r" % [
        l for l in node_log(node).splitlines() if l.startswith("SIMFS missing")]
    for i in range(20):
        assert c.add_contact_full(key(i), "n-%d" % i, lastmod=RTC + i)[0] == 0
    reboot_and_wait(node, c)
    open(calls, "w").close()
    before = len(node_log(node).splitlines())
    node.start()
    c, again = _calls_after_boot(node, calls)
    assert again["missing"] == 0, "a steady-state boot looked up missing files: %r" % [
        l for l in node_log(node).splitlines()[before:] if l.startswith("SIMFS missing")]
    count, _ = c.get_contacts()
    assert count == 20, "every contact is still loaded"


def test_a_contact_save_looks_up_no_file_that_is_not_there(nodes, tmp_path):
    # Measured on a Heltec V3 with 230+ contacts: each new bucket's save held the loop 5.4 s. Saving a bucket looked
    # up names that were not there (the bucket itself when new, its .tmp and .new leftovers, and the old file to
    # remove), each a scan of the whole SPIFFS file table. Now a save makes only the three name checks a swap cannot
    # avoid (creating the temp file and two renames), whether the bucket is new or already there.
    node, calls = _counted(nodes, tmp_path, "savelookup")
    node.start()
    c, _ = _calls_after_boot(node, calls)
    for i in range(16):                                   # buckets 0 and 1 full
        assert c.add_contact_full(key(i), "s-%d" % i, lastmod=RTC + i)[0] == 0

    def creates(lines):
        return [l for l in lines if l.startswith("SIMFS create")]

    def wait_settled(before, want_creates):
        end = time.time() + 30
        while time.time() < end:
            if len(creates(node_log(node).splitlines()[before:])) >= want_creates:
                break
            time.sleep(0.2)
        time.sleep(1.0)                                   # anything else that save would do
        return node_log(node).splitlines()[before:]

    wait_settled(0, 6)                                    # both buckets saved at least once
    for label, idx in (("new bucket", 16), ("existing bucket", 17)):
        before = len(node_log(node).splitlines())
        assert c.add_contact_full(key(idx), "s-%d" % idx, lastmod=RTC + idx)[0] == 0
        got = wait_settled(before, 3)
        missing = [l for l in got if l.startswith("SIMFS missing")]
        assert missing == [], "%s: the save looked up missing files: %r" % (label, missing)
        assert len(creates(got)) == 3, "%s: %r" % (label, creates(got))
    count, _ = c.get_contacts()
    assert count == 18


def test_a_swap_left_by_a_power_cut_in_a_high_bucket_is_recovered_at_boot(nodes, tmp_path):
    donor = start(nodes, "donor")                        # four contacts with other keys: one bucket, ct0
    donor.start()
    c = connect(donor)
    for i in range(100, 104):
        assert c.add_contact_full(key(i), "donor-%d" % i, lastmod=RTC + i)[0] == 0
    reboot_and_wait(donor, c)
    verified = open(p(donor, "/ct0"), "rb").read()
    donor.stop()
    node, calls = _counted(nodes, tmp_path, "cut")
    node.start()
    c = connect(node)
    for i in range(12):
        assert c.add_contact_full(key(i), "before-%d" % i, lastmod=RTC + i)[0] == 0
    reboot_and_wait(node, c)
    node.stop()
    with open(p(node, "/ct40.new"), "wb") as f:          # cut after the old ct40 was removed: a verified copy is all that is left
        f.write(verified)
    with open(p(node, "/ct41.tmp"), "wb") as f:          # cut while writing: never verified, to be dropped
        f.write(b"\x01" * 300)
    with open(p(node, "/ct1.new"), "wb") as f:           # cut after the live file was kept: the live file wins
        f.write(b"\x02" * 300)
    node.start()
    c, got = _calls_after_boot(node, calls)
    assert os.path.exists(p(node, "/ct40")) and not os.path.exists(p(node, "/ct40.new"))
    assert not os.path.exists(p(node, "/ct41.tmp")) and not os.path.exists(p(node, "/ct1.new")) and os.path.exists(p(node, "/ct1"))
    count, got_contacts = c.get_contacts()
    names = {x["key"]: x["name"] for x in got_contacts}
    assert count == 16 and names[key(101)] == "donor-101" and names[key(7)] == "before-7"
    files = _files_present(node)
    assert got["exists"] <= 30 and got["all"] <= 60 + 4 * files, "boot used %r file system calls for %d files" % (got, files)


# ------------------------------------------------------------------------------------------------ rescue "rebuild"

def _standalone(nodes, extra=None, fault=None, name="r"):
    return start(nodes, name, kind="companion_standalone", extra=["--heap-budget", "50000"] + (extra or []), fault=fault)


def _fill_and_reboot(node, count):
    """count contacts through the app, then a clean reboot: they are on the file system, few of them in the cache."""
    c = connect(node)
    for i in range(count):
        assert c.add_contact_full(key(i), "c%d" % i, lastmod=RTC + i)[0] == 0, i
    reboot_and_wait(node, c)


def _rescue(node, command="rebuild", timeout=20):
    """Long press in the first 8 s of the UI opens the rescue console on the serial port; returns what `command` printed."""
    s = node.connect()
    s.settimeout(0.3)
    node.ui.press("long")
    buf = b""

    def read_until(marker, secs):
        nonlocal buf
        end = time.time() + secs
        while time.time() < end and marker not in buf:
            try:
                d = s.recv(4096)
            except OSError:
                continue
            if d == b"":
                break
            buf += d
        return marker in buf

    assert read_until(b"CLI Rescue", 10), buf
    buf = b""
    s.sendall(command.encode() + b"\r")
    read_until(b"damaged\n", timeout) or read_until(b"failed", 0.1)
    out = buf.decode(errors="replace")
    s.close()
    return out


def _count_after_restart(node):
    node.stop()
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    return count, got, c


def test_rebuild_looks_up_no_bucket_that_is_not_there(nodes, tmp_path):
    # Measured on v3test: rebuild with 2 contacts took ~88 s. It asked for every possible bucket file, and on SPIFFS a
    # lookup that finds nothing scans the whole file table (~0.9 s each). It now lists the file system once, as boot
    # does; the only missing names it may still look up are those of the one bucket it rewrites (the swap's checks).
    calls = str(tmp_path / "rebuild.calls")
    node = _standalone(nodes, extra=["--fs-calls", calls], name="rbl").start()
    _fill_and_reboot(node, 2)
    node.stop()
    node.start()
    before = len(node_log(node).splitlines())
    out = _rescue(node)
    assert "rebuilt 2 contacts, 0 damaged" in out, out
    missing = [l for l in node_log(node).splitlines()[before:] if l.startswith("SIMFS missing") and "/ct" in l]
    print("REBUILD_MISSING", missing)
    assert len(missing) <= 3, "rebuild looked up missing bucket files: %r" % missing
    count, _, _ = _count_after_restart(node)
    assert count == 2


def test_rebuild_keeps_every_stored_contact_beyond_the_cache(nodes):
    node = _standalone(nodes).start()
    _fill_and_reboot(node, 120)
    node.start()
    before = connect(node)
    n_before, got_before = before.get_contacts()
    line = before.contacts_line()
    assert n_before == 120 and line["cache"][0] < 120 <= line["store"][0], line
    node.stop()
    node.start()
    out = _rescue(node)
    assert "rebuilt 120 contacts, 0 damaged" in out, out
    n_after, got, c = _count_after_restart(node)
    assert n_after == n_before == 120
    assert {x["key"] for x in got} == {x["key"] for x in got_before}
    assert by_key(got) == by_key(got_before), "every field of every contact is as before"
    assert c.contacts_line()["damaged"] == 0
    assert not [f for f in os.listdir(node.dir) if f.endswith(".bad")], "nothing was damaged, nothing is kept aside"


def test_rebuild_isolates_a_damaged_record_and_keeps_all_the_others(nodes):
    node = _standalone(nodes).start()
    _fill_and_reboot(node, 100)
    with open(p(node, "/ct3"), "r+b") as f:             # contact 3*8+2 = 26 (records are numbered in the order they were added)
        f.seek(2 * 160 + 40)
        b = f.read(1)
        f.seek(2 * 160 + 40)
        f.write(bytes([b[0] ^ 0xFF]))
    found = open(p(node, "/ct3"), "rb").read()
    node.start()
    out = _rescue(node)
    assert "rebuilt 99 contacts, 1 damaged" in out, out
    assert "kept aside" in out, out
    assert open(p(node, "/ct3.bad"), "rb").read() == found, "the bucket is kept as it was found"
    count, got, c = _count_after_restart(node)
    assert count == 99
    assert {x["key"] for x in got} == {key(i) for i in range(100)} - {key(26)}
    assert c.contacts_line()["damaged"] == 0, "the rewritten bucket is clean"
    assert os.path.getsize(p(node, "/ct3")) == 1280


def test_a_power_cut_during_rebuild_keeps_the_previous_buckets(nodes):
    node = _standalone(nodes).start()
    _fill_and_reboot(node, 100)
    snapshot = {f: open(p(node, f), "rb").read() for f in os.listdir(node.dir) if f.startswith("ct")}
    assert len(snapshot) == 13
    node.fault = "/ct5.tmp:700"                          # the sixth bucket write is cut in its temp file
    node.start()
    out = _rescue(node)
    assert node.wait_exit(15) == 99, "the rebuild never reached the injected fault: " + out
    for f, data in snapshot.items():
        assert open(p(node, f), "rb").read() == data, f   # every bucket as before (the clean ones were rewritten to the same bytes)
    node.fault = None
    count, got, c = _count_after_restart(node)
    assert count == 100 and {x["key"] for x in got} == {key(i) for i in range(100)}
    assert c.contacts_line()["damaged"] == 0


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


# ------------------------------------------------------------------------------------------------ lazy write after a burst

def test_a_burst_of_adds_is_written_bucket_by_bucket_and_survives_a_power_cut(nodes):
    # A flush of every changed bucket in one pass held the loop for ~5.6 s on hardware after 100 adds. The lazy write
    # now takes one bucket per pass and keeps going until the last one is written, with no reboot to force it.
    node = start(nodes).start()
    c = connect(node)
    for i in range(40):                               # records 0..39: buckets 0..4
        assert c.add_contact_full(key(i), "burst-%d" % i, lastmod=RTC + i)[0] == 0, i
    buckets = [p(node, "/ct%d" % b) for b in range(5)]
    end = time.time() + 20
    while time.time() < end and not all(os.path.exists(f) for f in buckets):
        time.sleep(0.2)
    assert all(os.path.exists(f) for f in buckets), [os.path.exists(f) for f in buckets]
    time.sleep(0.5)
    c.close()
    node.proc.kill()                                  # a power cut: no reboot command, nothing flushed on the way out
    node.proc.wait()
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    assert count == 40 and sorted(x["name"] for x in got) == sorted("burst-%d" % i for i in range(40))


def _werr(c):
    return c.contacts_line()["werr"]


def _wait(cond, secs):
    end = time.time() + secs
    while time.time() < end and not cond():
        time.sleep(0.3)
    return cond()


def test_a_save_that_fails_once_is_retried_without_another_change(nodes, tmp_path):
    # A write error used to end the delayed save until the next contact change, so a contact the app was told OK
    # could stay unsaved. The save is now set again a few times, further apart each time.
    fail = tmp_path / "write-fail"
    fail.write_text("/ct\n")
    node = start(nodes, extra=["--write-fail", str(fail)]).start()
    c = connect(node)
    for i in range(16):                               # buckets 0 and 1
        assert c.add_contact_full(key(i), "retry-%d" % i, lastmod=RTC + i)[0] == 0, i
    assert _wait(lambda: _werr(c) >= 1, 12), "the first save never ran"
    assert not os.path.exists(p(node, "/ct0")) and not os.path.exists(p(node, "/ct1"))
    fail.unlink()                                     # the file system recovers; nothing else changes
    buckets = [p(node, "/ct0"), p(node, "/ct1")]
    assert _wait(lambda: all(os.path.exists(f) for f in buckets), 12), [os.path.exists(f) for f in buckets]
    c.close()
    node.proc.kill()
    node.proc.wait()
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    assert count == 16


def test_a_save_that_keeps_failing_gives_up_after_three_retries(nodes, tmp_path):
    # 1 attempt plus retries 5, 15 and 45 s later: 4 write errors in about 70 s, then nothing until the next change.
    fail = tmp_path / "write-fail"
    fail.write_text("/ct\n")
    node = start(nodes, extra=["--write-fail", str(fail)]).start()
    c = connect(node)
    assert c.add_contact_full(key(1), "stuck", lastmod=RTC)[0] == 0
    assert _wait(lambda: _werr(c) >= 4, 90), _werr(c)
    time.sleep(20)                                    # longer than any retry delay but the last
    assert _werr(c) == 4, "the save kept retrying a file system that keeps failing"
    assert c.add_contact_full(key(2), "next", lastmod=RTC)[0] == 0   # the next change starts a new save
    assert _wait(lambda: _werr(c) >= 5, 12), _werr(c)
