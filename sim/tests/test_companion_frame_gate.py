"""The native Companion frames that manage the device are refused to an untrusted caller (docs/STANDALONE_POSTURES.md).

In Normal posture the TCP companion server and BLE paired with a default PIN serve messaging only. Every frame that
changes the device's settings, transmits raw packets, signs with the node key, reboots the device, or administers
another node under this node's identity is refused there with ERR_CODE_NOT_PERMITTED and audited. A read those callers
may make does not carry the device's secrets: the BLE PIN in the device info and a channel's secret read as zeros.
The cable is the positive control: the same frames are not refused there.

`companion_wifi` is the Companion with the TCP transport class; `companion_standalone` has the simulated BLE transport.
"""
import struct
import time

import pytest

from sim import harness
from sim.harness import CompanionClient, audit_entries, audit_file

RTC = 1760000000
SECRET = bytes(range(1, 17))
DEFAULT_PIN = 123456
ADV_TYPE_CHAT, ADV_TYPE_REPEATER, ADV_TYPE_ROOM = 1, 2, 3
W_APP_FRAME = 18
TXT_PLAIN, TXT_CLI_DATA = 0, 1
PEER = bytes(range(32, 64))          # a contact's public key; nothing needs to answer for a gate test

# Every gated management frame, with a body long enough to reach the handler on a trusted transport.
MANAGEMENT = [
    ("set-advert-name", 8, b"renamed"),
    ("set-radio-params", 11, struct.pack("<II", 869525, 250000) + bytes([11, 5])),
    ("set-tx-power", 12, bytes([10])),
    ("set-latlon", 14, struct.pack("<ii", 1, 1)),
    ("reboot", 19, b"reboot"),
    ("set-tuning", 21, struct.pack("<II", 0, 0)),
    ("send-raw-data", 25, bytes([0]) + b"x"),
    ("set-channel", 32, bytes([1]) + b"gated".ljust(32, b"\0") + SECRET),
    ("sign-start", 33, b""),
    ("sign-data", 34, b"data"),
    ("sign-finish", 35, b""),
    ("set-other-params", 38, bytes([0, 0, 0, 0])),
    ("set-custom-var", 41, b"gps:0"),
    ("send-binary-req", 50, PEER + bytes([1])),
    ("set-flood-default", 63, b""),
    ("send-control-data", 55, bytes([0x80, 0])),
    ("set-autoadd", 58, bytes([1])),
    ("set-path-hash-mode", 61, bytes([0, 0])),
    ("send-raw-packet", 65, bytes([0, 0, 0])),
    ("send-cli-text", 2, bytes([TXT_CLI_DATA, 0]) + struct.pack("<I", 0) + PEER[:6] + b"ver"),
]


def _client(node, sock=None):
    c = CompanionClient(node, sock=sock)
    c.device_query()
    c.app_start()
    return c


def _refused(f):
    return f is not None and f[0] == harness.RESP_ERR and f[1] == harness.ERR_NOT_PERMITTED


def _ble_default(nodes, name, seed):
    """A standalone Companion booted with the published default PIN, and a client paired over BLE with it."""
    n = nodes("companion_standalone", name, seed=seed, rtc=RTC).start()
    u = _client(n)
    assert u.cli("set pin %d" % DEFAULT_PIN).startswith("> pin is now")
    u.close()
    n.restart()
    return n, _client(n, sock=n.connect_ble(DEFAULT_PIN).sock)


@pytest.mark.parametrize("name,cmd,body", MANAGEMENT, ids=[m[0] for m in MANAGEMENT])
def test_management_frame_refused_over_tcp(nodes, name, cmd, body):
    n = nodes("companion_wifi", "g" + str(cmd), seed=40 + cmd, rtc=RTC).start()
    c = _client(n)
    c.add_contact(PEER, "peer", ctype=ADV_TYPE_CHAT)
    before = c.app_start()
    f = c.request(bytes([cmd]) + body)
    assert _refused(f), "%s (%d) was not refused over TCP: %r" % (name, cmd, f)
    assert n.proc.poll() is None                                   # it did not reboot
    assert c.app_start() == before                                 # name, radio, position and identity unchanged
    assert W_APP_FRAME in _refusal_whats(n), audit_file(n)


def _refusal_whats(node, timeout=8.0):
    """The `what` codes of the refusals in the node's audit file. A TCP-only build has no cable to read the ring with, so
    this waits for the file a refusal flushes."""
    end = time.time() + timeout
    while time.time() < end:
        entries = audit_file(node)
        if entries:
            return [e[3] for e in entries if e[1] == 2]             # event 2: refused
        time.sleep(0.1)
    return []


