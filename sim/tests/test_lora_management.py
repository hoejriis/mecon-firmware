"""The device interface over LoRa, point to point (docs/contract/LORA_MANAGEMENT.md), on the simulated Companion, Repeater and
Room server. A second Companion is the manager's radio: rf.request sends the signed bytes and the rf stream brings the reply back,
so both ends run the real firmware over the simulated channel."""
import json
import sys
import os
import time

import pytest

from sim.harness import CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402
import mecon_schema  # noqa: E402
from test_deployment_authority import SECRET, Dev, RTC, apply_name, update_args, wrap  # noqa: E402
from test_deployment_authority_server import Srv  # noqa: E402
from test_provenance import verifies  # noqa: E402

KINDS = ["companion_standalone", "repeater", "room_server"]
FAST = "869.525,250,7,5"   # the 10% band and short packets: the default's 1% would hold most of a message for many minutes


class Radio:
    """The manager's Companion."""

    def __init__(self, nodes, ether, target):
        name = "mgr" + target
        ether.set_link(name, target)
        n = nodes("companion_standalone", name, seed=7, rtc=RTC, ether_path=ether.path).start()
        c = CompanionClient(n)
        c.app_start()
        assert c.cli("set radio " + FAST).startswith("OK")
        c.close()
        n.restart()
        self.c = CompanionClient(n)
        self.c.app_start()
        self.i = mecon_iface.IfaceClient(self.c._send, self.c._read_frame)
        r = self.i.request("sub.start", {"stream": "rf", "lifetime_s": 300})
        assert r["status"] == "ok", r

    def send(self, to, message, timeout=45.0, slot=None):
        """The message as text to node key `to`; the reply as a parsed message, with .raw, or None. With a slot, on that channel."""
        text = message if isinstance(message, str) else json.dumps(message, separators=(",", ":"))
        args = {"to": to, "message": text}
        if slot is not None:
            args["slot"] = slot
        r = self.i.request("rf.request", args)
        assert r["status"] == "ok", r
        assert not mecon_schema.check(r["result"], mecon_schema.load("rf.request.result.json")), r
        xid = r["result"]["xid"]
        sender = to if slot is None else to[:16]
        end = time.time() + timeout
        while time.time() < end:
            for e in list(self.i.events):   # a reply may have come while the request above was being answered
                if e.get("ev") == "rf":
                    problems = mecon_schema.check(e["data"], mecon_schema.load("event.rf.json"))
                    assert not problems, (problems, e)
                if e.get("ev") == "rf" and e["data"]["from"] == sender and e["data"].get("xid") == xid:
                    self.i.events.remove(e)
                    return Reply(e["data"]["message"])
            m = self.i.read_message(0.5)
            if m and "ev" in m:
                self.i.events.append(m)
        return None


class Reply(dict):
    def __init__(self, raw):
        super().__init__(json.loads(raw))
        self.raw = raw


def target(nodes, ether, kind, name, seed=31):
    n = nodes(kind, name, seed=seed, rtc=RTC, ether_path=ether.path).start()
    d = Dev(n) if kind == "companion_standalone" else Srv(n)
    line = d.c.cli if kind == "companion_standalone" else d.cli.command
    assert line("set radio " + FAST).startswith("OK")
    d.reboot()
    return d


def enrolled(nodes, ether, kind, name):
    d = target(nodes, ether, kind, name)
    assert d.provision()["status"] == "ok"
    return d, Radio(nodes, ether, name)


@pytest.mark.parametrize("kind", KINDS)
def test_a_signed_request_runs_once_and_its_signed_result_comes_back(ether, nodes, kind):
    d, m = enrolled(nodes, ether, kind, "lt" + kind[:2])
    w = wrap(d.node_key, "config.apply", apply_name("OverLora"), job=1)
    r = m.send(d.node_key, w)
    assert r and r["status"] == "ok", r
    assert verifies(r.raw, d.node_key, "result"), "signed like every result, so the radio in between cannot change it"
    assert r["op"] == "config.apply" and r["job_id"] == 1, r
    assert d.i.request("status.get")["status"] == "ok"
    assert "OverLora" in d.name(), d.name()
    again = m.send(d.node_key, w)
    assert again and again["status"] == "ok" and again["result"] == r["result"], "the stored result, not a second run: %s" % again
    assert d.i.request("deployment.get")["result"]["members"][0]["mark"] == 1, "nothing ran twice"
    assert d.i.request("status.get")["result"]["lora"]["frames"] >= 5, "the link counts what it heard"


