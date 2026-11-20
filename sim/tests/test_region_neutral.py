"""Region-neutral forwarding on the Repeater: an opt-in that lets a scoped flood through when no configured region matches
its transport code. An explicit deny, the unscoped wildcard rule and every other native check stay as they are.

The instrument is the ether broker's transmission log: what the Repeater put on the air, compared with what the sender sent."""
import hashlib
import os
import sys

import pytest

from sim.harness import CliClient, CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402
from test_device_interface_server import client as iface_client, ok, start  # noqa: E402

SECRET = bytes(range(16))
RTC = 1760000000
CMD_SET_DEFAULT_FLOOD_SCOPE = 63
PAYLOAD_GRP_TXT = 5

radio = pytest.mark.lockstep()     # LoRa tests run on lock-step virtual time (docs/SIMULATION.md)


def scope_key(name):
    return hashlib.sha256(b"#" + name.encode()).digest()[:16]


class Rig:
    """Sender and receiver Companions that hear only the Repeater, all on one private channel."""

    def __init__(self, ether, nodes):
        self.ether = ether
        self.ca = nodes("companion", "ca", ether_path=ether.path, seed=11, rtc=RTC).start()
        self.cb = nodes("companion", "cb", ether_path=ether.path, seed=22, rtc=RTC).start()
        self.rr = nodes("repeater", "rr", ether_path=ether.path, seed=33, rtc=RTC).start()
        assert ether.wait_for_nodes(["ca", "cb", "rr"])
        ether.set_link("ca", "rr")
        ether.set_link("cb", "rr")
        self.a, self.b = CompanionClient(self.ca), CompanionClient(self.cb)
        for c in (self.a, self.b):
            c.device_query()
            c.app_start()
            assert c.set_channel(1, "simchan", SECRET)[0] == 0
        self.cli = CliClient(self.rr)
        self.ts = RTC + 10

    def scope(self, name):
        """The sender's default flood scope; None clears it."""
        if name is None:
            f = self.a.request(bytes([CMD_SET_DEFAULT_FLOOD_SCOPE]))
        else:
            f = self.a.request(bytes([CMD_SET_DEFAULT_FLOOD_SCOPE]) + name.encode().ljust(31, b"\0") + scope_key(name))
        assert f[0] == 0, f

    def send(self, text):
        """One channel flood from the sender. Returns (what the sender sent, what the Repeater relayed of it or None)."""
        before = len(self.ether.tx_log)
        self.ts += 1
        assert self.a.send_channel(1, text, self.ts)[0] == 0
        assert self.ether.wait_until_us(self.ether.now_us() + 60e6, timeout=60)
        txs = self.ether.tx_log[before:]
        sent = [t for t in txs if t.src == "ca" and (t.data[0] >> 2) & 0x0F == PAYLOAD_GRP_TXT]
        assert sent, "the sender never sent: the checks would prove nothing"
        relayed = [t for t in txs if t.src == "rr" and (t.data[0] >> 2) & 0x0F == PAYLOAD_GRP_TXT]
        return sent[0].data, (relayed[0].data if relayed else None)

    def got(self):
        return [m["text"] for m in self.b.sync_messages(timeout=30, want=1)]


def split(data):
    """(route type, transport codes, path, payload) of a raw packet."""
    route = data[0] & 3
    i = 1
    codes = b""
    if route in (0, 3):
        codes, i = data[1:5], 5
    plen = data[i] & 0x3F
    hsz = (data[i] >> 6) + 1
    i += 1
    return route, codes, data[i:i + plen * hsz], data[i + plen * hsz:]


