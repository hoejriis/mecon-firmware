"""Standalone security postures on the Companion (docs/SECURITY_MODEL.md).

Hardened: the private key is neither exported nor imported on any transport (USB included), the remote
CLI is off, the TCP companion server is not started, and only a cable factory-resets the device.
Management over USB keeps working in both postures. Refusals are explicit error frames or replies, and
audit entries. BLE needs a radio stack the simulator does not have; its rule is in test/test_posture.
"""
import os
import time

import pytest

from sim import harness
from sim.harness import CompanionClient, audit_entries, audit_file

RTC = 1760000000
REMOTE_CLI_FLAG = 0x10


def _node(nodes, kind="companion", name="co", seed=61, **kw):
    n = nodes(kind, name, seed=seed, rtc=RTC, **kw).start()
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    return n, c


def _audit(c):
    return audit_entries(c.cli)


def _refused(f):
    return f[0] == harness.RESP_ERR and f[1] == harness.ERR_NOT_PERMITTED


def _wait_audit_file(node, pred, timeout=8.0):
    end = time.time() + timeout
    while time.time() < end:
        entries = audit_file(node)
        if entries is not None and pred(entries):
            return entries
        time.sleep(0.1)
    return audit_file(node)


def test_hardened_refuses_key_export_and_import_on_usb(nodes):
    n, c = _node(nodes)
    assert c.cli("get posture") == "> normal"
    key = c.export_private_key()                                  # Normal: works
    assert c.request(bytes([harness.CMD_IMPORT_PRIVATE_KEY]) + key)[0] == harness.RESP_OK
    assert c.cli("set posture hardened") == "OK - posture hardened"
    assert _refused(c.request(bytes([harness.CMD_EXPORT_PRIVATE_KEY])))
    assert _refused(c.request(bytes([harness.CMD_IMPORT_PRIVATE_KEY]) + bytes(range(1, 65))))
    assert c.app_start()[4:36] == c.pub_key                       # identity untouched
    assert c.cli("get name").startswith("> ")                     # management over USB keeps working
    log = _audit(c)
    assert any("refused cable key-export" in ln for ln in log), log
    assert any("refused cable key-import" in ln for ln in log), log
    assert c.cli("set posture normal confirm") == "OK - posture normal"
    assert c.export_private_key() == key                          # and it comes back from the cable


def _pair(ether, nodes, prefix):
    victim = nodes("companion_wifi_usb", prefix + "v", seed=31, rtc=RTC, ether_path=ether.path).start()
    remote = nodes("companion", prefix + "r", seed=32, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes([prefix + "v", prefix + "r"])
    ether.set_link(prefix + "v", prefix + "r")
    v, r = CompanionClient(victim), CompanionClient(remote)
    for c in (v, r):
        c.device_query()
        c.app_start()
    assert v.add_contact(r.pub_key, "remote", flags=REMOTE_CLI_FLAG)[0] == 0
    assert r.add_contact(v.pub_key, "victim")[0] == 0
    return victim, v, r


def _remote(r, v, text):
    assert r.send_cli_command(v.pub_key, text)[0] == 6
    msgs = r.sync_messages(timeout=60, want=1)
    assert msgs, "no reply to remote command %r" % text
    return msgs[0]["text"]


def test_hardened_turns_the_remote_cli_off(ether, nodes):
    victim, v, r = _pair(ether, nodes, "a")
    assert "v1." in _remote(r, v, "ver")                          # Normal: reads work for a flagged contact
    assert v.cli("set posture hardened") == "OK - posture hardened"
    reply = _remote(r, v, "ver")                                  # Hardened: not even a read
    assert "hardened" in reply and "v1." not in reply, reply
    assert v.cli("get name").startswith("> ")                     # USB unaffected
    log = _audit(v)
    assert any(ln.endswith("refused remote-cli remote-cli x1") for ln in log), log
    assert v.cli("set posture normal confirm") == "OK - posture normal"
    assert "v1." in _remote(r, v, "ver")                          # positive control: back in Normal


def test_remote_cli_cannot_change_the_posture_and_it_is_audited(ether, nodes):
    victim, v, r = _pair(ether, nodes, "b")
    assert "not permitted" in _remote(r, v, "set posture hardened")      # Normal: not even raising
    assert v.cli("get posture") == "> normal"
    assert "not permitted" in _remote(r, v, "get audit")
    assert _remote(r, v, "get posture") == "> normal"                    # a read is fine
    log = _audit(v)
    assert any(ln.endswith("refused remote-cli posture x1") for ln in log), log
    assert any(ln.endswith("refused remote-cli audit x1") for ln in log), log
    assert v.cli("set posture hardened") == "OK - posture hardened"
    assert v.cli("set posture normal") != "OK - posture normal"          # needs the confirm token
    assert v.cli("get posture") == "> hardened"


def test_hardened_wifi_companion_does_not_serve_the_tcp_port(nodes):
    n, c = _node(nodes, "companion_wifi", "w1", seed=62)         # Normal: the TCP port answers (positive control)
    assert c.device_query()[0] != harness.RESP_ERR
    h = nodes("companion_wifi", "w2", seed=63, rtc=RTC)
    with open(os.path.join(h.dir, "prefs.json"), "w") as f:
        f.write("{posture:1}")                                    # a device that was hardened before this boot (the prefs format has unquoted keys)
    h.start()
    hc = CompanionClient(h)
    with pytest.raises(TimeoutError):
        hc.request(bytes([harness.CMD_DEVICE_QUERY, 3]), timeout=3)
    entries = _wait_audit_file(h, lambda e: len(e) > 0)
    assert entries and (entries[0][1], entries[0][2], entries[0][3]) == (2, 3, 13), entries    # refused / net / net-start


def test_untrusted_transport_refusals_are_audited(nodes):
    n, c = _node(nodes, "companion_wifi", "w3", seed=64)
    assert _refused(c.request(bytes([harness.CMD_EXPORT_PRIVATE_KEY])))
    entries = _wait_audit_file(n, lambda e: len(e) > 0)
    assert entries and (entries[0][1], entries[0][2], entries[0][3]) == (2, 3, 2), entries      # refused / net / key-export


def test_usb_factory_reset_recovers_a_hardened_companion(nodes):
    n, c = _node(nodes, name="fr", seed=65)
    assert c.cli("set posture hardened") == "OK - posture hardened"
    assert c.cli("set name Gone") == "OK"
    c._send(bytes([harness.CMD_FACTORY_RESET]) + b"reset")
    assert n.wait_exit(10) == 75                                  # refused nowhere: it rebooted
    n.start()
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    assert c.cli("get posture") == "> normal"
    log = _audit(c)
    assert any("reset cable - x1" in ln for ln in log), log
    assert any("posture cable hardened" in ln for ln in log), log


def test_hardened_survives_a_reboot(nodes):
    n, c = _node(nodes, name="pers", seed=66)
    assert c.cli("set posture hardened") == "OK - posture hardened"
    n.restart()
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    assert c.cli("get posture") == "> hardened"
    assert any("posture cable hardened" in ln for ln in _audit(c))


@pytest.mark.parametrize("cut", [1, 40])
def test_a_power_cut_while_lowering_leaves_a_companion_hardened(nodes, cut):
    n, c = _node(nodes, name="cut%d" % cut, seed=67)
    assert c.cli("set posture hardened") == "OK - posture hardened"
    n.stop()
    n.fault = "/prefs.json.tmp:%d" % cut
    n.start()
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    c._send(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"set posture normal confirm")
    assert n.wait_exit(10) == 99
    n.fault = None
    n.start()
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    assert c.cli("get posture") == "> hardened"