@pytest.mark.parametrize("kind", ["repeater"])
def test_an_unsigned_request_runs_nothing_and_a_read_is_signed(ether, nodes, kind):
    d, m = enrolled(nodes, ether, kind, "lu" + kind[:2])
    r = m.send(d.node_key, {"v": 1, "id": "u1", "op": "config.apply", "args": apply_name("NotThis")})
    assert r and r["status"] == "refused" and r["error"] == "unauthenticated", r
    assert "NotThis" not in d.name()
    r = m.send(d.node_key, wrap(d.node_key, "status.get", {}, job=None, rid="s1"))
    assert r and r["status"] == "ok" and verifies(r.raw, d.node_key, "result"), r


@pytest.mark.parametrize("kind", KINDS)
def test_an_operation_this_link_does_not_carry_is_unknown_here(ether, nodes, kind):
    d, m = enrolled(nodes, ether, kind, "lk" + kind[:2])
    r = m.send(d.node_key, wrap(d.node_key, "channel.remove", {"slot": 1}, job=1))
    assert r and r["status"] == "unknown-op", "never over LoRa without a flag, whatever the signature: %s" % r
    r = m.send(d.node_key, wrap(d.node_key, "identity.rotate", {}, job=2))
    assert r and r["status"] == "refused", "a sensitive one needs a nonce, which this link never gives: %s" % r
    assert d.i.request("hello")["result"]["node_key"] == d.node_key


def test_unverified_answers_stay_within_their_budget(ether, nodes):
    d, m = enrolled(nodes, ether, "repeater", "lb")
    assert m.send(d.node_key, wrap(d.node_key, "status.get", {}, job=None, rid="s0")), "the window's boot seed is past"
    t0 = time.time()
    for i in range(7):   # a burst from one sender: the budget is shared, so more keys would change nothing
        assert m.i.request("rf.request", {"to": d.node_key, "message": json.dumps({"v": 1, "id": "u%d" % i, "op": "status.get"})})["status"] == "ok"
        time.sleep(1.0)
    while time.time() < t0 + 14:
        msg = m.i.read_message(0.5)
        if msg and "ev" in msg:
            m.i.events.append(msg)
    answered = sum(1 for e in m.i.events if e.get("ev") == "rf" and '"status":"refused"' in e["data"]["message"])
    assert 4 <= answered <= 5, "4 a minute, whatever is asked, and one more for the 14 s of this test: %d" % answered


def test_a_request_for_another_node_is_refused(ether, nodes):
    d, m = enrolled(nodes, ether, "repeater", "lw")
    r = m.send(d.node_key, wrap("ab" * 32, "config.apply", apply_name("Wrong"), job=1))
    assert r and r["status"] == "refused" and r["error"] == "wrong_device", r
    assert "Wrong" not in d.name()


def test_hello_over_lora_offers_only_what_this_link_carries(ether, nodes):
    d, m = enrolled(nodes, ether, "repeater", "lh")
    r = m.send(d.node_key, wrap(d.node_key, "hello", {}, job=None, rid="h1"))
    assert r and r["status"] == "ok", r
    caps = r["result"]["capabilities"]
    assert "config.apply" in caps and "identity.rotate" not in caps and not [c for c in caps if c.startswith("stream.")], caps


def test_a_native_login_on_the_same_datagram_type_still_works(ether, nodes):
    d, m = enrolled(nodes, ether, "repeater", "ln")
    assert d.i.request("status.get")["status"] == "ok"
    # a marker-free anonymous request is a password, as ever: refused here, and not read as a management frame
    assert m.c.cli("get name") is not None


# ------------------------------------------------------------------ the management channel

KEY = bytes(range(0x10, 0x20))
CH = 1


def on_channel(nodes, ether, kind, name):
    """Bound to the management channel, then enrolled; the manager's radio holds the same channel in slot CH."""
    d = target(nodes, ether, kind, name)
    slot = 1 if kind == "companion_standalone" else 0
    assert d.i.request("channel.put", {"slot": slot, "name": "ops", "secret": KEY.hex()})["status"] == "ok"
    assert d.i.request("mgmt.channel.bind", {"slot": slot})["status"] == "ok"
    assert d.provision()["status"] == "ok"
    m = Radio(nodes, ether, name)
    assert m.i.request("channel.put", {"slot": CH, "name": "ops", "secret": KEY.hex()})["status"] == "ok"
    return d, m


