"""The radio stream's always-on diagnostic counters and the observations stream's header fields
(docs/contract/TELEMETRY.md): each counter moves for the event it names and for nothing else, the counters cost no
subscriber, and an observation's type, route and hop count are read from the frame as heard."""
import os
import struct
import sys
import time

import pytest

from sim.harness import CliClient

sys.path.insert(0, os.path.dirname(__file__))
from test_telemetry_streams import RTC, check, first_event, wait_event, mecon_iface  # noqa: E402

HDR_TXT_FLOOD = (0x02 << 2) | 0x01        # payload type TXT_MSG, route flood
HDR_TXT_DIRECT = (0x02 << 2) | 0x02       # payload type TXT_MSG, route direct


def repeater_iface(node):
    cli = CliClient(node)
    return cli, mecon_iface.ConsoleClient(cli.sock.sendall, mecon_iface.socket_lines(cli.sock))


def radio(i):
    """A fresh snapshot of the radio stream (a new subscription is answered at once)."""
    return check(first_event(i, "radio"), "radio")


def txt_flood(path=b"", payload_len=40, fill=0x5A):
    return bytes([HDR_TXT_FLOOD, len(path)]) + path + bytes([fill]) * payload_len


def settle(ether, seconds=2.0):
    ether.wait_until_us(ether.now_us() + seconds * 1e6, timeout=60)


