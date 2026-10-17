"""The simulated BLE transport on the standalone Companion (variants/sim/sim_ble.h): the rules that were unit-only before.

The standalone Companion's BLE interface is a loopback listener that carries the Companion frames after a PIN pairing and
classifies itself exactly like the real class (docs/CLI.md: BLE is trusted only with a per-device PIN, never 0 or 123456).
What this verifies end to end, through the real application: the pairing handshake (right PIN, wrong PIN, data before
pairing), the commands a paired phone may and may not run with a per-device PIN and with the published default PIN, the
Hardened rules for BLE (no stack with the default PIN, no key export, no posture lowering, no factory reset), and that
leaving BLE mode or switching Bluetooth off takes the link down.

What it does not verify: the radio, advertising, the real GATT/NimBLE stack and its pairing exchange, encryption of the air
link, a phone's behaviour. Those need a board and a phone.

Setup note: the PIN is set over the USB console and takes effect at the next boot (the display build otherwise draws a
random one each boot). A test reads BLE only after the USB client is closed, because the Companion answers every
interface at once.
"""
import socket
import struct
import time

import pytest

from sim import harness
from sim.harness import BleUnavailable, CompanionClient, PairingRejected, audit_entries, node_log

RTC = 1760000000
PIN = 424242            # a per-device PIN
DEFAULT = 123456        # the published default
SECRET = b"\x11" * 16
pytestmark = pytest.mark.timescale(10)


def usb(node):
    c = CompanionClient(node)
    c.device_query()
    c.app_start()
    return c


def usb_after_ble(node):
    """A USB client attached after a BLE one. The node writes each reply to every interface in turn (BLE first, then the
    USB port), so a USB client that connects in the instant between the two would be handed the BLE client's reply;
    wait for the node to finish writing before attaching."""
    time.sleep(0.5)
    return usb(node)


def boot(nodes, name, pin, posture=None, seed=7, **kw):
    """A standalone Companion that has booted with BLE PIN `pin` (and `posture`), returned with no client attached."""
    n = nodes("companion_standalone", name, seed=seed, rtc=RTC, **kw).start()
    c = usb(n)
    assert c.cli("set pin %d" % pin).startswith("> pin is now")
    if posture:
        assert c.cli("set posture %s" % posture) == "OK - posture %s" % posture
    c.close()
    n.restart()
    return n


def log_count(node, text):
    return node_log(node).count(text)


def refused(f):
    return f[0] == harness.RESP_ERR and f[1] == harness.ERR_NOT_PERMITTED


def eof(sock, timeout=5.0):
    """True when the peer closes the socket within `timeout` (anything it sends first is read and ignored)."""
    sock.settimeout(0.5)
    end = time.time() + timeout
    while time.time() < end:
        try:
            if sock.recv(4096) == b"":
                return True
        except socket.timeout:
            pass
        except ConnectionError:
            return True
    return False


# ------------------------------------------------------------------ pairing

def test_the_right_pin_pairs_and_the_link_carries_frames(nodes):
    n = boot(nodes, "p1", PIN)
    b = n.connect_ble(PIN)
    assert b.device_query()[0] != harness.RESP_ERR
    assert b.app_start()[0] == harness.RESP_SELF_INFO
    assert log_count(n, "SIMBLE paired") == 1
    assert "SIMBLE begin pin=per-device" in node_log(n)
    assert str(PIN) not in node_log(n)                           # the PIN is never logged


def test_a_wrong_pin_is_disconnected_and_nothing_is_carried(nodes):
    n = boot(nodes, "p2", PIN)
    for wrong in (PIN + 1, 0, DEFAULT):
        with pytest.raises(PairingRejected):
            n.connect_ble(wrong)
    assert log_count(n, "SIMBLE pairing rejected") == 3
    assert log_count(n, "SIMBLE paired") == 0
    assert n.proc.poll() is None
    b = n.connect_ble(PIN)                                       # no lockout: the right PIN still pairs
    assert b.app_start()[0] == harness.RESP_SELF_INFO