@radio
def test_off_by_default_an_unknown_scope_is_not_forwarded_on_it_is_unchanged(ether, nodes):
    r = Rig(ether, nodes)
    assert r.cli.command("get region.neutral") == "> off"
    assert r.cli.command("get region.neutral.count") == "> 0"
    r.scope("alpha")
    sent, relayed = r.send("first")
    assert split(sent)[0] == 0, "a scoped flood carries transport codes"
    assert relayed is None, "an unknown scope is rejected, as stock does"
    assert r.cli.command("get region.neutral.count") == "> 0"

    assert r.cli.command("set region.neutral on") == "OK"
    assert r.cli.command("get region.neutral") == "> on"
    sent, relayed = r.send("second")
    assert relayed is not None, "neutral forwards a scope no region matches"
    s, rl = split(sent), split(relayed)
    assert rl[0] == s[0] and rl[1] == s[1] and rl[3] == s[3], "codes and payload are unchanged"
    assert len(rl[2]) == len(s[2]) + 1, "only the path grew, as every forward does"
    assert r.cli.command("get region.neutral.count") == "> 1"
    texts = r.got()
    assert any(t.endswith("second") for t in texts), texts

    assert r.cli.command("set region.neutral off") == "OK"
    _, relayed = r.send("third")
    assert relayed is None
    assert r.cli.command("get region.neutral.count") == "> 1", "off again: nothing more counted"


@radio
def test_an_explicit_deny_beats_neutral_and_an_allowed_region_is_not_counted(ether, nodes):
    r = Rig(ether, nodes)
    assert r.cli.command("set region.neutral on") == "OK"
    assert r.cli.command("region put city").startswith("OK")
    assert r.cli.command("region denyf city").startswith("OK")
    assert r.cli.command("region save").startswith("OK")
    r.scope("city")
    _, relayed = r.send("denied")
    assert relayed is None, "the scope matches a configured region that denies flood: it stays rejected"
    assert r.cli.command("get region.neutral.count") == "> 0"
    assert r.cli.command("region allowf city").startswith("OK")
    _, relayed = r.send("allowed")
    assert relayed is not None, "allowed by the region itself"
    assert r.cli.command("get region.neutral.count") == "> 0", "a native forward is not a neutral forward"


@radio
def test_an_unscoped_flood_keeps_the_wildcard_rule(ether, nodes):
    r = Rig(ether, nodes)
    assert r.cli.command("set region.neutral on") == "OK"
    r.scope(None)
    sent, relayed = r.send("plain")
    assert split(sent)[0] == 1 and relayed is not None, "an unscoped flood goes through while the wildcard allows it"
    assert r.cli.command("get region.neutral.count") == "> 0"
    assert r.cli.command("region denyf *").startswith("OK")
    _, relayed = r.send("plain denied")
    assert relayed is None, "neutral does not touch unscoped floods"
    assert r.cli.command("get region.neutral.count") == "> 0"


@radio
def test_a_direct_packet_is_the_same_on_or_off(ether, nodes):
    r = Rig(ether, nodes)
    pub = r.cli.command("get public.key")[2:]
    h = bytes.fromhex(pub)[0:1]
    pkt = bytes([(PAYLOAD_GRP_TXT << 2) | 2, 1]) + h + bytes(range(1, 21))

    def relayed():
        before = len(ether.tx_log)
        ether.inject("rr", pkt)
        assert ether.wait_until_us(ether.now_us() + 30e6, timeout=60)
        return [t for t in ether.tx_log[before:] if t.src == "rr" and (t.data[0] >> 2) & 0x0F == PAYLOAD_GRP_TXT]

    off = relayed()
    assert r.cli.command("set region.neutral on") == "OK"
    pkt = pkt[:-1] + b"\xfe"      # a new packet: the first one is a seen duplicate
    on = relayed()
    assert len(off) == 1 and len(on) == 1, "a direct packet naming this Repeater is relayed on or off: %s %s" % (off, on)
    assert r.cli.command("get region.neutral.count") == "> 0"


def test_the_setting_survives_a_reboot(nodes):
    n = nodes("repeater", "pers").start()
    cli = CliClient(n)
    assert cli.command("set region.neutral on") == "OK"
    cli.close()
    n.restart()
    cli = CliClient(n)
    assert cli.command("get region.neutral") == "> on"
    assert cli.command("get region.neutral.count") == "> 0", "the counter is since boot"
    assert cli.command("set region.neutral maybe").startswith("Err - ")
    assert cli.command("get region.neutral") == "> on", "a bad value changes nothing"