@pytest.fixture
def rp(ether, nodes):
    n = nodes("repeater", "ctr", seed=3, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes(["ctr"])
    cli, i = repeater_iface(n)
    return n, cli, i


def test_the_counters_start_at_zero_with_no_subscriber_ever_asked_for_them(rp):
    n, cli, i = rp
    d = radio(i)
    assert d["rx"]["dup"] == 0 and d["rx"]["dropped"] == 0 and d["rx"]["unparsable"] == 0, d
    assert d["tx"]["failed"] == 0 and d["queue_full_count"] == 0 and d["cad_busy_retries"] == 0, d
    assert d["queue"]["free"] >= 1 and d["queue"]["depth"] >= 0, d


def test_a_frame_that_is_not_a_packet_counts_as_unparsable_and_nothing_else(ether, rp):
    n, cli, i = rp
    before = radio(i)
    ether.inject("ctr", bytes([0xC0 | (0x02 << 2) | 0x01, 0]) + b"\x11" * 20)        # payload version 3: unsupported
    ether.inject("ctr", bytes([HDR_TXT_FLOOD, 0xC0]) + b"\x22" * 20)                  # reserved path mode
    ether.inject("ctr", bytes([HDR_TXT_FLOOD, 40]) + b"\x33" * 5)                     # path longer than the frame
    settle(ether)
    after = radio(i)
    assert after["rx"]["unparsable"] == before["rx"]["unparsable"] + 3, (before["rx"], after["rx"])
    assert after["rx"]["flood"] == before["rx"]["flood"] and after["rx"]["dup"] == 0 and after["rx"]["dropped"] == 0, after["rx"]


def test_the_same_packet_heard_twice_counts_one_duplicate(ether, rp):
    n, cli, i = rp
    before = radio(i)
    pkt = txt_flood()
    ether.inject("ctr", pkt)
    settle(ether)
    mid = radio(i)
    assert mid["rx"]["dup"] == before["rx"]["dup"], "the first hearing is not a duplicate"
    ether.inject("ctr", pkt)
    ether.inject("ctr", txt_flood(fill=0x6B))                    # a different packet is not one
    settle(ether)
    after = radio(i)
    assert after["rx"]["dup"] == before["rx"]["dup"] + 1, (before["rx"], after["rx"])
    assert after["rx"]["flood"] == before["rx"]["flood"] + 3, "all three were received; only one repeated"


def test_a_direct_packet_for_another_hop_counts_as_dropped(ether, rp):
    n, cli, i = rp
    mine = bytes.fromhex(cli.command("get public.key").lstrip("> ").strip())[0]
    before = radio(i)
    other = bytes([(mine + 1) & 0xFF])
    ether.inject("ctr", bytes([HDR_TXT_DIRECT, 1]) + other + b"\x44" * 40)           # next hop is someone else
    settle(ether)
    after = radio(i)
    assert after["rx"]["dropped"] == before["rx"]["dropped"] + 1, (before["rx"], after["rx"])
    assert after["rx"]["unparsable"] == before["rx"]["unparsable"] and after["rx"]["dup"] == before["rx"]["dup"], after["rx"]


def test_a_send_that_waits_for_airtime_is_a_duty_hold_and_sits_in_the_queue(ether, rp):
    # The duty window is seeded at the limit at boot (docs/PLATFORM.md): the first advert waits for airtime to age out.
    n, cli, i = rp
    before = radio(i)
    cli.command("advert.zerohop")
    seen = None
    end = time.time() + 20
    while time.time() < end:
        d = radio(i)
        if d["duty_holds"] > before["duty_holds"] and d["queue"]["depth"] >= 1:
            seen = d
            break
        time.sleep(0.5)
    assert seen, "no duty hold seen with the packet queued: %s" % (d,)
    # a queued packet is one fewer in the pool: depth and free always add up to the pool
    assert seen["queue"]["depth"] + seen["queue"]["free"] == before["queue"]["depth"] + before["queue"]["free"], (before["queue"], seen["queue"])
    assert seen["queue"]["free"] < before["queue"]["free"], (before["queue"], seen["queue"])


def test_a_packet_longer_than_the_whole_duty_limit_is_a_failed_send(ether, rp):
    # 1 % of an hour is 36 s: at SF12 and 7.8 kHz one advert takes longer than that, so it can never be sent.
    n, cli, i = rp
    assert cli.command("tempradio 868.1,7.8,12,8,30").startswith("OK")
    settle(ether, 3.0)
    before = radio(i)
    cli.command("advert.zerohop")
    end = time.time() + 20
    d = before
    while time.time() < end and d["tx"]["failed"] == before["tx"]["failed"]:
        time.sleep(1.0)
        d = radio(i)
    assert d["tx"]["failed"] > before["tx"]["failed"], (before["tx"], d["tx"])
    assert d["tx"]["flood"] + d["tx"]["direct"] == before["tx"]["flood"] + before["tx"]["direct"], "none of them went on the air"
    assert not [t for t in ether.tx_of("ctr") if len(t.data)], "nothing was transmitted"


@pytest.mark.timescale(5)
def test_a_send_put_off_because_the_channel_is_busy_counts_each_retry(ether, rp):
    n, cli, i = rp
    before = radio(i)
    cli.command("advert.zerohop")
    d = before
    end = time.time() + 40
    while time.time() < end and d["cad_busy_retries"] == before["cad_busy_retries"]:
        with ether.lock:
            ether._send(ether._conns["ctr"], "B", struct.pack("<I", 3_000_000))     # the channel is busy for 3 s
        time.sleep(0.1)
        if int(time.time() * 10) % 20 == 0:
            d = radio(i)
    assert d["cad_busy_retries"] > before["cad_busy_retries"], (before, d)


def test_the_pool_running_dry_is_counted_each_time(ether, rp):
    # The window is seeded at the limit, so what the Repeater forwards waits: unique floods fill its 32-packet pool.
    n, cli, i = rp
    before = radio(i)
    for k in range(70):
        ether.inject("ctr", bytes([HDR_TXT_FLOOD, 0]) + k.to_bytes(2, "little") + b"\x5A" * 38)
        time.sleep(0.02)
    settle(ether)
    after = radio(i)
    assert after["queue_full_count"] > before["queue_full_count"], (before, after)
    assert after["queue"]["free"] <= 1 and after["queue"]["depth"] >= 25, after["queue"]


def observed(ether, i, dst, frames):
    """Injects each frame and returns the observations event for it, in order."""
    out = []
    for f in frames:
        while i.read_message(0.05):
            pass
        ether.inject(dst, f)
        e = wait_event(i, "observations", 20)
        assert e is not None, "no observation for %s" % f.hex()
        d = check(e, "observations")
        assert d["raw"] == f.hex(), "the event is for another frame"
        out.append(d)
    return out


def header_frames():
    return [
        txt_flood(path=b"\x01\x02\x03"),                                                     # flood, 3 hops
        bytes([HDR_TXT_DIRECT, 1, 0x7F]) + b"\x44" * 30,                                      # direct, 1 hop
        bytes([(0x04 << 2) | 0x00]) + b"\x01\x02\x03\x04" + bytes([2, 9, 9]) + b"\x55" * 20,  # transport flood, type 4, 2 hops
        bytes([(0x0F << 2) | 0x03]) + b"\x01\x02\x03\x04" + bytes([0]) + b"\x66" * 10,        # transport direct, type 15, 0 hops
        bytes([HDR_TXT_FLOOD]),                                                              # ends before its path length
        bytes([HDR_TXT_FLOOD, 0xC1]) + b"\x77" * 10,                                         # reserved path mode
        bytes([0xC0 | HDR_TXT_FLOOD, 0]) + b"\x77" * 10,                                     # a version this firmware refuses
    ]


EXPECT = [(2, "flood", 3), (2, "direct", 1), (4, "tflood", 2), (15, "tdirect", 0), (2, "flood", None),
          (2, "flood", None), (2, "flood", 0)]


@pytest.mark.parametrize("kind", ["repeater", "companion_standalone"])
def test_an_observation_carries_the_type_route_and_hop_count_of_the_frame_as_heard(ether, nodes, kind):
    from sim.harness import CompanionClient
    n = nodes(kind, "hdr", seed=3, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes(["hdr"])
    if kind == "repeater":
        cli, i = repeater_iface(n)
    else:
        c = CompanionClient(n)
        c.app_start()
        i = mecon_iface.IfaceClient(c._send, c._read_frame)
    assert i.request("sub.start", {"stream": "observations", "lifetime_s": 120})["status"] == "ok"
    got = observed(ether, i, "hdr", header_frames())
    assert [(d["type"], d["route"], d["path_len"]) for d in got] == EXPECT, got
    assert all(d["len"] == len(bytes.fromhex(d["raw"])) for d in got)


def test_nothing_is_kept_for_a_subscriber_that_comes_later(ether, rp):
    n, cli, i = rp
    for f in header_frames()[:3]:
        ether.inject("ctr", f)
    settle(ether)
    before = radio(i)
    assert before["rx"]["flood"] >= 2, "the packets were received and counted without a subscriber"
    assert i.request("sub.start", {"stream": "observations", "lifetime_s": 60})["status"] == "ok"
    assert wait_event(i, "observations", 3) is None, "a packet from before the subscription was replayed"
    d = observed(ether, i, "ctr", [txt_flood(path=b"\x09")])[0]
    assert d["dropped"] == 0 and d["path_len"] == 1, d


def test_clear_stats_zeroes_the_new_counters_with_the_rest(ether, rp):
    n, cli, i = rp
    ether.inject("ctr", bytes([0xC0 | HDR_TXT_FLOOD, 0]) + b"\x11" * 20)
    pkt = txt_flood()
    ether.inject("ctr", pkt)
    ether.inject("ctr", pkt)
    ether.inject("ctr", bytes([HDR_TXT_DIRECT, 1, 0x7E]) + b"\x44" * 40 if bytes.fromhex(cli.command("get public.key").lstrip("> ").strip())[0] != 0x7E
                 else bytes([HDR_TXT_DIRECT, 1, 0x7D]) + b"\x44" * 40)
    settle(ether)
    d = radio(i)
    assert d["rx"]["unparsable"] == 1 and d["rx"]["dup"] == 1 and d["rx"]["dropped"] == 1, d["rx"]
    assert "OK" in cli.command("clear stats")
    d = radio(i)
    assert d["rx"]["unparsable"] == d["rx"]["dup"] == d["rx"]["dropped"] == 0, d["rx"]
    assert d["tx"]["failed"] == d["duty_holds"] == d["cad_busy_retries"] == d["queue_full_count"] == 0, d
