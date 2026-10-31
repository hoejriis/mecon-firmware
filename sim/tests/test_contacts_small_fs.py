"""A stock /contacts3 on a file system too small to hold it and the contact store's buckets at once (Issue #223).

A SenseCAP T1000-E keeps its contacts on a 100 KB ExtraFS. 350 stock records (53,200 bytes), the store's buckets for
them, /adv_blobs and /channels2 need about 130 KB, so they cannot all be there at once. On the board, main's boot
migration evicted while the serial interface was still NULL (the simulator's crash), and an attempt to shrink the
old file as it migrated corrupted it: the Adafruit LittleFS's truncate cannot be trusted. So:

- the T1000-E Companions keep the stock contact array (CONTACTS_STOCK_ARRAY): /contacts3 stays the table, nothing
  migrates, and a board moved from the private fork keeps every contact;
- an adaptive build that meets such a file never evicts during the migration, keeps the old file while anything
  in it is not in the store, and reports how many with `unmigrated N` on `get contacts.capacity`.

Software verification only: the simulated file system is one host directory counted in exact bytes, with
--fs-enforce failing writes past its size. It does not model the T1000-E's two file systems or LittleFS itself.
"""
import os

import pytest

from sim.tests.test_contacts import STOCK, by_key, connect, contact, key, p, start, stock_file

EXTRAFS = 0x19000                                            # main.cpp: CustomLFS ExtraFS(0xD4000, 0x19000, 128)
ADV_BLOBS = (4 + 7 + 1 + 2 + 32 + 32 + 4 + 64 + 32) * 100    # the nRF52 build pre-allocates 100 blob records
CHANNELS = 40 * 68                                           # 40 channels in /channels2
T1000E = ["--fs-size", str(EXTRAFS), "--fs-enforce", "--heap-budget", "50000"]


def seed(node, n):
    want = [contact(i, flags=1 if i % 9 == 0 else 0) for i in range(n)]
    with open(p(node, "/contacts3"), "wb") as f:
        f.write(stock_file(want))
    with open(p(node, "/adv_blobs"), "wb") as f:
        f.write(bytes(ADV_BLOBS))
    with open(p(node, "/channels2"), "wb") as f:
        f.write(bytes(CHANNELS))
    return {c["key"] for c in want}


def keys_in_old_file(node):
    data = open(p(node, "/contacts3"), "rb").read()
    return {STOCK.unpack_from(data, i)[0] for i in range(0, len(data) - len(data) % STOCK.size, STOCK.size)}


def test_the_stock_array_build_keeps_350_fork_contacts_on_a_t1000e_sized_fs(nodes):
    node = start(nodes, "t1k", kind="companion_stockcontacts", extra=T1000E)
    keys = seed(node, 350)
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    assert count == 350 and set(by_key(got)) == keys, (count, len(got))
    assert keys_in_old_file(node) == keys, "the stock table stays the table"
    c.close()
    node.restart()
    c = connect(node)
    assert set(by_key(c.get_contacts()[1])) == keys
    c.close()


def test_the_stock_array_build_still_saves_a_full_table_with_no_room_for_a_second_copy(nodes):
    # 345 records (52,440 B) beside /adv_blobs and /channels2 leave no room on 100 KB for the atomic save's temp copy;
    # the save then replaces the file the way upstream does, rather than failing and losing the change at the next boot.
    node = start(nodes, "full", kind="companion_stockcontacts", extra=T1000E)
    keys = seed(node, 340)
    node.start()
    c = connect(node)
    for i in range(340, 345):
        assert c.add_contact(key(i), "new-%d" % i) == b"\x00"
    import time
    time.sleep(8)                                            # the lazy save
    c.close()
    node.restart()
    c = connect(node)
    count, got = c.get_contacts()
    assert count == 345 and set(by_key(got)) == keys | {key(i) for i in range(340, 345)}
    c.close()


@pytest.mark.parametrize("kind", ["companion", "companion_asan"])
def test_an_adaptive_build_on_a_t1000e_sized_fs_boots_loses_nothing_and_reports_the_shortfall(nodes, kind):
    node = start(nodes, "ad", kind=kind, extra=T1000E)
    node.start()                                             # overwriting the oldest is on, and the migration still
    c = connect(node)                                        # must not evict: what it evicts is not in the old file
    assert c.set_overwrite_oldest(True)[0] == 0
    c.close()
    node.stop()
    keys = seed(node, 350)
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    stored = set(by_key(got))
    assert 0 < count < 350 and stored <= keys
    assert stored | keys_in_old_file(node) == keys, "nothing is lost: what the store could not take is still in the old file"
    line = c.contacts_line()
    assert line["unmigrated"] == len(keys - stored) and line["evicted"] == 0, "and the shortfall is reported, not silent"
    size = os.path.getsize(p(node, "/contacts3"))
    c.close()
    node.restart()                                           # a later boot neither loses nor churns
    c = connect(node)
    assert set(by_key(c.get_contacts()[1])) == stored and os.path.getsize(p(node, "/contacts3")) == size
    assert node.sanitizer_report() == ""
    c.close()


def test_a_later_boot_with_room_takes_the_rest_without_duplicates(nodes):
    node = start(nodes, "room", extra=T1000E)
    keys = seed(node, 350)
    node.start()                                             # part of the file moves before the room runs out
    c = connect(node)
    first = c.get_contacts()[0]
    assert 0 < first < 350
    c.close()
    node.stop()
    node.extra = ["--fs-size", "300000", "--fs-enforce", "--heap-budget", "50000"]   # room for all of them now
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    assert count == 350 and len(got) == 350 and set(by_key(got)) == keys
    assert not os.path.exists(p(node, "/contacts3"))
    assert c.contacts_line()["unmigrated"] == 0
    c.close()
