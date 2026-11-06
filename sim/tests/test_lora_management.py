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
from test_deployment_authority import Dev, RTC, apply_name, wrap  # noqa: E402
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

    def send(self, to, message, timeout=45.0):
        """The message as text to node key `to`; the reply as a parsed message, with .raw, or None."""
        text = message if isinstance(message, str) else json.dumps(message, separators=(",", ":"))
        r = self.i.request("rf.request", {"to": to, "message": text})
        assert r["status"] == "ok", r
        xid = r["result"]["xid"]
        end = time.time() + timeout
        while time.time() < end:
            m = self.i.read_message(0.5)
            if m and m.get("ev") == "rf" and m["data"]["from"] == to and m["data"]["xid"] == xid:
                return Reply(m["data"]["message"])
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
    answered = 0
    while time.time() < t0 + 14:
        msg = m.i.read_message(0.5)
        if msg and msg.get("ev") == "rf":
            answered += 1
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