@pytest.mark.timescale(10)
def test_management_frames_refused_over_default_pin_ble(nodes):
    n, b = _ble_default(nodes, "gb", seed=90)
    b.add_contact(PEER, "peer", ctype=ADV_TYPE_CHAT)
    for name, cmd, body in MANAGEMENT:
        if cmd == 19:
            continue                                                # checked last: a slip would end the test
        f = b.request(bytes([cmd]) + body)
        assert _refused(f), "%s (%d) was not refused over default-PIN BLE: %r" % (name, cmd, f)
    assert _refused(b.request(bytes([19]) + b"reboot"))
    assert n.proc.poll() is None
    b.close()
    u = _client(n)                                                  # the cable reads the audit
    whats = [e for e in audit_entries(u.cli) if "app-frame" in e]
    assert whats, audit_entries(u.cli)


@pytest.mark.parametrize("name,cmd,body", [m for m in MANAGEMENT if m[1] != 19], ids=[m[0] for m in MANAGEMENT if m[1] != 19])
def test_management_frame_not_refused_over_the_cable(nodes, name, cmd, body):
    n = nodes("companion", "u" + str(cmd), seed=140 + cmd, rtc=RTC).start()
    c = _client(n)
    c.add_contact(PEER, "peer", ctype=ADV_TYPE_CHAT)
    f = c.request(bytes([cmd]) + body)
    assert not _refused(f), "%s (%d) was refused over the cable: %r" % (name, cmd, f)


def test_reboot_frame_works_over_the_cable(nodes):
    n = nodes("companion", "ur", seed=170, rtc=RTC).start()
    c = _client(n)
    c._send(bytes([19]) + b"reboot")
    assert n.wait_exit(10) == 75


def test_text_to_a_repeater_is_remote_administration(nodes):
    """A repeater runs any text from an admin as a command line, so a plain text to one is gated; to a chat or a room
    server a plain text is a message and is not."""
    n = nodes("companion_wifi", "gt", seed=60, rtc=RTC).start()
    c = _client(n)
    rep, chat, room = bytes([1]) * 32, bytes([2]) * 32, bytes([3]) * 32
    assert c.add_contact(rep, "rep", ctype=ADV_TYPE_REPEATER)[0] == 0
    assert c.add_contact(chat, "chat", ctype=ADV_TYPE_CHAT)[0] == 0
    assert c.add_contact(room, "room", ctype=ADV_TYPE_ROOM)[0] == 0
    assert _refused(c.send_direct(rep, "set name x", RTC + 5))
    assert c.send_direct(chat, "hello", RTC + 6)[0] == harness.RESP_SENT
    assert c.send_direct(room, "a post", RTC + 7)[0] == harness.RESP_SENT


def test_untrusted_reads_carry_no_secrets(nodes):
    u = nodes("companion_wifi_usb", "gs", seed=61, rtc=RTC).start()
    uc = _client(u)
    assert uc.set_channel(1, "simchan", SECRET)[0] == 0
    assert uc.cli("set pin 424242").startswith("> pin is now")
    assert uc.get_channel(1) == ("simchan", SECRET)                 # the cable sees the secret
    assert struct.unpack_from("<I", uc.device_query(), 4)[0] == 424242
    u.stop()
    n = nodes("companion_wifi", "gs", seed=61, rtc=RTC).start()     # the same device, served over TCP
    c = _client(n)
    assert c.get_channel(1) == ("simchan", bytes(16))               # the name serves messaging; no secret
    assert struct.unpack_from("<I", c.device_query(), 4)[0] == 0    # no PIN to pair with


REMOTE_CLI, TELEM_LOC, FAVOURITE = 0x10, 0x04, 0x01
UNSET_CLOCK = 1715770351            # what the clock reads at boot until it is set (VolatileRTCClock)


def _flags_of(c, pub):
    return {x["key"]: x["flags"] for x in c.get_contacts()[1]}[pub]


@pytest.mark.parametrize("grant", [REMOTE_CLI, TELEM_LOC], ids=["remote-cli", "telemetry"])
def test_granting_a_contact_flag_is_refused_over_tcp(nodes, grant):
    """A contact flag that grants something (the remote CLI runs the key's texts as commands; telemetry releases the
    node's position and sensors) is a setting. The rest of the record stays contact handling."""
    n = nodes("companion_wifi", "cg%d" % grant, seed=80 + grant, rtc=RTC).start()
    c = _client(n)
    assert c.add_contact(PEER, "peer", ctype=ADV_TYPE_CHAT)[0] == 0
    assert _refused(c.add_contact(PEER, "peer", flags=grant, ctype=ADV_TYPE_CHAT))         # an update granting it
    assert _refused(c.add_contact(bytes([7]) * 32, "new", flags=grant, ctype=ADV_TYPE_CHAT))  # a new contact with it
    assert c.add_contact(PEER, "renamed", flags=FAVOURITE, ctype=ADV_TYPE_CHAT)[0] == 0    # no grant: not gated
    assert _flags_of(c, PEER) == FAVOURITE
    assert bytes([7]) * 32 not in {x["key"] for x in c.get_contacts()[1]}
    assert W_APP_FRAME in _refusal_whats(n), audit_file(n)


