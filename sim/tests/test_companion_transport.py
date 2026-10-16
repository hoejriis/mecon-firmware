"""Transport trust for Companion management commands.

A _usb or _ble Companion build talks to a paired phone or a cable; a _wifi build serves its app
protocol on an unauthenticated TCP port. Management and secret-bearing commands must be refused
on the TCP class with an explicit error frame and still work on USB. Ordinary messaging must work
on both. `companion_wifi` is the same Companion code with the TCP transport class.
"""
import struct

import pytest

from sim import harness
from sim.harness import CompanionClient

RTC = 1760000000
SECRET = bytes(range(16))


def _node(nodes, kind, name, **kw):
    n = nodes(kind, name, rtc=RTC, **kw).start()
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    return n, c


def _is_refusal(f):
    return f[0] == harness.RESP_ERR and f[1] == harness.ERR_NOT_PERMITTED


def test_management_commands_work_on_usb(nodes):
    node, c = _node(nodes, "companion", "usb1", seed=3)
    f = c.request(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"ver")
    assert f[0] == harness.RESP_CLI_REPLY and len(f) > 2, f
    f = c.request(bytes([harness.CMD_EXPORT_PRIVATE_KEY]))
    assert f[0] == harness.RESP_PRIVATE_KEY and len(f) == 65, f
    key = f[1:]
    assert c.request(bytes([harness.CMD_IMPORT_PRIVATE_KEY]) + key)[0] == harness.RESP_OK
    assert c.request(bytes([harness.CMD_SET_DEVICE_PIN]) + struct.pack("<I", 123456))[0] == harness.RESP_OK
    # stock disables the transport before it would answer, so there is no reply: the proof is the reboot
    c._send(bytes([harness.CMD_FACTORY_RESET]) + b"reset")
    assert node.wait_exit(10) == 75        # the board erased itself and rebooted


@pytest.mark.parametrize("cmd,body", [
    (harness.CMD_RUN_CLI_COMMAND, b"ver"),
    (harness.CMD_EXPORT_PRIVATE_KEY, b""),
    (harness.CMD_IMPORT_PRIVATE_KEY, bytes(range(1, 65))),
    (harness.CMD_SET_DEVICE_PIN, struct.pack("<I", 123456)),
    (harness.CMD_FACTORY_RESET, b"reset"),
])
def test_management_commands_refused_on_tcp(nodes, cmd, body):
    node, c = _node(nodes, "companion_wifi", "wifi1", seed=4)
    f = c.request(bytes([cmd]) + body)
    assert _is_refusal(f), "command %d was not refused over TCP: %r" % (cmd, f)
    # nothing happened: the node is still running and still answers an allowed command
    assert node.proc.poll() is None
    assert c.device_query()[0] != harness.RESP_ERR


def test_refused_commands_leave_state_untouched_on_tcp(nodes):
    node, c = _node(nodes, "companion_wifi", "wifi2", seed=5)
    before = c.app_start()
    assert _is_refusal(c.request(bytes([harness.CMD_IMPORT_PRIVATE_KEY]) + bytes(range(1, 65))))
    assert c.app_start()[4:36] == before[4:36]      # identity unchanged


def test_messaging_still_works_over_tcp_class(ether, nodes):
    wa, a = _node(nodes, "companion_wifi", "wa", seed=11, ether_path=ether.path)
    ub, b = _node(nodes, "companion", "ub", seed=22, ether_path=ether.path)
    assert ether.wait_for_nodes(["wa", "ub"])
    ether.set_link("wa", "ub")
    assert a.add_contact(b.pub_key, "bob")[0] == 0
    assert b.add_contact(a.pub_key, "alice")[0] == 0
    for c in (a, b):
        assert c.set_channel(1, "simchan", SECRET)[0] == 0
    assert a.send_direct(b.pub_key, "hello over tcp class", RTC + 10)[0] == harness.RESP_SENT
    assert a.send_channel(1, "chan from tcp class", RTC + 11)[0] == harness.RESP_OK
    got = b.sync_messages(timeout=60, want=2)
    texts = {m["text"] for m in got}
    assert "hello over tcp class" in texts, got
    assert any(t.endswith("chan from tcp class") for t in texts), got
    # and the TCP-class node receives too
    assert b.send_direct(a.pub_key, "reply", RTC + 12)[0] == harness.RESP_SENT
    back = a.sync_messages(timeout=60, want=1)
    assert any(m["text"] == "reply" for m in back), back
