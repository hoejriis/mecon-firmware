"""Reports kept for later (docs/contract/TELEMETRY.md, "Reports kept for later"; Issue #610): a standing instruction with `keep`
leaves its signed reports on the device when no receiver hears them, `report.list` hands them back as the exact bytes,
`report.ack` releases them, a reboot keeps them, and a full ring counts what it pushed out."""
import base64
import json
import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_report  # noqa: E402
import mecon_schema  # noqa: E402
from sim import ed25519_ref  # noqa: E402
from test_lora_management import CH, KEY, Radio, checked, enrolled, next_report, on_channel, radio_key, target, wrap  # noqa: E402
from test_deployment_authority import RTC, Dev  # noqa: E402
from sim.harness import CompanionClient  # noqa: E402
import mecon_iface  # noqa: E402

NOBODY = "ab" * 32   # a node key no radio holds: the report goes into the air and nothing hears it


def rq(d, op, args=None):
    """A request on the cable. A request of two frames can lose a byte in the simulated UART under load: it answers
    `invalid` `incomplete`, ran nothing, and the contract has the client send it again."""
    for _ in range(4):
        r = d.i.request(op, args) if args is not None else d.i.request(op)
        if not (r["status"] == "invalid" and r.get("error") == "incomplete"):
            return r
        time.sleep(1.0)
    return r


def pages(call, **args):
    """Every report the device holds, read page by page as a manager does: each page continues after the last cursor."""
    out, after = [], 0
    for _ in range(40):
        r = call(dict(args, after=after))
        assert r["status"] == "ok", r
        res = r["result"]
        assert not mecon_schema.check(res, mecon_schema.load("report.list.result.json")), res
        out += res["reports"]
        if not res["more"]:
            return out, res
        assert res["reports"], "a page that says more has a report to continue from"
        after = res["reports"][-1]["cursor"]
    pytest.fail("never ended")


def unsigned_list(d):
    return lambda args: rq(d, "report.list", args)


def verify(entry, node_key):
    raw = base64.b64decode(entry["report"])
    out, signed, sig = mecon_report.decode(raw)
    assert ed25519_ref.verify(bytes.fromhex(node_key), signed, sig), "the signature the device made when it sent it"
    assert (out["boot"], out["seq"]) == (entry["boot"], entry["seq"])
    return out


def wait_for(d, n, timeout=90):
    end = time.time() + timeout
    got = []
    while time.time() < end:
        got, _ = pages(unsigned_list(d))
        if len(got) >= n:
            return got
        time.sleep(0.5)
    pytest.fail("only %d kept reports: %s" % (len(got), got))


@pytest.mark.timescale(60)
@pytest.mark.parametrize("kind", ["repeater", "companion_standalone"])
def test_a_keeping_instruction_keeps_signed_reports_nobody_heard(ether, nodes, kind):
    d = target(nodes, ether, kind, "kp" + kind[:2])
    key = rq(d, "hello")["result"]["node_key"]
    caps = rq(d, "hello")["result"]["capabilities"]
    assert "report.list" in caps and "report.ack" in caps, caps
    r = rq(d, "standing.put", {"id": "k", "fields": ["health", "radio"], "every_s": 900, "to": NOBODY, "keep": True})
    assert r["status"] == "ok", r
    r = rq(d, "standing.put", {"id": "n", "fields": ["health"], "every_s": 900, "to": NOBODY})
    assert r["status"] == "ok", r
    got = wait_for(d, 2, timeout=100)
    cursors = [e["cursor"] for e in got]
    assert cursors == list(range(cursors[0], cursors[0] + len(cursors))) and cursors[0] == 1, cursors
    seqs = [e["seq"] for e in got]
    assert seqs == sorted(seqs) and len(set(seqs)) == len(seqs), seqs
    for e in got:
        out = verify(e, key)
        assert "health" in out and "radio" in out and "location" not in out, "only the keeping instruction's reports: %s" % out
    listed = rq(d, "standing.list")["result"]["instructions"]
    assert [s.get("keep") for s in sorted(listed, key=lambda s: s["id"])] == [True, None], listed
    assert not mecon_schema.check(rq(d, "standing.list")["result"], mecon_schema.load("standing.list.result.json"))
    assert rq(d, "report.list", {"limit": 0})["status"] == "invalid"


