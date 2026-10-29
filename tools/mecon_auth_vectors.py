"""Public test vectors for the Deployment authority layer (docs/contract/AUTHORITY.md, management authority v1).

Generated from the public contract with this repository's own test keys, so an independently written Backend
can check its signing against them. Every key below is derived from a public label: they are test keys, never valid
anywhere. Deterministic: running it twice gives the same bytes (Ed25519 signing is deterministic).

    python3 tools/mecon_auth_vectors.py            # rewrite the fixture and the C header
    python3 tools/mecon_auth_vectors.py --check    # exit 1 if either file differs from what this would write

Objects marked "provisional" follow the device-interface binding proposed on the trust Issue, and change if its
review changes the binding. The contract-defined objects (authority sets, rotation) are stable.
"""
import base64
import hashlib
import json
import os
import sys

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURE = os.path.join(ROOT, "test", "fixtures", "deployment_authority_public_v1.json")
HEADER = os.path.join(ROOT, "test", "test_deployment_vectors", "vectors.h")

DOMAIN_AUTHSET = b"MECON-AUTHSET-v1"
DOMAIN_DAKROT = b"MECON-DAKROT-v1"
DOMAIN_DEVMSG = b"MECON-DEVMSG-v1"
DOMAIN_UPLINK = b"MECON-UPLINK-v1"
DOMAIN_NODEROT = b"MECON-NODEROT-v1"
DOMAIN_DEVID = b"MECON-DEVID-v1"

DEPLOYMENT_ID = hashlib.sha256(b"mecon-firmware public test deployment").hexdigest()[:32]


class Key:
    def __init__(self, label):
        self.label = label
        self.seed = hashlib.sha256(("mecon-firmware public test key: " + label).encode()).digest()
        self._sk = Ed25519PrivateKey.from_private_bytes(self.seed)
        self.public = self._sk.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)

    def sign(self, msg):
        return self._sk.sign(msg)

    @property
    def pub_hex(self):
        return self.public.hex()


def instance_id(label):
    return hashlib.sha256(("mecon-firmware public test instance: " + label).encode()).hexdigest()[:32]


def b64(sig):
    return base64.b64encode(sig).decode()


def compact(obj):
    return json.dumps(obj, separators=(",", ":"))


def signed(domain, *parts):
    """domain 0x00 part [0x00 part ...]: every contract signed-byte string has this shape."""
    out = domain
    for p in parts:
        out += b"\x00" + p
    return out


