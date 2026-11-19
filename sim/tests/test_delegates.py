"""Delegated receive identities (AUTHORITY.md section 10b; Issue #552). A member provisions up to four extra identities, each
with its private key sealed to the node key (mecon-dl-v1, tools/mecon_dl.py). The device then receives, decrypts and
natively ACKs direct text messages addressed to them, from contacts it already holds, and puts them in the inbox with the
identity; it never sends as one. Keys are sealed at rest, never read back, destroyed by Hardened, deployment.reset and a
factory reset, and kept by identity.rotate."""
import base64
import hashlib
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_dl  # noqa: E402
import mecon_schema  # noqa: E402
from sim import ed25519_ref  # noqa: E402
from sim.ether import Ether  # noqa: F401,E402
from sim.harness import CMD_SEND_TXT_MSG, CompanionClient  # noqa: E402
from test_deployment_authority import INST_A, RTC, boot, boot_with_secret, wrap  # noqa: E402
from test_device_ops import next_job  # noqa: E402
from test_device_reset import reset  # noqa: E402
from test_identity_rotate import rotate  # noqa: E402
from test_mesh_protocol import PUSH_SEND_CONFIRMED  # noqa: E402

VEC = json.load(open(os.path.join(os.path.dirname(__file__), "..", "..", "test", "fixtures", "delegate_seal_v1.json")))


def ident(label, avoid_hash=None, want_hash=None):
    """A delegated identity from a public label: (seed, public key, MeshCore private key)."""
    i = 0
    while True:
        seed = hashlib.sha256(("%s %d" % (label, i)).encode()).digest()
        pub = ed25519_ref.public_key(seed)
        if pub[0] not in (0x00, 0xFF) and (want_hash is None or pub[0] == want_hash):
            return seed, pub, mecon_dl.private_key(seed)
        i += 1


def sealed_args(d, job, slot, who, node_pub=None, **bad):
    """identity.delegate.put arguments for `who`, sealed under `job` and `slot` to this device's node key."""
    seed, pub, prv = who
    npub = node_pub or bytes.fromhex(d.node_key)
    s = bytearray(mecon_dl.seal(hashlib.sha256(b"eph %d" % job).digest(), npub, bad.get("seal_job", job),
                                bad.get("seal_slot", slot), bad.get("seal_pub", pub), prv))
    if "flip" in bad:
        s[bad["flip"]] ^= 1
    a = {"slot": slot, "pub": pub.hex(), "sealed": base64.b64encode(bytes(s)).decode()}
    if "channel" in bad:
        a["channel"] = bad["channel"]
    return a


def put(d, who, slot=0, job=None, nonce=True, **bad):   # channel=<slot> gives the identity a local channel
    job = job or next_job(d)
    return d.send(wrap(d.node_key, "identity.delegate.put", sealed_args(d, job, slot, who, **bad), job=job,
                       nonce=d.nonce() if nonce else None))


def dlist(d):
    r = d.i.request("identity.delegate.list")
    assert r["status"] == "ok", r
    return r["result"]


def files_holding(d, needle):
    d.n.stop()
    hits = [f for f in os.listdir(d.n.dir) if os.path.isfile(os.path.join(d.n.dir, f)) and
            needle in open(os.path.join(d.n.dir, f), "rb").read()]
    d.n.start()
    d.attach()
    return hits


CAR = ident("car")
BOAT = ident("boat")


