"""Two Companions that cannot hear each other talk through a Repeater."""
import time

from sim.harness import CompanionClient

SECRET = bytes(range(16))
RTC = 1760000000


def _setup(ether, nodes):
    ca = nodes("companion", "ca", ether_path=ether.path, seed=11, rtc=RTC).start()
    cb = nodes("companion", "cb", ether_path=ether.path, seed=22, rtc=RTC).start()
    rr = nodes("repeater", "rr", ether_path=ether.path, seed=33, rtc=RTC).start()
    assert ether.wait_for_nodes(["ca", "cb", "rr"])
    # topology: both companions hear only the repeater
    ether.set_link("ca", "rr")
    ether.set_link("cb", "rr")
    a, b = CompanionClient(ca), CompanionClient(cb)
    for c in (a, b):
        c.device_query()
        c.app_start()
    return a, b


def test_direct_and_channel_message_via_repeater(ether, nodes):
    a, b = _setup(ether, nodes)
    # each companion learns the other as a contact, and both join one private channel
    assert a.add_contact(b.pub_key, "bob")[0] == 0
    assert b.add_contact(a.pub_key, "alice")[0] == 0
    for c in (a, b):
        assert c.set_channel(1, "simchan", SECRET)[0] == 0

    f = a.send_direct(b.pub_key, "hello bob", RTC + 10)
    assert f[0] == 6, f                       # RESP_SENT
    f = a.send_channel(1, "hello channel", RTC + 11)
    assert f[0] == 0, f

    msgs = b.sync_messages(timeout=60, want=2)
    got = {(m["kind"], m["text"]) for m in msgs}
    assert ("direct", "hello bob") in got, msgs
    assert any(k == "channel" and t.endswith("hello channel") for k, t in got), msgs
    direct = [m for m in msgs if m["kind"] == "direct"][0]
    assert direct["from"] == a.pub_key[:6]
    assert [m for m in msgs if m["kind"] == "channel"][0]["channel"] == 1

    # the companions never heard each other: every delivery went through the repeater
    for tx in ether.tx_log:
        if tx.src in ("ca", "cb"):
            assert tx.heard_by == ["rr"]
    assert any(tx.src == "rr" for tx in ether.tx_log)


def test_no_delivery_without_repeater_link(ether, nodes):
    a, b = _setup(ether, nodes)
    ether.clear_links()                       # nobody hears anybody
    a.add_contact(b.pub_key, "bob")
    b.add_contact(a.pub_key, "alice")
    a.send_direct(b.pub_key, "lost", RTC + 10)
    time.sleep(3)
    assert b.request(bytes([10]))[0] == 10    # RESP_NO_MORE_MESSAGES
    assert all(not tx.delivered_to for tx in ether.tx_log)
