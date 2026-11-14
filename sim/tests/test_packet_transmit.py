"""packet.transmit (OPERATIONS.md, "Packet transmission"): a Repeater puts a manager's native packet on the air, as given. The
packet here is a real Companion channel message, taken from the air log while nobody could hear it, so the Repeater forwards
captured bytes it cannot read, and a stock Companion receives them."""
import base64
import json
import os
import sys
import time

from sim.harness import CompanionClient
from test_deployment_authority import RTC, wrap  # noqa: E402
from test_deployment_authority_server import Srv  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_schema  # noqa: E402

PAYLOAD_TYPE_GRP_TXT, PAYLOAD_TYPE_ADVERT = 5, 4


def companion(nodes, ether, name, seed):
    n = nodes("companion_standalone", name, seed=seed, rtc=RTC, ether_path=ether.path).start()
    c = CompanionClient(n)
    c.device_query()   # app protocol 3: the V3 message frames sync_messages reads
    c.app_start()
    return c


def capabilities(srv):
    """Every page of hello's capabilities."""
    caps, cursor = [], None
    for _ in range(10):
        r = srv.i.request("hello", {"cursor": cursor} if cursor is not None else None)["result"]
        caps += r["capabilities"]
        cursor = r.get("next")
        if cursor is None:
            return caps
    raise AssertionError("hello never ended")


def captured_channel_message(ether, sender, text):
    """sender's channel message on the Public channel, sent where nobody hears it: its raw bytes from the air log."""
    seen = len(ether.tx_log)
    assert sender.send_channel(0, text, RTC + 100)[0] == 0
    end = time.time() + 20
    while time.time() < end:
        for t in ether.tx_log[seen:]:
            if t.src == "pta" and (t.data[0] >> 2) & 0x0F == PAYLOAD_TYPE_GRP_TXT:
                return t.data
        time.sleep(0.1)
    raise AssertionError("the channel message never went on air")


def test_a_repeater_transmits_a_captured_packet_once_and_a_stock_companion_receives_it(ether, nodes):
    r = Srv(nodes("repeater", "ptr", seed=41, rtc=RTC, ether_path=ether.path).start())
    assert r.provision()["status"] == "ok"
    assert "packet.transmit" in capabilities(r)
    a = companion(nodes, ether, "pta", 42)
    b = companion(nodes, ether, "ptb", 43)
    ether.set_link("ptr", "ptb")                      # the Repeater reaches b; a reaches nobody
    raw = captured_channel_message(ether, a, "via the repeater")
    assert b.sync_messages(timeout=3) == [], "b could not hear a"
    w = wrap(r.node_key, "packet.transmit", {"packet": base64.b64encode(raw).decode()}, job=1)
    res = r.send(w, 10)
    assert res["status"] == "ok" and res["result"]["job_state"] == "accepted", res
    assert "packet" not in json.dumps(res["result"]), "the bytes are not echoed"
    assert not mecon_schema.check(res["result"], mecon_schema.load("packet.transmit.result.json")), res["result"]
    job = res["result"]["job"]
    got = b.sync_messages(timeout=20)
    assert got and got[0]["kind"] == "channel" and "via the repeater" in got[0]["text"], got
    end, st = time.time() + 10, None
    while time.time() < end:
        st = r.i.request("job.get", {"job": job})["result"]
        if st["state"] != "accepted" and st["state"] != "progress":
            break
        time.sleep(0.3)
    assert st["state"] == "applied" and st["result"] == {"outcome": "transmitted"}, st
    sent = [t for t in ether.tx_log if t.src == "ptr" and t.data == raw]
    assert len(sent) == 1, "on air once, exactly as supplied: %d" % len(sent)
    again = r.send(w, 10)                              # the same signed bytes: its job, not a second send
    assert again["status"] == "ok" and again["result"]["job"] == job, again
    assert not mecon_schema.check(again["result"], mecon_schema.load("packet.transmit.result.json")), again["result"]
    time.sleep(2)
    assert len([t for t in ether.tx_log if t.src == "ptr" and t.data == raw]) == 1
    other = r.send(wrap(r.node_key, "packet.transmit", {"packet": base64.b64encode(raw).decode()}, job=2), 10)
    assert other["status"] == "refused" and other["error"] == "already-heard", other


def test_only_a_signed_caller_may_and_only_allowed_well_formed_packets_go(ether, nodes):
    r = Srv(nodes("repeater", "ptq", seed=44, rtc=RTC, ether_path=ether.path).start())
    a = companion(nodes, ether, "pta", 45)
    raw = captured_channel_message(ether, a, "never sent")
    b64 = base64.b64encode(raw).decode()
    # standalone, the cable may change settings unsigned; it still may not inject RF
    u = r.i.request("packet.transmit", {"packet": b64})
    assert u["status"] == "refused" and u["error"] == "signature-required", u
    assert r.provision()["status"] == "ok"
    u = r.i.request("packet.transmit", {"packet": b64})   # enrolled: every unsigned write is refused before the operation
    assert u["status"] == "refused" and u["error"] == "unauthenticated", u
    advert = bytes([(raw[0] & 0xC3) | (PAYLOAD_TYPE_ADVERT << 2)]) + raw[1:]
    x = r.send(wrap(r.node_key, "packet.transmit", {"packet": base64.b64encode(advert).decode()}, job=3), 10)
    assert x["status"] == "invalid" and x["error"] == "unsupported-type", x
    y = r.send(wrap(r.node_key, "packet.transmit", {"packet": base64.b64encode(raw[:2]).decode()}, job=4), 10)
    assert y["status"] == "invalid" and y["error"] == "malformed", y
    z = r.send(wrap(r.node_key, "packet.transmit", {"packet": b64, "within_s": 600}, job=5), 10)
    assert z["status"] == "invalid", z
    assert not [t for t in ether.tx_log if t.src == "ptq" and t.data in (raw, advert)], "nothing refused went on air"


def test_the_room_server_does_not_offer_it(ether, nodes):
    s = Srv(nodes("room_server", "pts", seed=46, rtc=RTC, ether_path=ether.path).start())
    assert "packet.transmit" not in capabilities(s)
    assert s.i.request("packet.transmit", {"packet": "AAAA"})["error"] == "not-on-this-device"
