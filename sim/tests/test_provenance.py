"""Signed events and results (docs/contract/AUTHORITY.md section 3.4) on the simulated Companion (USB frames, MQTT) and the
Repeater and Room console: every result and event carries usig_boot, usig_seq and a node-key signature while a record
exists, the signature is checked here by removing `usig` textually (as a manager behind a bridge or broker would) and
verifying with sim/ed25519_ref.py against the node key, independently of the firmware's signer, and the boot counter that
could not be saved turns signing off. The reference verifier is itself checked against the public vectors."""
import base64
import json
import os
import re
import sys
import time

import pytest
from sim import ed25519_ref
from sim.harness import CliClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402
from test_deployment_authority import Dev, V, boot as boot_companion, broker, mqtt_boot, wrap, apply_name  # noqa: E402,F401
from test_deployment_authority_server import Srv, boot as boot_server  # noqa: E402

USIG = re.compile(r',"usig":"([A-Za-z0-9+/]{86}==)"\}$')


def signed_text(raw):
    """(the object text with usig removed textually, the signature) of one message as received; None when unsigned."""
    raw = raw.decode() if isinstance(raw, bytes) else raw
    m = USIG.search(raw)
    return (raw[:m.start()] + "}", base64.b64decode(m.group(1))) if m else None


def verifies(raw, node_key, stream):
    st = signed_text(raw)
    if not st:
        return False
    text, sig = st
    return ed25519_ref.verify(bytes.fromhex(node_key), b"MECON-UPLINK-v1\x00" + stream.encode() + b"\x00" + text.encode(), sig)


def test_the_reference_verifier_agrees_with_the_public_vectors():
    node = V["keys"]["device node"]["public_key"]
    assert verifies(V["uplink_health"]["wire"], node, "health")
    assert verifies(V["uplink_result"]["wire"], node, "result")
    assert not verifies(V["uplink_health"]["wire"], node, "result"), "the stream is part of what is signed"
    assert not verifies(V["uplink_result"]["wire"], node, "health")
    nr = V["node_rotation"]
    assert ed25519_ref.verify(bytes.fromhex(node), b"MECON-NODEROT-v1\x00" + nr["statement"].encode(), base64.b64decode(nr["sig"]))
    assert not verifies(V["uplink_health"]["wire"].replace('"uptime_s":60', '"uptime_s":61'), node, "health")


def event_of(i, stream, timeout=15.0):
    r = i.request("sub.start", {"stream": stream, "period_s": 5 if stream == "radio" else 3600, "lifetime_s": 60})
    assert r["status"] == "ok", r
    end = time.time() + timeout
    while time.time() < end:
        for e in list(i.events):
            if e.get("ev") == stream:
                i.events.remove(e)
                return e
        m = i.read_message(0.5)
        if m and m.get("ev") == stream:
            return m
        if m and "ev" in m:
            i.events.append(m)
    raise AssertionError("no %s event" % stream)


def members(m):
    return list(m.keys())


def check_provenance(i, node_key, boot):
    """Results and events of an enrolled device: signed, usig last, one counter that only rises."""
    h = i.request("hello")
    s = i.request("status.get")
    e = event_of(i, "health")
    e2 = event_of(i, "inventory")
    seqs = []
    for m, stream in ((h, "result"), (s, "result"), (e, "health"), (e2, "inventory")):
        assert members(m)[-3:] == ["usig_boot", "usig_seq", "usig"], members(m)
        assert m["usig_boot"] == boot
        assert verifies(m.raw, node_key, stream), (stream, m.raw)
        other = "health" if stream == "result" else "result"
        assert not verifies(m.raw, node_key, other), "a result and an event are not interchangeable"
        seqs.append(m["usig_seq"])
    assert seqs == sorted(seqs) and len(set(seqs)) == len(seqs), seqs
    return seqs


def test_companion_results_and_events_are_signed_once_a_record_exists(nodes):
    d = boot_companion(nodes, "pv", 31)
    # a standalone device signs nothing
    h = d.i.request("hello")
    assert not any(k.startswith("usig") for k in h) and "unsigned" not in h, h
    assert d.provision()["status"] == "ok"
    boot_n = d.i.request("hello")["result"]["epoch"]
    seqs = check_provenance(d.i, d.node_key, boot_n)
    # the counter is one for the whole boot: a rejected request and a signed one are on it too
    refused = d.i.request("config.apply", {"values": {"name": "x"}})
    assert refused["status"] == "refused" and verifies(refused.raw, d.node_key, "result")
    assert refused["usig_seq"] > seqs[-1]
    # a signed request's result is signed by the node, whoever signed the request
    w = wrap(d.node_key, "config.apply", apply_name("Prov"), job=1)
    r = d.send(w)
    assert r["status"] == "ok" and verifies(r.raw, d.node_key, "result") and r["usig_seq"] > refused["usig_seq"]
    # a reboot is a new boot: the epoch moves, the counter starts again (the attach's own hello was number 1)
    d.reboot()
    h2 = d.i.request("hello")
    assert h2["usig_boot"] == boot_n + 1 and h2["usig_seq"] == 2 and verifies(h2.raw, d.node_key, "result")


