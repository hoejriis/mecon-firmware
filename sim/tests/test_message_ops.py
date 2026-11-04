"""message.send / get / list / ack over the device interface on simulated Companions (docs/contract/OPERATIONS.md
section 6): what the interface sends is what a stock receiver reads, a repeat is not a second transmission, each outcome
is correlated and told, and a caller the CLI table does not trust sends nothing."""
import os
import sys
import time

import pytest

from sim.ether import Ether  # noqa: F401
from sim.harness import CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402

RTC = 1792800000


def client(n):
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    return c, mecon_iface.IfaceClient(c._send, c._read_frame)


def pair(nodes, ether, a="ma", b="mb"):
    """Two Companions in radio range, each holding the other as a contact. The sender's interface client, and both apps."""
    na = nodes("companion_standalone", a, ether_path=ether.path, seed=31, rtc=RTC).start()
    nb = nodes("companion_standalone", b, ether_path=ether.path, seed=32, rtc=RTC).start()
    ca, ia = client(na)
    cb = CompanionClient(nb)
    cb.device_query()
    cb.app_start()
    assert ether.wait_for_nodes([a, b])
    ether.set_link(a, b)
    assert ca.add_contact(cb.pub_key, "bob")[0] == 0
    assert cb.add_contact(ca.pub_key, "alice")[0] == 0
    return na, nb, ca, ia, cb


def wait_state(i, msg, states, timeout=40.0):
    end = time.time() + timeout
    r = None
    while time.time() < end:
        r = i.request("message.get", {"message": msg})
        assert r["status"] == "ok", r
        if r["result"]["state"] in states:
            return r["result"]
        time.sleep(0.3)
    raise AssertionError("message %s stayed %s, wanted %s" % (msg, r, states))


def test_a_direct_message_reaches_a_stock_receiver_and_its_ack_is_correlated(nodes, ether):
    na, nb, ca, ia, cb = pair(nodes, ether)
    r = ia.request("message.send", {"to": cb.pub_key.hex(), "text": "hello from a manager", "timestamp": RTC + 5}, rid="dm-1")
    assert r["status"] == "ok", r
    res = r["result"]
    assert res["message"] == "dm-1" and res["state"] == "queued" and res["to"] == cb.pub_key[:6].hex(), res
    assert res["route"] == "flood" and res["ts"] == RTC + 5 and res["epoch"] >= 1, res
    got = cb.sync_messages(timeout=30, want=1)
    assert [m["text"] for m in got] == ["hello from a manager"], got
    assert got[0]["ts"] == RTC + 5 and got[0]["from"] == ca.pub_key[:6], got
    done = wait_state(ia, "dm-1", ("acked",))
    assert done["hash"] == res["hash"] and done["ack"] == res["ack"], (done, res)
    states = [e["data"]["state"] for e in ia.events if e.get("ev") == "message" and e["data"]["message"] == "dm-1"]
    assert states and states[-1] == "acked", ia.events
    assert all("text" not in e["data"] and "to" not in e["data"] for e in ia.events if e.get("ev") == "message")
    assert ca.wait_push(0x82, timeout=1.0) is None, "a stock app is not told of a message it did not send"


def test_a_channel_message_reaches_a_stock_receiver_and_ends_transmitted(nodes, ether):
    na, nb, ca, ia, cb = pair(nodes, ether)
    r = ia.request("message.send", {"channel": 0, "text": "to everyone", "timestamp": RTC + 6}, rid="ch-1")
    assert r["status"] == "ok" and r["result"]["channel"] == 0, r
    assert "ack" not in r["result"] and r["result"]["route"] == "flood", r
    got = cb.sync_messages(timeout=30, want=1)
    assert got and got[0]["kind"] == "channel" and got[0]["text"].endswith("to everyone"), got
    done = wait_state(ia, "ch-1", ("transmitted",))
    time.sleep(1.0)
    assert ia.request("message.list")["result"]["open"] == 0, "a channel message waits for no ACK"
    assert done["channel"] == 0


