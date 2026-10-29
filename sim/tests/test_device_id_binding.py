"""The signed device_id binding (docs/contract/AUTHORITY.md section 3.5) on the simulated Companion (USB frames, the legacy
MQTT CLI topic) and the Repeater and Room console: `get device.id [nonce]` and `hello` carry the node key's signature over
MECON-DEVID-v1 0x00 || device_id || node_key [|| nonce], verified here with sim/ed25519_ref.py independently of the
firmware's signer; a bad nonce is refused; the provisioning read-back carries device_id and node_key."""
import base64
import os
import sys

import pytest
from sim import ed25519_ref
from sim import harness
from sim.harness import CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402

from test_deployment_authority import V, boot as boot_companion
from test_deployment_authority_server import boot as boot_server
from test_device_interface import broker, mqtt_boot  # noqa: F401  (broker is a fixture)
from test_identity_rotate import rotate

NONCE_OK = ["a", "Az09._-", "x" * 32]
NONCE_BAD = ["x" * 33, "a b", "a:b", "a/b", "å"]


def devid_bytes(device_id, node_key, nonce=""):
    return b"MECON-DEVID-v1\x00" + bytes.fromhex(device_id) + bytes.fromhex(node_key) + nonce.encode()


def binds(sig_b64, device_id, node_key, nonce=""):
    sig = base64.b64decode(sig_b64)
    return len(sig) == 64 and ed25519_ref.verify(bytes.fromhex(node_key), devid_bytes(device_id, node_key, nonce), sig)


def cli_answer(reply, device_id, node_key, nonce=""):
    """True when a `get device.id` reply is "> <device_id> <sig>[ <nonce>]" and the signature binds the pair."""
    parts = reply.split(" ")
    want = 4 if nonce else 3
    if len(parts) != want or parts[0] != ">" or parts[1] != device_id or (nonce and parts[3] != nonce):
        return False
    return binds(parts[2], device_id, node_key, nonce)


def test_the_reference_verifier_agrees_with_the_public_vectors():
    node = V["keys"]["device node"]["public_key"]
    b, bn = V["device_id_binding"], V["device_id_binding_nonce"]
    assert devid_bytes(b["device_id"], node).hex() == b["signed_bytes_hex"]
    assert devid_bytes(bn["device_id"], node, bn["nonce"]).hex() == bn["signed_bytes_hex"]
    assert binds(b["sig"], b["device_id"], node)
    assert binds(bn["sig"], bn["device_id"], node, bn["nonce"])
    assert not binds(b["sig"], b["device_id"], node, bn["nonce"]), "the nonce form is a different message"
    other = V["keys"]["instance a"]["public_key"]
    assert not binds(b["sig"], b["device_id"], other), "the signature binds the node key"
    # A foreign claim verifies under its own signer's key, for the same device_id: the signature alone cannot refuse it,
    # which is why the Backend binds one device_id to one key (AUTHORITY.md section 3.5, rule 1).
    f = V["device_id_binding_foreign"]
    assert f["device_id"] == b["device_id"] and binds(f["sig"], f["device_id"], other)
    assert not binds(f["sig"], f["device_id"], node)


def check_hello(i, node_key):
    h = i.request("hello")["result"]
    dev = h["device_id"]
    assert len(dev) == 32 and h["node_key"] == node_key
    assert binds(h["device_id_sig"], dev, node_key)
    assert "device_id_sig_nonce" not in h and "nonce" not in h
    for nonce in NONCE_OK:
        hn = i.request("hello", {"nonce": nonce})
        assert hn["status"] == "ok", hn
        r = hn["result"]
        assert r["nonce"] == nonce and binds(r["device_id_sig_nonce"], dev, node_key, nonce), nonce
        assert binds(r["device_id_sig"], dev, node_key)
    # a signature for one nonce is no answer to another: an old (nonce, sig) pair replayed against a fresh nonce fails
    old = i.request("hello", {"nonce": "n-old"})["result"]["device_id_sig_nonce"]
    fresh = i.request("hello", {"nonce": "n-fresh"})["result"]
    assert binds(fresh["device_id_sig_nonce"], dev, node_key, "n-fresh")
    assert not binds(old, dev, node_key, "n-fresh") and binds(old, dev, node_key, "n-old")
    for nonce in NONCE_BAD + [""]:
        bad = i.request("hello", {"nonce": nonce})
        assert bad["status"] == "invalid", (nonce, bad)
    assert i.request("hello", {"nonce": 7})["status"] == "invalid"
    return dev