@pytest.mark.parametrize("kind", ["repeater", "room_server"])
def test_console_results_and_events_are_signed_once_a_record_exists(nodes, kind):
    d = boot_server(nodes, kind, "pv" + kind, 33)
    h = d.i.request("hello")
    assert not any(k.startswith("usig") for k in h), h
    assert d.provision()["status"] == "ok"
    boot_n = d.i.request("hello")["result"]["epoch"]
    check_provenance(d.i, d.node_key, boot_n)


def test_an_unsaved_boot_epoch_signs_nothing(nodes):
    n = nodes("companion_standalone", "pu", seed=35, rtc=1792800000)
    # /mecon_dev cannot be written: its temporary file is a directory that cannot be removed
    blocker = os.path.join(n.dir, "mecon_dev.tmp")
    os.makedirs(os.path.join(blocker, "keep"))
    n.start()
    d = Dev(n)
    assert d.provision()["status"] == "ok"
    for m in (d.i.request("hello"), d.i.request("status.get"), event_of(d.i, "health")):
        assert m.get("unsigned") == "epoch_unsaved", m
        assert not any(k.startswith("usig") for k in m), m
        assert members(m)[-1] == "unsigned"


@pytest.mark.timescale(10)
def test_provenance_survives_a_broker(nodes, tmp_path, broker):
    """The broker forwards the bytes; the manager's check over what it received passes (a result and an event over MQTT)."""
    b = broker()
    d, dev = mqtt_boot(nodes, tmp_path, "pm", b)
    assert d.provision()["status"] == "ok"
    raw = b.iface_request_raw(dev, {"v": 1, "id": "h1", "op": "hello"})
    assert raw and verifies(raw, d.node_key, "result"), raw
    assert json.loads(raw)["usig_boot"] >= 1
    import threading
    got = []
    t = threading.Thread(target=lambda: got.extend(b.next_raw("mecon/%s/ev/health" % dev, timeout=40)))
    t.start()
    time.sleep(1.0)
    r = b.iface_request(dev, {"v": 1, "id": "s1", "op": "sub.start", "args": {"stream": "health", "period_s": 5, "lifetime_s": 60}})
    assert r["status"] == "ok", r
    t.join()
    assert got, "no health event over MQTT"
    assert verifies(got[0], d.node_key, "health"), got[0]
    assert json.loads(got[0])["ev"] == "health"


def test_a_result_names_what_it_answers(nodes):
    d = boot_companion(nodes, "pn", 37)
    assert d.provision()["status"] == "ok"
    h = d.i.request("hello")
    assert h["op"] == "hello" and "job_id" not in h and verifies(h.raw, d.node_key, "result"), h
    r = d.send(wrap(d.node_key, "config.apply", apply_name("Names"), job=7))
    assert r["status"] == "ok" and r["op"] == "config.apply" and r["job_id"] == 7 and verifies(r.raw, d.node_key, "result"), r
    assert members(r)[-5:-3] == ["op", "job_id"], "inside the signed text, before the signature members"
    u = d.i.request("no.such.op")
    assert u["status"] == "unknown-op" and u["op"] == "no.such.op" and verifies(u.raw, d.node_key, "result"), u
    # a refusal names the operation too, and the stored result of a resend names the same request
    old = d.send(wrap(d.node_key, "config.apply", apply_name("Old"), job=3, rid="s-old"))
    assert old["error"] == "replay" and old["op"] == "config.apply" and old["job_id"] == 3, old
    dup = d.send(wrap(d.node_key, "config.apply", apply_name("Names"), job=7))
    assert dup["op"] == "config.apply" and dup["job_id"] == 7 and verifies(dup.raw, d.node_key, "result"), dup
    # a result cannot be moved to another request of the same id: the signed text differs by op and job
    assert not verifies(r.raw.replace(b'"op":"config.apply"', b'"op":"config.get"'), d.node_key, "result")