def test_a_recipient_or_a_channel_that_is_not_there_is_an_answer_and_creates_nothing(nodes, ether):
    na, nb, ca, ia, cb = pair(nodes, ether)
    before = len(ca.get_contacts()[1])
    r = ia.request("message.send", {"to": "ab" * 32, "text": "x"}, rid="no-1")
    assert r["status"] == "failed" and r["error"] == "no-contact", r
    assert len(ca.get_contacts()[1]) == before, "no silent contact"
    r = ia.request("message.send", {"channel": 30, "text": "x"}, rid="no-2")
    assert r["status"] == "failed" and r["error"] == "no-channel", r
    assert ia.request("message.get", {"message": "no-1"})["result"]["state"] == "unknown"


@pytest.mark.parametrize("args", [{"text": "no recipient"}, {"to": "ab" * 32}, {"to": "ab" * 32, "channel": 0, "text": "both"},
                                  {"to": "abcd", "text": "a prefix"}, {"channel": 0, "text": "x", "attempt": 1},
                                  {"to": "ab" * 32, "text": "x", "attempt": 4}, {"to": "ab" * 32, "text": ""}])
def test_bad_arguments_are_invalid(nodes, ether, args):
    n = nodes("companion_standalone", "mi", ether_path=ether.path, seed=33, rtc=RTC).start()
    c, i = client(n)
    r = i.request("message.send", args, rid="bad")
    assert r["status"] in ("invalid", "failed") and r.get("error") != "no-contact", r


def test_text_too_long_is_refused_rather_than_cut(nodes, ether):
    na, nb, ca, ia, cb = pair(nodes, ether)
    tx = len(ether.tx_log)
    r = ia.request("message.send", {"to": cb.pub_key.hex(), "text": "x" * 161}, rid="long-1")
    assert r["status"] == "failed" and r["error"] == "text-too-long", r
    r = ia.request("message.send", {"channel": 0, "text": "x" * 160}, rid="long-2")
    assert r["status"] == "failed" and r["error"] == "text-too-long", "the sender's name goes in front of it"
    time.sleep(1.5)
    assert len(ether.tx_log) == tx, "nothing went out"


def test_a_repeated_request_is_one_transmission(nodes, ether):
    na, nb, ca, ia, cb = pair(nodes, ether)
    args = {"to": cb.pub_key.hex(), "text": "only once", "timestamp": RTC + 7}
    first = ia.request("message.send", args, rid="dup-1")
    assert first["status"] == "ok", first
    assert ia.request("message.send", args, rid="dup-1")["result"] == first["result"], "the kept answer"
    # Past the result cache: the id is remembered as spent, and the answer says the result has gone. Not a second send.
    for k in range(20):
        assert ia.request("contact.put", {"key": "%02x" % k * 32, "name": "c%d" % k, "type": "chat"}, rid="fill-%d" % k)["status"] == "ok"
    again = ia.request("message.send", args, rid="dup-1")
    assert again["status"] == "failed" and again["error"] == "result-expired", again
    # Past the memory of spent ids too: the outcome table still knows it.
    for k in range(20, 90):
        assert ia.request("contact.put", {"key": "%02x" % k * 32, "name": "c%d" % k, "type": "chat"}, rid="fill-%d" % k)["status"] == "ok"
    again = ia.request("message.send", args, rid="dup-1")
    assert again["status"] == "ok" and again["result"]["hash"] == first["result"]["hash"], again
    got = cb.sync_messages(timeout=60, want=1)
    assert [m["text"] for m in got] == ["only once"], got
    assert cb.sync_messages(timeout=4, want=1) == [], "one on the air"


