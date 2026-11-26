"""Which received messages notify a Companion's screen (Issue #652, docs/UI.md "Notifications").

A direct message always shows on the screen; a channel message shows only when its slot is marked favourite, and
reaches the app's queue either way. The screen is the real ui-new code on the recording display, so "notifies" is
read from the frames it drew (the simulated board has no buzzer pin: the buzzer and the screen are the one UI call).
The flag is set from the CLI and from channel.put, read back by channel.get and channel.list, and survives a reboot.
"""
import os
import sys
import time

import pytest

from sim import harness
from sim.harness import CompanionClient

import test_connectivity_mode as t

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402

pytestmark = pytest.mark.timescale(10)

KEY = bytes(range(0x30, 0x40))
KEY2 = bytes(range(0x50, 0x60))
RTC = t.RTC


def iface(c):
    return mecon_iface.IfaceClient(c._send, c._read_frame)


def put(i, slot, name, key, **extra):
    args = {"slot": slot, "name": name, "secret": key.hex()}
    args.update(extra)
    return i.request("channel.put", args)


def pair(ether, nodes, seed):
    """A standalone Companion with a screen (the receiver) and a plain Companion on one radio link, sharing channel
    slot 1 (key KEY) and slot 2 (key KEY2); the two also know each other as contacts for a direct message."""
    rv = nodes("companion_standalone", "fv", seed=seed, rtc=RTC, ether_path=ether.path).start()
    rs = nodes("companion", "fs", seed=seed + 1, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes(["fv", "fs"])
    ether.set_link("fv", "fs")
    v, s = t._client(rv), t._client(rs)
    for slot, name, key in ((1, "quiet", KEY), (2, "loud", KEY2)):
        assert v.set_channel(slot, name, key)[0] == harness.RESP_OK
        assert s.set_channel(slot, name, key)[0] == harness.RESP_OK
    assert v.add_contact(s.pub_key, "sender", flags=0)[0] == 0
    assert s.add_contact(v.pub_key, "victim")[0] == 0
    t.home_settled(rv)
    return rv, v, s


def drawn_since(node, mark):
    return "\n".join(node.ui.text(f) for f in node.ui.frames()[mark:])


def send_channel_until_heard(s, v, slot, text, ts):
    """A channel message is a flood with no ACK, and the simulated radio drops one that meets the receiver's own
    transmission (a DM's ACK and path return); send again, with a later timestamp, until the receiver's app queue has it."""
    for attempt in range(4):
        assert s.send_channel(slot, text, ts + attempt)[0] == harness.RESP_OK
        got = v.sync_messages(timeout=12, want=1)
        if any(m["text"].endswith(text) for m in got):
            return got
        time.sleep(2.0)
    pytest.fail("the channel message never reached the receiver")


def test_a_dm_notifies_and_a_channel_only_when_favourite(ether, nodes):
    rv, v, s = pair(ether, nodes, 41)
    assert v.cli("get channel.favourites") == "> none"                 # off for every slot, the public channel included

    # a direct message: always on the screen
    assert s.send_direct(v.pub_key, "direct-hello", RTC + 10)[0] == harness.RESP_SENT
    assert rv.ui.wait_text("direct-hello", 30), rv.ui.last_text()
    assert [m["text"] for m in v.sync_messages(timeout=30, want=1)] == ["direct-hello"]

    # a channel message on a slot that is not a favourite: queued for the app, nothing on the screen
    mark = len(rv.ui.frames())
    send_channel_until_heard(s, v, 1, "quiet-text", RTC + 20)           # it reached the app queue
    time.sleep(3.0)                                                      # the screen had every chance to draw it
    assert "quiet-text" not in drawn_since(rv, mark), drawn_since(rv, mark)

    # mark slot 2 favourite: its message is on the screen (positive control: this UI path draws a channel message)
    assert v.cli("set channel.favourite 2 on") == "OK"
    assert v.cli("get channel.favourites") == "> 2"
    mark = len(rv.ui.frames())
    for attempt in range(4):   # the screen first: draining the app queue sends it back to the home page
        assert s.send_channel(2, "loud-text", RTC + 30 + attempt)[0] == harness.RESP_OK
        if rv.ui.wait_text("loud-text", 12):
            break
        time.sleep(2.0)
    assert "loud-text" in rv.ui.last_text(), rv.ui.last_text()          # the screen shows it
    got = v.sync_messages(timeout=30, want=1)
    assert any(m["text"].endswith("loud-text") for m in got), got       # and the app still gets it

    # slot 1 is still quiet after the favourite on slot 2
    mark = len(rv.ui.frames())
    send_channel_until_heard(s, v, 1, "quiet-again", RTC + 40)
    time.sleep(3.0)
    assert "quiet-again" not in drawn_since(rv, mark)


def test_the_flag_is_set_by_channel_put_read_back_and_survives_a_reboot(nodes):
    n = nodes("companion_standalone", "ff1", seed=45, rtc=RTC).start()
    c = CompanionClient(n)
    c.app_start()
    i = iface(c)
    r = put(i, 3, "ops", KEY, favourite=True)
    assert r["status"] == "ok" and r["result"]["favourite"] is True, r
    assert put(i, 4, "plain", KEY2)["result"]["favourite"] is False        # not given: off
    assert i.request("channel.get", {"slot": 3})["result"]["favourite"] is True
    listed = {ch["slot"]: ch["favourite"] for ch in i.request("channel.list")["result"]["channels"]}
    assert listed[3] is True and listed[4] is False and listed[0] is False, listed   # the public channel is off
    assert c.cli("get channel.favourites") == "> 3"
    bad = i.request("channel.put", {"slot": 5, "name": "x", "secret": bytes(range(1, 17)).hex(), "favourite": "yes"})
    assert bad["status"] == "invalid", bad
    c.close()
    n.restart()
    c = CompanionClient(n)
    c.app_start()
    i = iface(c)
    assert c.cli("get channel.favourites") == "> 3", "saved with the channel table"
    assert i.request("channel.get", {"slot": 3})["result"]["favourite"] is True
    assert i.request("channel.get", {"slot": 4})["result"]["favourite"] is False


def test_a_replaced_or_removed_slot_loses_its_mark_and_the_same_channel_keeps_it(nodes):
    n = nodes("companion_standalone", "ff2", seed=46, rtc=RTC).start()
    c = CompanionClient(n)
    c.app_start()
    i = iface(c)
    assert put(i, 3, "ops", KEY, favourite=True)["status"] == "ok"
    assert put(i, 3, "ops", KEY)["result"]["favourite"] is True            # the same channel written again keeps it
    assert c.set_channel(3, "ops", KEY)[0] == harness.RESP_OK              # and so does the app's own frame
    assert c.cli("get channel.favourites") == "> 3"
    assert put(i, 3, "other", KEY2)["result"]["favourite"] is False        # a different channel through the interface
    assert c.cli("get channel.favourites") == "> none"
    assert c.cli("set channel.favourite 3 on") == "OK"
    assert c.set_channel(3, "native", KEY)[0] == harness.RESP_OK            # a different key through the app's frame
    assert c.cli("get channel.favourites") == "> none"
    assert c.cli("set channel.favourite 3 on") == "OK"
    assert i.request("channel.remove", {"slot": 3})["result"]["removed"] is True
    assert put(i, 3, "again", KEY)["result"]["favourite"] is False         # nothing comes back with the slot
    assert c.cli("set channel.favourite 3 on") == "OK"
    assert c.cli("get channel.favourites") == "> 3"
    assert c.cli("set channel.favourite 3 off") == "OK"
    assert c.cli("get channel.favourites") == "> none"


def test_the_cli_refuses_a_bad_slot_or_word(nodes):
    n = nodes("companion_standalone", "ff3", seed=47, rtc=RTC).start()
    c = CompanionClient(n)
    c.app_start()
    assert c.cli("set channel.favourite 7 on") == "Error, empty slot"
    assert c.cli("set channel.favourite 40 on").startswith("Error, usage")
    assert c.cli("set channel.favourite 0 maybe").startswith("Error, usage")
    assert c.cli("set channel.favourite on").startswith("Error, usage")
    assert c.cli("set channel.favourite 0 on") == "OK"                      # the public channel can be marked too
    assert c.cli("get channel.favourites") == "> 0"
