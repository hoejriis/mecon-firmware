"""inbox.list / inbox.ack on simulated Companions (docs/contract/OPERATIONS.md section 6.4): the manager reads what was
received from its own copy, the native queue stays the stock app's, what cannot be kept is counted, and only a caller who
may read private data sees a sender or a text."""
import json
import os
import sys
import time

from sim.ether import Ether  # noqa: F401
from sim.harness import CompanionClient, MqttEnv, WifiEnv

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import mecon_iface  # noqa: E402
import mecon_schema  # noqa: E402
from test_device_interface import broker, mqtt_boot  # noqa: E402,F401  (broker is a fixture)

RTC = 1792800000
CONTACT_WRITE_S = 7   # the contact store writes five seconds after a change


def client(n):
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    return c, mecon_iface.IfaceClient(c._send, c._read_frame)


def pair(nodes, ether, a="ia", b="ib", reader_kw=None):
    """A stock sender and a receiver in radio range, each holding the other. Returns (receiver node, sender app, receiver
    app, receiver's interface client)."""
    ns = nodes("companion_standalone", a, ether_path=ether.path, seed=41, rtc=RTC).start()
    nr = nodes("companion_standalone", b, ether_path=ether.path, seed=42, rtc=RTC, **(reader_kw or {})).start()
    cs = CompanionClient(ns)
    cs.device_query()
    cs.app_start()
    cr, ir = client(nr)
    assert ether.wait_for_nodes([a, b])
    ether.set_link(a, b)
    assert cs.add_contact(cr.pub_key, "reader")[0] == 0
    assert cr.add_contact(cs.pub_key, "sender")[0] == 0
    return nr, cs, cr, ir


def wait_received(i, count, timeout=75.0):
    end = time.time() + timeout
    r = None
    while time.time() < end:
        r = i.request("status.get")["result"]["inbox"]
        if r["received"] >= count:
            return r
        time.sleep(0.3)
    raise AssertionError("received %d of %d: %s" % (r["received"], count, r))


def listed(i, **args):
    r = i.request("inbox.list", args)
    assert r["status"] == "ok", r
    return r["result"]