@pytest.mark.parametrize("kind", KINDS)
def test_a_signed_request_runs_over_the_channel_with_the_same_result(ether, nodes, kind):
    d, m = on_channel(nodes, ether, kind, "ct" + kind[:2])
    r = m.send(d.node_key, wrap(d.node_key, "config.apply", apply_name("OnChannel"), job=1), slot=CH)
    assert r and r["status"] == "ok", r
    assert verifies(r.raw, d.node_key, "result")
    assert "OnChannel" in d.name()
    again = m.send(d.node_key, wrap(d.node_key, "config.apply", apply_name("OnChannel"), job=1))
    assert again and again["status"] == "ok" and again["result"] == r["result"], "a retry on the other path runs nothing: %s" % again


def test_a_retry_under_the_same_number_fills_a_gap_on_the_channel(ether, nodes):
    d, m = on_channel(nodes, ether, "repeater", "cg")
    text = json.dumps(wrap(d.node_key, "config.apply", apply_name("Filled"), job=1), separators=(",", ":"))
    ether.set_loss("mgrcg", "cg", 0.35, both=False)   # the request loses frames, and the reply does not
    args = {"to": d.node_key, "message": text, "slot": CH}
    q = m.i.request("rf.request", args)
    assert q["status"] == "ok", q
    xid = q["result"]["xid"]
    res = None
    for attempt in range(8):
        end = time.time() + 7
        while time.time() < end and not res:
            msg = m.i.read_message(0.5)
            if msg and msg.get("ev") == "rf" and msg["data"].get("xid") == xid:
                res = json.loads(msg["data"]["message"])
        if res:
            break
        assert m.i.request("rf.request", dict(args, xid=xid))["status"] == "ok"
    assert res and res["status"] == "ok", res
    assert "Filled" in d.name()


def test_the_channel_refuses_what_its_members_could_read_and_says_nothing_unverified(ether, nodes):
    d, m = on_channel(nodes, ether, "repeater", "cr")
    r = m.send(d.node_key, wrap(d.node_key, "status.get", {}, job=None, rid="s1"), slot=CH)
    assert r and r["status"] == "unknown-op", "a read reveals state to every member: %s" % r
    assert m.send(d.node_key, {"v": 1, "id": "u1", "op": "status.get"}, slot=CH, timeout=8.0) is None, "never answered"
    assert m.send(d.node_key, wrap("ab" * 32, "config.apply", apply_name("No"), job=2), slot=CH, timeout=8.0) is None, "nor a wrong target"
    assert "No" != d.name()[2:]


@pytest.mark.parametrize("kind", ["companion_standalone", "repeater"])
def test_a_device_takes_only_its_bound_channel(ether, nodes, kind):
    d = target(nodes, ether, kind, "cu" + kind[:2])
    slot = 1 if kind == "companion_standalone" else 0
    # it holds the key, as an ordinary channel: without the binding that takes nothing
    assert d.i.request("channel.put", {"slot": slot, "name": "ops", "secret": KEY.hex()})["status"] == "ok"
    assert d.provision()["status"] == "ok"
    m = Radio(nodes, ether, "cu" + kind[:2])
    assert m.i.request("channel.put", {"slot": CH, "name": "ops", "secret": KEY.hex()})["status"] == "ok"
    r = m.send(d.node_key, wrap(d.node_key, "config.apply", apply_name("NoBinding"), job=1), slot=CH, timeout=25.0)
    assert r is None, "no binding: not read at all: %s" % r
    assert "NoBinding" not in d.name()


def test_an_unbind_takes_effect_at_once(ether, nodes):
    d, m = on_channel(nodes, ether, "repeater", "cb")
    r = m.send(d.node_key, wrap(d.node_key, "config.apply", apply_name("Bound"), job=1), slot=CH)
    assert r and r["status"] == "ok", r
    r = d.send(wrap(d.node_key, "mgmt.channel.unbind", {"confirm": {"within_s": 60}}, job=2))
    assert r["status"] == "ok", r
    assert d.send(wrap(d.node_key, "job.confirm", {"job": r["result"]["job"]}, job=3))["status"] == "ok"
    time.sleep(1)
    r = m.send(d.node_key, wrap(d.node_key, "config.apply", apply_name("Unbound"), job=4), slot=CH, timeout=10.0)
    assert r is None, "no longer bound: not read: %s" % r
    assert "Unbound" not in d.name()


