"""identity.rotate (docs/contract/AUTHORITY.md section 11) and revocation on the simulated Companion, Repeater and Room:
a rotation needs a member's signature and a readable record, replaces the node key and drops the contacts, keeps the
configuration, channels, posture, device_id and every part of the authority state, and a revoked member can neither
command the device nor rotate its key. Requests are signed here with sim/ed25519_ref.py; the DAK seed of the public
vectors signs the set that revokes a member."""
import base64
import json
import os
import sys

import pytest
from sim import ed25519_ref

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from test_deployment_authority import (DEP, INST_A, INST_B, SET1, V, apply_name, boot as boot_companion, wreck_record,  # noqa: E402
                                       revive, wrap)
from test_deployment_authority_server import boot as boot_server  # noqa: E402
from test_provenance import verifies  # noqa: E402

DAK1_SEED = bytes.fromhex(V["keys"]["dak epoch 1"]["seed_hex"])
PUB_B = V["keys"]["instance b"]["public_key"]
CONTACT = bytes(range(1, 33))
CHANNEL = (1, "kept", bytes(range(50, 66)))


def revoking_set(revision=2):
    """The DAK's next authority set (epoch 1): B admitted at this revision, A revoked."""
    text = json.dumps({"v": 1, "deployment_id": DEP, "authority_epoch": 1, "authority_revision": revision,
                       "issued_at": 1790002000,
                       "members": [{"instance_id": INST_B, "public_key": PUB_B, "admitted_revision": revision}],
                       "revoked": [INST_A]}, separators=(",", ":"))
    sig = ed25519_ref.sign(DAK1_SEED, b"MECON-AUTHSET-v1\x00" + text.encode())
    return {"authority_set": text, "authority_set_sig": base64.b64encode(sig).decode()}


def rotate(d, job, old_key, **kw):
    return d.send(wrap(old_key, "identity.rotate", job=job, nonce=d.nonce(), **kw))


def enrolled_companion(nodes, name, seed):
    d = boot_companion(nodes, name, seed)
    d.c.add_contact(CONTACT, "Alice")
    assert d.c.set_channel(*CHANNEL)[0] == 0
    assert d.provision()["status"] == "ok"
    return d


def members_of(g):
    return {m["instance_id"]: m for m in g["members"]}


def test_rotation_changes_the_key_and_keeps_everything_else(nodes):
    d = enrolled_companion(nodes, "rk", 41)
    assert d.send(wrap(d.node_key, "config.apply", apply_name("Keeper"), job=1))["status"] == "ok"
    assert d.c.get_contacts()[0] == 1 and d.c.get_channel(1) is not None
    hello0 = d.i.request("hello")["result"]
    g0 = d.i.request("deployment.get")["result"]
    posture0 = d.i.request("status.get")["result"]["posture"]
    old_key = d.node_key

    r = rotate(d, 2, old_key)
    assert r["status"] == "ok", r
    res = r["result"]
    assert res["old_public_key"] == old_key and res["new_public_key"] != old_key and len(res["new_public_key"]) == 64
    assert verifies(r.raw, old_key, "result"), "the answer is signed by the key being retired, so its manager can verify it"
    assert not verifies(r.raw, res["new_public_key"], "result")
    assert {"node_key", "contacts", "message_queue"} <= set(res["removed"])
    assert {"configuration", "record", "channels", "posture", "device_id"} <= set(res["kept"])

    new_key = res["new_public_key"]
    hello1 = d.i.request("hello")["result"]
    assert hello1["node_key"] == new_key and hello1["device_id"] == hello0["device_id"]
    assert verifies(d.i.request("hello").raw, new_key, "result"), "from now on the new key signs"
    assert d.c.get_contacts()[0] == 0, "the contacts belonged to the old key"
    assert d.name() == "> Keeper" and d.c.get_channel(1) == (CHANNEL[1], CHANNEL[2]), "configuration and channels stay"
    assert d.i.request("status.get")["result"]["posture"] == posture0

    # the authority state is untouched (the issuer's mark moved by the rotation request itself)
    g1 = d.i.request("deployment.get")["result"]
    assert members_of(g1)[INST_A]["mark"] == 2
    for g in (g0, g1):
        members_of(g)[INST_A]["mark"] = 0
    assert g1 == g0, "same DAK, set, revision and members"

    # a request aimed at the old key is another device's; one aimed at the new key works
    old = d.send(wrap(old_key, "config.apply", apply_name("ToOld"), job=3))
    assert old["status"] == "refused" and old["error"] == "wrong_device", old
    new = d.send(wrap(new_key, "config.apply", apply_name("ToNew"), job=3))
    assert new["status"] == "ok" and d.name() == "> ToNew"

    # the key is the node's identity and not authority: nothing is granted by it, old or new
    for key in (old_key, new_key):
        u = d.i.request("config.apply", apply_name("Nope"))
        assert u["status"] == "refused" and u["error"] == "unauthenticated", u
        stranger = d.send(wrap(key, "config.apply", apply_name("Nope"), job=1, issuer=INST_B))
        assert stranger["status"] == "refused" and stranger["error"] == "unknown_issuer", stranger
    assert d.name() == "> ToNew"
    assert d.i.request("status.get")["result"]["deployment"]["deployment_id"] == DEP

    # the new key and the unchanged record survive a reboot
    d.reboot()
    assert d.node_key == new_key
    g2 = d.i.request("deployment.get")["result"]
    assert members_of(g2)[INST_A]["mark"] == 3
    members_of(g2)[INST_A]["mark"] = 0
    assert g2 == g0
    assert d.c.get_contacts()[0] == 0 and d.name() == "> ToNew"


