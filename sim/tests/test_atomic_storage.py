"""Power cuts during a save: the previous complete file must survive (prefs, identity, channels).

Cuts are injected two ways by the SimFS fault hook: after N bytes of a named file, and just before
a named filesystem operation (rename/remove) - so a cut can land at any byte of the temporary file
and between the write and the swap. The SimFS refuses rename-over like ESP32 SPIFFS does.
"""
import os

import pytest

from sim.harness import CliClient, CompanionClient

RTC = 1760000000
S1, S2 = bytes(range(16)), bytes(range(100, 116))


def _p(node, path):
    return os.path.join(node.dir, path.lstrip("/"))


def _size(node, path):
    return os.path.getsize(_p(node, path)) if os.path.exists(_p(node, path)) else None


# ------------------------------------------------------------------ repeater prefs
def prefs_after_cut(nodes, name, fault=None, fault_before=None, expect_cut=True):
    rep = nodes("repeater", name, seed=5).start()
    cli = CliClient(rep)
    assert cli.command("set name Before") == "OK"
    assert cli.command("set tx 7") == "OK"
    cli.close()
    rep.stop()
    full = _size(rep, "/prefs.json")
    rep.fault, rep.fault_before = fault, fault_before
    rep.start()
    cli = CliClient(rep)
    if expect_cut:
        cli.sock.sendall(b"set name After\r")
        assert rep.wait_exit(10) == 99, "the save never reached the injected fault (not written via the temp file?)"
    else:
        assert cli.command("set name After") == "OK"      # the armed in-place cut never fires
    rep.fault = rep.fault_before = None
    if not expect_cut:
        rep.stop()
    rep.start()
    cli = CliClient(rep)
    out = (cli.command("get name"), cli.command("get tx"))
    cli.close()
    return full, out, rep


@pytest.mark.parametrize("cut", [0, 1, 40, 200, "tail"])
def test_prefs_cut_in_temp_file_keeps_previous_prefs(nodes, cut):
    probe = nodes("repeater", "probe", seed=5).start()
    c = CliClient(probe); c.command("set name Before"); c.command("set tx 7"); c.close(); probe.stop()
    full = _size(probe, "/prefs.json")
    n = full - 5 if cut == "tail" else cut
    _, (name, tx), rep = prefs_after_cut(nodes, "cut%s" % cut, fault="/prefs.json.tmp:%d" % n)
    assert (name, tx) == ("> Before", "> 7")          # previous complete prefs, nothing reverted to a default
    assert _size(rep, "/prefs.json") == full           # the live file was never touched


@pytest.mark.parametrize("op,expect", [
    ("rename:/prefs.json.tmp", "> Before"),    # tmp complete, nothing swapped yet
    ("remove:/prefs.json", "> Before"),        # verified new copy exists, old not yet removed: old stays
    ("rename:/prefs.json.new", "> After"),     # old removed: boot must finish the swap, not lose prefs
])
def test_prefs_cut_between_write_and_rename(nodes, op, expect):
    _, (name, tx), rep = prefs_after_cut(nodes, "op" + op.split(":")[0] + str(abs(hash(op)) % 1000), fault_before=op)
    assert (name, tx) == (expect, "> 7")


def test_prefs_are_never_written_in_place(nodes):
    # a cut armed on the live file itself must never fire: saves go to a temp file and are renamed in
    full, (name, tx), rep = prefs_after_cut(nodes, "inplace", fault="/prefs.json:40", expect_cut=False)
    assert (name, tx) == ("> After", "> 7")
    for leftover in ("/prefs.json.tmp", "/prefs.json.new"):
        assert _size(rep, leftover) is None, leftover


# ------------------------------------------------------------------ companion identity
def _companion(nodes, name, seed, **kw):
    n = nodes("companion", name, seed=seed, rtc=RTC, **kw).start()
    c = CompanionClient(n)
    c.device_query()
    return n, c, c.app_start()[4:36]


def _new_key(nodes):
    n, c, pub = _companion(nodes, "keysrc", 99)
    return c.export_private_key(), pub