def build():
    dak1, dak2 = Key("dak epoch 1"), Key("dak epoch 2")
    inst_a, inst_b = Key("instance a"), Key("instance b")
    node = Key("device node")
    node2 = Key("device node after rotation")
    ia, ib = instance_id("a"), instance_id("b")

    set1_text = compact({
        "v": 1, "deployment_id": DEPLOYMENT_ID, "authority_epoch": 1, "authority_revision": 1,
        "issued_at": 1790000000,
        "members": [{"instance_id": ia, "public_key": inst_a.pub_hex, "admitted_revision": 1}],
        "revoked": []})
    set1_bytes = signed(DOMAIN_AUTHSET, set1_text.encode())

    rot_text = compact({"v": 1, "deployment_id": DEPLOYMENT_ID, "from_epoch": 1, "to_epoch": 2,
                        "new_public_key": dak2.pub_hex, "issued_at": 1790001000})
    rot_bytes = signed(DOMAIN_DAKROT, rot_text.encode())

    # Revision 2 after the rotation: instance b admitted at 2, signed by the new DAK.
    set2_text = compact({
        "v": 1, "deployment_id": DEPLOYMENT_ID, "authority_epoch": 2, "authority_revision": 2,
        "issued_at": 1790001001,
        "members": [{"instance_id": ia, "public_key": inst_a.pub_hex, "admitted_revision": 1},
                    {"instance_id": ib, "public_key": inst_b.pub_hex, "admitted_revision": 2}],
        "revoked": []})
    set2_bytes = signed(DOMAIN_AUTHSET, set2_text.encode())

    # Provisional: a device-interface request carried as the wrapper body, suffix "iface".
    body = compact({"v": 1, "id": "r-1", "op": "config.apply", "args": {"values": {"name": "test.example"}},
                    "issuer": ib, "deployment_id": DEPLOYMENT_ID, "authority_revision": 2,
                    "issued_at": 1790001100, "target_public_key": node.pub_hex, "job_id": 1})
    req_bytes = signed(DOMAIN_DEVMSG, b"iface", body.encode())

    # Signed event (AUTHORITY.md 3.4): the stream name sits between two NULs after the domain.
    payload = compact({"v": 1, "ev": "health", "epoch": 7, "seq": 3, "ts": None, "data": {"uptime_s": 60}})
    payload = payload[:-1] + ',"usig_boot":7,"usig_seq":12}'
    up_bytes = signed(DOMAIN_UPLINK, b"health", payload.encode())
    up_sig = node.sign(up_bytes)
    up_wire = payload[:-1] + ',"usig":"%s"}' % b64(up_sig)

    # Signed result: the same, with the literal "result" in place of the stream.
    # (it names what it answers: "op", and "job_id" for a signed request, before the signature members)
    res = compact({"v": 1, "id": "r-1", "status": "ok", "result": {"complete": True}, "op": "config.apply", "job_id": 1})
    res = res[:-1] + ',"usig_boot":7,"usig_seq":13}'
    res_bytes = signed(DOMAIN_UPLINK, b"result", res.encode())
    res_wire = res[:-1] + ',"usig":"%s"}' % b64(node.sign(res_bytes))

    # Rotation statement (AUTHORITY.md section 11): the old node key signs that the new one replaces it.
    nr_text = compact({"v": 1, "old": node.pub_hex, "new": node2.pub_hex, "device_id": "00112233445566778899aabbccddeeff",
                       "job_id": 5})
    nr_bytes = signed(DOMAIN_NODEROT, nr_text.encode())

    # Signed device_id binding (AUTHORITY.md section 3.5): device_id and node key as raw bytes after the domain's NUL,
    # then the optional nonce's bytes with no separator.
    dev_id = "00112233445566778899aabbccddeeff"
    dv_bytes = DOMAIN_DEVID + b"\x00" + bytes.fromhex(dev_id) + node.public
    dvn_bytes = dv_bytes + b"r-7.nonce_A"
    # A foreign claim: another device's key ("instance a", standing in for device A) signing this device's id. It verifies
    # under A's key, and only the Backend's one-id-one-key rule refuses it (section 3.5, rule 1).
    dvf_bytes = DOMAIN_DEVID + b"\x00" + bytes.fromhex(dev_id) + inst_a.public

    keys = {k.label: {"seed_hex": k.seed.hex(), "public_key": k.pub_hex}
            for k in (dak1, dak2, inst_a, inst_b, node, node2)}
    return {
        "about": "Public test vectors, Deployment authority contract v1. Test keys derived from public labels; "
                 "never valid anywhere. Generated by tools/mecon_auth_vectors.py.",
        "deployment_id": DEPLOYMENT_ID,
        "instance_ids": {"a": ia, "b": ib},
        "keys": keys,
        "authority_set_rev1": {"status": "stable", "authority_set": set1_text, "sig": b64(dak1.sign(set1_bytes)),
                               "signed_bytes_hex": set1_bytes.hex(), "signer": "dak epoch 1"},
        "rotation_1_to_2": {"status": "stable", "rotation": rot_text, "sig": b64(dak1.sign(rot_bytes)),
                            "signed_bytes_hex": rot_bytes.hex(), "signer": "dak epoch 1"},
        "authority_set_rev2": {"status": "stable", "authority_set": set2_text, "sig": b64(dak2.sign(set2_bytes)),
                               "signed_bytes_hex": set2_bytes.hex(), "signer": "dak epoch 2"},
        "iface_request": {"status": "provisional", "suffix": "iface",
                          "wire": compact({"mecon_auth": 1, "issuer": ib, "sig": b64(inst_b.sign(req_bytes)),
                                           "body": body}),
                          "signed_bytes_hex": req_bytes.hex(), "signer": "instance b"},
        "uplink_health": {"status": "stable", "stream": "health", "wire": up_wire,
                          "signed_bytes_hex": up_bytes.hex(), "signer": "device node"},
        "node_rotation": {"status": "stable", "statement": nr_text, "sig": b64(node.sign(nr_bytes)),
                          "signed_bytes_hex": nr_bytes.hex(), "signer": "device node"},
        "device_id_binding": {"status": "stable", "device_id": dev_id, "sig": b64(node.sign(dv_bytes)),
                              "signed_bytes_hex": dv_bytes.hex(), "signer": "device node"},
        "device_id_binding_nonce": {"status": "stable", "device_id": dev_id, "nonce": "r-7.nonce_A",
                                    "sig": b64(node.sign(dvn_bytes)), "signed_bytes_hex": dvn_bytes.hex(),
                                    "signer": "device node"},
        "device_id_binding_foreign": {"status": "stable", "device_id": dev_id, "sig": b64(inst_a.sign(dvf_bytes)),
                                      "signed_bytes_hex": dvf_bytes.hex(), "signer": "instance a",
                                      "note": "a valid signature by another key over this device_id: a Backend "
                                              "that has bound device_id to 'device node' must refuse it"},
        "uplink_result": {"status": "stable", "stream": "result", "wire": res_wire,
                          "signed_bytes_hex": res_bytes.hex(), "signer": "device node"},
    }


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def header(v):
    lines = ["// Generated by tools/mecon_auth_vectors.py from test/fixtures/deployment_authority_public_v1.json.",
             "// Do not edit. Test keys only, derived from public labels.", "#pragma once", ""]
    for label, k in v["keys"].items():
        name = label.upper().replace(" ", "_")
        lines.append("static const char* PUB_%s = %s;" % (name, c_str(k["public_key"])))
        lines.append("static const char* SEED_%s = %s;" % (name, c_str(k["seed_hex"])))
    for obj in ("authority_set_rev1", "rotation_1_to_2", "authority_set_rev2", "iface_request", "uplink_health", "uplink_result",
                "node_rotation", "device_id_binding", "device_id_binding_nonce", "device_id_binding_foreign"):
        o = v[obj]
        name = obj.upper()
        lines.append("static const char* %s_SIGNED_HEX = %s;" % (name, c_str(o["signed_bytes_hex"])))
        if "sig" in o:
            lines.append("static const char* %s_SIG = %s;" % (name, c_str(o["sig"])))
        if "nonce" in o:
            lines.append("static const char* %s_NONCE = %s;" % (name, c_str(o["nonce"])))
        if "device_id" in o:
            lines.append("static const char* %s_DEVICE_ID = %s;" % (name, c_str(o["device_id"])))
        if "wire" in o:
            lines.append("static const char* %s_WIRE = %s;" % (name, c_str(o["wire"])))
    lines.append("static const char* DEPLOYMENT_ID = %s;" % c_str(v["deployment_id"]))
    for k, iid in v["instance_ids"].items():
        lines.append("static const char* INSTANCE_IDS_%s = %s;" % (k.upper(), c_str(iid)))
    return "\n".join(lines) + "\n"


def main():
    v = build()
    outputs = {FIXTURE: json.dumps(v, indent=2) + "\n", HEADER: header(v)}
    if "--check" in sys.argv:
        bad = [p for p, text in outputs.items() if not os.path.exists(p) or open(p).read() != text]
        for p in bad:
            print("out of date:", os.path.relpath(p, ROOT))
        sys.exit(1 if bad else 0)
    for p, text in outputs.items():
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w") as f:
            f.write(text)
        print("wrote", os.path.relpath(p, ROOT))


if __name__ == "__main__":
    main()
