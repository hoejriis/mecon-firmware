"""What an observation says beyond the frame's own header (docs/contract/TELEMETRY.md, `observations`): the packet
fingerprint (the packet hash), the radio parameters of the reception and the outcome of handling the packet."""
import hashlib
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from test_observation_counters import (HDR_TXT_DIRECT, HDR_TXT_FLOOD, RTC, header_frames, mecon_iface, observed,  # noqa: E402,F401
                                       repeater_iface, txt_flood, wait_event, check)

TRACE = 0x09


def fingerprint(raw):
    """The first 8 bytes of SHA-256 over the payload type, for a TRACE the path-length field (the mesh holds it as two
    bytes, little-endian), and the payload: how the firmware identifies a packet whatever path it took."""
    route = raw[0] & 3
    at = 5 if route in (0, 3) else 1
    ptype = (raw[0] >> 2) & 0x0F
    plen = raw[at]
    payload = raw[at + 1 + (plen & 63) * ((plen >> 6) + 1):]
    h = hashlib.sha256(bytes([ptype]) + (bytes([plen, 0]) if ptype == TRACE else b"") + payload)
    return h.hexdigest()[:16]


def open_stream(ether, nodes, kind, name):
    from sim.harness import CompanionClient
    n = nodes(kind, name, seed=3, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes([name])
    cli = None
    if kind == "repeater":
        cli, i = repeater_iface(n)
    else:
        c = CompanionClient(n)
        c.app_start()
        i = mecon_iface.IfaceClient(c._send, c._read_frame)
    assert i.request("sub.start", {"stream": "observations", "lifetime_s": 120})["status"] == "ok"
    return n, cli, i


KINDS = ["repeater", "companion_standalone"]


@pytest.mark.parametrize("kind", KINDS)
def test_an_observation_carries_the_fingerprint_of_a_valid_packet_and_none_for_a_frame_that_is_not_one(ether, nodes, kind):
    n, cli, i = open_stream(ether, nodes, kind, "fp")
    frames = header_frames()
    got = observed(ether, i, "fp", frames)
    want = [fingerprint(f) if k < 4 else None for k, f in enumerate(frames)]
    # the last three are not valid packets: no path-length byte, the reserved path mode, a version this firmware refuses
    assert [d.get("fp") for d in got] == want, got
    assert all(len(d["fp"]) == 16 for d in got if "fp" in d)
    assert all(d.get("fp") is None for d in got[4:]), "a frame that is not a packet has no fingerprint"


@pytest.mark.parametrize("kind", KINDS)
def test_the_fingerprint_is_the_same_whatever_path_a_packet_took_and_differs_with_the_payload(ether, nodes, kind):
    n, cli, i = open_stream(ether, nodes, kind, "fp")
    a, b, c = observed(ether, i, "fp", [txt_flood(), txt_flood(path=b"\x01\x02\x03"), txt_flood(fill=0x6B)])
    assert a["fp"] == b["fp"], "one packet by two paths"
    assert a["fp"] != c["fp"], "another payload"
    assert a["fp"] == fingerprint(bytes.fromhex(a["raw"]))


def test_the_fingerprint_of_a_trace_is_taken_as_heard_not_after_this_node_added_its_hop(ether, nodes):
    n, cli, i = open_stream(ether, nodes, "repeater", "fp")
    mine = bytes.fromhex(cli.command("get public.key").lstrip("> ").strip())[0]
    # tag(4) auth(4) flags(1: one-byte hashes) then the hash list: this node is the first hop to relay it
    trace = bytes([(TRACE << 2) | 0x02, 0]) + b"\x01\x00\x00\x00" + b"\x02\x00\x00\x00" + b"\x00" + bytes([mine, 0x55])
    d = observed(ether, i, "fp", [trace])[0]
    assert d["fp"] == fingerprint(trace), d


def stored_radio(i):
    f, bw, sf, cr = i.request("config.get", {"keys": ["radio"]})["result"]["values"]["radio"].split(",")
    return float(f), float(bw), int(sf), int(cr)


@pytest.mark.parametrize("kind", KINDS)
def test_an_observation_carries_the_radio_parameters_the_radio_is_set_to(ether, nodes, kind):
    n, cli, i = open_stream(ether, nodes, kind, "rp")
    freq, bw, sf, cr = stored_radio(i)
    d = observed(ether, i, "rp", [txt_flood()])[0]
    assert (d["freq_mhz"], d["bw_khz"], d["sf"], d["cr"]) == (pytest.approx(freq, abs=0.0006), pytest.approx(bw, rel=1e-3), sf, cr), (d, (freq, bw, sf, cr))
    assert isinstance(d["sf"], int) and isinstance(d["cr"], int) and 5 <= d["cr"] <= 8 and 5 <= d["sf"] <= 12, d


def test_the_radio_parameters_follow_a_temporary_radio_change_and_not_only_the_stored_settings(ether, nodes):
    n, cli, i = open_stream(ether, nodes, "repeater", "rp")
    before = observed(ether, i, "rp", [txt_flood()])[0]
    assert (before["sf"], before["cr"]) != (9, 6) and before["freq_mhz"] != pytest.approx(868.1), before
    assert cli.command("tempradio 868.1,125,9,6,30").startswith("OK")
    ether.wait_until_us(ether.now_us() + 3e6, timeout=60)
    after = observed(ether, i, "rp", [txt_flood(fill=0x6B)])[0]
    assert (after["freq_mhz"], after["bw_khz"], after["sf"], after["cr"]) == (pytest.approx(868.1, abs=0.0006), 125, 9, 6), after
    assert stored_radio(i)[2:] == (before["sf"], before["cr"]), "the stored settings did not change"


def mine(cli):
    return bytes.fromhex(cli.command("get public.key").lstrip("> ").strip())[0]


def outcomes(ether, i, frames):
    return [d["outcome"] for d in observed(ether, i, "oc", frames)]


def test_a_repeater_reports_what_became_of_each_packet(ether, nodes):
    n, cli, i = open_stream(ether, nodes, "repeater", "oc")
    me = mine(cli)
    other = (me + 1) & 0xFF
    unique = txt_flood(fill=0x21)
    got = outcomes(ether, i, [
        unique,                                                       # a flood this node repeats
        unique,                                                       # the same packet again
        bytes([HDR_TXT_DIRECT, 1, other]) + b"\x44" * 40,             # a direct packet whose next hop is another node
        bytes([HDR_TXT_DIRECT, 1, me]) + b"\x45" * 40,                # a direct packet for this node's hop
        bytes([HDR_TXT_DIRECT, 1, me]) + b"\x45" * 40,                # and again
        bytes([0xC0 | HDR_TXT_FLOOD, 0]) + b"\x11" * 20,              # a version this firmware refuses
        bytes([HDR_TXT_DIRECT, 0]) + b"\x46" * 40,                    # a direct packet with no hops left: for this node or nobody
    ])
    assert got == ["forwarded", "duplicate", "not-next-hop", "forwarded", "duplicate", "unparsable", None], got


def test_a_flood_packet_whose_path_is_full_has_no_route_on(ether, nodes):
    n, cli, i = open_stream(ether, nodes, "repeater", "oc")
    # two-byte hashes, 32 of them: this node's own would not fit in the 64 bytes of path
    full = bytes([HDR_TXT_FLOOD, 0x40 | 32]) + bytes(range(64)) + b"\x5A" * 20
    ok = bytes([HDR_TXT_FLOOD, 0x40 | 31]) + bytes(range(62)) + b"\x5B" * 20     # one fewer: room for one more
    assert outcomes(ether, i, [full, ok]) == ["no-route", "forwarded"]


def test_a_repeater_that_does_not_repeat_says_the_packet_was_filtered(ether, nodes):
    n, cli, i = open_stream(ether, nodes, "repeater", "oc")
    assert cli.command("set repeat off").startswith("OK")
    me = mine(cli)
    got = outcomes(ether, i, [txt_flood(fill=0x31), bytes([HDR_TXT_DIRECT, 1, me]) + b"\x47" * 40,
                              bytes([HDR_TXT_DIRECT, 1, (me + 1) & 0xFF]) + b"\x48" * 40])
    assert got == ["filtered", "filtered", "not-next-hop"], got


def test_a_companion_does_not_forward_and_says_so(ether, nodes):
    n, cli, i = open_stream(ether, nodes, "companion_standalone", "oc")
    unique = txt_flood(fill=0x41)
    got = outcomes(ether, i, [unique, unique, bytes([HDR_TXT_DIRECT, 1, 0x7F]) + b"\x44" * 40])
    assert got == ["filtered", "duplicate", "not-next-hop"], got


def test_a_flood_packet_held_back_by_the_receive_delay_is_reported_as_heard_with_no_outcome(ether, nodes):
    # The packet is handled after the delay, so nothing is known when it is observed. A weak signal (a low score)
    # with a receive delay set is held back; it must not carry the outcome of the packet handled before it.
    n, cli, i = open_stream(ether, nodes, "repeater", "oc")
    assert cli.command("set rxdelay 20").startswith("OK")
    assert outcomes(ether, i, [txt_flood(fill=0x53, payload_len=150)]) == ["forwarded"], "a strong signal is handled at once"
    got = []
    for fill in (0x51, 0x52):
        while i.read_message(0.05):
            pass
        f = txt_flood(fill=fill, payload_len=150)
        ether.inject("oc", f, snr=-20.0)
        d = check(wait_event(i, "observations", 20), "observations")
        assert d["raw"] == f.hex()
        got.append(d["outcome"])
    assert got == [None, None], got