def check_cli(cli, dev, node_key):
    assert cli_answer(cli("get device.id"), dev, node_key)
    for nonce in NONCE_OK:
        assert cli_answer(cli("get device.id " + nonce), dev, node_key, nonce), nonce
    for nonce in NONCE_BAD + [""]:   # "" leaves a space with nothing after it: not the fixed form
        assert cli("get device.id " + nonce) == "Error: bad nonce", repr(nonce)
    # the longest answer, behind the "XX|" prefix the CLI echoes, arrives whole
    longest = "n" * 32
    r = cli("07|get device.id " + longest)
    assert r.startswith("07|") and cli_answer(r[3:], dev, node_key, longest), r
    # a longer word is not the command
    assert not cli("get device.idx").startswith("> " + dev)


def test_companion_hello_and_cli_sign_the_device_id(nodes):
    d = boot_companion(nodes, "dvc", 41)
    dev = check_hello(d.i, d.node_key)
    check_cli(d.c.cli, dev, d.node_key)
    # enrolled, the same: the binding never depends on the record
    assert d.provision()["status"] == "ok"
    assert check_hello(d.i, d.node_key) == dev
    check_cli(d.c.cli, dev, d.node_key)


@pytest.mark.parametrize("kind", ["repeater", "room_server"])
def test_console_hello_and_cli_sign_the_device_id(nodes, kind):
    d = boot_server(nodes, kind, "dv" + kind, 43)
    dev = check_hello(d.i, d.node_key)
    check_cli(d.cli.command, dev, d.node_key)
    assert d.provision()["status"] == "ok"
    assert check_hello(d.i, d.node_key) == dev
    check_cli(d.cli.command, dev, d.node_key)


@pytest.mark.timescale(10)
def test_the_legacy_cli_topic_answers_a_signed_device_id(nodes, tmp_path, broker):
    b = broker()
    n, c, i, dev = mqtt_boot(nodes, tmp_path, "dvm", b)
    key = i.request("hello")["result"]["node_key"]
    r = b.cli_request(c.pub_key[:4].hex(), "get device.id")
    assert r is not None and r["status"] == "ok" and cli_answer(r["reply"], dev, key), r
    r = b.cli_request(c.pub_key[:4].hex(), "get device.id m-1")
    assert r is not None and r["status"] == "ok" and cli_answer(r["reply"], dev, key, "m-1"), r
    longest = "n" * 32
    r = b.cli_request(c.pub_key[:4].hex(), "07|get device.id " + longest)
    assert r is not None and r["status"] == "ok" and r["reply"].startswith("07|"), r
    assert cli_answer(r["reply"][3:], dev, key, longest), r


def test_companion_provisioning_read_back_carries_the_pair(nodes):
    d = boot_companion(nodes, "dvp", 45)
    dev = d.i.request("hello")["result"]["device_id"]
    r = d.provision()
    assert r["status"] == "ok" and r["result"]["device_id"] == dev and r["result"]["node_key"] == d.node_key, r
    again = d.provision()
    assert again["result"]["duplicate"] is True and again["result"]["device_id"] == dev, again
    assert again["result"]["node_key"] == d.node_key
    g = d.i.request("deployment.get")["result"]
    assert "device_id" not in g and "node_key" not in g, "only the provisioning read-back carries the pair"