def test_a_member_provisions_an_identity_whose_key_is_sealed_in_flight_and_at_rest(nodes):
    d = boot(nodes, "dg1")
    caps, cur = [], None
    for _ in range(10):
        h = d.i.request("hello", {"cursor": cur} if cur is not None else None)["result"]
        caps += h["capabilities"]
        cur = h.get("next")
        if cur is None:
            break
    assert all(caps.count(op) == 1 for op in ("identity.delegate.put", "identity.delegate.delete", "identity.delegate.list"))
    assert dlist(d) == {"max": 4, "identities": []}
    u = d.i.request("identity.delegate.put", sealed_args(d, 1, 0, CAR))
    assert u["status"] == "refused" and u["error"] == "not_enrolled", u
    assert d.provision()["status"] == "ok"
    u = d.i.request("identity.delegate.put", sealed_args(d, 1, 0, CAR))
    assert u["status"] == "refused" and u["error"] == "unauthenticated", "a transport is not authority: %s" % u
    r = put(d, CAR, nonce=False)
    assert r["status"] != "ok" and r["error"] == "bad_channel_nonce", "a sensitive operation: %s" % r
    r = put(d, CAR, slot=0)
    assert r["status"] == "ok" and r["result"] == {"delegate": "stored", "slot": 0, "pub": CAR[1].hex()}, r
    assert not mecon_schema.check(r["result"], mecon_schema.load("identity.delegate.put.result.json"))
    assert CAR[2][:32].hex() not in r.raw.decode() and CAR[2][32:].hex() not in r.raw.decode(), "the key is never echoed"
    assert put(d, BOAT, slot=3)["status"] == "ok"
    expect = {"max": 4, "identities": [{"slot": 0, "pub": CAR[1].hex()}, {"slot": 3, "pub": BOAT[1].hex()}]}
    assert dlist(d) == expect
    assert not mecon_schema.check(dlist(d), mecon_schema.load("identity.delegate.list.result.json"))
    assert files_holding(d, CAR[2]) == [] and files_holding(d, CAR[2][:32]) == [], "sealed at rest: no file holds the key"
    d.reboot()
    assert dlist(d) == expect, "and it is back after a reboot"
    r = put(d, BOAT, slot=3, job=next_job(d))
    assert r["status"] == "ok", "replacing a slot with its own key is allowed: %s" % r
    r = d.send(wrap(d.node_key, "identity.delegate.delete", {"slot": 0}, job=next_job(d)))
    assert r["status"] == "ok" and r["result"] == {"slot": 0, "removed": True}, r
    assert not mecon_schema.check(r["result"], mecon_schema.load("identity.delegate.delete.result.json"))
    r = d.send(wrap(d.node_key, "identity.delegate.delete", {"slot": 0}, job=next_job(d)))
    assert r["status"] == "ok" and r["result"]["removed"] is False, r
    assert dlist(d)["identities"] == [{"slot": 3, "pub": BOAT[1].hex()}]
    d.reboot()
    assert dlist(d)["identities"] == [{"slot": 3, "pub": BOAT[1].hex()}], "a delete is stored too"
    u = d.i.request("identity.delegate.delete", {"slot": 3})
    assert u["status"] == "refused" and u["error"] == "unauthenticated", u
    assert dlist(d)["identities"], "an unsigned delete removed nothing"


def test_the_sealed_key_must_open_for_this_device_job_slot_and_key_and_nothing_else_is_accepted(nodes):
    d = boot(nodes, "dg2")
    assert d.provision()["status"] == "ok"
    other_node = ed25519_ref.public_key(hashlib.sha256(b"another device").digest())
    cases = [
        ("a key sealed to another device", dict(node_pub=other_node)),
        ("a tampered ciphertext", dict(flip=40)),
        ("a tampered tag", dict(flip=111)),
        ("a tampered ephemeral key", dict(flip=3)),
        ("another job", dict(seal_job=999999)),
        ("another slot", dict(seal_slot=2)),
        ("another public key in the seal", dict(seal_pub=BOAT[1])),
    ]
    for name, bad in cases:
        r = put(d, CAR, **bad)
        assert r["status"] == "refused" and r["error"] == "seal_invalid", "%s: %s" % (name, r)
    assert dlist(d)["identities"] == [], "nothing was stored"
    # a seal made as an exported key is made (mecon-kx-v1's label): it never opens as a delegated one
    import mecon_kx
    job = next_job(d)
    eph = hashlib.sha256(b"kx").digest()
    shared = mecon_kx.shared_secret(eph, bytes.fromhex(d.node_key))
    kx = mecon_kx.wrap(shared, job, CAR[2])
    args = {"slot": 0, "pub": CAR[1].hex(), "sealed": base64.b64encode(ed25519_ref.public_key(eph) + kx).decode()}
    r = d.send(wrap(d.node_key, "identity.delegate.put", args, job=job, nonce=d.nonce()))
    assert r["status"] == "refused" and r["error"] == "seal_invalid", "a wrong label: %s" % r
    # a public key that is not the sealed key's
    job = next_job(d)
    a = sealed_args(d, job, 0, CAR, seal_pub=BOAT[1])
    a["pub"] = BOAT[1].hex()
    r = d.send(wrap(d.node_key, "identity.delegate.put", a, job=job, nonce=d.nonce()))
    assert r["status"] == "refused" and r["error"] == "key_mismatch", r
    # the reserved one-byte hashes, the node's own key and a key already in another slot
    for h in (0x00, 0xFF):
        seed = hashlib.sha256(b"reserved %d" % h).digest()
        i = 0
        while True:   # a key whose hash is the reserved byte, found by search
            seed = hashlib.sha256(b"reserved %d %d" % (h, i)).digest()
            if ed25519_ref.public_key(seed)[0] == h:
                break
            i += 1
        who = (seed, ed25519_ref.public_key(seed), mecon_dl.private_key(seed))
        r = put(d, who)
        assert r["status"] == "refused" and r["error"] == "reserved_hash", "%02x: %s" % (h, r)
    assert put(d, CAR, slot=1)["status"] == "ok"
    r = put(d, CAR, slot=2)
    assert r["status"] == "refused" and r["error"] == "duplicate", r
    r = put(d, CAR, slot=4)
    assert r["status"] == "invalid" and r["error"] == "bad-args", r
    r = put(d, CAR, slot=1)
    assert r["status"] == "ok", "the same key in its own slot is a replacement: %s" % r
    # a request aimed at another device
    r = d.send(wrap("11" * 32, "identity.delegate.put", sealed_args(d, next_job(d), 3, CAR), job=next_job(d), nonce=d.nonce()))
    assert r["status"] == "refused" and r["error"] == "wrong_device", r


