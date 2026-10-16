"""Power-cut during a prefs write on the repeater, reproduced with the SimFS fault hook.

The point is to record what stock 1.18 does, not to claim it is good; the observed
outcome is described in docs/SIMULATION.md and asserted here so a change shows up.
"""
import os

from sim.harness import CliClient

PREFS = "/prefs.json"
CUT_AT = 40   # bytes of the new prefs file that reach "flash" before power goes


def _size(node, path):
    p = os.path.join(node.dir, path.lstrip("/"))
    return os.path.getsize(p) if os.path.exists(p) else None


def cut_and_restart(nodes, name):
    rep = nodes("repeater", name, seed=5).start()
    cli = CliClient(rep)
    assert cli.command("set name Before") == "OK"             # a complete prefs file now exists
    assert cli.command("set tx 7") == "OK"                    # a setting stored late in the file
    cli.close()
    full = _size(rep, PREFS)
    rep.stop()

    rep.fault = "%s:%d" % (PREFS, CUT_AT)                     # the next save is cut after CUT_AT bytes
    rep.start()
    cli = CliClient(rep)
    cli.sock.sendall(b"set name After\r")
    assert rep.wait_exit(10) == 99                            # process died mid-write
    after_cut = _size(rep, PREFS)

    rep.fault = None
    rep.start()                                               # reboot on whatever is on "flash"
    cli = CliClient(rep)
    outcome = (cli.command("get name"), cli.command("get tx"))
    cli.close()
    return full, after_cut, outcome


def test_prefs_write_cut_midway_is_reproducible(nodes):
    first = cut_and_restart(nodes, "cut_a")
    second = cut_and_restart(nodes, "cut_b")
    full, after_cut, (name, tx) = first
    assert after_cut == CUT_AT < full          # a truncated file is left behind: stock 1.18 writes in place
    assert first == second                     # and the cut is deterministic
    # Observed on stock 1.18: the loader accepts the 40-byte prefix. The early field (name) takes
    # the NEW value, while a field that was in the lost tail silently reverts to its default (20).
    assert name == "> After"
    assert tx == "> 20"
    print("OBSERVED prefs sizes full=%s cut=%s, after reboot name=%r tx=%r" % (full, after_cut, name, tx))