@pytest.mark.parametrize("kind", ["repeater", "room_server"])
def test_console_provisioning_read_back_carries_the_pair(nodes, kind):
    d = boot_server(nodes, kind, "dvp" + kind, 47)
    dev = d.i.request("hello")["result"]["device_id"]
    r = d.provision()
    assert r["status"] == "ok" and r["result"]["device_id"] == dev and r["result"]["node_key"] == d.node_key, r
    again = d.provision()
    assert again["result"]["duplicate"] is True and again["result"]["device_id"] == dev, again


def test_after_a_rotation_hello_signs_with_the_new_key_and_untrusted_links_never_see_the_id(nodes):
    """The rotation statement names device_id: identity.rotation is table-gated and deployment.get carries the statement
    only for a caller that may read. BLE on the published default PIN is untrusted, as is the open TCP port."""
    d = boot_companion(nodes, "dvr", 51)
    assert d.c.cli("set pin 123456").startswith("> pin is now")
    dev = d.i.request("hello")["result"]["device_id"]
    assert d.provision()["status"] == "ok"
    old_key = d.node_key
    r = rotate(d, 1, old_key)
    assert r["status"] == "ok", r
    d.reboot()
    assert d.node_key != old_key
    assert check_hello(d.i, d.node_key) == dev, "the binding is signed with the new key"
    assert not binds(d.i.request("hello")["result"]["device_id_sig"], dev, old_key)
    # the trusted cable sees the statement in both reads (the positive control for the checks below)
    assert dev in d.i.request("identity.rotation")["result"]["rotation"]["statement"]
    assert dev in d.i.request("deployment.get")["result"]["rotation"]["statement"]
    d.c.close()
    d.n.restart()
    b = CompanionClient(d.n, sock=d.n.connect_ble(123456).sock)
    b.device_query()
    b.app_start()
    bi = mecon_iface.IfaceClient(b._send, b._read_frame)
    for op in ("hello", "identity.rotation", "status.get"):
        m = bi.request(op)
        assert m["status"] == "refused", (op, m)
        assert dev.encode() not in m.raw, op
    g = bi.request("deployment.get")
    assert g["status"] == "ok" and "rotation" not in g["result"], g
    assert dev.encode() not in g.raw
    # the CLI frame itself is refused on this link, so the answer cannot carry the id
    f = b.request(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"get device.id")
    assert f is not None and f[0] == harness.RESP_ERR and f[1] == harness.ERR_NOT_PERMITTED, f
    assert dev.encode() not in f


@pytest.mark.parametrize("kind", ["companion", "repeater"])
def test_a_stored_write_result_cannot_be_fetched_under_another_operation(nodes, kind):
    """The result cache is keyed by id and bound to the operation: deployment.get (open to any caller) naming the id
    of a stored provisioning gets id-in-use, not the read-back with device_id and node_key."""
    from test_deployment_authority import apply_name, provision_args
    d = boot_companion(nodes, "dvk", 53) if kind == "companion" else boot_server(nodes, "repeater", "dvk", 55)
    # a write small enough to be kept: its id answers only the same operation
    a = d.i.request("config.apply", apply_name("Kept"), rid="a1")
    assert a["status"] == "ok", a
    g = d.i.request("deployment.get", rid="a1")
    assert g["status"] == "invalid" and g["error"] == "id-in-use", g
    again = d.i.request("config.apply", apply_name("Kept"), rid="a1")
    assert again.raw == a.raw, "the same operation under the same id still gets its stored result"
    # the provisioning read-back (device_id and node_key): held as too large to keep, and still bound to its operation
    p = d.i.request("deployment.provision", provision_args(d.node_key), rid="p1")
    assert p["status"] == "ok" and "device_id" in p["result"], p
    g = d.i.request("deployment.get", rid="p1")
    assert g["status"] == "invalid" and g["error"] == "id-in-use", g
    assert b"node_key" not in g.raw