def test_a_send_waiting_for_airtime_says_so_and_still_goes(nodes, ether):
    """The duty window keeps a node quiet just after boot: the same hold a sustained load meets, and quick to reach."""
    na, nb, ca, ia, cb = pair(nodes, ether)
    r = ia.request("message.send", {"to": cb.pub_key.hex(), "text": "held a while"}, rid="held-1")
    assert r["status"] == "ok", r
    held = wait_state(ia, "held-1", ("duty-held",), timeout=15)
    assert held["wait_ms"] > 0, held
    assert any(e.get("ev") == "message" and e["data"]["state"] == "duty-held" for e in ia.events), ia.events
    done = wait_state(ia, "held-1", ("acked",), timeout=90)
    assert "wait_ms" not in done
    assert [m["text"] for m in cb.sync_messages(timeout=5, want=1)] == ["held a while"]


def test_no_ack_is_a_timeout_and_a_reboot_is_unknown_under_a_new_epoch(nodes, ether):
    na, nb, ca, ia, cb = pair(nodes, ether)
    ether.clear_links()
    r = ia.request("message.send", {"to": cb.pub_key.hex(), "text": "nobody hears"}, rid="to-1")
    assert r["status"] == "ok", r
    epoch = r["result"]["epoch"]
    done = wait_state(ia, "to-1", ("timed-out",), timeout=90)
    assert done["hash"] == r["result"]["hash"]
    ca.close()
    na.restart()
    ca, ia = client(na)
    r = ia.request("message.get", {"message": "to-1"})
    assert r["result"]["state"] == "unknown" and r["result"]["epoch"] > epoch, r


def test_a_full_table_of_open_sends_answers_busy_and_a_final_one_makes_room(nodes, ether):
    na, nb, ca, ia, cb = pair(nodes, ether)
    ether.clear_links()
    for k in range(8):
        r = ia.request("message.send", {"to": cb.pub_key.hex(), "text": "open %d" % k}, rid="op-%d" % k)
        assert r["status"] == "ok", (k, r)
    tx = len(ether.tx_log)
    r = ia.request("message.send", {"to": cb.pub_key.hex(), "text": "one too many"}, rid="op-8")
    assert r["status"] == "busy" and r["error"] == "outcomes-full", r
    assert ia.request("message.list")["result"]["open"] == 8
    wait_state(ia, "op-0", ("timed-out",), timeout=120)
    assert ia.request("message.ack", {"message": "op-0"})["result"]["known"] is True
    r = ia.request("message.send", {"to": cb.pub_key.hex(), "text": "room now"}, rid="op-9")
    assert r["status"] == "ok", r
    assert ia.request("message.get", {"message": "op-0"})["result"]["state"] == "unknown", "the collected entry went"
    assert tx >= 0


def test_ack_of_an_open_send_is_refused_and_a_status_counts_what_is_owed(nodes, ether):
    na, nb, ca, ia, cb = pair(nodes, ether)
    ether.clear_links()
    assert ia.request("message.send", {"to": cb.pub_key.hex(), "text": "x"}, rid="own-1")["status"] == "ok"
    r = ia.request("message.ack", {"message": "own-1"})
    assert r["status"] == "failed" and r["error"] == "not-final", r
    assert ia.request("status.get")["result"]["messages"]["open"] == 1
    wait_state(ia, "own-1", ("timed-out",), timeout=90)
    s = ia.request("status.get")["result"]["messages"]
    assert s == {"open": 0, "owed": 1}, s
    assert ia.request("message.ack", {"message": "own-1"})["result"]["known"] is True
    assert ia.request("status.get")["result"]["messages"]["owed"] == 0
    assert ia.request("message.ack", {"message": "own-1"})["status"] == "ok", "a retry of an ack whose reply was lost"
    assert ia.request("message.ack", {"message": "never"})["result"]["known"] is False