def test_a_replayed_request_stores_nothing_twice(nodes):
    d = boot(nodes, "dg3")
    assert d.provision()["status"] == "ok"
    job = next_job(d)
    w = wrap(d.node_key, "identity.delegate.put", sealed_args(d, job, 0, CAR), job=job, nonce=d.nonce())
    assert d.send(w)["status"] == "ok"
    d.send(wrap(d.node_key, "identity.delegate.delete", {"slot": 0}, job=next_job(d)))
    assert dlist(d)["identities"] == []
    r = d.send(w)   # the captured request again, after the key was removed: its nonce was spent
    assert r["status"] == "refused" and r["error"] == "bad_channel_nonce", r
    r = d.send(wrap(d.node_key, "identity.delegate.put", sealed_args(d, job, 0, CAR), job=job, nonce=d.nonce()))
    assert r["status"] == "refused" and r["error"] == "replay", "its job id was spent too: %s" % r
    assert dlist(d)["identities"] == [], "a replay never puts a removed key back"


def test_the_published_vector_is_what_the_firmware_opens(nodes):
    d = boot(nodes, "dgv")
    f = d.c.request(bytes([24]) + bytes.fromhex(VEC["node"]["private_key_hex"]))   # CMD_IMPORT_PRIVATE_KEY, no record yet
    assert f[0] == 0, f
    d.reboot()
    assert d.node_key == VEC["node"]["public_key"]
    assert d.provision()["status"] == "ok"
    r = d.send(wrap(d.node_key, "identity.delegate.put", VEC["request_args"], job=VEC["job_id"], nonce=d.nonce()))
    assert r["status"] == "ok" and r["result"]["pub"] == VEC["delegated"]["public_key"], r
    assert dlist(d)["identities"] == [{"slot": VEC["slot"], "pub": VEC["delegated"]["public_key"]}]
    # the node's own key is not a delegate; and a sealed value that is no valid private key is refused (here the zero scalar,
    # whose public key, the identity point, the request names correctly)
    node = (None, bytes.fromhex(VEC["node"]["public_key"]), bytes.fromhex(VEC["node"]["private_key_hex"]))
    r = put(d, node, slot=0)
    assert r["status"] == "refused" and r["error"] == "same_as_node", r
    zero = (None, b"\x01" + bytes(31), bytes(64))
    r = put(d, zero, slot=2)
    assert r["status"] == "refused" and r["error"] == "bad_key", r
    assert len(dlist(d)["identities"]) == 1