def test_frames_before_pairing_are_not_accepted(nodes):
    n = boot(nodes, "p3", PIN)
    frame = bytes([harness.CMD_DEVICE_QUERY, 3])
    s = n.connect_ble(PIN, raw=b"<" + struct.pack("<H", len(frame)) + frame)     # a frame, no pairing
    assert eof(s), "the link stayed open for a central that sent data before pairing"
    assert log_count(n, "SIMBLE paired") == 0 and log_count(n, "data before pairing") == 1
    # an app start is not answered either: the only way in is the pairing
    s2 = n.connect_ble(PIN, raw=b"PAIR 1\n")
    assert eof(s2)
    assert n.connect_ble(PIN).app_start()[0] == harness.RESP_SELF_INFO


# ------------------------------------------------------------------ a per-device PIN is a trusted transport

def test_with_a_per_device_pin_the_cli_frame_and_key_export_work(nodes):
    n = boot(nodes, "t1", PIN)
    b = n.connect_ble(PIN)
    b.app_start()
    f = b.request(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"ver")
    assert f[0] == harness.RESP_CLI_REPLY and len(f) > 2, f
    assert b.cli("get mecon.cli.who").startswith("> transport=ble auth=paired-pin remote=0 posture=normal principal=paired")
    key = b.export_private_key()
    assert len(key) == 64
    assert b.cli("set name Blephone").startswith("OK")           # a paired phone may configure
    # the key a paired phone exports is the device's own: the same one the cable exports
    b.close()
    u = usb_after_ble(n)
    assert u.export_private_key() == key


# ------------------------------------------------------------------ the published default PIN is not

def test_with_the_default_pin_management_is_refused_and_messaging_works(ether, nodes):
    a = boot(nodes, "d1", DEFAULT, seed=11, ether_path=ether.path)
    ub = nodes("companion", "d2", seed=22, rtc=RTC, ether_path=ether.path).start()
    b = usb(ub)
    ba = a.connect_ble(DEFAULT)                                  # the default PIN pairs (the app can connect) ...
    ba.device_query()
    ba.app_start()
    assert "SIMBLE begin pin=default" in node_log(a)
    for cmd, body in ((harness.CMD_RUN_CLI_COMMAND, b"ver"),     # ... but is not trusted
                      (harness.CMD_EXPORT_PRIVATE_KEY, b""),
                      (harness.CMD_IMPORT_PRIVATE_KEY, bytes(range(1, 65))),
                      (harness.CMD_SET_DEVICE_PIN, struct.pack("<I", PIN)),
                      (harness.CMD_FACTORY_RESET, b"reset")):
        f = ba.request(bytes([cmd]) + body)
        assert refused(f), "command %d was not refused over a default-PIN BLE link: %r" % (cmd, f)
    assert a.proc.poll() is None
    assert ba.app_start()[4:36] == ba.pub_key                    # identity untouched, node still answering
    # messaging works over the same link
    assert ether.wait_for_nodes(["d1", "d2"])
    ether.set_link("d1", "d2")
    assert ba.add_contact(b.pub_key, "bob")[0] == 0
    assert b.add_contact(ba.pub_key, "alice")[0] == 0
    for c in (ba, b):
        assert c.set_channel(1, "simchan", SECRET)[0] == 0
    assert ba.send_direct(b.pub_key, "hello over ble", RTC + 10)[0] == harness.RESP_SENT
    assert ba.send_channel(1, "chan from ble", RTC + 11)[0] == harness.RESP_OK
    got = b.sync_messages(timeout=60, want=2)
    texts = {m["text"] for m in got}
    assert "hello over ble" in texts and any(t.endswith("chan from ble") for t in texts), got
    assert b.send_direct(ba.pub_key, "reply", RTC + 12)[0] == harness.RESP_SENT
    assert any(m["text"] == "reply" for m in ba.sync_messages(timeout=60, want=1))
    ba.close()
    u = usb_after_ble(a)
    ev = audit_entries(u.cli)
    for what in ("cli", "key-export", "key-import", "set-pin", "factory-reset"):   # audited, as an untrusted transport ("net")
        assert any(e.endswith("refused net %s x1" % what) for e in ev), (what, ev)


# ------------------------------------------------------------------ Hardened