def test_an_enrolled_device_sends_only_for_a_signature(nodes, ether):
    from test_deployment_authority import Dev, wrap
    na, nb, ca, ia, cb = pair(nodes, ether)
    ca.close()
    d = Dev(na)
    assert d.provision()["status"] == "ok"
    r = d.i.request("message.send", {"to": cb.pub_key.hex(), "text": "not signed"}, rid="un-1")
    assert r["status"] == "refused", r
    r = d.send(wrap(d.node_key, "message.send", {"to": cb.pub_key.hex(), "text": "signed", "timestamp": RTC + 9}, job=5))
    assert r["status"] == "ok" and r["result"]["message"].endswith(":5"), r
    got = cb.sync_messages(timeout=30, want=1)
    assert [m["text"] for m in got] == ["signed"], got
    # The same bytes again: the kept answer, not a second send. Across a reboot the watermark says so.
    again = d.send(wrap(d.node_key, "message.send", {"to": cb.pub_key.hex(), "text": "signed", "timestamp": RTC + 9}, job=5))
    assert again["result"]["hash"] == r["result"]["hash"], again
    assert cb.sync_messages(timeout=4, want=1) == [], "only one on the air"
    d.reboot()
    replay = d.send(wrap(d.node_key, "message.send", {"to": cb.pub_key.hex(), "text": "signed", "timestamp": RTC + 9}, job=5))
    assert replay["status"] != "ok", "after a reboot the watermark refuses it: never twice"
    assert cb.sync_messages(timeout=4, want=1) == []


def test_a_caller_the_cli_table_does_not_trust_sends_nothing(nodes, ether):
    na = nodes("companion_standalone", "mu", ether_path=ether.path, seed=35, rtc=RTC).start()
    c, i = client(na)
    assert c.cli("set pin 123456").startswith("> pin is now")   # the published default PIN: an untrusted link
    c.request_reboot()
    assert na.wait_exit(15) == 75
    c.close()
    na.start()
    b = na.connect_ble(123456)
    b.app_start()
    bi = mecon_iface.IfaceClient(b._send, b._read_frame)
    tx = len(ether.tx_log)
    assert bi.request("message.send", {"channel": 0, "text": "intruder"}, rid="bad-1")["status"] == "refused"
    assert bi.request("message.get", {"message": "x"})["status"] == "refused", "an outcome names its recipient: private"
    assert bi.request("message.list")["status"] == "refused"
    assert bi.request("message.ack", {"message": "x"})["status"] == "refused"
    time.sleep(1.0)
    assert len(ether.tx_log) == tx


def test_results_and_events_follow_their_schemas(nodes, ether):
    import mecon_schema
    na, nb, ca, ia, cb = pair(nodes, ether)
    r = ia.request("message.send", {"to": cb.pub_key.hex(), "text": "schema", "timestamp": RTC + 11}, rid="sc-1")
    assert not mecon_schema.check(r["result"], mecon_schema.load("message.send.result.json")), r
    wait_state(ia, "sc-1", ("acked",), timeout=90)
    g = ia.request("message.get", {"message": "sc-1"})
    assert not mecon_schema.check(g["result"], mecon_schema.load("message.get.result.json")), g
    assert not mecon_schema.check(ia.request("message.list")["result"], mecon_schema.load("message.list.result.json"))
    a = ia.request("message.ack", {"message": "sc-1"})
    assert not mecon_schema.check(a["result"], mecon_schema.load("message.ack.result.json")), a
    evs = [e for e in ia.events if e.get("ev") == "message"]
    assert evs
    for e in evs:
        assert not mecon_schema.check(e["data"], mecon_schema.load("event.message.json")), e


def test_hello_offers_the_family(nodes, ether):
    n = nodes("companion_standalone", "mh", ether_path=ether.path, seed=34, rtc=RTC).start()
    c, i = client(n)
    caps, cursor = [], None
    while True:
        r = i.request("hello", {} if cursor is None else {"cursor": cursor})["result"]
        caps += r["capabilities"]
        cursor = r.get("next")
        if cursor is None:
            break
    for op in ("message.send", "message.get", "message.list", "message.ack"):
        assert op in caps, op
