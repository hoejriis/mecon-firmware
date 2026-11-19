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
