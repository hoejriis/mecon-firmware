#!/usr/bin/env python3
"""mecon-dl-v1 in plain Python: seal (and open) the private key of a delegated receive identity to a device's node key, as
identity.delegate.put takes it (src/helpers/iface/DelegateSeal.h, docs/contract/AUTHORITY.md section 10b). Standard library
only, built on tools/mecon_kx.py's X25519 and AES-128; a reference and a test aid, not fast.

    eph    = an ephemeral Ed25519 key pair (seed -> public key)
    shared = X25519(eph scalar, node public key taken to Curve25519)
    K      = HMAC-SHA256(shared, "mecon delegate-seal v1" || job_id (4 bytes big-endian) || slot (1 byte) || eph public || delegated public)
    ct     = AES-128-CTR(K[0..15], counter from zero, one 128-bit big-endian number) over the 64-byte private key
    tag    = HMAC-SHA256(K[16..31], ct)[0..15]
    sealed = eph public (32) || ct (64) || tag (16)      # 112 bytes, base64 in the request

The device opens it with its node private key: shared = X25519(node scalar, eph public taken to Curve25519).
"""
import hashlib
import hmac
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import mecon_kx  # noqa: E402
from sim import ed25519_ref  # noqa: E402

LABEL = b"mecon delegate-seal v1"


def private_key(seed):
    """MeshCore's 64-byte private key for an Ed25519 seed: the expanded, clamped seed."""
    h = bytearray(hashlib.sha512(seed).digest())
    h[0] &= 248
    h[31] &= 63
    h[31] |= 64
    return bytes(h)


def kdf(shared, job_id, slot, eph_pub, pub):
    return hmac.new(shared, LABEL + job_id.to_bytes(4, "big") + bytes([slot]) + eph_pub + pub, hashlib.sha256).digest()


def seal(eph_seed, node_pub, job_id, slot, pub, prv):
    eph_pub = ed25519_ref.public_key(eph_seed)
    shared = mecon_kx.shared_secret(eph_seed, node_pub)
    k = kdf(shared, job_id, slot, eph_pub, pub)
    ct = mecon_kx._ctr(k[:16], prv)
    return eph_pub + ct + hmac.new(k[16:], ct, hashlib.sha256).digest()[:16]


def unseal(node_prv, job_id, slot, pub, sealed):
    """The 64-byte private key, or ValueError when the tag does not verify (wrong node, job, slot, public key or label)."""
    eph_pub, ct, tag = sealed[:32], sealed[32:96], sealed[96:]
    scalar = int.from_bytes(node_prv[:32], "little")
    shared = mecon_kx.x25519(scalar, mecon_kx.ed_pub_to_u(eph_pub))
    k = kdf(shared, job_id, slot, eph_pub, pub)
    if not hmac.compare_digest(hmac.new(k[16:], ct, hashlib.sha256).digest()[:16], tag):
        raise ValueError("mecon-dl-v1: tag does not verify")
    return mecon_kx._ctr(k[:16], ct)


if __name__ == "__main__":
    node_seed = hashlib.sha256(b"mecon-dl-v1 self-test node").digest()
    node_prv, node_pub = private_key(node_seed), ed25519_ref.public_key(node_seed)
    dseed = hashlib.sha256(b"mecon-dl-v1 self-test identity").digest()
    d_prv, d_pub = private_key(dseed), ed25519_ref.public_key(dseed)
    s = seal(hashlib.sha256(b"eph").digest(), node_pub, 7, 2, d_pub, d_prv)
    assert unseal(node_prv, 7, 2, d_pub, s) == d_prv
    for args in ((8, 2, d_pub), (7, 3, d_pub), (7, 2, node_pub)):
        try:
            unseal(node_prv, args[0], args[1], args[2], s)
            raise SystemExit("a changed context must not open")
        except ValueError:
            pass
    print("mecon_dl self-test: ok")