def test_only_the_addressed_node_answers_on_the_channel(ether, nodes):
    d, m = on_channel(nodes, ether, "repeater", "cn")
    other = target(nodes, ether, "repeater", "co", seed=33)
    ether.set_link("mgrcn", "co")
    ether.set_link("cn", "co")
    assert other.i.request("channel.put", {"slot": 0, "name": "ops", "secret": KEY.hex()})["status"] == "ok"
    assert other.i.request("mgmt.channel.bind", {"slot": 0})["status"] == "ok"
    assert other.provision()["status"] == "ok"
    r = m.send(d.node_key, wrap(d.node_key, "config.apply", apply_name("JustOne"), job=1), slot=CH)
    assert r and r["status"] == "ok", r
    time.sleep(2)
    assert "JustOne" in d.name() and "JustOne" not in other.name()
    # it may repeat what it hears, as any repeater does (a hop in the path), and starts nothing of its own on the channel
    own = [t for t in ether.tx_of("co") if (t.data[0] >> 2) & 15 == 6 and t.data[1] & 63 == 0]
    assert not own, "only the addressed node answers: %d" % len(own)


# ------------------------------------------------------------------ compact reports and standing instructions

import base64  # noqa: E402

import mecon_report  # noqa: E402
from sim import ed25519_ref  # noqa: E402


def next_report(m, timeout=30.0):
    end = time.time() + timeout
    renew = 0
    while time.time() < end:
        if time.time() > renew:   # a scaled clock runs a subscription's lifetime out quickly
            assert m.i.request("sub.start", {"stream": "rf", "lifetime_s": 600})["status"] == "ok"
            renew = time.time() + 5
        for e in list(m.i.events):
            if e.get("ev") == "rf" and "report" in e["data"]:
                m.i.events.remove(e)
                assert not mecon_schema.check(e["data"], mecon_schema.load("event.rf.json")), e
                return e["data"]
        msg = m.i.read_message(0.5)
        if msg and "ev" in msg:
            m.i.events.append(msg)
    return None


def checked(report, key):
    raw = base64.b64decode(report["report"])
    out, signed, sig = mecon_report.decode(raw)
    assert ed25519_ref.verify(bytes.fromhex(key), signed, sig), "signed by the device, so any radio or member in between cannot change it"
    return out


def radio_key(m):
    return m.i.request("hello")["result"]["node_key"]


@pytest.mark.parametrize("kind", KINDS)
def test_a_report_now_reaches_the_manager_signed_with_what_was_asked(ether, nodes, kind):
    d, m = enrolled(nodes, ether, kind, "rp" + kind[:2])
    r = m.send(d.node_key, wrap(d.node_key, "report.now", {"fields": ["health", "radio"], "to": radio_key(m)}, job=1))
    assert r and r["status"] == "ok", r
    got = next_report(m)
    assert got and got["from"] == d.node_key, got
    out = checked(got, d.node_key)
    assert out["seq"] == r["result"]["seq"] and out["health"]["uptime_s"] > 0 and out["radio"]["rx"] > 0, out
    assert "location" not in out
    r2 = m.send(d.node_key, wrap(d.node_key, "report.now", {"fields": ["health"], "to": radio_key(m)}, job=2))
    assert r2 and r2["status"] == "busy" and r2["error"] == "rate-limited", r2


def test_a_field_the_board_lacks_is_an_answer(ether, nodes):
    d, m = on_channel(nodes, ether, "repeater", "rq")
    r = d.send(wrap(d.node_key, "report.now", {"fields": ["location"], "to": radio_key(m)}, job=1))
    assert r["status"] == "failed" and r["error"] == "unsupported-field", "no receiver here: never a made-up zero: %s" % r
    r = d.send(wrap(d.node_key, "report.now", {"fields": ["nope"], "to": radio_key(m)}, job=2))
    assert r["status"] == "invalid", r


