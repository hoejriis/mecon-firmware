"""The Repeater's region table (/regions2): saved without a window that loses it, and listed without recursing forever
on a damaged file."""
import os
import struct
import time

import pytest

from sim.harness import CliClient


def _p(node, path):
    return os.path.join(node.dir, path.lstrip("/"))


def _size(node, path):
    return os.path.getsize(_p(node, path)) if os.path.exists(_p(node, path)) else None


def _with_regions(nodes, name, kind="repeater"):
    rep = nodes(kind, name, seed=5).start()
    cli = CliClient(rep)
    assert cli.command("region put Alpha").startswith("OK")
    assert cli.command("region put Beta Alpha").startswith("OK")
    assert cli.command("region save") == "OK"
    cli.close()
    rep.stop()
    return rep


@pytest.mark.parametrize("cut", [0, 1, 100, 300])
def test_a_save_cut_in_the_temp_file_keeps_the_previous_regions(nodes, cut):
    rep = _with_regions(nodes, "cut%d" % cut)
    full = _size(rep, "/regions2")
    rep.fault = "/regions2.tmp:%d" % cut
    rep.start()
    cli = CliClient(rep)
    assert cli.command("region put Gamma").startswith("OK")
    cli.sock.sendall(b"region save\r")
    assert rep.wait_exit(10) == 99, "the save never reached the injected fault (not written via the temp file?)"
    rep.fault = None
    rep.start()
    cli = CliClient(rep)
    tree = cli.command("region")
    cli.close()
    assert tree == "*^ F\n Alpha F\n  Beta F"     # the previous complete table, Gamma never half-saved
    assert _size(rep, "/regions2") == full          # the live file was never touched


def test_a_save_cut_before_the_swap_keeps_the_previous_regions(nodes):
    rep = _with_regions(nodes, "swap")
    rep.fault_before = "rename:/regions2.tmp"
    rep.start()
    cli = CliClient(rep)
    assert cli.command("region put Gamma").startswith("OK")
    cli.sock.sendall(b"region save\r")
    assert rep.wait_exit(10) == 99
    rep.fault_before = None
    rep.start()
    cli = CliClient(rep)
    tree = cli.command("region")
    cli.close()
    assert tree == "*^ F\n Alpha F\n  Beta F"


def _entry(rid, parent, name, flags=0):
    return struct.pack("<HH", rid, parent) + name.encode().ljust(31, b"\0") + bytes([flags]) + bytes(128)


def test_a_damaged_table_with_a_self_parent_and_a_cycle_is_listed_without_overflowing(nodes):
    # on the AddressSanitizer build: walking a cycle past the listing's fixed stack is an overflow it stops the node for,
    # where an ordinary build may write past the array and carry on
    rep = _with_regions(nodes, "damaged", kind="repeater_asan")
    header = bytes(3) + struct.pack("<HHBH", 0, 0, 0, 10)          # default, home, the wildcard's flags, next id
    with open(_p(rep, "/regions2"), "wb") as f:
        # a duplicated id makes a cycle the wildcard reaches (7 -> 8 -> the other 7 -> 8 ...): only the depth bound ends it
        f.write(header + _entry(0, 0, "Zero") + _entry(3, 3, "Self") + _entry(4, 5, "Loop1") + _entry(5, 4, "Loop2")
                + _entry(6, 0, "Fine") + _entry(7, 0, "Dup7a") + _entry(8, 7, "Child8") + _entry(7, 8, "Dup7b"))
    rep.start()
    cli = CliClient(rep)
    tree = cli.command("region")
    alive = cli.command("ver")
    cli.close()
    assert rep.proc.poll() is None, "the node died listing a damaged region table"
    assert tree.splitlines()[0] == "*^ F" and " Fine F" in tree.splitlines()
    assert "Zero" not in tree and "Self" not in tree   # id 0 and a self-parent are damage, not children
    assert " Dup7a F" in tree.splitlines()               # the reachable cycle was walked (the reply is cut, not it)
    assert alive


def test_a_branching_tree_is_listed_in_the_same_order_as_before(nodes):
    # pre-order, each region's children in table order: the order the recursive listing printed
    rep = nodes("repeater", "tree", seed=5).start()
    cli = CliClient(rep)
    for c in ("region put Alpha", "region put Beta Alpha", "region put Gamma", "region put Delta Alpha",
              "region put Eps Beta"):
        assert cli.command(c).startswith("OK")
    tree = cli.command("region")
    cli.close()
    assert tree == "*^ F\n Alpha F\n  Beta F\n   Eps F\n  Delta F\n Gamma F"


def test_duplicated_ids_that_branch_at_every_level_are_listed_at_once(nodes):
    # 15 regions with id 8 under parent 7 and 16 with id 7 under parent 8, reached through one id 7 under the wildcard:
    # every level branches 15 or 16 ways to the depth bound, so only a bound on the lines listed ends the walk
    rep = _with_regions(nodes, "branching")
    header = bytes(3) + struct.pack("<HHBH", 0, 0, 0, 10)
    body = _entry(7, 0, "Top") + b"".join(_entry(8, 7, "Eight%d" % k) for k in range(15)) + \
        b"".join(_entry(7, 8, "Seven%d" % k) for k in range(16))
    with open(_p(rep, "/regions2"), "wb") as f:
        f.write(header + body)
    rep.start()
    cli = CliClient(rep)
    t0 = time.monotonic()
    tree = cli.command("region")
    took = time.monotonic() - t0
    alive = cli.command("ver")
    cli.close()
    assert tree.splitlines()[:2] == ["*^ F", " Top F"]
    assert took < 1.0, "the listing took %.1f s" % took
    assert alive