def test_hardened_and_reset_destroy_every_key_and_rotation_keeps_them(nodes):
    d = boot_with_secret(nodes, "dg4")
    assert put(d, CAR, slot=0)["status"] == "ok"
    old = d.node_key
    r = rotate(d, next_job(d), old)
    assert r["status"] == "ok", r
    d.node_key = r["result"]["new_public_key"]
    assert [x["pub"] for x in dlist(d)["identities"]] == [CAR[1].hex()], "rotating the node key keeps them"
    assert put(d, BOAT, slot=1)["status"] == "ok", "and the new node key opens new seals"
    assert d.send(wrap(d.node_key, "posture.set", {"posture": "hardened"}, job=next_job(d)))["status"] == "ok"
    d.i.request("hello")   # a pass of the loop: the reconcile has run
    r = put(d, CAR)
    assert r["status"] == "refused" and r["error"] == "hardened", r
    assert files_holding(d, b"MDG1") == [], "the sealed table and its wrapping key are gone"
    assert d.send(wrap(d.node_key, "posture.set", {"posture": "normal", "confirm": True}, job=next_job(d)))["status"] == "ok"
    assert dlist(d)["identities"] == [], "Normal again holds none until a new put"
    assert put(d, CAR)["status"] == "ok"
    r = d.send(wrap(d.node_key, "deployment.reset", job=next_job(d)))
    assert r["status"] == "ok" and "delegates" in r["result"]["removed"], r
    assert not mecon_schema.check(r["result"], mecon_schema.load("deployment.reset.result.json"))
    assert dlist(d)["identities"] == [] and files_holding(d, b"MDG1") == []


def test_a_factory_reset_destroys_them(nodes):
    d = boot(nodes, "dg5")
    assert d.provision()["status"] == "ok"
    assert put(d, CAR)["status"] == "ok"
    assert reset(d, "factory")["status"] == "ok"
    assert dlist(d)["identities"] == []
    assert files_holding(d, b"MDG1") == []


# ------------------------------------------------------------------ the radio

def pair(nodes, ether, a, b, who=CAR, recv_seed=42):
    """A stock sender and an enrolled receiver in range, the receiver holding the sender and one delegated identity; the
    sender holds the identity as a contact, as an app that knows the car would."""
    ns = nodes("companion_standalone", a, ether_path=ether.path, seed=41, rtc=RTC).start()
    nr = nodes("companion_standalone", b, ether_path=ether.path, seed=recv_seed, rtc=RTC).start()
    from test_deployment_authority import Dev
    cs = CompanionClient(ns)
    cs.device_query()
    cs.app_start()
    d = Dev(nr)
    d.c.device_query()   # an app that reads V3 frames
    d.c.app_start()
    assert ether.wait_for_nodes([a, b])
    ether.set_link(a, b)
    assert d.provision()["status"] == "ok"
    if who is not None:
        assert put(d, who)["status"] == "ok"
        assert cs.add_contact(who[1], "car")[0] == 0
    assert cs.add_contact(bytes.fromhex(d.node_key), "reader")[0] == 0
    return d, cs


def wait_received(d, count, timeout=75.0):
    import time
    end = time.time() + timeout
    r = None
    while time.time() < end:
        r = d.i.request("status.get")["result"]["inbox"]
        if r["received"] >= count:
            return r
        time.sleep(0.3)
    raise AssertionError("received %d of %d: %s" % (r["received"], count, r))


def test_a_message_to_a_delegated_identity_is_received_acked_and_listed_with_it(nodes, ether):
    d, cs = pair(nodes, ether, "dra", "drb")
    assert d.c.add_contact(cs.pub_key, "sender")[0] == 0
    r = cs.send_direct(CAR[1], "hello car", RTC + 5)
    assert r[0] == 6, r
    f = cs.wait_push(PUSH_SEND_CONFIRMED, timeout=60.0)
    assert f is not None, "the sender got the native ACK for a message to the delegated identity"
    assert f[1:5] == r[2:6], "and it is the ACK of that very message"
    wait_received(d, 1)
    m = d.i.request("inbox.list")["result"]["messages"]
    assert len(m) == 1, m
    assert m[0]["text"] == "hello car" and m[0]["from"] == cs.pub_key.hex() and m[0]["identity"] == CAR[1].hex(), m
    got = d.c.sync_messages(timeout=5, want=1)
    assert [g["text"] for g in got] == ["hello car"], "the stock app has it in the sender's conversation: %s" % got
    # a message to the primary identity carries none
    cs.send_direct(bytes.fromhex(d.node_key), "to the node", RTC + 6)
    wait_received(d, 2)
    m = d.i.request("inbox.list")["result"]["messages"]
    assert [x["text"] for x in m] == ["hello car", "to the node"] and "identity" not in m[1], m
    assert not mecon_schema.check(d.i.request("inbox.list")["result"], mecon_schema.load("inbox.list.result.json"))
    # a command is never run, and nothing is answered, as a delegated identity
    cs.request(bytes([CMD_SEND_TXT_MSG, 1, 0]) + struct.pack("<I", RTC + 7) + CAR[1][:6] + b"hello")   # TXT_TYPE_CLI_DATA
    cs.send_direct(CAR[1], "after the command", RTC + 8)
    wait_received(d, 3)
    m = d.i.request("inbox.list")["result"]["messages"]
    assert [x["text"] for x in m][-1] == "after the command" and len(m) == 3, "the command left no entry: %s" % m


