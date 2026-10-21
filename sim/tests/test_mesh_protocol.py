"""Three stock behaviours of the Companion's mesh path, asserted on the ether log and the app pushes:

* the ACK flow: a direct message is acknowledged (PUSH_CODE_SEND_CONFIRMED), a lost ACK leaves the sender
  unconfirmed, and the retry is the app's (the stock Companion firmware has no retry of its own: its
  onSendTimeout() is empty; the app resends with attempt + 1)
* path learning: a flood-routed first exchange teaches the sender an out-path, and the next message goes direct
* advert auto-add: a heard advert becomes a contact when auto-add is on, and is only reported when it is off

No test sleeps for a fixed time. Each waits on a push or on a transmission appearing in the ether log. The
absence checks (no confirmation after a lost ACK) are made after the lost ACK-bearing frame is already in the log.
"""
import struct
import time

import pytest

from sim.harness import CMD_SEND_TXT_MSG, CompanionClient

RTC = 1760000000
OUT_PATH_UNKNOWN = 0xFF

PUSH_ADVERT = 0x80            # PUSH_CODE_ADVERT: an advert from a node that is already a contact
PUSH_PATH_UPDATED = 0x81
PUSH_SEND_CONFIRMED = 0x82
PUSH_NEW_ADVERT = 0x8A        # PUSH_CODE_NEW_ADVERT: an advert from a node that is not stored as a contact
CMD_SEND_SELF_ADVERT = 7
CMD_SET_OTHER_PARAMS = 38     # frame: cmd, manual_add_contacts (bit 0 set = manual add, no auto-add)

ROUTE_FLOOD, ROUTE_DIRECT = 1, 2
PT_TXT, PT_ACK, PT_ADVERT, PT_PATH = 0x02, 0x03, 0x04, 0x08


def _route(tx):
    return tx.data[0] & 0x03


def _ptype(tx):
    return (tx.data[0] >> 2) & 0x0F


def _txs(ether, src, ptype):
    return [t for t in ether.tx_of(src) if _ptype(t) == ptype]


def _wait_for(pred, timeout=60.0):
    end = time.time() + timeout
    while time.time() < end:
        if pred():
            return True
        time.sleep(0.02)
    return False


def _pair(ether, nodes):
    """Two Companions that only hear one Repeater, each holding the other as a contact (no path known)."""
    ca = nodes("companion", "ca", ether_path=ether.path, seed=11, rtc=RTC).start()
    cb = nodes("companion", "cb", ether_path=ether.path, seed=22, rtc=RTC).start()
    rr = nodes("repeater", "rr", ether_path=ether.path, seed=33, rtc=RTC).start()
    assert ether.wait_for_nodes(["ca", "cb", "rr"])
    ether.set_link("ca", "rr")
    ether.set_link("cb", "rr")
    a, b = CompanionClient(ca), CompanionClient(cb)
    for c in (a, b):
        c.device_query()
        c.app_start()
    assert a.add_contact(b.pub_key, "bob")[0] == 0
    assert b.add_contact(a.pub_key, "alice")[0] == 0
    return a, b


def _send(client, to_pub, text, ts, attempt=0):
    """CMD_SEND_TXT_MSG with an explicit attempt number (what an app sends when it retries)."""
    f = client.request(bytes([CMD_SEND_TXT_MSG, 0, attempt]) + struct.pack("<I", ts) + to_pub[:6] + text.encode())
    assert f[0] == 6, f                       # RESP_SENT: flag (1 = flood), expected ack (4), est timeout (4)
    return {"flood": f[1] == 1, "ack": f[2:6], "est_timeout": struct.unpack_from("<I", f, 6)[0]}


def _confirmed(client, timeout=60.0):
    f = client.wait_push(PUSH_SEND_CONFIRMED, timeout=timeout)
    assert f is not None, "no PUSH_CODE_SEND_CONFIRMED"
    return {"ack": f[1:5], "rtt": struct.unpack_from("<I", f, 5)[0]}


def _contact(client, pub_key):
    _, contacts = client.get_contacts()
    return next((c for c in contacts if c["key"] == pub_key), None)


@pytest.mark.timescale(10)
def test_direct_message_is_acknowledged_flood_and_direct(ether, nodes):
    a, b = _pair(ether, nodes)

    # first message: no path yet, so it floods; the ACK comes back inside the returned path
    s1 = _send(a, b.pub_key, "first", RTC + 10)
    assert s1["flood"]
    c1 = _confirmed(a)
    assert c1["ack"] == s1["ack"], (c1, s1)
    assert c1["rtt"] > 0
    assert [m["text"] for m in b.sync_messages(timeout=60, want=1)] == ["first"]

    # second message: the path is known, so it goes direct, and its ACK is a plain ACK packet
    s2 = _send(a, b.pub_key, "second", RTC + 11)
    assert not s2["flood"]
    assert s2["ack"] != s1["ack"]
    c2 = _confirmed(a)
    assert c2["ack"] == s2["ack"], (c2, s2)
    assert [m["text"] for m in b.sync_messages(timeout=60, want=1)] == ["second"]

    # the ether shows the ACK for the direct message being sent by the receiver, not just a push appearing
    assert any(_ptype(t) == PT_ACK for t in ether.tx_of("cb")), "cb never transmitted an ACK packet"


