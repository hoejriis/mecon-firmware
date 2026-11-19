"""The compact report's `counters` field (docs/contract/TELEMETRY.md, "Compact reports over LoRa"): the radio stream's drop and
queue counters in a signed report, sent only when a request names them, with the older fields byte for byte as they were."""
import base64
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from test_lora_management import KINDS, checked, enrolled, next_report, radio_key, wrap  # noqa: E402
from test_observation_counters import HDR_TXT_FLOOD, txt_flood  # noqa: E402
from test_telemetry_streams import check, first_event  # noqa: E402


def radio(d):
    return check(first_event(d.i, "radio"), "radio")


@pytest.mark.parametrize("kind", KINDS)
def test_a_report_with_counters_carries_what_the_radio_stream_counts(ether, nodes, kind):
    name = "rc" + kind[:2]
    d, m = enrolled(nodes, ether, kind, name)
    ether.inject(name, bytes([0xC0 | HDR_TXT_FLOOD, 0]) + b"\x11" * 20)         # a version this firmware refuses
    ether.inject(name, bytes([HDR_TXT_FLOOD, 0xC0]) + b"\x22" * 20)             # the reserved path mode
    pkt = txt_flood(fill=0x33)
    ether.inject(name, pkt)
    ether.inject(name, pkt)                                                     # the same packet again
    ether.wait_until_us(ether.now_us() + 2e6, timeout=60)
    before = radio(d)
    r = m.send(d.node_key, wrap(d.node_key, "report.now", {"fields": ["health", "radio", "counters"], "to": radio_key(m)}, job=1))
    assert r and r["status"] == "ok" and r["result"]["fields"] == ["health", "radio", "counters"], r
    got = next_report(m)
    assert got and got["from"] == d.node_key, got
    raw = base64.b64decode(got["report"])
    assert len(raw) == 15 + 8 + 12 + 16 + 64, len(raw)
    c = checked(got, d.node_key)["counters"]
    assert before["rx"]["unparsable"] >= 2 and before["rx"]["dup"] >= 1, before["rx"]   # so that what follows is not 0 == 0
    assert c["rx_unparsable"] == before["rx"]["unparsable"] and c["rx_dup"] == before["rx"]["dup"], (c, before["rx"])
    assert c["rx_dropped"] == before["rx"]["dropped"], (c, before["rx"])
    # the rest only grow, and nothing here has reached 65,536
    for key, path in (("tx_failed", ("tx", "failed")), ("queue_full", ("queue_full_count",)),
                      ("cad_busy_retries", ("cad_busy_retries",)), ("duty_holds", ("duty_holds",))):
        v = before
        for p in path:
            v = v[p]
        assert c[key] >= v, (key, v, c[key])
    assert before["queue"]["depth"] >= 0 and 0 <= c["queue_depth"] <= 255 and 1 <= c["queue_free"] <= 255, c
    assert c["queue_depth"] + c["queue_free"] == before["queue"]["depth"] + before["queue"]["free"], (c, before["queue"])


@pytest.mark.parametrize("kind", ["repeater"])
def test_a_report_that_does_not_name_the_counters_is_the_length_it_always_was(ether, nodes, kind):
    d, m = enrolled(nodes, ether, kind, "rn" + kind[:2])
    r = m.send(d.node_key, wrap(d.node_key, "report.now", {"fields": ["health", "radio"], "to": radio_key(m)}, job=1))
    assert r and r["status"] == "ok" and r["result"]["fields"] == ["health", "radio"], r
    got = next_report(m)
    assert len(base64.b64decode(got["report"])) == 15 + 8 + 12 + 64
    assert "counters" not in checked(got, d.node_key)


def test_the_counters_can_be_asked_for_by_a_standing_instruction(ether, nodes):
    d, m = enrolled(nodes, ether, "repeater", "rs")
    r = d.send(wrap(d.node_key, "standing.put", {"id": "c", "fields": ["radio", "counters"], "every_s": 3600, "to": radio_key(m)}, job=1))
    assert r["status"] == "ok", r
    lst = d.send(wrap(d.node_key, "standing.list", {}, job=2, rid="l1"))
    assert lst["status"] == "ok" and lst["result"]["instructions"][0]["fields"] == ["radio", "counters"], lst