@pytest.mark.timescale(600)
def test_report_ack_releases_what_was_listed_and_a_reboot_keeps_the_rest(ether, nodes):
    d = target(nodes, ether, "repeater", "kpa")
    key = rq(d, "hello")["result"]["node_key"]
    assert rq(d, "standing.put", {"id": "k", "fields": ["health"], "every_s": 900, "to": NOBODY, "keep": True})["status"] == "ok"
    got = wait_for(d, 4)
    third = got[2]["cursor"]
    r = rq(d, "report.ack", {"upto": third})
    assert r["status"] == "ok" and r["result"]["acked"] == third and r["result"]["released"] == 3, r
    assert not mecon_schema.check(r["result"], mecon_schema.load("report.ack.result.json")), r
    left, _ = pages(unsigned_list(d))
    assert left and left[0]["cursor"] == third + 1, left
    again = rq(d, "report.ack", {"upto": 1})
    assert again["status"] == "ok" and again["result"]["released"] == 0, "an older ack changes nothing: %s" % again
    r = rq(d, "report.ack", {"upto": 100000})
    assert r["status"] == "failed" and r["error"] == "unknown-cursor", r
    before = [(e["cursor"], e["report"]) for e in left]
    d.reboot()
    after, _ = pages(unsigned_list(d))
    assert [(e["cursor"], e["report"]) for e in after][:len(before)] == before, "the same bytes after a reboot, and the release held"
    assert after[0]["cursor"] == third + 1
    newer = wait_for(d, len(before) + 1)
    assert newer[-1]["cursor"] > before[-1][0], "a cursor is never reused after a reboot"
    assert verify(newer[-1], key)["boot"] > verify(newer[0], key)["boot"], "and the report after the reboot names the new boot"


@pytest.mark.timescale(3000)
def test_a_full_ring_keeps_the_last_64_and_counts_what_it_pushed_out(ether, nodes):
    d = target(nodes, ether, "repeater", "kpo")
    assert rq(d, "standing.put", {"id": "k", "fields": ["health"], "every_s": 900, "to": NOBODY, "keep": True})["status"] == "ok"
    end = time.time() + 150
    got, last = [], None
    while time.time() < end:
        got, last = pages(unsigned_list(d))
        if last["dropped"] >= 3 and len(got) == 64:
            break
        time.sleep(1.0)
    assert last["dropped"] >= 3 and len(got) == 64, (len(got), last)
    # At this timescale a report is due every 0.3 s: stop the standing report so the ring holds still between the list and
    # the ack, or a report made in between pushes the oldest out and the ack releases one fewer.
    assert rq(d, "standing.remove", {"id": "k"})["status"] == "ok"
    got, last = pages(unsigned_list(d))
    assert last["dropped"] >= 3 and len(got) == 64, (len(got), last)
    first = got[0]["cursor"]
    assert first == got[-1]["cursor"] - 63, "the last 64, in order"
    assert last["dropped"] == first - 1, "what was pushed out is what the gap in the cursors shows: %s" % last
    released = rq(d, "report.ack", {"upto": first + 9})["result"]
    assert released["released"] == 10, released
    assert rq(d, "report.list", {"after": first + 9})["result"]["dropped"] == 0, "counted since the last ack"


@pytest.mark.timescale(60)
def test_the_listed_bytes_are_the_ones_that_went_on_the_air_and_the_ack_is_the_members(ether, nodes):
    d, m = enrolled(nodes, ether, "repeater", "kpl")
    r = d.send(wrap(d.node_key, "standing.put", {"id": "k", "fields": ["health", "radio"], "every_s": 900, "to": radio_key(m), "keep": True}, job=1))
    assert r["status"] == "ok", r
    live = next_report(m, timeout=40)
    assert live and checked(live, d.node_key)["health"]["uptime_s"] > 0
    got, _ = pages(unsigned_list(d))
    mine = [e for e in got if e["seq"] == mecon_report.decode(base64.b64decode(live["report"]))[0]["seq"]]
    assert len(mine) == 1 and mine[0]["report"] == live["report"], "byte for byte what the radio sent: signature included"
    # the ack is a write: it needs the member who set the instruction, signed
    assert rq(d, "report.ack", {"upto": mine[0]["cursor"]})["status"] == "refused", "an unsigned caller on an enrolled device"
    r = d.send(wrap(d.node_key, "report.ack", {"upto": mine[0]["cursor"]}, job=2))
    assert r["status"] == "ok" and r["result"]["released"] >= 1, r
    assert not [e for e in pages(unsigned_list(d))[0] if e["cursor"] <= mine[0]["cursor"]]