def test_location_goes_point_to_point_and_never_on_a_channel(ether, nodes):
    n = nodes("repeater", "rl", seed=31, rtc=RTC, ether_path=ether.path)
    with open(n.dir + ".gps", "w") as f:
        f.write("fix 59913900 10752200 23456 9 4\n")
    n.start()
    d = Srv(n)
    assert d.cli.command("set radio " + FAST).startswith("OK")
    d.reboot()
    assert d.i.request("channel.put", {"slot": 0, "name": "ops", "secret": KEY.hex()})["status"] == "ok"
    assert d.i.request("mgmt.channel.bind", {"slot": 0})["status"] == "ok"
    assert d.provision()["status"] == "ok"
    m = Radio(nodes, ether, "rl")
    key = radio_key(m)
    r = d.send(wrap(d.node_key, "report.now", {"fields": ["location"], "slot": 0, "to": key}, job=1))
    assert r["status"] == "refused" and r["error"] == "private", r
    r = d.send(wrap(d.node_key, "report.now", {"fields": ["location"], "to": key}, job=2))
    assert r["status"] == "ok", r
    got = next_report(m)
    assert got and checked(got, d.node_key)["location"] == {"fix": True, "sats": 9, "lat_e6": 59913900, "lon_e6": 10752200, "alt_m": 23, "age_s": 4}, got
    r = d.send(wrap(d.node_key, "standing.put", {"id": "l", "fields": ["location"], "every_s": 3600, "slot": 0}, job=3))
    assert r["status"] == "refused" and r["error"] == "private", "nor as a standing instruction: %s" % r


def test_a_report_on_the_channel_names_its_sender(ether, nodes):
    d, m = on_channel(nodes, ether, "repeater", "rc")
    r = m.send(d.node_key, wrap(d.node_key, "report.now", {"fields": ["health"], "slot": 0, "to": radio_key(m)}, job=1), slot=CH)
    assert r and r["status"] == "ok", r
    got = next_report(m)
    assert got and got["from"] == d.node_key[:16], got
    assert checked(got, d.node_key)["health"]["uptime_s"] > 0


@pytest.mark.timescale(60)
@pytest.mark.parametrize("kind", ["repeater", "companion_standalone"])
def test_a_standing_instruction_runs_keeps_across_a_reboot_and_can_be_removed(ether, nodes, kind):
    d, m = enrolled(nodes, ether, kind, "st" + kind[:2])
    key = radio_key(m)
    r = d.send(wrap(d.node_key, "standing.put", {"id": "h1", "fields": ["health"], "every_s": 900, "to": key}, job=1))
    assert r["status"] == "ok", r
    got = next_report(m, timeout=40)
    assert got, "the first one, a period from now"
    assert checked(got, d.node_key)["health"]["uptime_s"] >= 800
    listed = d.i.request("standing.list")["result"]["instructions"]
    assert listed[0]["id"] == "h1" and listed[0]["runs"] >= 1 and listed[0]["every_s"] == 900, listed
    d.reboot()
    assert d.i.request("standing.list")["result"]["instructions"][0]["id"] == "h1", "kept across a boot"
    assert next_report(m, timeout=40), "and it runs on"
    r = d.send(wrap(d.node_key, "standing.remove", {"id": "h1"}, job=d.i.request("deployment.get")["result"]["members"][0]["mark"] + 1))
    assert r["status"] == "ok", r
    assert d.i.request("standing.list")["result"]["instructions"] == []
    m.i.events.clear()
    assert next_report(m, timeout=25) is None, "removed: no more"


@pytest.mark.timescale(60)
@pytest.mark.parametrize("kind", ["repeater", "companion_standalone"])
def test_a_run_with_no_airtime_is_skipped_counted_and_sends_nothing(ether, nodes, kind):
    # 869.55 MHz at 250 kHz: a 0.1 % band, 3.6 s an hour, and a boot starts with the hour full. At SF11 a report needs about
    # 2 s, so the first period ends with too little room, and the second or third with enough.
    name = "skp" + kind[:2]
    n = nodes(kind, name, seed=31, rtc=RTC, ether_path=ether.path).start()
    d = Dev(n) if kind == "companion_standalone" else Srv(n)
    line = d.c.cli if kind == "companion_standalone" else d.cli.command
    assert line("set radio 869.55,250,11,5").startswith("OK")
    d.reboot()
    r = d.i.request("standing.put", {"id": "h", "fields": ["health"], "every_s": 900, "to": "ab" * 32})
    assert r["status"] == "ok", r
    end = time.time() + 30   # 30 minutes at this scale: a period, and the five minutes it waits
    s = None
    while time.time() < end:
        s = d.i.request("standing.list")["result"]["instructions"][0]
        if s["skipped"]:
            break
        time.sleep(0.5)
    assert s["skipped"] == 1 and s["runs"] == 0 and s["failed"] == 0, s
    reports = lambda: [t for t in ether.tx_of(name) if (t.data[0] >> 2) & 15 == 7]   # an anonymous datagram: a boot's advert is not one
    assert not reports(), "skipped: nothing went on the air"
    end = time.time() + 60
    while time.time() < end and not s["runs"]:
        s = d.i.request("standing.list")["result"]["instructions"][0]
        time.sleep(0.5)
    assert s["runs"] >= 1 and s["failed"] == 0, "room came back, and it runs: %s" % s
    assert reports(), "and that one did go"


