"""Power cuts during a save of a Companion blob and of the client ACL (Repeater, Room server).

The previous complete file must survive a cut at any byte of the temp file and between write and rename:
a blob byte-identical, an ACL that still recognises the logged-in client. The SimFS refuses rename-over like
ESP32 SPIFFS, so the two-step swap and its boot recovery are held to the same standard as the other saves.
"""
import os
import struct
import subprocess
import time

import pytest

from sim.harness import CliClient, CompanionClient

RTC = 1760000000
PW = "s3cretpw"
CMD_EXPORT_CONTACT, CMD_IMPORT_CONTACT, RESP_EXPORT_CONTACT = 17, 18, 11


def _p(node, path):
    return os.path.join(node.dir, path.lstrip("/"))


def _size(node, path):
    # One stat, not exists() then getsize(): boot recovery may rename or remove the file between the two calls.
    try:
        return os.path.getsize(_p(node, path))
    except FileNotFoundError:
        return None


def _exit_code(node, timeout):
    try:
        return node.wait_exit(timeout)
    except subprocess.TimeoutExpired:
        return None


def _wait(pred, timeout=10.0):
    end = time.time() + timeout
    while time.time() < end:
        if pred():
            return True
        time.sleep(0.05)
    return False


# ------------------------------------------------------------------ client ACL
def _acl_setup(ether, nodes, kind):
    rep = nodes(kind, "rp", seed=41, rtc=RTC, ether_path=ether.path).start()
    adm = nodes("companion", "ad", seed=42, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes(["rp", "ad"])
    ether.set_link("rp", "ad")
    # the server's boot advert first: both nodes booted together hold their first packet to the same 30 s bin edge
    assert ether.wait_tx("rp") and ether.wait_quiet(), "the server never advertised"
    cli = CliClient(rep)
    pub = bytes.fromhex(cli.command("get public.key").lstrip("> ").strip())
    assert cli.command("password " + PW).startswith("password now")
    c = CompanionClient(adm)
    c.device_query(); c.app_start()
    assert c.add_contact(pub, "srv", ctype=2 if kind == "repeater" else 3)[0] == 0
    assert c.login(pub, PW)
    assert _wait(lambda: _size(rep, "/s_contacts") == 136), "the login was never saved to the ACL file"
    return rep, cli, c, pub


def _cut_acl_save(rep, cli_cls, fault=None, fault_before=None):
    """Restart the node with a fault armed, trigger a second ACL save and expect the cut."""
    rep.stop()
    rep.fault, rep.fault_before = fault, fault_before
    rep.start()
    cli = cli_cls(rep)
    cli.sock.sendall(b"setperm " + b"ab" * 32 + b" 3\r")      # a second admin: marks the ACL dirty, saved a moment later
    assert _exit_code(rep, 15) == 99, "the ACL save never reached the injected fault (not written via the temp file?)"
    rep.fault = rep.fault_before = None
    rep.start()
    return CliClient(rep)


@pytest.mark.lockstep()
@pytest.mark.parametrize("kind", ["repeater", "room_server"])
@pytest.mark.parametrize("cut", [0, 1, 40, 135])
def test_acl_cut_in_temp_file_keeps_logged_in_client(ether, nodes, kind, cut):
    rep, cli, c, pub = _acl_setup(ether, nodes, kind)
    _cut_acl_save(rep, CliClient, fault="/s_contacts.tmp:%d" % cut)
    assert ether.wait_for_nodes(["rp", "ad"])
    assert _size(rep, "/s_contacts") == 136, "the live ACL file was touched"
    assert c.login(pub, "", timeout=30), "the logged-in client is no longer in the ACL after the cut"


@pytest.mark.timescale(10)
@pytest.mark.parametrize("kind", ["repeater", "room_server"])
@pytest.mark.parametrize("op,size_after", [
    ("rename:/s_contacts.tmp", 136),    # temp complete, nothing swapped yet
    ("remove:/s_contacts", 136),        # verified new copy exists, old not yet removed: old stays
    ("rename:/s_contacts.new", 272),    # old removed: boot must finish the swap, not lose the list
])
@pytest.mark.lockstep()
def test_acl_cut_between_write_and_rename(ether, nodes, kind, op, size_after):
    rep, cli, c, pub = _acl_setup(ether, nodes, kind)
    _cut_acl_save(rep, CliClient, fault_before=op)
    assert ether.wait_for_nodes(["rp", "ad"])

    def recovered():
        # Where the size after recovery equals the size before (136), a size check alone is already true before boot
        # recovery has run, and the leftover check then raced it (issue #48). Wait for the whole recovered state.
        leftovers_ok = all(_size(rep, f) is None or (f == "/s_contacts.new" and size_after == 272)
                           for f in ("/s_contacts.tmp", "/s_contacts.new"))
        return _size(rep, "/s_contacts") == size_after and leftovers_ok

    assert _wait(recovered), "after the cut: /s_contacts=%s .tmp=%s .new=%s" % (
        _size(rep, "/s_contacts"), _size(rep, "/s_contacts.tmp"), _size(rep, "/s_contacts.new"))
    assert c.login(pub, "", timeout=30)


@pytest.mark.lockstep()
def test_acl_is_never_written_in_place(ether, nodes):
    rep, cli, c, pub = _acl_setup(ether, nodes, "repeater")
    rep.stop()
    rep.fault = "/s_contacts:40"                       # armed on the live file itself: must never fire
    rep.start()
    cli = CliClient(rep)
    assert cli.command("setperm " + "ab" * 32 + " 3") == "OK"
    _wait(lambda: rep.proc.poll() is not None or _size(rep, "/s_contacts") == 272)
    assert rep.proc.poll() is None, "the node was cut writing the live ACL file in place (file left at %s bytes)" % _size(rep, "/s_contacts")
    assert _size(rep, "/s_contacts") == 272, "the second client was never saved"
    for leftover in ("/s_contacts.tmp", "/s_contacts.new"):
        assert _size(rep, leftover) is None, leftover


# ------------------------------------------------------------------ companion blob
def _advert(node, rtc_now):
    """A signed advert of this Companion at the given time, as the export command returns it."""
    c = CompanionClient(node)
    c.device_query(); c.app_start()
    assert c.request(bytes([6]) + struct.pack("<I", rtc_now))[0] == 0     # CMD_SET_DEVICE_TIME
    f = c.request(bytes([CMD_EXPORT_CONTACT]))
    assert f[0] == RESP_EXPORT_CONTACT, f
    return c.pub_key, f[1:]


def _blob_of(c, pub):
    f = c.request(bytes([CMD_EXPORT_CONTACT]) + pub)
    return f[1:] if f[0] == RESP_EXPORT_CONTACT else None


def _blob_setup(nodes, name):
    src = nodes("companion", name + "src", seed=77, rtc=RTC).start()
    pub, adv1 = _advert(src, RTC)
    _, adv2 = _advert(src, RTC + 600)
    assert adv1 != adv2
    n = nodes("companion", name, seed=5, rtc=RTC + 1000).start()
    c = CompanionClient(n); c.device_query(); c.app_start()
    assert c.add_contact(pub, "peer")[0] == 0
    assert c.request(bytes([CMD_IMPORT_CONTACT]) + adv1)[0] == 0
    assert _wait(lambda: _blob_of(c, pub) == adv1), "the first advert was not stored as a blob"
    c.request_reboot()                                   # saves the pending contact: the blob is only reachable through it
    assert _exit_code(n, 10) == 75
    n.start()
    c = CompanionClient(n); c.device_query(); c.app_start()
    assert _blob_of(c, pub) == adv1, "the first blob did not survive an orderly restart"
    return n, c, pub, adv1, adv2


def _blob_file(n):
    names = [x for x in os.listdir(_p(n, "/bl")) if not x.endswith((".tmp", ".new"))]
    assert len(names) == 1, names
    return "/bl/" + names[0]


def _import_under_fault(n, adv2, fault=None, fault_before=None):
    n.stop()
    n.fault, n.fault_before = fault, fault_before
    n.start()
    c = CompanionClient(n); c.device_query(); c.app_start()
    c._send(bytes([CMD_IMPORT_CONTACT]) + adv2)
    assert _exit_code(n, 10) == 99, "the blob save never reached the injected fault (not written via the temp file?)"
    n.fault = n.fault_before = None
    n.start()
    c = CompanionClient(n); c.device_query(); c.app_start()
    return c


@pytest.mark.parametrize("cut", [0, 1, 40, 100, "tail"])
def test_blob_cut_in_temp_file_keeps_previous_blob(nodes, cut):
    n, c, pub, adv1, adv2 = _blob_setup(nodes, "bc%s" % cut)
    path = _blob_file(n)
    live = open(_p(n, path), "rb").read()
    k = len(adv2) - 3 if cut == "tail" else cut
    c = _import_under_fault(n, adv2, fault="%s.tmp:%d" % (path, k))
    assert open(_p(n, path), "rb").read() == live       # the live file was never touched
    assert _blob_of(c, pub) == adv1                      # byte-identical


@pytest.mark.parametrize("op,expect_new", [
    ("rename:%s.tmp", False),
    ("remove:%s", False),
    ("rename:%s.new", True),
])
def test_blob_cut_between_write_and_rename(nodes, op, expect_new):
    n, c, pub, adv1, adv2 = _blob_setup(nodes, "bo%d" % len(op))
    path = _blob_file(n)
    c = _import_under_fault(n, adv2, fault_before=op % path)
    got = _blob_of(c, pub)
    assert got == (adv2 if expect_new else adv1)         # a whole blob either way, never a truncated or missing one


def test_blob_is_never_written_in_place(nodes):
    n, c, pub, adv1, adv2 = _blob_setup(nodes, "bin")
    path = _blob_file(n)
    n.stop()
    n.fault = "%s:40" % path                             # armed on the live file itself: must never fire
    n.start()
    c = CompanionClient(n); c.device_query(); c.app_start()
    assert c.request(bytes([CMD_IMPORT_CONTACT]) + adv2)[0] == 0
    assert _wait(lambda: _blob_of(c, pub) == adv2)
    assert n.proc.poll() is None
    assert _size(n, path + ".tmp") is None and _size(n, path + ".new") is None
