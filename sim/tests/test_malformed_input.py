"""Malformed input must not read or write out of bounds. Runs the sanitizer builds (sim_*_asan): an
out-of-bounds access aborts the node, so the node staying up and answering is the assertion."""
import struct
import time

import pytest

from sim.harness import CliClient, CompanionClient

RTC = 1760000000
HDR_TRACE_DIRECT = (0x09 << 2) | 0x02       # payload type TRACE, route DIRECT


def trace_packet(path, payload):
    return bytes([HDR_TRACE_DIRECT, len(path)]) + bytes(path) + bytes(payload)


def truncated_traces():
    """TRACE packets whose payload is shorter than the 9-byte header (tag, auth, flags) they claim, with
    long SNR paths. Several path lengths, since the flags byte it then reads is whatever the buffer held."""
    out = []
    for path_len in (28, 45, 50, 63):
        for payload_len in (0, 4, 8):
            out.append(trace_packet(bytes(path_len), struct.pack("<II", 0x1234, 0x5678)[:payload_len]))
    return out


@pytest.mark.parametrize("kind", ["repeater_asan", "companion_asan"])
def test_truncated_trace_packets_do_not_crash_the_node(ether, nodes, kind):
    n = nodes(kind, "victim", seed=3, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes(["victim"])
    for _ in range(3):
        for pkt in truncated_traces():
            ether.inject("victim", pkt)
            time.sleep(0.05)
    time.sleep(1.0)
    assert n.proc.poll() is None, "node died: " + n.sanitizer_report()[:2000]
    assert n.sanitizer_report() == ""
    # and it still serves its console / app protocol
    if kind.startswith("repeater"):
        assert CliClient(n).command("ver").startswith("v1.")
    else:
        c = CompanionClient(n)
        assert c.device_query()[0] != 1


@pytest.mark.timescale(10)
def test_wellformed_trace_is_still_forwarded_by_repeater(ether, nodes):
    n = nodes("repeater_asan", "rp", seed=3, rtc=RTC, ether_path=ether.path).start()
    other = nodes("repeater", "obs", seed=4, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes(["rp", "obs"])
    ether.set_link("rp", "obs")
    first_byte = bytes.fromhex(CliClient(n).command("get public.key").lstrip("> ").strip())[0:1]
    pkt = trace_packet(b"", struct.pack("<IIB", 0x1234, 0x5678, 0) + first_byte)   # hop list names the repeater
    before = len(ether.tx_log)
    ether.inject("rp", pkt)
    end = time.time() + 20
    fwd = None
    while time.time() < end and fwd is None:
        fwd = next((t for t in ether.tx_log[before:] if t.src == "rp" and t.data[0] == HDR_TRACE_DIRECT), None)
        time.sleep(0.1)
    assert fwd is not None, "repeater did not forward a valid TRACE"
    assert fwd.data[1] == 1                      # one SNR entry appended to the path
    assert n.proc.poll() is None and n.sanitizer_report() == ""


def test_wellformed_trace_reaches_companion_app(ether, nodes):
    n = nodes("companion_asan", "co", seed=3, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes(["co"])
    c = CompanionClient(n)
    c.device_query(); c.app_start()
    ether.inject("co", trace_packet(b"", struct.pack("<IIB", 0xAABBCCDD, 0x11223344, 0)))
    push = c.wait_push(0x89, timeout=15)          # PUSH_CODE_TRACE_DATA
    assert push is not None and struct.unpack_from("<I", push, 4)[0] == 0xAABBCCDD


# ---------------------------------------------------------------- over-long CLI arguments
def test_overlong_radio_argument_on_repeater_console(nodes):
    n = nodes("repeater_asan", "rc", seed=3, rtc=RTC).start()
    cli = CliClient(n)
    reply = cli.command("set radio " + "9," * 70)            # 140 characters: longer than the 132-byte parse buffer
    assert "invalid radio params" in reply, reply
    assert n.proc.poll() is None and n.sanitizer_report() == ""
    assert cli.command("set radio 869.5,62.5,8,5").startswith("OK")        # a valid one still works


def test_overlong_radio_argument_on_companion_cli(nodes):
    n = nodes("companion_asan", "cc", seed=3, rtc=RTC).start()
    c = CompanionClient(n)
    c.device_query(); c.app_start()
    reply = c.cli("set radio " + "9," * 80)                   # 160 characters
    assert "invalid radio params" in reply, reply
    assert n.proc.poll() is None and n.sanitizer_report() == ""
    assert c.cli("set radio 869.5,62.5,8,5").startswith("OK")


def test_overlong_unknown_get_on_repeater_console(nodes):
    n = nodes("repeater_asan", "rg", seed=3, rtc=RTC).start()
    cli = CliClient(n)
    reply = cli.command("get " + "x" * 150)
    assert reply.startswith("??: x") and n.proc.poll() is None and n.sanitizer_report() == ""
    assert cli.command("ver").startswith("v1.")