def test_rotation_needs_a_signature_and_a_readable_record(nodes):
    d = boot_companion(nodes, "rn", 43)
    key = d.node_key
    u = d.i.request("identity.rotate")
    assert u["status"] == "refused" and u["error"] == "not_enrolled", "no record, no new owner: %s" % u
    assert d.i.request("hello")["result"]["node_key"] == key
    assert d.provision()["status"] == "ok"
    u = d.i.request("identity.rotate")
    assert u["status"] == "refused" and u["error"] == "unauthenticated", u
    # signed, but without the channel nonce of this link: sensitive
    r = d.send(wrap(key, "identity.rotate", job=1))
    assert r["status"] == "refused" and r["error"] == "bad_channel_nonce", r
    # a damaged record: every request but recovery is refused, and the key stays
    d.n.stop()
    wreck_record(d)
    revive(d)
    for r in (d.i.request("identity.rotate"), d.send(wrap(key, "identity.rotate", job=2, nonce=d.nonce()))):
        assert r["status"] == "refused" and r["error"] == "deployment_record_corrupt", r
    assert d.i.request("hello")["result"]["node_key"] == key


@pytest.mark.parametrize("kind", ["companion", "repeater"])
def test_a_revoked_member_is_refused_and_cannot_rotate_the_key(nodes, kind):
    d = enrolled_companion(nodes, "rv", 45) if kind == "companion" else boot_server(nodes, "repeater", "rv", 45)
    if kind != "companion":
        assert d.provision()["status"] == "ok"
    key = d.node_key
    assert d.send(wrap(key, "config.apply", apply_name("Before"), job=1))["status"] == "ok"
    upd = d.i.request("deployment.authority_update", revoking_set(2))
    assert upd["status"] == "ok", upd
    g = d.i.request("deployment.get")["result"]
    assert g["authority_revision"] == 2 and list(members_of(g)) == [INST_B]
    # the revoked member is refused for an ordinary request ...
    r = d.send(wrap(key, "config.apply", apply_name("Revoked"), job=2, rev=2))
    assert r["status"] == "refused" and r["error"] == "issuer_revoked", r
    assert d.name() == "> Before"
    # ... and for the rotation: it is refused, and the node key is the same
    r = rotate(d, 3, key, rev=2)
    assert r["status"] == "refused" and r["error"] == "issuer_revoked", r
    assert d.i.request("hello")["result"]["node_key"] == key
    if kind == "companion":
        assert d.c.get_contacts()[0] == 1, "nothing was dropped"
    d.reboot()
    assert d.node_key == key


@pytest.mark.parametrize("kind", ["repeater", "room_server"])
def test_the_server_roles_do_not_rotate_their_key(nodes, kind):
    d = boot_server(nodes, kind, "rs" + kind, 47)
    assert d.provision()["status"] == "ok"
    key = d.node_key
    r = d.send(wrap(key, "identity.rotate", job=1, nonce=d.nonce()))
    assert r["status"] == "unknown-op", r
    assert d.i.request("hello")["result"]["node_key"] == key