def test_a_direct_message_is_read_and_the_stock_app_still_has_it(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    assert listed(ir)["messages"] == [], "the first read makes the slots"
    assert cs.send_direct(cr.pub_key, "to the reader", RTC + 5)[0] != 1
    wait_received(ir, 1)
    r = listed(ir)
    assert (r["received"], r["unread"], r["lost"], r["acked"], r["more"]) == (1, 1, 0, 0, False), r
    m = r["messages"][0]
    assert m["seq"] == 1 and m["kind"] == "direct" and m["from"] == cs.pub_key.hex(), m
    assert m["text"] == "to the reader" and m["type"] == "plain" and m["ts"] == RTC + 5, m
    assert m["rx_ts"] >= RTC and m["age_s"] >= 0 and m["route"] == "flood" and len(m["hash"]) == 16, m
    assert "channel" not in m and "cut" not in m and "signer" not in m, m
    assert [x["seq"] for x in listed(ir)["messages"]] == [1], "a read takes nothing"
    # the native queue is the stock app's: the manager's read took nothing from it
    got = cr.sync_messages(timeout=5, want=1)
    assert [g["text"] for g in got] == ["to the reader"], got
    # and the stock app's read took nothing from the manager
    assert listed(ir)["messages"][0]["text"] == "to the reader"
    a = ir.request("inbox.ack", {"epoch": r["epoch"], "upto": 1})
    assert a["status"] == "ok" and a["result"]["unread"] == 0 and a["result"]["acked"] == 1, a
    assert listed(ir)["messages"] == [], "what was acknowledged is gone"
    assert ir.request("inbox.ack", {"epoch": r["epoch"], "upto": 1})["status"] == "ok", "a retry of a lost reply"
    assert listed(ir, after=0)["messages"] == [], "and is not there for any later read"


def test_a_message_acknowledged_by_the_manager_is_still_the_stock_apps(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    epoch = listed(ir)["epoch"]
    cs.send_direct(cr.pub_key, "both read this", RTC + 6)
    wait_received(ir, 1)
    assert ir.request("inbox.ack", {"epoch": epoch, "upto": 1})["status"] == "ok"
    assert [g["text"] for g in cr.sync_messages(timeout=5, want=1)] == ["both read this"]


def test_a_channel_message_names_its_slot_and_no_sender(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    listed(ir)
    cs.send_channel(0, "to everyone", RTC + 7)
    wait_received(ir, 1)
    m = listed(ir)["messages"][0]
    assert m["kind"] == "channel" and m["channel"] == 0 and m["ts"] == RTC + 7, m
    assert "from" not in m and m["text"].endswith("to everyone"), m
    assert [g["kind"] for g in cr.sync_messages(timeout=5, want=1)] == ["channel"]


def test_with_no_manager_nothing_is_kept_and_the_reception_is_counted(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    cs.send_direct(cr.pub_key, "before any manager", RTC + 8)
    assert wait_received(ir, 1) == {"received": 1, "unread": 0, "lost": 1}
    r = listed(ir)
    assert r["messages"] == [] and r["received"] == 1 and r["lost"] == 1, r
    assert [g["text"] for g in cr.sync_messages(timeout=5, want=1)] == ["before any manager"], "the stock app has it"
    assert not [e for e in ir.events if e.get("ev") == "inbox"], "nothing to tell a device with no manager"


def test_an_enrolled_device_keeps_from_the_boot_on(nodes, ether):
    from test_deployment_authority import Dev, wrap
    nr, cs, cr, ir = pair(nodes, ether)
    cr.close()
    d = Dev(nr)
    assert d.provision()["status"] == "ok"
    time.sleep(CONTACT_WRITE_S)
    nr.restart()
    d.attach()
    cs.send_direct(d.c.pub_key, "kept with no read yet", RTC + 9)
    end = time.time() + 75
    while time.time() < end and d.i.request("status.get")["result"]["inbox"]["received"] < 1:
        time.sleep(0.3)
    assert d.i.request("status.get")["result"]["inbox"] == {"received": 1, "unread": 1, "lost": 0}
    assert d.i.request("inbox.list")["result"]["messages"][0]["text"] == "kept with no read yet"


def test_a_full_inbox_counts_what_it_loses_and_the_oldest_goes(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    listed(ir)
    for k in range(9):
        cs.send_direct(cr.pub_key, "n%d" % k, RTC + 20 + k)
        wait_received(ir, k + 1)
    r = listed(ir)
    assert (r["received"], r["unread"], r["lost"]) == (9, 8, 1), r
    held = list(r["messages"])
    while r["more"]:
        r = listed(ir, after=held[-1]["seq"])
        held += r["messages"]
    assert [m["seq"] for m in held] == list(range(2, 10)), held
    assert [m["text"] for m in held] == ["n%d" % k for k in range(1, 9)], held
    evs = [e["data"] for e in ir.events if e.get("ev") == "inbox"]
    assert evs and evs[-1]["received"] <= 9, evs
    assert all(set(e) == {"received", "unread", "lost"} for e in evs), "an event names no sender and no text"


def test_a_page_stops_where_a_reply_would_overflow_and_the_next_read_continues(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    listed(ir)
    # All escapes: the longest JSON a text can make. Sent as stock frames, so nothing checks the text on the way.
    worst = '"\x01' * 80
    assert len(worst) == 160
    cs.send_direct(cr.pub_key, worst, RTC + 40)
    wait_received(ir, 1)
    cs.send_direct(cr.pub_key, "short", RTC + 41)
    wait_received(ir, 2)
    p1 = ir.request("inbox.list", {"limit": 8})
    assert p1["status"] == "ok", {k: p1.get(k) for k in ("status", "error")}
    assert [m["seq"] for m in p1["result"]["messages"]] == [1] and p1["result"]["more"] is True, p1
    assert p1["result"]["messages"][0]["text"] == worst and "cut" not in p1["result"]["messages"][0]
    p2 = listed(ir, after=1)
    assert [m["text"] for m in p2["messages"]] == ["short"] and p2["more"] is False, p2
    lim = listed(ir, limit=1)
    assert len(lim["messages"]) == 1 and lim["more"] is True, lim


def test_a_reboot_loses_the_copy_and_the_epoch_says_so(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    r = listed(ir)
    old = r["epoch"]
    cs.send_direct(cr.pub_key, "lost to a reboot", RTC + 50)
    wait_received(ir, 1)
    cr.close()
    nr.restart()
    cr, ir = client(nr)
    r = listed(ir)
    assert r["epoch"] > old and (r["received"], r["unread"], r["lost"]) == (0, 0, 0) and r["messages"] == [], r
    a = ir.request("inbox.ack", {"epoch": old, "upto": 1})
    assert a["status"] == "failed" and a["error"] == "wrong-epoch", a
    a = ir.request("inbox.ack", {"epoch": r["epoch"], "upto": 1})
    assert a["status"] == "failed" and a["error"] == "not-received", a


def test_an_ack_from_before_a_reboot_releases_nothing_received_since(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    old = listed(ir)["epoch"]
    time.sleep(CONTACT_WRITE_S)
    cr.close()
    nr.restart()
    cr, ir = client(nr)
    listed(ir)
    assert ether.wait_for_nodes(["ib"])
    cs.send_direct(cr.pub_key, "after the reboot", RTC + 60)
    wait_received(ir, 1)
    assert ir.request("inbox.ack", {"epoch": old, "upto": 1})["error"] == "wrong-epoch"
    assert listed(ir)["messages"][0]["text"] == "after the reboot"


def test_the_same_packet_heard_twice_is_one_entry_and_a_resend_is_another(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    r = nodes("repeater", "ir", ether_path=ether.path, seed=43, rtc=RTC).start()
    assert ether.wait_for_nodes(["ir"])
    ether.set_link("ia", "ir")
    ether.set_link("ib", "ir")
    listed(ir)
    tx = len(ether.tx_log)
    cs.send_direct(cr.pub_key, "heard twice", RTC + 70)
    wait_received(ir, 1)
    end = time.time() + 30
    while time.time() < end and not [t for t in ether.tx_log[tx:] if t.src == "ir"]:
        time.sleep(0.3)
    assert [t for t in ether.tx_log[tx:] if t.src == "ir"], "the repeater forwarded it, so the receiver heard it twice"
    time.sleep(3.0)
    assert ir.request("status.get")["result"]["inbox"]["received"] == 1
    assert len(cr.sync_messages(timeout=5, want=2)) == 1, "the stock app sees the same one"
    # A sender's own retry is a new packet with the same time and text, as the stock app also sees it.
    assert cs.request(bytes([2, 0, 1]) + (RTC + 70).to_bytes(4, "little") + cr.pub_key[:6] + b"heard twice")[0] != 1
    wait_received(ir, 2)
    a, b = listed(ir)["messages"]
    assert (a["ts"], a["from"], a["text"]) == (b["ts"], b["from"], b["text"]) and a["hash"] != b["hash"], (a, b)
    assert len(cr.sync_messages(timeout=5, want=1)) == 1


def test_a_caller_who_may_not_read_private_data_reads_nothing(nodes, ether):
    from test_deployment_authority import Dev, wrap
    nr, cs, cr, ir = pair(nodes, ether)
    cr.close()
    d = Dev(nr)
    assert d.c.cli("set pin 123456").startswith("> pin is now")   # the published default PIN: an untrusted link
    assert d.provision()["status"] == "ok"
    key = d.node_key
    d.c.close()
    nr.restart()
    b = CompanionClient(nr, sock=nr.connect_ble(123456).sock)
    b.device_query()
    b.app_start()
    bi = mecon_iface.IfaceClient(b._send, b._read_frame)
    assert bi.request("inbox.list")["status"] == "refused"
    assert bi.request("inbox.ack", {"epoch": 1, "upto": 1})["status"] == "refused"

    def signed(op, args, job):
        bi.send_raw(json.dumps(wrap(key, op, args, job=job), separators=(",", ":")).encode())
        return bi.wait_result("s-%d-%s" % (job, op))
    for job, (op, args) in enumerate((("inbox.list", {}), ("inbox.ack", {"epoch": 1, "upto": 1})), 1):
        r = signed(op, args, job)
        assert r["status"] == "refused" and r["error"] == "private", (op, r)


def test_a_verified_broker_reads_the_same_record_as_the_cable_and_an_unverified_one_nothing(nodes, ether, tmp_path, broker):
    b = broker()
    wenv = WifiEnv(str(tmp_path / "ib.wifi"))
    wenv.set("sim")
    nr, cs, cr, ir = pair(nodes, ether, reader_kw={"wifi": wenv, "mqtt": MqttEnv(str(tmp_path / "ib.mqtt"))})
    dev = ir.request("hello")["result"]["device_id"]
    for cmd in ["set wifi.profile 0 ssid sim", "set mqtt.profile 0 type static", "set mqtt.profile 0 host %s" % b.ip,
                "set mqtt.profile 0 port %d" % b.port, "set mqtt.profile 0 tls on",
                "set mqtt.profile 0 anchor %s" % b.fingerprint]:
        assert cr.cli(cmd).startswith(">"), cmd
    time.sleep(CONTACT_WRITE_S)
    assert cr.cli("set connectivity.mode wifi").startswith("OK")
    assert nr.wait_exit(10) == 75
    nr.start()
    cr, ir = client(nr)
    end = time.time() + 40
    while time.time() < end and not cr.cli("get mqtt.status").startswith("> connected"):
        time.sleep(0.3)
    assert cr.cli("get mqtt.status").startswith("> connected")
    assert ether.wait_for_nodes(["ib"])
    time.sleep(2.0)
    listed(ir)
    cs.send_direct(cr.pub_key, "one record", RTC + 80)
    wait_received(ir, 1)
    usb = listed(ir)["messages"][0]
    r = b.iface_request(dev, {"v": 1, "id": "ib1", "op": "inbox.list"})
    assert r["status"] == "ok", r
    over_mqtt = r["result"]["messages"][0]
    assert {k: v for k, v in usb.items() if k != "age_s"} == {k: v for k, v in over_mqtt.items() if k != "age_s"}


def test_an_unverified_broker_reads_nothing(nodes, tmp_path, broker):
    b = broker()
    n, c, i, dev = mqtt_boot(nodes, tmp_path, "ibu", b, anchor=False)   # TLS without an anchor: unverified
    r = b.iface_request(dev, {"v": 1, "id": "ib2", "op": "inbox.list"})
    assert r["status"] == "refused", r
    r = b.iface_request(dev, {"v": 1, "id": "ib3", "op": "inbox.ack", "args": {"epoch": 1, "upto": 1}})
    assert r["status"] == "refused", r


def test_results_events_and_a_hello_follow_the_contract(nodes, ether):
    nr, cs, cr, ir = pair(nodes, ether)
    r = listed(ir)
    assert not mecon_schema.check(r, mecon_schema.load("inbox.list.result.json")), r
    cs.send_direct(cr.pub_key, "schema", RTC + 90)
    wait_received(ir, 1)
    r = listed(ir)
    assert not mecon_schema.check(r, mecon_schema.load("inbox.list.result.json")), r
    a = ir.request("inbox.ack", {"epoch": r["epoch"], "upto": 1})
    assert not mecon_schema.check(a["result"], mecon_schema.load("inbox.ack.result.json")), a
    assert not mecon_schema.check(ir.request("status.get")["result"], mecon_schema.load("status.get.result.json"))
    end = time.time() + 5
    while time.time() < end and not [e for e in ir.events if e.get("ev") == "inbox"]:
        ir.request("status.get")
    evs = [e for e in ir.events if e.get("ev") == "inbox"]
    assert evs
    for e in evs:
        assert not mecon_schema.check(e["data"], mecon_schema.load("event.inbox.json")), e
    caps, cursor = [], None
    while True:
        h = ir.request("hello", {} if cursor is None else {"cursor": cursor})["result"]
        caps += h["capabilities"]
        cursor = h.get("next")
        if cursor is None:
            break
    assert "inbox.list" in caps and "inbox.ack" in caps, caps