@pytest.mark.timescale(60)
def test_a_report_with_location_is_withheld_from_a_link_that_may_not_carry_it(ether, nodes):
    """The same signed member reads the location over the cable, and over a BLE link on the default PIN gets the page without
    it, counted in `withheld`, and cannot release what it was not shown (the link is not trusted with private data)."""
    n = nodes("companion_standalone", "kpg", seed=69, rtc=RTC, ether_path=ether.path)
    with open(n.dir + ".gps", "w") as f:
        f.write("fix 59913900 10752200 23456 9 4\n")
    n.start()
    d = Dev(n)
    assert d.c.cli("set pin 123456").startswith("> pin is now")
    assert d.provision()["status"] == "ok"
    key = d.node_key
    d.c.close()
    n.restart()
    d.attach()
    r = d.send(wrap(key, "standing.put", {"id": "g", "fields": ["health", "location"], "every_s": 900, "to": NOBODY, "keep": True}, job=1))
    assert r["status"] == "ok", r
    cable = wait_for(d, 2)
    assert verify(cable[0], key)["location"]["lat_e6"] == 59913900, "the cable reads the location"
    b = CompanionClient(n, sock=n.connect_ble(123456).sock)
    b.device_query()
    b.app_start()
    bi = mecon_iface.IfaceClient(b._send, b._read_frame)

    def signed(op, args, job):
        w = wrap(key, op, args, job=job)
        bi.send_raw(json.dumps(w, separators=(",", ":")).encode())
        return bi.wait_result(json.loads(w["body"])["id"], 8.0)

    got = signed("report.list", {}, 2)
    assert got["status"] == "ok", got
    assert got["result"]["reports"] == [], "left out whole, never cut: %s" % got
    assert got["result"]["withheld"] >= 2, got
    assert not mecon_schema.check(got["result"], mecon_schema.load("report.list.result.json"))
    ack = signed("report.ack", {"upto": cable[0]["cursor"]}, 3)
    assert ack["status"] == "refused" and ack.get("error") == "location-withheld", "this link could not list what the ack names: %s" % ack
    assert wait_for(d, 2)[0]["cursor"] == cable[0]["cursor"], "and nothing was released"


def test_keep_needs_a_destination_like_every_instruction_and_a_report_now_is_never_kept(ether, nodes):
    d = target(nodes, ether, "repeater", "kpn")
    assert rq(d, "standing.put", {"id": "k", "fields": ["health"], "every_s": 900, "keep": True})["status"] == "invalid"
    assert rq(d, "standing.put", {"id": "k", "fields": ["health"], "every_s": 900, "to": NOBODY, "keep": "yes"})["status"] == "invalid"
    assert rq(d, "report.now", {"fields": ["health"], "to": NOBODY, "keep": True})["status"] == "ok"
    assert rq(d, "report.list")["result"] == {"reports": [], "more": False, "dropped": 0, "withheld": 0}


@pytest.mark.timescale(60)
def test_report_list_answers_point_to_point_over_lora_within_a_message_and_not_on_the_channel(ether, nodes):
    d, m = on_channel(nodes, ether, "repeater", "kpc")
    assert "report.list" in rq(d, "hello")["result"]["capabilities"] and "report.ack" in rq(d, "hello")["result"]["capabilities"]
    r = d.send(wrap(d.node_key, "standing.put", {"id": "k", "fields": ["health", "radio"], "every_s": 900, "to": NOBODY, "keep": True}, job=1))
    assert r["status"] == "ok", r
    wait_for(d, 3)
    assert m.i.request("sub.start", {"stream": "rf", "lifetime_s": 600})["status"] == "ok"   # the one made at setup ran out at this scale
    r = m.send(d.node_key, wrap(d.node_key, "report.list", {}, job=2, rid="c1"), slot=CH)
    assert r and r["status"] == "unknown-op", "every member could read it, so the channel does not carry it: %s" % r
    assert m.i.request("sub.start", {"stream": "rf", "lifetime_s": 600})["status"] == "ok"
    r = m.send(d.node_key, wrap(d.node_key, "report.ack", {"upto": 1}, job=3), slot=CH)
    assert r and r["status"] == "unknown-op", r
    assert m.i.request("sub.start", {"stream": "rf", "lifetime_s": 600})["status"] == "ok"
    r = m.send(d.node_key, wrap(d.node_key, "report.list", {}, job=4, rid="c2"))
    assert r and r["status"] == "ok", "point to point it is answered: %s" % r
    got = r["result"]["reports"]
    assert 3 <= len(got) <= 4 and r["result"]["more"] is not None, "what fits one 1,024-byte message: %s" % r
    assert not mecon_schema.check(r["result"], mecon_schema.load("report.list.result.json"))
    assert verify(got[0], d.node_key)["health"]["uptime_s"] > 0
    assert len(r.raw) <= 1024