def test_hardened_with_the_default_pin_does_not_start_ble(nodes):
    n = boot(nodes, "h1", DEFAULT, posture="hardened")
    u = usb(n)
    assert u.cli("get posture") == "> hardened"
    assert not n.ble_listening()
    with pytest.raises(BleUnavailable):
        n.connect_ble(DEFAULT, timeout=0.5)
    assert log_count(n, "SIMBLE begin") == 1                     # only the boot before the posture was raised
    ev = audit_entries(u.cli)
    assert any(e.endswith("refused ble ble-start x1") for e in ev), ev


def test_hardened_with_a_per_device_pin_starts_ble_but_keeps_the_rules(nodes):
    n = boot(nodes, "h2", PIN, posture="hardened")
    b = n.connect_ble(PIN)                                       # the stack starts: a per-device PIN is allowed in Hardened
    b.app_start()
    assert b.cli("get posture") == "> hardened"
    assert refused(b.request(bytes([harness.CMD_EXPORT_PRIVATE_KEY])))                  # no key export
    assert refused(b.request(bytes([harness.CMD_IMPORT_PRIVATE_KEY]) + bytes(range(1, 65))))
    assert refused(b.request(bytes([harness.CMD_FACTORY_RESET]) + b"reset"))            # only the cable resets a Hardened device
    low = b.request(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"set posture normal confirm")
    assert b"OK" not in low, low                                  # nor lowers the posture
    assert n.proc.poll() is None
    b.close()
    u = usb_after_ble(n)
    assert u.cli("get posture") == "> hardened"                   # still Hardened after all of it


def test_a_per_device_pin_factory_reset_is_allowed_in_normal(nodes):
    """The positive control for the Hardened refusal above: the same frame over the same link resets a Normal device."""
    n = boot(nodes, "h3", PIN)
    b = n.connect_ble(PIN)
    b.app_start()
    b._send(bytes([harness.CMD_FACTORY_RESET]) + b"reset")
    assert n.wait_exit(10) == 75


# ------------------------------------------------------------------ leaving BLE

def test_switching_to_wifi_mode_takes_ble_down_and_a_reboot_does_not_bring_it_back(nodes):
    n = boot(nodes, "m1", PIN)
    begins = log_count(n, "SIMBLE begin")
    b = n.connect_ble(PIN)
    b.app_start()
    assert b.cli("set connectivity.mode wifi").startswith("OK")  # the paired phone switches the mode
    assert eof(b.sock, 20), "the BLE link survived the switch"
    assert n.wait_exit(10) == 75
    n.start()
    u = usb(n)
    assert u.cli("get connectivity.mode") == "> wifi"
    assert not n.ble_listening()
    with pytest.raises(BleUnavailable):
        n.connect_ble(PIN, timeout=0.5)
    assert log_count(n, "SIMBLE begin") == begins                # the stack was not brought up in Wi-Fi mode


def test_the_bluetooth_page_switches_the_link_off_and_on(nodes):
    n = boot(nodes, "m2", PIN)
    assert n.ui.wait_text("Unread Msg", 20), n.ui.last_text()
    time.sleep(1.0)                                              # past the first 8 s of the UI (a long press there is the rescue console)
    b = n.connect_ble(PIN)
    b.app_start()
    for _ in range(8):                                           # to the Bluetooth page
        if "toggle:" in n.ui.last_text():
            break
        before = n.ui.last_text()
        n.ui.press("click")
        end = time.time() + 15
        while time.time() < end and n.ui.last_text() == before:
            time.sleep(0.05)
    assert "toggle:" in n.ui.last_text(), n.ui.last_text()
    n.ui.press("long")                                           # Bluetooth off
    assert eof(b.sock, 20), "switching Bluetooth off left the link up"
    with pytest.raises(PairingRejected):
        n.connect_ble(PIN, timeout=1.0)                          # nothing answers a pairing: the listener drops the central
    n.ui.press("long")                                           # and on again
    end = time.time() + 20
    while time.time() < end:
        try:
            n.connect_ble(PIN, timeout=1.0).app_start()
            break
        except (PairingRejected, ConnectionError, AssertionError):
            time.sleep(0.3)
    else:
        raise AssertionError("Bluetooth did not come back")