@pytest.mark.timescale(10)
def test_lost_ack_gives_no_confirmation_and_the_apps_retry_goes_out_again(ether, nodes):
    a, b = _pair(ether, nodes)
    # the way back to the sender is dead: the message arrives, its ACK is lost
    ether.set_loss("rr", "ca", 1.0, both=False)

    s1 = _send(a, b.pub_key, "needs an ack", RTC + 10)
    assert s1["flood"]
    assert [m["text"] for m in b.sync_messages(timeout=60, want=1)] == ["needs an ack"]
    # the ACK rode a PATH packet that cb sent and the repeater relayed; the ether lost it on the way to ca
    assert _wait_for(lambda: any(_ptype(t) == PT_PATH and ("ca", "loss") in t.lost for t in ether.tx_of("rr"))), \
        [(t.src, _ptype(t), t.lost) for t in ether.tx_log]
    assert not any(f[0] == PUSH_SEND_CONFIRMED for f in a.pushes)
    assert a.wait_push(PUSH_SEND_CONFIRMED, timeout=1.0) is None
    # and the firmware does not resend by itself: still exactly one transmission of the message from ca
    assert len(_txs(ether, "ca", PT_TXT)) == 1

    # the app's retry: same text and timestamp, attempt 1, over a link that works again
    ether.set_loss("rr", "ca", 0.0, both=False)
    s2 = _send(a, b.pub_key, "needs an ack", RTC + 10, attempt=1)
    assert s2["ack"] != s1["ack"]             # the attempt number is part of what the ACK covers
    c = _confirmed(a)
    assert c["ack"] == s2["ack"], (c, s2)

    ca_txt = _txs(ether, "ca", PT_TXT)
    assert len(ca_txt) == 2, ca_txt
    assert ca_txt[0].data != ca_txt[1].data   # a second transmission of the message, not a replay of the first
    assert ca_txt[1].start_us > ca_txt[0].start_us
    assert ca_txt[1].heard_by == ["rr"]


@pytest.mark.timescale(10)
def test_path_is_learned_from_the_first_flood_and_the_next_message_goes_direct(ether, nodes):
    a, b = _pair(ether, nodes)

    bob = _contact(a, b.pub_key)
    assert bob["path_len"] == OUT_PATH_UNKNOWN

    s1 = _send(a, b.pub_key, "one", RTC + 10)
    assert s1["flood"]
    _confirmed(a)
    assert a.wait_push(PUSH_PATH_UPDATED, timeout=30) is not None
    bob = _contact(a, b.pub_key)
    assert bob["path_len"] != OUT_PATH_UNKNOWN and bob["path_len"] >= 1, bob   # one hop: the repeater

    s2 = _send(a, b.pub_key, "two", RTC + 11)
    assert not s2["flood"]
    _confirmed(a)
    assert [m["text"] for m in b.sync_messages(timeout=60, want=2)] == ["one", "two"]

    sent = _txs(ether, "ca", PT_TXT)
    assert [_route(t) for t in sent] == [ROUTE_FLOOD, ROUTE_DIRECT], sent
    assert sent[1].heard_by == ["rr"]
    # the repeater carried the direct message on to cb, along the stored path (no second flood from anybody)
    assert not any(_ptype(t) == PT_TXT and _route(t) == ROUTE_FLOOD for t in ether.tx_log[ether.tx_log.index(sent[1]):])


def _two_in_range(ether, nodes):
    """An advertiser and a listener that hear each other; the listener knows nobody."""
    na = nodes("companion", "adv", ether_path=ether.path, seed=44, rtc=RTC).start()
    nl = nodes("companion", "lis", ether_path=ether.path, seed=55, rtc=RTC).start()
    assert ether.wait_for_nodes(["adv", "lis"])
    ether.set_link("adv", "lis")
    adv, lis = CompanionClient(na), CompanionClient(nl)
    for c in (adv, lis):
        c.device_query()
        c.app_start()
    return adv, lis


def _advertise(adv):
    assert adv.request(bytes([CMD_SEND_SELF_ADVERT, 0]))[0] == 0   # zero-hop


@pytest.mark.timescale(10)
def test_heard_advert_is_added_as_a_contact_when_auto_add_is_on(ether, nodes):
    adv, lis = _two_in_range(ether, nodes)
    assert lis.request(bytes([CMD_SET_OTHER_PARAMS, 0]))[0] == 0   # manual_add_contacts = 0: auto-add on
    assert _contact(lis, adv.pub_key) is None

    _advertise(adv)
    # the contact exists in the node, so the push is the known-contact advert (PUSH_CODE_ADVERT, public key only)
    f = lis.wait_push(PUSH_ADVERT, timeout=60)
    assert f is not None, "no advert push"
    assert f[1:33] == adv.pub_key
    assert not any(p[0] == PUSH_NEW_ADVERT for p in lis.pushes)
    c = _contact(lis, adv.pub_key)
    assert c is not None
    assert c["adv"] > 0
    assert any(_ptype(t) == PT_ADVERT and t.delivered_to == ["lis"] for t in ether.tx_of("adv"))


@pytest.mark.timescale(10)
def test_heard_advert_is_only_reported_when_auto_add_is_off(ether, nodes):
    adv, lis = _two_in_range(ether, nodes)
    assert lis.request(bytes([CMD_SET_OTHER_PARAMS, 1]))[0] == 0   # manual_add_contacts = 1, no per-type bits set
    _advertise(adv)
    # PUSH_CODE_NEW_ADVERT carries the whole contact record of a node that is not stored
    f = lis.wait_push(PUSH_NEW_ADVERT, timeout=60)
    assert f is not None, "no new-advert push"
    assert f[1:33] == adv.pub_key
    assert not any(p[0] == PUSH_ADVERT for p in lis.pushes)
    assert _contact(lis, adv.pub_key) is None
    count, contacts = lis.get_contacts()
    assert count == 0 and contacts == []
    assert any(_ptype(t) == PT_ADVERT and t.delivered_to == ["lis"] for t in ether.tx_of("adv"))