def test_a_grant_held_from_the_cable_survives_tcp_edits_and_can_be_taken_away(nodes):
    u = nodes("companion_wifi_usb", "ch", seed=95, rtc=RTC).start()
    uc = _client(u)
    assert uc.add_contact(PEER, "peer", flags=REMOTE_CLI, ctype=ADV_TYPE_CHAT)[0] == 0      # the cable grants it
    uc.request_reboot()                                                                   # saves the contacts
    assert u.wait_exit(10) == 75
    n = nodes("companion_wifi", "ch", seed=95, rtc=RTC).start()                            # the same device over TCP
    c = _client(n)
    assert c.add_contact(PEER, "peer", flags=REMOTE_CLI | FAVOURITE, ctype=ADV_TYPE_CHAT)[0] == 0  # held: no new grant
    assert _flags_of(c, PEER) == REMOTE_CLI | FAVOURITE
    assert c.add_contact(PEER, "peer", flags=0, ctype=ADV_TYPE_CHAT)[0] == 0               # taking it away is not gated
    assert _flags_of(c, PEER) == 0
    assert _refused(c.add_contact(PEER, "peer", flags=REMOTE_CLI, ctype=ADV_TYPE_CHAT))     # and it cannot come back


def _set_time(c, secs):
    return c.request(bytes([harness.CMD_SET_DEVICE_TIME]) + struct.pack("<I", secs))


def test_setting_a_set_clock_is_refused_over_tcp(nodes):
    """The first time set since boot is open to any caller, as the app does on connecting. After that it is a setting
    like any other, whatever the clock read at boot."""
    n = nodes("companion_wifi", "ct", seed=97, rtc=RTC).start()
    c = _client(n)
    assert _set_time(c, RTC + 60)[0] == 0
    assert _refused(_set_time(c, RTC + 86400 * 365))
    assert W_APP_FRAME in _refusal_whats(n), audit_file(n)


def test_the_first_set_after_a_reboot_with_contacts_is_open(nodes):
    """Contacts set the clock at boot (their newest modification time), which is not a set by the app: a phone that
    connects over TCP after a reboot still sets the time once."""
    u = nodes("companion_wifi_usb", "cr", seed=96, rtc=UNSET_CLOCK).start()
    uc = _client(u)
    assert _set_time(uc, RTC)[0] == 0
    assert uc.add_contact(PEER, "peer", ctype=ADV_TYPE_CHAT)[0] == 0
    uc.request_reboot()                                                                   # saves the contacts
    assert u.wait_exit(10) == 75
    n = nodes("companion_wifi", "cr", seed=96, rtc=UNSET_CLOCK).start()                  # the same device over TCP
    c = _client(n)
    assert _set_time(c, RTC + 600)[0] == 0
    assert _refused(_set_time(c, RTC + 86400 * 365))


def test_an_untrusted_contact_time_is_held_at_the_clock(nodes):
    """The newest contact modification time sets the clock at boot, so one from an untrusted caller is not ahead of it."""
    n = nodes("companion_wifi", "cm", seed=94, rtc=RTC).start()
    c = _client(n)
    assert c.add_contact_full(PEER, "peer", lastmod=0xFFFFFFF0)[0] == 0
    assert c.add_contact_full(bytes([9]) * 32, "past", lastmod=RTC - 1000)[0] == 0
    got = {x["key"]: x["lastmod"] for x in c.get_contacts()[1]}
    assert RTC <= got[PEER] < RTC + 600, got[PEER]
    assert got[bytes([9]) * 32] == RTC - 1000                                             # an older time is kept


def test_a_cable_contact_time_is_kept(nodes):
    n = nodes("companion", "ck", seed=93, rtc=RTC).start()
    c = _client(n)
    assert c.add_contact_full(PEER, "peer", lastmod=RTC + 86400)[0] == 0
    assert {x["key"]: x["lastmod"] for x in c.get_contacts()[1]}[PEER] == RTC + 86400


def test_setting_a_set_clock_works_over_the_cable(nodes):
    n = nodes("companion", "cc", seed=99, rtc=RTC).start()
    assert _set_time(_client(n), RTC + 60)[0] == 0
