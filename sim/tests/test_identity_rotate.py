"""identity.rotate (docs/contract/AUTHORITY.md section 11) and revocation on the simulated Companion, Repeater and Room:
a rotation needs a member's signature and a readable record, replaces the node key and drops the contacts (a server's ACL,
and a room's posts), keeps the
configuration, channels, posture, device_id and every part of the authority state, and a revoked member can neither
command the device nor rotate its key. Requests are signed here with sim/ed25519_ref.py; the DAK seed of the public
vectors signs the set that revokes a member."""
import base64
import json
import os
import re
import sys
import time

import pytest
from sim import ed25519_ref
from sim.ether import Ether  # noqa: F401
from sim.harness import CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from test_deployment_authority import (update_args, Dev, RTC, DEP, INST_A, INST_B, SET1, V, apply_name, boot as boot_companion, wreck_record,  # noqa: E402
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
    assert g1.pop("rotation")["statement"].startswith('{"v":1,"old":"%s"' % old_key), "deployment.get carries the statement"
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
    g2.pop("rotation")
    assert members_of(g2)[INST_A]["mark"] >= 3, "a reboot lands on the reserved mark, never below what ran"
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


# ------------------------------------------------------------------ the rotation statement (AUTHORITY.md section 11)

def rotation_file(d):
    return os.path.join(d.n.dir, "rotation")


def statement_checks(rot, old_key, new_key, device_id, job):
    """The stored statement: the exact text the old key signed, and that signature, checked here with the reference verifier."""
    stmt = json.loads(rot["statement"])
    assert stmt == {"v": 1, "old": old_key, "new": new_key, "device_id": device_id, "job_id": job}, stmt
    msg = b"MECON-NODEROT-v1\x00" + rot["statement"].encode()
    assert ed25519_ref.verify(bytes.fromhex(old_key), msg, base64.b64decode(rot["sig"])), "signed by the old key"
    assert not ed25519_ref.verify(bytes.fromhex(new_key), msg, base64.b64decode(rot["sig"])), "and not by the new one"


def test_a_lost_rotate_result_is_recovered_from_the_stored_statement(nodes):
    d = enrolled_companion(nodes, "rl", 51)
    old_key = d.node_key
    assert "rotation" not in d.i.request("identity.rotation")["result"] and \
        "rotation" not in d.i.request("deployment.get")["result"], "a key never rotated has no statement"
    w = wrap(old_key, "identity.rotate", job=2, nonce=d.nonce())
    d.i.send_raw(json.dumps(w, separators=(",", ":")).encode())
    lost = d.i.wait_result("s-2-identity.rotate")   # the bridge drops it: from here on the manager never has this result
    new_key = lost["result"]["new_public_key"]
    # the resend is another device's request now, so the result cannot be fetched again by repeating the request
    again = d.send(w)
    assert again["status"] == "refused" and again["error"] == "wrong_device", again
    # the manager reads hello (signed by the new key, which it cannot yet trust) and the statement (signed by the old key)
    h = d.i.request("hello")
    assert h["result"]["node_key"] == new_key and "identity.rotation" in h["result"]["capabilities"]
    rot = d.i.request("identity.rotation")["result"]["rotation"]
    statement_checks(rot, old_key, new_key, h["result"]["device_id"], 2)
    # the same through a signed plain read (no job id, no nonce) and through deployment.get
    sr = d.send(wrap(new_key, "identity.rotation", job=None, target=new_key, rid="rr"))
    assert sr["status"] == "ok" and sr["result"]["rotation"] == rot, sr
    assert d.i.request("deployment.get")["result"]["rotation"] == rot
    assert open(rotation_file(d), "rb").read().endswith(b"\n"), "the drop is done, so nothing is pending"
    d.reboot()
    assert d.i.request("identity.rotation")["result"]["rotation"] == rot, "kept across a reboot"
    # a second rotation replaces it
    # (the reboot landed on the reserved mark, so the manager's next id goes above it)
    job = members_of(d.i.request("deployment.get")["result"])[INST_A]["mark"] + 1
    r2 = d.send(wrap(new_key, "identity.rotate", job=job, nonce=d.nonce(), target=new_key))
    assert r2["status"] == "ok", r2
    rot2 = d.i.request("identity.rotation")["result"]["rotation"]
    statement_checks(rot2, new_key, r2["result"]["new_public_key"], h["result"]["device_id"], job)


def contacts_on_flash_now(d):
    return [f for f in os.listdir(d.n.dir) if re.fullmatch(r"ct\d+", f)]


def contacts_on_flash(d):
    """The contact store writes lazily (five seconds after a change): wait until its bucket file exists."""
    end = time.time() + 20
    while time.time() < end:
        found = [f for f in os.listdir(d.n.dir) if re.fullmatch(r"ct\d+", f)]
        if found:
            return found
        time.sleep(0.5)
    raise AssertionError("the contact was never written")


def start_with_fault(d, fault_before):
    d.n.stop()
    d.n.fault_before = fault_before
    d.n.start()
    d.attach()


def rotate_into_a_cut(d, job):
    d.i.send_raw(json.dumps(wrap(d.node_key, "identity.rotate", job=job, nonce=d.nonce()), separators=(",", ":")).encode())
    assert d.n.wait_exit(10) == 99, "the rotation never reached the injected power cut"
    d.n.fault_before = None


def test_a_cut_after_the_statement_and_before_the_key_save_leaves_the_key(nodes):
    d = enrolled_companion(nodes, "rc", 53)
    key = d.node_key
    contacts_on_flash(d)
    start_with_fault(d, "rename:/identity/_main.id.tmp")   # the key's atomic save, just before it is swapped in
    rotate_into_a_cut(d, 2)
    assert os.path.exists(rotation_file(d)), "the statement was stored before the key save"
    d.n.start()
    d.attach()
    assert d.node_key == key, "the key change never took effect"
    assert "rotation" not in d.i.request("identity.rotation")["result"], "so its statement is deleted at boot"
    assert not os.path.exists(rotation_file(d))
    assert d.c.get_contacts()[0] == 1, "and nothing was dropped"


def test_a_cut_after_the_key_save_and_before_the_contacts_drop_finishes_at_boot(nodes):
    d = enrolled_companion(nodes, "rd", 55)
    old_key = d.node_key
    cts = contacts_on_flash(d)
    assert len(cts) == 1, os.listdir(d.n.dir)
    start_with_fault(d, "remove:/" + cts[0])   # the first step of the drop
    rotate_into_a_cut(d, 2)
    assert open(rotation_file(d), "rb").read().endswith(b"\nP"), "the statement still says the drop is pending"
    d.n.start()
    d.attach()
    assert d.node_key != old_key, "the new key was saved before the cut"
    assert d.c.get_contacts()[0] == 0, "the old owner's contacts are gone after the boot"
    assert open(rotation_file(d), "rb").read().endswith(b"\n"), "and the pending mark is cleared"
    rot = d.i.request("identity.rotation")["result"]["rotation"]
    statement_checks(rot, old_key, d.node_key, d.i.request("hello")["result"]["device_id"], 2)
    d.reboot()
    assert d.c.get_contacts()[0] == 0 and d.i.request("identity.rotation")["result"]["rotation"] == rot


def sign_with_native_signer(c, data):
    c._send(bytes([33]))
    start = c._read_frame(5)
    assert start[0] == 19, start
    c._send(bytes([34]) + data)
    assert c._read_frame(5)[0] == 0
    c._send(bytes([35]))
    return c._read_frame(5)


def test_the_native_signer_never_signs_bytes_that_look_like_a_mecon_message(nodes):
    d = boot_companion(nodes, "rs", 57)   # not enrolled: the native signer is open, so this is its attack surface
    plain = sign_with_native_signer(d.c, b"hello world")
    assert plain[0] == 20 and ed25519_ref.verify(bytes.fromhex(d.node_key), b"hello world", plain[1:65]), "ordinary data signs"
    for text in (b"MECON-UPLINK-v1\x00result\x00{}", b"MECON-NODEROT-v1\x00{}", b"MECON-"):
        r = sign_with_native_signer(d.c, text)
        assert r[0] == 1 and r[1] == 6, "ILLEGAL_ARG, not a signature: %r" % r
    assert sign_with_native_signer(d.c, b"MECON")[0] == 20, "only the MECON- prefix is held back"


# ------------------------------------------------------------------ the server roles: the same steps, their own drop

SERVERS = ["repeater", "room_server"]
ACL_KEY = "ab" * 32


def acl_file(d):
    return os.path.join(d.n.dir, "s_contacts")


def enrolled_server(nodes, kind, name, seed, **kw):
    """Enrolled, holding one ACL entry on flash."""
    d = boot_server(nodes, kind, name, seed, **kw)
    assert d.cli.command("setperm %s 3" % ACL_KEY) == "OK"
    end = time.time() + 20
    while time.time() < end and not os.path.exists(acl_file(d)):
        time.sleep(0.3)
    assert os.path.exists(acl_file(d)), "the ACL entry was never written"
    assert d.provision()["status"] == "ok"
    return d


@pytest.mark.parametrize("kind", SERVERS)
def test_a_server_rotation_changes_the_key_drops_the_acl_and_keeps_the_rest(nodes, kind):
    d = enrolled_server(nodes, kind, "sr" + kind[:2], 61)
    old_key = d.node_key
    device_id = d.i.request("hello")["result"]["device_id"]
    assert d.send(wrap(old_key, "config.apply", apply_name("Kept"), job=1))["status"] == "ok"
    g0 = d.i.request("deployment.get")["result"]
    assert "rotation" not in g0 and "rotation" not in d.i.request("identity.rotation")["result"]
    r = rotate(d, 2, old_key)
    assert r["status"] == "ok", r
    assert verifies(r.raw, old_key, "result"), "the answer is signed by the key being retired, so its manager can verify it"
    new_key = r["result"]["new_public_key"]
    assert r["result"]["old_public_key"] == old_key and new_key != old_key, r
    removed = ["node_key", "acl"] + (["posts"] if kind == "room_server" else [])
    assert r["result"]["removed"] == removed, r
    assert r["result"]["kept"] == ["configuration", "record", "posture", "device_id"], r
    assert not os.path.exists(acl_file(d)), "the shared secrets were made with the old key"
    h = d.i.request("hello")["result"]
    assert h["node_key"] == new_key and h["device_id"] == device_id, h
    statement_checks(d.i.request("identity.rotation")["result"]["rotation"], old_key, new_key, device_id, 2)
    g1 = d.i.request("deployment.get")["result"]
    assert g1["rotation"] == d.i.request("identity.rotation")["result"]["rotation"]
    assert open(rotation_file(d), "rb").read().endswith(b"\n"), "the drop is done, so nothing is pending"
    old = d.send(wrap(old_key, "config.apply", apply_name("ToOld"), job=3))
    assert old["status"] == "refused" and old["error"] == "wrong_device", old
    assert d.send(wrap(new_key, "config.apply", apply_name("ToNew"), job=3))["status"] == "ok"
    assert d.name() == "> ToNew"
    d.reboot()
    assert d.node_key == new_key, "the new key survives a reboot"
    assert d.i.request("status.get")["result"]["deployment"]["deployment_id"] == DEP
    assert not os.path.exists(acl_file(d))
    statement_checks(d.i.request("identity.rotation")["result"]["rotation"], old_key, new_key, device_id, 2)


@pytest.mark.parametrize("kind", SERVERS)
def test_a_server_rotation_needs_a_signature_and_a_record(nodes, kind):
    d = boot_server(nodes, kind, "sn" + kind[:2], 63)
    key = d.node_key
    r = d.i.request("identity.rotate")
    assert r["status"] == "refused" and r["error"] == "not_enrolled", r
    assert d.provision()["status"] == "ok"
    r = d.i.request("identity.rotate")
    assert r["status"] == "refused" and r["error"] == "unauthenticated", r
    r = d.send(wrap(key, "identity.rotate", job=1))
    assert r["status"] == "refused", "sensitive: it needs the channel nonce too"
    assert d.i.request("hello")["result"]["node_key"] == key
    wreck_record(d)
    d.reboot()
    r = d.send(wrap(key, "identity.rotate", job=2))
    assert r["status"] == "refused", r
    assert d.i.request("hello")["result"]["node_key"] == key


@pytest.mark.parametrize("kind", SERVERS)
def test_a_cut_before_the_key_save_leaves_a_server_as_it_was(nodes, kind):
    d = enrolled_server(nodes, kind, "sc" + kind[:2], 65)
    key = d.node_key
    d.n.stop()
    d.n.fault_before = "rename:/identity/_main.id.tmp"
    d.n.start()
    d.attach()
    d.i.send_raw(json.dumps(wrap(key, "identity.rotate", job=2, nonce=d.nonce()), separators=(",", ":")))
    assert d.n.wait_exit(10) == 99, "the rotation never reached the injected power cut"
    d.n.fault_before = None
    assert os.path.exists(rotation_file(d)), "the statement was stored before the key save"
    d.n.start()
    d.attach()
    assert d.node_key == key, "the key change never took effect"
    assert "rotation" not in d.i.request("identity.rotation")["result"] and not os.path.exists(rotation_file(d))
    assert os.path.getsize(acl_file(d)) > 0, "and nothing was dropped"


@pytest.mark.parametrize("kind", SERVERS)
def test_a_cut_before_a_server_drop_finishes_at_boot(nodes, kind):
    d = enrolled_server(nodes, kind, "sd" + kind[:2], 67)
    old_key = d.node_key
    device_id = d.i.request("hello")["result"]["device_id"]
    d.n.stop()
    d.n.fault_before = "remove:/s_contacts"
    d.n.start()
    d.attach()
    d.i.send_raw(json.dumps(wrap(old_key, "identity.rotate", job=2, nonce=d.nonce()), separators=(",", ":")))
    assert d.n.wait_exit(10) == 99, "the rotation never reached the injected power cut"
    d.n.fault_before = None
    assert open(rotation_file(d), "rb").read().endswith(b"\nP"), "the statement still says the drop is pending"
    assert os.path.exists(acl_file(d))
    d.n.start()
    d.attach()
    assert d.node_key != old_key, "the new key was saved before the cut"
    assert not os.path.exists(acl_file(d)), "the old owner's clients are gone after the boot"
    assert open(rotation_file(d), "rb").read().endswith(b"\n"), "and the pending mark is cleared"
    statement_checks(d.i.request("identity.rotation")["result"]["rotation"], old_key, d.node_key, device_id, 2)


def test_a_rotated_room_keeps_no_posts_of_the_old_owner(nodes, ether):
    room = boot_server(nodes, "room_server", "sp", 69, ether_path=ether.path)
    assert room.cli.command("set guest.password joinme").startswith("OK")
    assert room.provision()["status"] == "ok"
    na = nodes("companion_standalone", "spa", ether_path=ether.path, seed=71, rtc=1792800000).start()
    nb = nodes("companion_standalone", "spb", ether_path=ether.path, seed=72, rtc=1792800000).start()
    nc = nodes("companion_standalone", "spc", ether_path=ether.path, seed=73, rtc=1792800000).start()
    clients = []
    for n in (na, nb, nc):
        c = CompanionClient(n)
        c.device_query()
        c.app_start()
        clients.append(c)
    alice, bob, carol = clients
    assert ether.wait_for_nodes(["sp", "spa", "spb", "spc"])
    for name in ("spa", "spb", "spc"):
        ether.set_link("sp", name)
    old_key = bytes.fromhex(room.node_key)
    assert alice.add_contact(old_key, "room", ctype=3)[0] == 0
    assert bob.add_contact(old_key, "room", ctype=3)[0] == 0
    assert alice.login(old_key, "joinme", timeout=75)
    assert alice.send_direct(old_key, "the old owner's post", 1792800100)[0] != 1
    time.sleep(3.0)
    assert bob.login(old_key, "joinme", timeout=75)
    got = bob.sync_messages(timeout=60, want=1)
    assert got and "the old owner's post" in got[0]["text"], "with no rotation a new member is given it: %s" % got
    r = rotate(room, 1, room.node_key)
    assert r["status"] == "ok", r
    new_key = bytes.fromhex(r["result"]["new_public_key"])
    assert carol.add_contact(new_key, "room", ctype=3)[0] == 0
    time.sleep(3.0)   # let the air clear: a native login is not retried, and a collision would lose it
    assert carol.login(new_key, "joinme", timeout=25) or carol.login(new_key, "joinme", timeout=25)
    assert carol.sync_messages(timeout=25, want=1) == [], "nothing of the old owner's room reaches a new member"


# ------------------------------------------------------------------ the refusals: a rotation that cannot run changes nothing

ALL_KINDS = ["companion_standalone"] + SERVERS


def enrolled_any(nodes, kind, name, seed):
    if kind != "companion_standalone":
        return enrolled_server(nodes, kind, name, seed)
    d = enrolled_companion(nodes, name, seed)
    contacts_on_flash(d)   # a restart below would otherwise lose it: the store writes lazily
    return d


def old_state_kept(d, kind):
    if kind == "companion_standalone":
        return d.c.get_contacts()[0] == 1
    return os.path.getsize(acl_file(d)) > 0


def assert_nothing_changed(d, kind, key):
    assert d.i.request("hello")["result"]["node_key"] == key, "the key stays"
    assert "rotation" not in d.i.request("identity.rotation")["result"] and not os.path.exists(rotation_file(d))
    assert old_state_kept(d, kind), "and nothing was dropped"


def block(d, path):
    """The path cannot be written or removed: a directory that holds a file."""
    full = os.path.join(d.n.dir, path)
    if os.path.exists(full):
        os.remove(full)
    os.makedirs(os.path.join(full, "keep"))
    return full


def unblock(full):
    os.rmdir(os.path.join(full, "keep"))
    os.rmdir(full)


def stuck_source(d):
    """Restart the node reading its random bytes through a link, and return what re-points that link."""
    link = os.path.join(d.n.dir, "random-source")
    os.symlink("/dev/urandom", link)
    d.n.stop()
    d.n.extra_env = {"SIM_RANDOM_FILE": link}
    d.n.start()
    d.attach()

    def point_at(target):
        os.remove(link)
        os.symlink(target, link)
    return point_at


@pytest.mark.parametrize("kind", ALL_KINDS)
@pytest.mark.parametrize("source", ["zero", "pattern"])
def test_a_random_source_that_fails_refuses_the_rotation(nodes, kind, source):
    d = enrolled_any(nodes, kind, "rr" + kind[:2] + source[0], 71)
    key = d.node_key
    point_at = stuck_source(d)
    target = "/dev/zero"
    if source == "pattern":   # not zero, and only 15 byte values, one short of what a draw must have
        target = os.path.join(d.n.dir, "pattern-source")
        with open(target, "wb") as f:
            f.write(bytes(range(1, 16)) * 8)
    nonce = d.nonce()
    point_at(target)
    r = d.send(wrap(key, "identity.rotate", job=2, nonce=nonce))
    assert r["status"] == "failed" and r["error"] == "rng", r
    assert verifies(r.raw, key, "result"), "the refusal is signed, so the manager can trust it"
    assert_nothing_changed(d, kind, key)
    point_at("/dev/urandom")
    r = rotate(d, 3, key)
    assert r["status"] == "ok" and r["result"]["new_public_key"] != key, "a source that recovers can rotate"


@pytest.mark.parametrize("kind", ALL_KINDS)
@pytest.mark.parametrize("blocked", ["rotation.tmp", "identity/_main.id.tmp"])
def test_a_rotation_that_cannot_save_leaves_the_device_as_it_was(nodes, kind, blocked):
    d = enrolled_any(nodes, kind, "rs" + kind[:2] + blocked[0], 73)
    key = d.node_key
    full = block(d, blocked)
    r = rotate(d, 2, key)
    assert r["status"] == "busy" or r["status"] == "failed", r
    assert r["error"] == "storage", r
    assert verifies(r.raw, key, "result"), "signed by the key that stayed"
    assert_nothing_changed(d, kind, key)
    unblock(full)
    assert rotate(d, 3, key)["status"] == "ok", "the next try runs once the store works"


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_a_drop_that_fails_says_so_and_the_next_boot_finishes_it(nodes, kind):
    d = enrolled_any(nodes, kind, "rd" + kind[:2], 75)
    old_key = d.node_key
    device_id = d.i.request("hello")["result"]["device_id"]
    if kind == "companion_standalone":   # the store lists its buckets, so a directory in the way would not be seen as one
        contacts_on_flash(d)
        flag = os.path.join(d.n.dir, "no_remove.flag")
        d.n.extra_env["SIM_FS_NO_BUCKET_REMOVE"] = flag
        d.reboot()
        old_key = d.node_key
        open(flag, "w").close()
    else:
        full = block(d, "s_contacts")
    r = rotate(d, 2, old_key)
    assert r["status"] == "failed" and r["error"] == "contacts_not_dropped", r
    assert verifies(r.raw, old_key, "result"), "signed by the retired key, the one its manager knows"
    new_key = d.i.request("hello")["result"]["node_key"]
    assert new_key != old_key, "the key had already changed, so the old secrets are useless to anyone holding the new one"
    rot = d.i.request("identity.rotation")["result"]["rotation"]
    statement_checks(rot, old_key, new_key, device_id, 2)
    assert open(rotation_file(d), "rb").read().endswith(b"\nP"), "the statement still says the drop is pending"
    if kind == "companion_standalone":
        os.remove(flag)
    else:
        unblock(full)
    d.reboot()
    assert d.node_key == new_key
    assert open(rotation_file(d), "rb").read().endswith(b"\n"), "the boot finished the drop"
    assert d.i.request("identity.rotation")["result"]["rotation"] == rot
    if kind == "companion_standalone":
        assert d.c.get_contacts()[0] == 0
    else:
        assert not os.path.exists(acl_file(d))


def test_a_rotation_looks_up_no_missing_contact_bucket(nodes, tmp_path):
    """On SPIFFS a lookup that finds nothing scans the whole file table: per-bucket lookups held the loop for 52 s on a
    Heltec V3 and about 2 minutes on a V4, with no contacts, and the broker session and the signed result went with them."""
    calls = str(tmp_path / "rot.calls")
    n = nodes("companion_standalone", "rlook", seed=23, rtc=RTC, extra=["--fs-calls", calls]).start()
    d = Dev(n)
    d.c.add_contact(CONTACT, "Alice")
    assert d.provision()["status"] == "ok"
    contacts_on_flash(d)

    def missing():
        return int(re.search(r"missing=(\d+)", open(calls).read()).group(1))
    before = missing()
    r = rotate(d, 2, d.node_key)
    assert r["status"] == "ok", r
    assert missing() - before < 12, "a handful for the statement's and the key's own saves, none per possible bucket"
    assert d.c.get_contacts()[0] == 0 and not contacts_on_flash_now(d)


def test_a_rotation_removes_every_stored_advert(nodes):
    d = enrolled_companion(nodes, "rblob", 24)
    bl = os.path.join(d.n.dir, "bl")
    os.makedirs(bl, exist_ok=True)
    for i in range(20):   # more than one batch of the directory walk, keyed by contacts the store never held
        open(os.path.join(bl, "%016x" % i), "wb").write(b"advert")
    assert rotate(d, 2, d.node_key)["status"] == "ok"
    assert os.listdir(bl) == [], "they all belonged to the old key's world"


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_a_two_member_deployment_can_be_read_after_a_rotation(nodes, kind):
    """A manager reads marks and re-admission from deployment.get. The set's text, its statement of a rotation and two members
    together passed the result limit, so every read answered too-large-to-sign. The set's text gives way: its length stays."""
    d = enrolled_any(nodes, kind, "r2m" + kind[:2], 77)
    args = update_args()
    assert d.i.request("deployment.authority_update", args)["status"] == "ok"
    before = d.i.request("deployment.get")
    assert before["status"] == "ok" and before["result"]["authority_set"] == args["authority_set"], before
    time.sleep(1.1)
    assert rotate(d, 3, d.node_key)["status"] == "ok"
    d.node_key = d.i.request("hello")["result"]["node_key"]
    g = d.i.request("deployment.get")
    assert g["status"] == "ok", g
    assert verifies(g.raw, d.node_key, "result")
    res = g["result"]
    assert len(res["members"]) == 2 and "mark" in res["members"][0], res
    assert res["rotation"]["statement"] and res["rotation"]["sig"]
    assert res["authority_set_len"] == len(args["authority_set"]) and "authority_set" not in res
    assert len(g.raw) <= 1536
