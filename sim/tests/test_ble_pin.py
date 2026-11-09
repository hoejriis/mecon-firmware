"""ble.pin.get (docs/contract/OPERATIONS.md, SECURITY_AND_AUTHORITY.md): the BLE passkey over physical USB, on any device,
enrolled or not (owner, 2026-11-09); over a verified broker session on a device with no Deployment record; to a member's
signed request with its nonce once enrolled. A paired phone, and an unsigned broker session once enrolled, are refused."""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from test_deployment_authority import Dev, broker, mqtt_boot, wrap  # noqa: E402,F401 (broker: the fixture)
from test_device_ops import next_job, shaped  # noqa: E402


def test_the_cable_reads_the_pin_before_and_after_enrolment_and_a_paired_phone_never(nodes):
    from test_jobs import PIN, ble, ble_boot
    n = ble_boot(nodes, "bpc", PIN)
    d = Dev(n)
    r = d.i.request("ble.pin.get", rid="pin1")
    assert r["status"] == "ok" and shaped("ble.pin.get", r["result"]) == {"pin": PIN}, r
    b, i = ble(n)
    r = i.request("ble.pin.get")
    assert r["status"] == "refused" and r["error"] == "not-over-this-link", r
    r = i.request("ble.pin.get", rid="pin1")
    assert r["status"] == "refused", "the cable's answer is never served from the cache to another caller: %s" % r
    b.close()
    assert d.provision()["status"] == "ok"
    assert d.i.request("ble.pin.get")["result"] == {"pin": PIN}, "the cable keeps it once enrolled"
    b, i = ble(n)
    assert i.request("ble.pin.get")["status"] == "refused"
    b.close()


def test_a_broker_session_reads_it_until_enrolled_then_only_signed(nodes, tmp_path, broker):
    b = broker()
    d, dev = mqtt_boot(nodes, tmp_path, "bpm", b)
    pin = d.i.request("ble.pin.get")["result"]["pin"]
    r = b.iface_request(dev, {"v": 1, "id": "p1", "op": "ble.pin.get"})
    assert r["status"] == "ok" and r["result"] == {"pin": pin}, r
    assert d.provision()["status"] == "ok"
    r = b.iface_request(dev, {"v": 1, "id": "p2", "op": "ble.pin.get"})
    assert r["status"] == "refused", "unsigned, once enrolled: %s" % r
    nonce = b.iface_request(dev, {"v": 1, "id": "h1", "op": "hello"})["result"]["channel_nonce"]
    r = b.iface_request(dev, wrap(d.node_key, "ble.pin.get", job=next_job(d), nonce=nonce))
    assert r["status"] == "ok" and r["result"] == {"pin": pin}, "a member's signature, with the link's nonce: %s" % r
    r = b.iface_request(dev, wrap(d.node_key, "ble.pin.get", job=next_job(d)))
    assert r["status"] != "ok", "without the nonce: %s" % r
