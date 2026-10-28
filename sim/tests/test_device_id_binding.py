"""The signed device_id binding (docs/contract/AUTHORITY.md section 3.5) on the simulated Companion (USB frames, the legacy
MQTT CLI topic) and the Repeater and Room console: `get device.id [nonce]` and `hello` carry the node key's signature over
MECON-DEVID-v1 0x00 || device_id || node_key [|| nonce], verified here with sim/ed25519_ref.py independently of the
firmware's signer; a bad nonce is refused; the provisioning read-back carries device_id and node_key."""
import base64

import pytest
from sim import ed25519_ref

from test_deployment_authority import V, boot as boot_companion
from test_deployment_authority_server import boot as boot_server
from test_device_interface import broker, mqtt_boot  # noqa: F401  (broker is a fixture)

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
    for nonce in NONCE_BAD + [""]:
        bad = i.request("hello", {"nonce": nonce})
        assert bad["status"] == "invalid", (nonce, bad)
    assert i.request("hello", {"nonce": 7})["status"] == "invalid"
    return dev


def check_cli(cli, dev, node_key):
    assert cli_answer(cli("get device.id"), dev, node_key)
    for nonce in NONCE_OK:
        assert cli_answer(cli("get device.id " + nonce), dev, node_key, nonce), nonce
    for nonce in NONCE_BAD:
        assert cli("get device.id " + nonce) == "Error: bad nonce", nonce
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
