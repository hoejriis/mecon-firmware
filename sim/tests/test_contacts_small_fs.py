"""Migrating a stock /contacts3 into the contact store on a file system too small to hold both (Issue #223).

A SenseCAP T1000-E keeps its contacts on a 100 KB ExtraFS. 350 stock records (53,200 bytes) plus the store's buckets
for them do not fit there at once, so the migration moves the old file a bucket at a time from its end and cuts it
back after each bucket is written. Before this, the store was sized as if the old file stayed, came out at about 100
records, and the import evicted while the serial interface was still NULL: the board crashed at every boot.

Software verification only: the simulated file system is one host directory counted in exact bytes. It does not model
the T1000-E's two file systems or LittleFS block rounding (128-byte blocks there, so close to exact bytes).
"""
import os

import pytest

from sim.tests.test_contacts import STOCK, by_key, connect, contact, p, start, stock_file

EXTRAFS = 0x19000                                            # main.cpp: CustomLFS ExtraFS(0xD4000, 0x19000, 128)
ADV_BLOBS = (4 + 7 + 1 + 2 + 32 + 32 + 4 + 64 + 32) * 100    # the nRF52 build pre-allocates 100 blob records
CHANNELS = 40 * 68                                           # 40 channels in /channels2


def seed(node, n):
    want = [contact(i, flags=1 if i % 9 == 0 else 0) for i in range(n)]
    with open(p(node, "/contacts3"), "wb") as f:
        f.write(stock_file(want))
    with open(p(node, "/adv_blobs"), "wb") as f:
        f.write(bytes(ADV_BLOBS))
    with open(p(node, "/channels2"), "wb") as f:
        f.write(bytes(CHANNELS))
    return {c["key"] for c in want}


@pytest.mark.parametrize("kind", ["companion", "companion_asan"])
def test_350_stock_contacts_move_whole_into_the_store_on_a_t1000e_sized_fs(nodes, kind):
    node = start(nodes, "t1k", kind=kind, extra=["--fs-size", str(EXTRAFS), "--heap-budget", "50000"])
    keys = seed(node, 350)
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    assert count == 350 and set(by_key(got)) == keys, (count, len(got))
    assert not os.path.exists(p(node, "/contacts3")), "the old file goes once every record is in the store"
    assert c.contacts_line()["store"][0] == 350 and c.contacts_line()["unmigrated"] == 0
    c.close()
    node.restart()
    c = connect(node)
    count, got = c.get_contacts()
    assert count == 350 and set(by_key(got)) == keys
    assert node.sanitizer_report() == ""
    c.close()


def test_a_stock_file_the_fs_cannot_take_keeps_every_contact_the_store_has_no_room_for(nodes):
    # 80,000 bytes: the seeded files fit, but the store is sized for about 256 records
    node = start(nodes, "tiny", extra=["--fs-size", "80000", "--heap-budget", "50000"])
    keys = seed(node, 350)
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    stored = set(by_key(got))
    assert 0 < count < 350 and stored <= keys
    data = open(p(node, "/contacts3"), "rb").read()
    left = {STOCK.unpack_from(data, i)[0] for i in range(0, len(data) - len(data) % STOCK.size, STOCK.size)}
    assert stored | left == keys, "nothing is lost: what the store could not take is still in the old file"
    assert c.contacts_line()["unmigrated"] == len(keys - stored), "and the shortfall is reported, not silent"
    size = len(data)
    c.close()
    node.restart()                                           # a later boot neither loses nor churns
    c = connect(node)
    count2, got2 = c.get_contacts()
    assert set(by_key(got2)) == stored and os.path.getsize(p(node, "/contacts3")) == size
    c.close()


def test_a_cut_between_a_bucket_write_and_the_truncation_finishes_without_duplicates(nodes):
    node = start(nodes, "cut", extra=["--fs-size", "80000", "--heap-budget", "50000"])
    keys = seed(node, 350)
    node.start()                                             # part of the file moves before the room runs out
    c = connect(node)
    first = c.get_contacts()[0]
    assert 0 < first < 350
    c.close()
    node.stop()
    want = [contact(i, flags=1 if i % 9 == 0 else 0) for i in range(350)]
    with open(p(node, "/contacts3"), "wb") as f:             # as if no truncation had happened: every record twice
        f.write(stock_file(want))
    node.extra = ["--fs-size", "200000", "--heap-budget", "50000"]   # room for all of them now
    node.start()
    c = connect(node)
    count, got = c.get_contacts()
    assert count == 350 and len(got) == 350 and set(by_key(got)) == keys
    assert not os.path.exists(p(node, "/contacts3"))
    c.close()