@pytest.mark.parametrize("cut", [0, 1, 40, 95])
def test_identity_cut_in_temp_file_keeps_identity(nodes, cut):
    key, _ = _new_key(nodes)
    n, c, pub0 = _companion(nodes, "idn%d" % cut, 7)
    n.stop()
    n.fault = "/identity/_main.id.tmp:%d" % cut
    n.start()
    c = CompanionClient(n)
    c.device_query(); c.app_start()
    c._send(bytes([24]) + key)
    assert n.wait_exit(10) == 99
    n.fault = None
    n.start()
    c = CompanionClient(n); c.device_query()
    assert c.app_start()[4:36] == pub0                 # still the old, complete identity
    assert _size(n, "/identity/_main.id") == 96


@pytest.mark.parametrize("op,expect_new", [
    ("rename:/identity/_main.id.tmp", False),
    ("remove:/identity/_main.id", False),
    ("rename:/identity/_main.id.new", True),
])
def test_identity_cut_between_write_and_rename(nodes, op, expect_new):
    key, newpub = _new_key(nodes)
    n, c, pub0 = _companion(nodes, "idop%d" % len(op), 7)
    n.stop()
    n.fault_before = op
    n.start()
    c = CompanionClient(n); c.device_query(); c.app_start()
    c._send(bytes([24]) + key)
    assert n.wait_exit(10) == 99
    n.fault_before = None
    n.start()
    c = CompanionClient(n); c.device_query()
    now = c.app_start()[4:36]
    assert now == (newpub if expect_new else pub0)      # a whole identity either way, never a fresh random one
    assert now != b"\0" * 32


def test_damaged_identity_is_kept_aside_not_overwritten(nodes):
    n, c, pub0 = _companion(nodes, "dmg", 7)
    n.stop()
    path = _p(n, "/identity/_main.id")
    data = open(path, "rb").read()
    open(path, "wb").write(data[:50])                   # damage it (e.g. an old non-atomic write cut short)
    n.seed = 8                                          # (a fresh identity comes from the node's RNG seed)
    n.start()
    c = CompanionClient(n); c.device_query()
    pub1 = c.app_start()[4:36]
    assert pub1 != pub0                                 # the node cannot use it and starts fresh ...
    kept = _p(n, "/identity/_main.id.damaged")
    assert os.path.exists(kept) and open(kept, "rb").read() == data[:50]    # ... but the damaged file is kept


# ------------------------------------------------------------------ companion channel table
@pytest.mark.parametrize("cut", [0, 1, 70, 100])
def test_channels_cut_in_temp_file_keeps_previous_table(nodes, cut):
    n, c, _ = _companion(nodes, "ch%d" % cut, 8)
    assert c.set_channel(0, "alpha", S1)[0] == 0
    n.stop()
    n.fault = "/channels2.tmp:%d" % cut
    n.start()
    c = CompanionClient(n); c.device_query(); c.app_start()
    c._send(bytes([32, 1]) + b"beta".ljust(32, b"\0") + S2)
    assert n.wait_exit(10) == 99
    n.fault = None
    n.start()
    c = CompanionClient(n); c.device_query(); c.app_start()
    assert c.get_channel(0)[0] == "alpha" and c.get_channel(0)[1] == S1
    assert c.get_channel(1) is None


@pytest.mark.parametrize("op,both", [
    ("rename:/channels2.tmp", False),
    ("remove:/channels2", False),
    ("rename:/channels2.new", True),
])
def test_channels_cut_between_write_and_rename(nodes, op, both):
    n, c, _ = _companion(nodes, "chop%d" % len(op), 8)
    assert c.set_channel(0, "alpha", S1)[0] == 0
    n.stop()
    n.fault_before = op
    n.start()
    c = CompanionClient(n); c.device_query(); c.app_start()
    c._send(bytes([32, 1]) + b"beta".ljust(32, b"\0") + S2)
    assert n.wait_exit(10) == 99
    n.fault_before = None
    n.start()
    c = CompanionClient(n); c.device_query(); c.app_start()
    assert c.get_channel(0)[0] == "alpha"
    assert (c.get_channel(1) is not None) == both
