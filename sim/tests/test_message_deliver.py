"""message.deliver on simulated Companions (docs/contract/OPERATIONS.md section 6.5): a message a manager supplies reaches a
stock app's receive queue, marked, and never the radio. One message reaches the app once, however many ways it comes. The
manager's own read says which were supplied, in a field no contact can write."""
import json
import os
import sys
import time

from sim.ether import Ether  # noqa: F401
from sim.harness import CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import mecon_iface  # noqa: E402
import mecon_schema  # noqa: E402
from test_inbox import CONTACT_WRITE_S, RTC, client, listed, pair, wait_received  # noqa: E402

MARK = "[via manager] "


def deliver(i, from_key, text, ts, rid=None):
    args = {"from": from_key.hex(), "text": text, "timestamp": ts}
    return i.request("message.deliver", args, rid=rid) if rid else i.request("message.deliver", args)


def test_a_supplied_message_reaches_the_stock_app_marked_and_nothing_goes_on_the_air(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    listed(ir)
    tx = len(ether.tx_log)
    r = deliver(ir, cs.pub_key, "missed while away", RTC + 5)
    assert r["status"] == "ok" and r["result"]["state"] == "delivered", r
    got = cr.sync_messages(timeout=5, want=1)
    assert [m["text"] for m in got] == [MARK + "missed while away"], got
    assert got[0]["ts"] == RTC + 5 and got[0]["from"] == cs.pub_key[:6], got
    m = listed(ir)["messages"][0]
    assert m["source"] == "manager" and m["text"] == "missed while away" and m["from"] == cs.pub_key.hex(), m
    time.sleep(3.0)
    assert len(ether.tx_log) == tx, "no radio, and no ACK"
    assert cs.sync_messages(timeout=2, want=1) == []


def test_a_real_reception_says_radio_even_with_the_marker_in_its_text(nodes, ether):
    """Any contact can type the marker. The app cannot tell, and the manager's field still can."""
    nr, cs, cr, ir = pair(nodes, ether)
    listed(ir)
    cs.send_direct(cr.pub_key, MARK + "not really", RTC + 6)
    wait_received(ir, 1)
    m = listed(ir)["messages"][0]
    assert m["source"] == "radio" and m["text"] == MARK + "not really", m


def test_a_channel_message_goes_to_its_slot(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    listed(ir)
    r = ir.request("message.deliver", {"channel": 0, "text": "alice: hello all", "timestamp": RTC + 7})
    assert r["result"]["state"] == "delivered", r
    got = cr.sync_messages(timeout=5, want=1)
    assert got[0]["kind"] == "channel" and got[0]["channel"] == 0 and got[0]["text"] == MARK + "alice: hello all", got
    m = listed(ir)["messages"][0]
    assert m["source"] == "manager" and m["channel"] == 0 and "from" not in m, m


def test_what_cannot_be_delivered_is_an_answer(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    before = len(cr.get_contacts()[1])
    r = deliver(ir, b"\xab" * 32, "x", RTC)
    assert r["status"] == "failed" and r["error"] == "no-contact", r
    assert len(cr.get_contacts()[1]) == before, "no silent contact"
    r = ir.request("message.deliver", {"channel": 30, "text": "x", "timestamp": RTC})
    assert r["status"] == "failed" and r["error"] == "no-channel", r
    r = deliver(ir, cs.pub_key, "y" * 147, RTC)
    assert r["status"] == "failed" and r["error"] == "text-too-long", "refused rather than cut: %s" % r
    assert deliver(ir, cs.pub_key, "y" * 146, RTC)["result"]["state"] == "delivered", "the marker leaves 146"
    for args in ({"text": "x", "timestamp": RTC}, {"from": cs.pub_key.hex(), "timestamp": RTC}, {"from": cs.pub_key.hex(), "text": "x"},
                 {"from": cs.pub_key.hex(), "channel": 0, "text": "x", "timestamp": RTC}, {"from": "abcd", "text": "x", "timestamp": RTC}):
        assert ir.request("message.deliver", args)["status"] == "invalid", args
    assert [m["text"] for m in cr.sync_messages(timeout=3, want=2)] == [MARK + "y" * 146]


def test_one_message_reaches_the_app_once_however_it_comes(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    listed(ir)
    # delivered, then delivered again under another request id, then the same request again
    first = deliver(ir, cs.pub_key, "twice", RTC + 8, rid="d-1")
    assert first["result"]["state"] == "delivered"
    assert deliver(ir, cs.pub_key, "twice", RTC + 8, rid="d-2")["result"]["state"] == "duplicate"
    assert deliver(ir, cs.pub_key, "twice", RTC + 8, rid="d-1")["result"]["state"] == "delivered", "the kept answer"
    # another time or another text is another message
    assert deliver(ir, cs.pub_key, "twice", RTC + 9)["result"]["state"] == "delivered"
    assert [m["text"] for m in cr.sync_messages(timeout=3, want=3)] == [MARK + "twice", MARK + "twice"]
    # heard on the radio first: a delivery of it is a duplicate
    cs.send_direct(cr.pub_key, "radio first", RTC + 10)
    wait_received(ir, 3)
    assert deliver(ir, cs.pub_key, "radio first", RTC + 10)["result"]["state"] == "duplicate"
    assert [m["text"] for m in cr.sync_messages(timeout=3, want=2)] == ["radio first"]


def test_a_real_copy_arriving_after_a_delivery_is_not_queued_again(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    listed(ir)
    assert deliver(ir, cs.pub_key, "both ways", RTC + 11)["result"]["state"] == "delivered"
    n = listed(ir)["received"]
    tx = len(ether.tx_log)
    cs.send_direct(cr.pub_key, "both ways", RTC + 11)
    end = time.time() + 75
    while time.time() < end and not [t for t in ether.tx_log[tx:] if t.src == "ib"]:   # the native ACK: it was received
        time.sleep(0.3)
    assert [t for t in ether.tx_log[tx:] if t.src == "ib"], "the sender still gets its native ACK"
    time.sleep(2.0)
    ir.request("status.get")   # takes any event off the shared link, so the app frames below read clean
    assert [m["text"] for m in cr.sync_messages(timeout=3, want=2)] == [MARK + "both ways"], "once, as delivered"
    assert listed(ir)["received"] == n


def test_a_full_queue_answers_busy_rather_than_dropping(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    k = 0
    while k < 300:
        r = deliver(ir, cs.pub_key, "q%d" % k, RTC + 100 + k)
        if r["status"] != "ok":
            break
        k += 1
    assert r["status"] == "busy" and r["error"] == "queue-full", r
    assert k == 16, "OFFLINE_QUEUE_SIZE in the sim: %d" % k
    assert [m["text"] for m in cr.sync_messages(timeout=20, want=16)] == [MARK + "q%d" % n for n in range(16)]
    assert deliver(ir, cs.pub_key, "q%d" % k, RTC + 100 + k)["result"]["state"] == "delivered", "room again"


def test_a_reboot_loses_what_was_not_read_under_a_new_epoch(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    time.sleep(CONTACT_WRITE_S)
    r = deliver(ir, cs.pub_key, "lost", RTC + 12)
    epoch = r["result"]["epoch"]
    cr.close()
    nr.restart()
    cr, ir = client(nr)
    assert cr.sync_messages(timeout=3, want=1) == []
    r = deliver(ir, cs.pub_key, "lost", RTC + 12)
    assert r["result"]["state"] == "delivered" and r["result"]["epoch"] > epoch, r


def test_an_enrolled_device_delivers_only_for_a_signature(nodes, ether):
    from test_deployment_authority import Dev, wrap
    nr, cs, cr, ir = pair(nodes, ether)
    time.sleep(CONTACT_WRITE_S)
    cr.close()
    d = Dev(nr)
    assert d.provision()["status"] == "ok"
    args = {"from": cs.pub_key.hex(), "text": "signed", "timestamp": RTC + 13}
    r = d.i.request("message.deliver", args)
    assert r["status"] == "refused", r
    r = d.send(wrap(d.node_key, "message.deliver", args, job=5))
    assert r["status"] == "ok" and r["result"]["state"] == "delivered", r
    assert [m["text"] for m in d.c.sync_messages(timeout=3, want=1)] == [MARK + "signed"]
    again = d.send(wrap(d.node_key, "message.deliver", args, job=5))
    assert again["result"]["state"] == "delivered", "the same request: the kept answer"
    assert d.c.sync_messages(timeout=2, want=1) == []


def test_a_caller_the_table_does_not_trust_delivers_nothing(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    time.sleep(CONTACT_WRITE_S)
    assert cr.cli("set pin 123456").startswith("> pin is now")   # the published default PIN: an untrusted link
    cr.request_reboot()
    assert nr.wait_exit(15) == 75
    cr.close()
    nr.start()
    b = nr.connect_ble(123456)
    b.app_start()
    bi = mecon_iface.IfaceClient(b._send, b._read_frame)
    r = bi.request("message.deliver", {"from": cs.pub_key.hex(), "text": "intruder", "timestamp": RTC + 14})
    assert r["status"] == "refused", r
    assert b.sync_messages(timeout=2, want=1) == []


def test_results_follow_the_contract(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    r = deliver(ir, cs.pub_key, "schema", RTC + 15)
    assert not mecon_schema.check(r["result"], mecon_schema.load("message.deliver.result.json")), r
    assert not mecon_schema.check(listed(ir), mecon_schema.load("inbox.list.result.json"))
    r = deliver(ir, cs.pub_key, "schema two", RTC + 16)
    l = listed(ir)
    assert not mecon_schema.check(l, mecon_schema.load("inbox.list.result.json")), l
    assert l["messages"][0]["source"] == "manager"
    caps, cursor = [], None
    while True:
        h = ir.request("hello", {} if cursor is None else {"cursor": cursor})["result"]
        caps += h["capabilities"]
        cursor = h.get("next")
        if cursor is None:
            break
    assert "message.deliver" in caps