def test_only_a_known_sender_to_a_present_identity_is_received(nodes, ether):
    import time
    d, cs = pair(nodes, ether, "drc", "drd")
    assert put(d, BOAT, slot=2)["status"] == "ok"
    assert cs.add_contact(BOAT[1], "boat")[0] == 0
    cs.send_direct(CAR[1], "from a stranger", RTC + 5)
    time.sleep(8)
    assert d.i.request("status.get")["result"]["inbox"]["received"] == 0, "a sender the device does not hold is not read"
    assert d.c.add_contact(cs.pub_key, "sender")[0] == 0
    d.send(wrap(d.node_key, "identity.delegate.delete", {"slot": 0}, job=next_job(d)))
    cs.send_direct(CAR[1], "after the delete", RTC + 6)
    time.sleep(8)
    assert d.i.request("status.get")["result"]["inbox"]["received"] == 0, "a removed identity is no longer received"
    cs.send_direct(BOAT[1], "to the boat", RTC + 7)
    wait_received(d, 1)
    m = d.i.request("inbox.list")["result"]["messages"]
    assert [(x["text"], x["identity"]) for x in m] == [("to the boat", BOAT[1].hex())], "the one left, past the empty slot: %s" % m
    assert put(d, CAR)["status"] == "ok"
    cs.send_direct(CAR[1], "after the put", RTC + 8)
    wait_received(d, 2)
    assert d.i.request("inbox.list")["result"]["messages"][1]["text"] == "after the put"


def test_two_identities_that_share_a_hash_are_tried_in_order(nodes, ether):
    # the node's own hash: its key is tried first and its MAC fails, then the delegated one opens it
    from test_deployment_authority import Dev
    probe = nodes("companion_standalone", "drp", seed=43, rtc=RTC).start()
    node_hash = bytes.fromhex(Dev(probe).node_key)[0]
    probe.stop()
    twin = ident("twin", want_hash=node_hash)
    d, cs = pair(nodes, ether, "dre", "drf", who=twin, recv_seed=43)
    assert d.c.add_contact(cs.pub_key, "sender")[0] == 0
    cs.send_direct(twin[1], "to the twin", RTC + 5)
    assert cs.wait_push(PUSH_SEND_CONFIRMED, timeout=60.0) is not None
    wait_received(d, 1)
    m = d.i.request("inbox.list")["result"]["messages"][0]
    assert m["text"] == "to the twin" and m["identity"] == twin[1].hex(), m


# ------------------------------------------------------------------ the optional local channel

SECRET = bytes(range(16, 32))


def chput(d, slot, name, secret):
    """A channel on the enrolled device: signed, as a native set is refused once a record exists."""
    r = d.send(wrap(d.node_key, "channel.put", {"slot": slot, "name": name, "secret": secret.hex()}, job=next_job(d)))
    assert r["status"] == "ok", r