def test_the_interface_round_trips_the_key_on_a_repeater(nodes):
    n, (cli, i) = start(nodes, "repeater", "rnk")
    keys, cursor = [], 0
    while cursor is not None:
        r = ok(i.request("config.schema", {"cursor": cursor}), "config.schema.result.json")
        keys += [k["key"] for k in r["keys"]]
        cursor = r["next"]
    assert "region.neutral" in keys, keys
    v = ok(i.request("config.get", {"keys": ["region.neutral"]}), "config.get.result.json")["values"]
    assert v["region.neutral"] is False
    r = i.request("config.apply", {"set": {"region.neutral": True}})
    assert r["status"] == "ok", r
    assert cli.command("get region.neutral") == "> on"
    n.stop()
    n.start()
    cli, i = iface_client(n)
    v = ok(i.request("config.get", {"keys": ["region.neutral"]}), "config.get.result.json")["values"]
    assert v["region.neutral"] is True


def test_a_room_server_has_no_such_key_or_line(nodes):
    n, (cli, i) = start(nodes, "room_server", "rnr")
    keys, cursor = [], 0
    while cursor is not None:
        r = ok(i.request("config.schema", {"cursor": cursor}), "config.schema.result.json")
        keys += [k["key"] for k in r["keys"]]
        cursor = r["next"]
    assert "region.neutral" not in keys and "owner.info" in keys, keys
    r = i.request("config.get", {"keys": ["region.neutral"]})
    assert "region.neutral" not in (r.get("result") or {}).get("values", {}), r
    assert r["status"] != "ok" or "region.neutral" in str(r["result"].get("errors")), r
    r = i.request("config.apply", {"set": {"region.neutral": True}})
    assert r["status"] != "ok", r
    assert not cli.command("get region.neutral").startswith("> o"), "the Room server does not answer the line"
    assert cli.command("set region.neutral on") != "OK"


@radio
def test_the_bench_tool_sends_a_flood_scoped_to_an_unknown_region(ether, nodes):
    """tools/bench/scoped_flood.py is what a hardware session uses to show region.neutral: prove it against a simulated
    Companion, sending through a Repeater with the setting on and then off."""
    import subprocess
    tool = os.path.join(os.path.dirname(__file__), "..", "..", "tools", "bench", "scoped_flood.py")
    r = Rig(ether, nodes)
    r.a.close()   # the tool opens its own connection to the sender

    def run(text):
        before = len(ether.tx_log)
        out = subprocess.run([sys.executable, tool, "--tcp", "127.0.0.1:%d" % r.ca.port, "--channel", "1",
                              "--region", "bench-unknown", text], capture_output=True, text=True, timeout=60)
        assert out.returncode == 0, out.stdout + out.stderr
        assert "accepted" in out.stdout and "scope cleared" in out.stdout, out.stdout
        assert ether.wait_until_us(ether.now_us() + 60e6, timeout=60)
        txs = ether.tx_log[before:]
        sent = [t.data for t in txs if t.src == "ca" and (t.data[0] >> 2) & 0x0F == PAYLOAD_GRP_TXT]
        relayed = [t.data for t in txs if t.src == "rr" and (t.data[0] >> 2) & 0x0F == PAYLOAD_GRP_TXT]
        assert len(sent) == 1, "one message per run"
        return sent[0], relayed

    assert r.cli.command("set region.neutral on") == "OK"
    sent, relayed = run("tool on")
    route, codes, _, _ = split(sent)
    assert route == 0 and codes != b"\0\0\0\0", "a flood with the region's transport code"
    assert relayed, "neutral on: the Repeater forwards a scope it does not know"
    assert r.cli.command("get region.neutral.count") == "> 1"
    assert r.cli.command("set region.neutral off") == "OK"
    _, relayed = run("tool off")
    assert not relayed, "neutral off: dropped"