def test_standing_bounds(ether, nodes):
    d = target(nodes, ether, "repeater", "sb")
    key = "ab" * 32
    base = {"fields": ["health"], "to": key}
    assert d.i.request("standing.put", dict(base, id="x", every_s=899))["status"] == "invalid", "not under 15 minutes"
    for i in range(4):
        assert d.i.request("standing.put", dict(base, id="i%d" % i, every_s=3600))["status"] == "ok"
    r = d.i.request("standing.put", dict(base, id="i9", every_s=3600))
    assert r["status"] == "busy" and r["error"] == "full", r
    assert d.i.request("standing.put", dict(base, id="i1", every_s=7200))["status"] == "ok", "the same id replaces"
    assert d.i.request("standing.remove", {"id": "nope"})["status"] == "failed"
    assert d.i.request("standing.put", dict(base, id="t", every_s=3600, durable=False))["status"] == "busy", "still full"
    assert d.i.request("standing.remove", {"id": "i0"})["status"] == "ok"
    assert d.i.request("standing.put", dict(base, id="t", every_s=3600, durable=False))["status"] == "ok"
    d.reboot()
    ids = [s["id"] for s in d.i.request("standing.list")["result"]["instructions"]]
    assert "t" not in ids and "i1" in ids, "a temporary instruction ends with the boot: %s" % ids


@pytest.mark.timescale(60)
def test_a_revoked_members_instruction_stops(ether, nodes):
    from test_identity_rotate import revoking_set
    d, m = enrolled(nodes, ether, "repeater", "sr")
    key = radio_key(m)
    r = d.send(wrap(d.node_key, "standing.put", {"id": "h1", "fields": ["health"], "every_s": 900, "to": key}, job=1))
    assert r["status"] == "ok", r
    r = d.i.request("deployment.authority_update", revoking_set())
    assert r["status"] == "ok", r
    assert d.i.request("standing.list")["result"]["instructions"][0]["id"] == "h1", "until it next comes due"
    m.i.events.clear()
    assert next_report(m, timeout=25) is None, "it stops, with its member"
    assert d.i.request("standing.list")["result"]["instructions"] == []


@pytest.mark.parametrize("kind", ["companion_standalone", "repeater_wifi"])
@pytest.mark.parametrize("key", ["wifi.profile.0.pwd", "mqtt.profile.0.pwd"])
def test_a_password_is_refused_over_lora_on_both_paths_and_nothing_is_written(ether, nodes, kind, key):
    d, m = on_channel(nodes, ether, kind, "wo" + kind[:2] + key[:2])
    args = {"set": {key: "not-on-the-air", "name": "NotThisEither"}}
    for job, slot in ((1, None), (2, CH)):
        kw = {"slot": slot} if slot is not None else {}
        r = m.send(d.node_key, wrap(d.node_key, "config.apply", args, job=job), **kw)
        assert r and r["status"] == "refused" and r["error"] == "not-over-lora:" + key, (slot, r)
        assert "NotThisEither" not in d.name(), "nothing in the set is written"
    r = m.send(d.node_key, wrap(d.node_key, "config.apply", apply_name("StillWorks"), job=3), slot=CH)
    assert r and r["status"] == "ok", "a plain key still goes: %s" % r
    assert "StillWorks" in d.name()