def test_a_local_channel_is_set_listed_stored_sealed_and_cleared_by_a_put_without_one(nodes):
    d = boot(nodes, "dc1")
    assert d.provision()["status"] == "ok"
    r = put(d, CAR, slot=0, channel=5)
    assert r["status"] == "ok" and r["result"] == {"delegate": "stored", "slot": 0, "pub": CAR[1].hex(), "channel": 5}, r
    assert not mecon_schema.check(r["result"], mecon_schema.load("identity.delegate.put.result.json"))
    assert put(d, BOAT, slot=3)["status"] == "ok"
    expect = {"max": 4, "identities": [{"slot": 0, "pub": CAR[1].hex(), "channel": 5}, {"slot": 3, "pub": BOAT[1].hex()}]}
    assert dlist(d) == expect
    assert not mecon_schema.check(dlist(d), mecon_schema.load("identity.delegate.list.result.json"))
    assert files_holding(d, b"MDG1") != [] and files_holding(d, CAR[2][:32]) == [], "still sealed at rest"
    d.reboot()
    assert dlist(d) == expect, "the channel is stored with the table"
    r = put(d, CAR, slot=0)
    assert r["status"] == "ok" and "channel" not in r["result"], "a put replaces the whole record: %s" % r
    assert dlist(d)["identities"][0] == {"slot": 0, "pub": CAR[1].hex()}
    d.reboot()
    assert dlist(d)["identities"][0] == {"slot": 0, "pub": CAR[1].hex()}, "and the cleared channel stays cleared"
    for bad in (63, -1, "x", 1.5, None):
        r = put(d, CAR, slot=0, channel=bad)
        assert r["status"] == "invalid" and r["error"] == "bad-args", "%r: %s" % (bad, r)
    assert "channel" not in dlist(d)["identities"][0]
    assert put(d, CAR, slot=0, channel=62)["status"] == "ok", "the highest slot a record holds"
    d.reboot()
    assert dlist(d)["identities"][0]["channel"] == 62


def test_a_message_to_an_identity_with_a_local_channel_shows_on_that_slot_and_lists_with_the_identity(nodes, ether):
    import time
    d, cs = pair(nodes, ether, "dca", "dcb", who=CAR)
    assert put(d, CAR, slot=0, channel=2)["status"] == "ok"
    assert put(d, BOAT, slot=1)["status"] == "ok"
    assert cs.add_contact(BOAT[1], "boat")[0] == 0
    chput(d, 2, "car inbox", SECRET)
    assert d.c.add_contact(cs.pub_key, "sender")[0] == 0
    r = cs.send_direct(CAR[1], "hello car", RTC + 5)
    assert cs.wait_push(PUSH_SEND_CONFIRMED, timeout=60.0) is not None, "still ACKed natively"
    wait_received(d, 1)
    got = d.c.sync_messages(timeout=10, want=1)
    assert len(got) == 1 and got[0]["kind"] == "channel" and got[0]["channel"] == 2 and got[0]["txt_type"] == 0, got
    assert got[0]["text"] == "sender: hello car" and got[0]["ts"] == RTC + 5, "read as a channel message reads: %s" % got
    m = d.i.request("inbox.list")["result"]["messages"]
    assert len(m) == 1 and m[0]["text"] == "hello car" and m[0]["identity"] == CAR[1].hex() and m[0]["from"] == cs.pub_key.hex(), m
    assert "channel" not in m[0], "the manager's copy is the direct message: %s" % m
    assert not mecon_schema.check(d.i.request("inbox.list")["result"], mecon_schema.load("inbox.list.result.json"))
    # an identity without a slot, and the primary identity, are unchanged: the sender's thread
    cs.send_direct(BOAT[1], "to the boat", RTC + 6)
    wait_received(d, 2)
    cs.send_direct(bytes.fromhex(d.node_key), "to the node", RTC + 7)
    wait_received(d, 3)
    got = d.c.sync_messages(timeout=10, want=2)
    assert [(g["kind"], g["text"]) for g in got] == [("direct", "to the boat"), ("direct", "to the node")], got
    # a repeat is not shown twice
    assert len(d.i.request("inbox.list")["result"]["messages"]) == 3
    # the slot's channel is gone: back to the sender's thread
    r = d.send(wrap(d.node_key, "channel.remove", {"slot": 2}, job=next_job(d)))
    assert r["status"] == "ok", r
    cs.send_direct(CAR[1], "after the removal", RTC + 8)
    wait_received(d, 4)
    got = d.c.sync_messages(timeout=10, want=1)
    assert [(g["kind"], g["text"]) for g in got] == [("direct", "after the removal")], "the default path: %s" % got
    allm = d.i.request("inbox.list", {"after": 3})["result"]
    last = [x for x in allm["messages"] if x["text"] == "after the removal"]
    assert len(last) == 1 and last[0]["identity"] == CAR[1].hex(), json.dumps(allm)
    # a channel put on the slot again: the binding is the identity's, so delivery returns
    chput(d, 2, "reused", SECRET)
    cs.send_direct(CAR[1], "after the reuse", RTC + 9)
    wait_received(d, 5)
    got = d.c.sync_messages(timeout=10, want=1)
    assert [(g["kind"], g["channel"], g["text"]) for g in got] == [("channel", 2, "sender: after the reuse")], got
    # and the binding survives a reboot
    d.reboot()
    d.c.device_query()
    d.c.app_start()
    assert dlist(d)["identities"][0]["channel"] == 2
    cs.send_direct(CAR[1], "after the reboot", RTC + 10)
    wait_received(d, 1)
    got = d.c.sync_messages(timeout=10, want=1)
    assert [(g["kind"], g["channel"]) for g in got] == [("channel", 2)], got


