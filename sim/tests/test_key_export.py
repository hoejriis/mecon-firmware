"""identity.key.export (OPERATIONS.md 5.1; SECURITY_AND_AUTHORITY.md, private-key custody; the owner's decision of
2026-10-29, "Fork rule once enrolled"). With no Deployment record the key goes in clear to the cable, and on a Companion to
a phone paired with its own PIN. Once enrolled, only a member's signed request with the link's nonce gets it, wrapped for
that member (mecon-kx-v1, tools/mecon_kx.py). The wrap is checked byte for byte against the plain-Python reference, then
unwrapped with the member's seed."""
import base64
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_kx  # noqa: E402
from test_deployment_authority import (INST_A, SEEDS, boot as dev_boot, broker, mqtt_boot, wrap,  # noqa: E402,F401
                                       wreck_record)
from test_deployment_authority_server import boot as srv_boot  # noqa: E402
from test_device_ops import next_job, shaped  # noqa: E402


def clear_key(d):
    r = d.i.request("identity.key.export")
    assert r["status"] == "ok", r
    k = shaped("identity.key.export", r["result"])["key"]
    assert len(k) == 128, k
    return bytes.fromhex(k)


def signed_export(d, job):
    return d.send(wrap(d.node_key, "identity.key.export", job=job, nonce=d.nonce(), issuer=INST_A))


def check_wrapped(d, r, job, key):
    assert r["status"] == "ok", r
    res = shaped("identity.key.export", r["result"])
    assert "key" not in res and res["alg"] == "mecon-kx-v1" and res["for"] == INST_A, res
    w = base64.b64decode(res["wrapped"])
    node_pub = bytes.fromhex(d.node_key)
    shared = mecon_kx.shared_secret(SEEDS[INST_A], node_pub)
    assert w == mecon_kx.wrap(shared, job, key), "the firmware's wrap is the reference's, byte for byte"
    assert mecon_kx.unwrap(SEEDS[INST_A], node_pub, job, w) == key, "and the member unwraps the device's own key"
    bad = bytearray(w)
    bad[3] ^= 1
    with pytest.raises(ValueError):
        mecon_kx.unwrap(SEEDS[INST_A], node_pub, job, bytes(bad))


@pytest.mark.parametrize("kind", ["companion", "repeater"])
def test_clear_with_no_record_wrapped_for_the_signing_member_once_enrolled(nodes, kind):
    d = dev_boot(nodes, "ke" + kind[:2]) if kind == "companion" else srv_boot(nodes, "repeater", "ke" + kind[:2])
    key = clear_key(d)
    assert d.provision()["status"] == "ok"
    r = d.i.request("identity.key.export")
    assert r["status"] == "refused" and r["error"] == "signed-only", "never in clear once enrolled, cable included: %s" % r
    job = next_job(d)
    check_wrapped(d, signed_export(d, job), job, key)
    r = d.send(wrap(d.node_key, "identity.key.export", job=next_job(d), issuer=INST_A))
    assert r["status"] != "ok", "without the link's nonce: %s" % r


def test_a_paired_phone_may_export_on_a_standalone_companion_only(nodes):
    from test_jobs import PIN, ble, ble_boot
    from test_deployment_authority import Dev
    n = ble_boot(nodes, "kep", PIN)
    d = Dev(n)
    key = clear_key(d)
    b, i = ble(n)
    r = i.request("identity.key.export")
    assert r["status"] == "ok" and bytes.fromhex(r["result"]["key"]) == key, "stock: the device's own PIN pairing: %s" % r
    b.close()
    assert d.provision()["status"] == "ok"
    b, i = ble(n)
    r = i.request("identity.key.export")
    assert r["status"] == "refused", r
    b.close()


def test_a_broker_never_gets_it_unsigned(nodes, tmp_path, broker):
    b = broker()
    d, dev = mqtt_boot(nodes, tmp_path, "keb", b)
    r = b.iface_request(dev, {"v": 1, "id": "k1", "op": "identity.key.export"})
    assert r["status"] == "refused" and r["error"] == "not-over-this-link", r


def test_a_corrupt_record_leaves_the_cable_and_hardened_leaves_nothing(nodes):
    d = dev_boot(nodes, "kec")
    key = clear_key(d)
    assert d.provision()["status"] == "ok"
    d.n.stop()
    wreck_record(d)
    d.n.start()
    d.attach()
    assert clear_key(d) == key, "no authority can be verified: the cable, as SECURITY_AND_AUTHORITY says"
    h = dev_boot(nodes, "keh", seed=23)
    assert h.i.request("posture.set", {"posture": "hardened"})["status"] == "ok"
    assert h.n.wait_exit(10) == 75
    h.n.start()
    h.attach()
    r = h.i.request("identity.key.export")
    assert r["status"] == "refused" and r["error"] == "hardened", r


def test_the_published_vector_is_what_the_firmware_makes(nodes):
    # test/fixtures/key_export_v1.json: the vector a Backend checks its unwrap against. The firmware, given the vector's
    # node key and asked by its member under its job id, must produce exactly its wrap.
    import json
    v = json.load(open(os.path.join(os.path.dirname(__file__), "..", "..", "test", "fixtures", "key_export_v1.json")))
    d = dev_boot(nodes, "kev")
    f = d.c.request(bytes([24]) + bytes.fromhex(v["node"]["private_key_hex"]))   # CMD_IMPORT_PRIVATE_KEY, no record yet
    assert f[0] == 0, f
    d.reboot()
    assert d.node_key == v["node"]["public_key"], "the imported key is the node key"
    assert d.provision()["status"] == "ok"
    r = d.send(wrap(d.node_key, "identity.key.export", job=v["job_id"], nonce=d.nonce(), issuer=INST_A))
    assert r["status"] == "ok" and r["result"]["wrapped"] == v["wrapped_b64"], r
    assert r["result"]["for"] == v["member"]["instance_id"]