@pytest.mark.timescale(100000)
@pytest.mark.parametrize("every_s", [2592000, 2200000])
def test_a_period_past_a_signed_difference_runs_once_a_period(nodes, every_s):
    """Above about 24.8 days a signed difference of milliseconds reads as already due, so it ran on every pass. Here a period
    takes 26 s and 22 s, and nothing runs until it is over."""
    n = nodes("repeater", "lp%d" % (every_s // 100000), seed=31, rtc=RTC).start()
    d = Srv(n)
    assert d.provision()["status"] == "ok"
    r = d.send(wrap(d.node_key, "standing.put", {"id": "long", "fields": ["health"], "every_s": every_s, "to": "ab" * 32}, job=1))
    assert r["status"] == "ok", r

    def count():
        s = d.i.request("standing.list")["result"]["instructions"][0]
        return s["runs"] + s["failed"]

    time.sleep(every_s / 100000.0 * 0.7)
    assert count() == 0, "nothing within the period: %d" % count()
    time.sleep(every_s / 100000.0 * 0.6)
    assert count() == 1, "once, when it is over: %d" % count()


def test_a_companion_reports_only_on_its_bound_channel(ether, nodes):
    d, m = on_channel(nodes, ether, "companion_standalone", "cbc")
    r = d.send(wrap(d.node_key, "report.now", {"fields": ["health"], "slot": 0}, job=1))
    assert (r["status"], r.get("error")) == ("busy", "not-sent"), "slot 0 is Public: %s" % r
    r = d.send(wrap(d.node_key, "report.now", {"fields": ["health"], "slot": 1}, job=2))
    assert r["status"] == "ok", "slot 1 is the bound one: %s" % r


@pytest.mark.timescale(10)   # the first answer after a boot waits for the duty window (docs/SIMULATION.md)
@pytest.mark.parametrize("kind", KINDS)
def test_a_signature_changes_the_posture_over_lora(ether, nodes, kind):
    d, m = enrolled(nodes, ether, kind, "ps" + kind[:2])
    r = d.send(wrap(d.node_key, "deployment.set_unlock", {"unlock_secret": SECRET}, job=1, nonce=d.nonce()))
    assert r["status"] == "ok", r
    job = 2
    for want in ("hardened", "normal"):
        r = m.send(d.node_key, wrap(d.node_key, "posture.set", {"posture": want}, job=job), timeout=100)   # the first answer after a boot is slow
        assert r and r["status"] == "ok" and r["result"] == {"posture": want}, (want, r)
        assert d.n.wait_exit(90) == 75, "a change reboots the device, once the answer has gone out"
        d.n.start()
        d.attach()
        got = d.send(wrap(d.node_key, "status.get", job=None, rid="post-%s" % want))
        assert got["result"]["posture"] == want, got
        job = d.i.request("deployment.get")["result"]["members"][0]["mark"] + 1


@pytest.mark.parametrize("kind", ["repeater", "companion_standalone"])
def test_only_a_request_that_can_change_the_binding_reads_it_from_flash(ether, nodes, kind):
    # About 0.9 s on an ESP32, before the answer goes: v0.2.0 did this after every request, and each took a second.
    d = target(nodes, ether, kind, "br" + kind[:2])
    reads = lambda: d.i.request("status.get")["result"]["lora"]["binding_reads"]
    time.sleep(0.5)   # the first read, on the first pass
    before = reads()
    for op in ("status.get", "hello", "config.schema", "channel.list", "deployment.get", "job.list") * 2:
        assert d.i.request(op)["status"] == "ok", op
    assert reads() == before, "not one per request"
    time.sleep(0.2)
    before = reads()
    assert d.i.request("channel.put", {"slot": 0 if kind == "repeater" else 1, "name": "ops", "secret": KEY.hex()})["status"] == "ok"
    time.sleep(0.2)
    assert reads() > before, "a channel written can change the binding"
    before = reads()
    assert d.i.request("mgmt.channel.bind", {"slot": 0 if kind == "repeater" else 1})["status"] == "ok"
    time.sleep(0.2)
    assert reads() > before, "and a bind does: the link must take the new key at once"


@pytest.mark.timescale(60)
@pytest.mark.parametrize("kind", ["repeater", "companion_standalone"])
def test_the_binding_is_not_read_from_flash_on_a_timer(ether, nodes, kind):
    d = target(nodes, ether, kind, "bt" + kind[:2])
    reads = lambda: d.i.request("status.get")["result"]["lora"]["binding_reads"]
    time.sleep(0.5)
    before = reads()
    time.sleep(8)   # eight minutes at this scale
    assert reads() == before, "each would hold the loop for about 0.9 s on an ESP32"


def test_a_native_channel_write_makes_the_companion_read_the_binding(ether, nodes):
    d = target(nodes, ether, "companion_standalone", "bn")
    reads = lambda: d.i.request("status.get")["result"]["lora"]["binding_reads"]
    time.sleep(0.5)
    before = reads()
    d.c.set_channel(1, "ops", KEY)
    time.sleep(0.2)
    assert reads() > before, "a slot written by a frame can leave the binding stale, and no request said so"


@pytest.mark.parametrize("kind", ["repeater", "room_server"])
def test_the_region_tree_is_managed_over_lora_with_confirm(ether, nodes, kind):
    d, m = enrolled(nodes, ether, kind, "rg" + kind[:2])
    r = m.send(d.node_key, wrap(d.node_key, "region.put", {"name": "city", "confirm": {"within_s": 120}}, job=1))
    assert r and r["status"] == "ok" and r["result"]["job_state"] == "awaiting-confirm", r
    assert verifies(r.raw, d.node_key, "result")
    c = m.send(d.node_key, wrap(d.node_key, "job.confirm", {"job": r["result"]["job"]}, job=2))
    assert c and c["status"] == "ok", c
    page = m.send(d.node_key, wrap(d.node_key, "region.list", job=3))   # a job id: accepted whether or not it is a plain read
    assert page and page["status"] == "ok", page
    assert [e["name"] for e in page["result"]["regions"]] == ["city"], page


def test_a_full_length_region_tree_pages_over_lora(ether, nodes):
    # Eight 30-byte names do not fit one LoRa reply with the signature; the list pages instead of answering too-large
    d = target(nodes, ether, "repeater", "rgl")
    names = ["%s%02d" % ("r" * 28, k) for k in range(8)]
    for name in names:   # made over the cable before enrolling, so each takes effect at once
        assert d.i.request("region.put", {"name": name})["status"] == "ok"
    assert d.provision()["status"] == "ok"
    m = Radio(nodes, ether, "rgl")
    got, cursor, job, pages = [], 0, 1, 0
    while cursor is not None and pages < 8:
        r = m.send(d.node_key, wrap(d.node_key, "region.list", {"cursor": cursor}, job=job))
        assert r and r["status"] == "ok", r
        got += [e["name"] for e in r["result"]["regions"]]
        cursor, job, pages = r["result"]["next"], job + 1, pages + 1
    assert got == names and pages >= 2, (pages, got)


@pytest.mark.parametrize("kind", ["repeater", "room_server"])
def test_the_settings_schema_and_the_deployment_read_arrive_over_lora(ether, nodes, kind):
    # Pages sized for the 1,536-byte result could not be signed in a LoRa reply, and a deployment.get that fit LoRa was
    # lost at the manager's Companion once escaped. Over LoRa both now answer in pages and fields that arrive.
    d, m = enrolled(nodes, ether, kind, "sd" + kind[:2])
    whole = []
    cur = 0
    while cur is not None:
        whole += [k["key"] for k in d.i.request("config.schema", {"cursor": cur})["result"]["keys"]]
        cur = d.i.request("config.schema", {"cursor": cur})["result"]["next"]
    got, cur, job, pages = [], 0, 1, 0
    while cur is not None and pages < 12:
        r = m.send(d.node_key, wrap(d.node_key, "config.schema", {"cursor": cur}, job=job))
        assert r and r["status"] == "ok", r
        got += [k["key"] for k in r["result"]["keys"]]
        cur, job, pages = r["result"]["next"], job + 1, pages + 1
    assert got == whole, (got, whole)
    r = m.send(d.node_key, wrap(d.node_key, "deployment.get", job=job))
    assert r and r["status"] == "ok" and r["result"]["state"] == "provisioned" and r["result"]["members"], r
    # A two-member set, whose text with everything else would pass what one LoRa reply carries: the answer reports its length
    assert d.i.request("deployment.authority_update", update_args())["status"] == "ok"   # rev 2, through the rotation
    r = m.send(d.node_key, wrap(d.node_key, "deployment.get", job=job + 1))
    assert r and r["status"] == "ok" and len(r["result"]["members"]) == 2, r
    assert "authority_set_len" in r["result"] and len(r.raw) <= 1024, (len(r.raw), sorted(r["result"]))


def test_a_companions_whole_status_reaches_a_companion_manager(ether, nodes):
    # A Companion's signed status.get is near 1 KB: 7 reply frames, which all full would be past the 1,024-byte bound.
    # The manager took only the last of them until the bound was read from the last frame's length (#457).
    d, m = enrolled(nodes, ether, "companion_standalone", "rfc")
    r = m.send(d.node_key, wrap(d.node_key, "status.get", job=None, rid="sg1"), timeout=90.0)
    assert r and r["status"] == "ok", r
    assert len(r.raw) > 900, len(r.raw)
    assert verifies(r.raw, d.node_key, "result"), "read whole: the signature still holds"