def test_the_local_channel_is_never_transmitted_on_and_a_send_on_it_is_refused(nodes, ether):
    import time
    d, cs = pair(nodes, ether, "dcc", "dcd", who=CAR)
    assert put(d, CAR, slot=0, channel=2)["status"] == "ok"
    chput(d, 2, "car inbox", SECRET)
    assert cs.set_channel(2, "car inbox", SECRET)[0] == 0, "the far end holds the same channel: a send would reach it"
    assert cs.set_channel(3, "open", SECRET[::-1])[0] == 0
    chput(d, 3, "open", SECRET[::-1])
    # the native text send, and the native datagram send (CMD_SEND_CHANNEL_DATA), on the slot
    f = d.c.send_channel(2, "leak", RTC + 5)
    assert f[0] == 1 and f[1] == 7, "ERR_CODE_NOT_PERMITTED: %s" % f
    f = d.c.request(bytes([62, 2, 0xFF]) + struct.pack("<H", 1) + b"data")
    assert f[0] == 1 and f[1] == 7, "CMD_SEND_CHANNEL_DATA: %s" % f
    # message.send to the slot
    r = d.send(wrap(d.node_key, "message.send", {"channel": 2, "text": "leak", "timestamp": RTC + 6}, job=next_job(d)))
    assert r["status"] == "failed" and r["error"] == "local-channel", r
    assert d.i.request("message.list")["result"]["open"] == 0
    assert cs.sync_messages(timeout=6, want=1) == [], "nothing went on the air"
    # another slot still sends, natively and through message.send
    assert d.c.send_channel(3, "fine", RTC + 7)[0] == 0
    r = d.send(wrap(d.node_key, "message.send", {"channel": 3, "text": "fine too", "timestamp": RTC + 8}, job=next_job(d)))
    assert r["status"] == "ok", r
    got = cs.sync_messages(timeout=60, want=2)
    assert sorted(g["text"].split(": ", 1)[1] for g in got) == ["fine", "fine too"], got
    # removing the identity frees the slot
    r = d.send(wrap(d.node_key, "identity.delegate.delete", {"slot": 0}, job=next_job(d)))
    assert r["status"] == "ok"
    assert d.c.send_channel(2, "free again", RTC + 9)[0] == 0
    assert [g["text"].split(": ", 1)[1] for g in cs.sync_messages(timeout=60, want=1)] == ["free again"]


def test_a_reboot_and_a_reset_keep_and_release_the_local_channel_slot(nodes):
    d = boot_with_secret(nodes, "dc4")
    assert put(d, CAR, slot=1, channel=4)["status"] == "ok"
    chput(d, 4, "car inbox", SECRET)
    assert d.c.send_channel(4, "x", RTC + 5)[0] == 1, "refused while held"
    d.reboot()
    assert d.c.send_channel(4, "x", RTC + 6)[0] == 1, "and after a reboot"
    assert d.send(wrap(d.node_key, "deployment.reset", job=next_job(d)))["status"] == "ok"
    assert dlist(d)["identities"] == [] and files_holding(d, b"MDG1") == []
    assert d.c.send_channel(4, "x", RTC + 9)[0] == 0, "the reset released it"
