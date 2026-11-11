#!/usr/bin/env python3
"""mecon-kx-v1 in plain Python: unwrap (and wrap) a node private key exported by identity.key.export to a Deployment member
(src/helpers/iface/KeyWrap.h). Standard library only, so it runs where no crypto package is installed; it is a reference
and a test aid, not fast.

    shared = X25519(member scalar, node public key taken to Curve25519)
    K      = HMAC-SHA256(shared, "mecon key-export v1" || job_id, 4 bytes big-endian)
    ct     = AES-128-CTR(K[0..15], counter from zero, one 128-bit big-endian number)
    tag    = HMAC-SHA256(K[16..31], ct)[0..15]

The member's key is a standard Ed25519 seed; its X25519 scalar is the clamped first half of SHA-512(seed), as libsodium's
crypto_sign_ed25519_sk_to_curve25519 gives. The node's Ed25519 public key becomes a Montgomery u: (1 + y) / (1 - y).
"""
import hashlib
import hmac

P = 2 ** 255 - 19
LABEL = b"mecon key-export v1"


# ---- X25519 (RFC 7748)
def _clamp(k):
    k = bytearray(k)
    k[0] &= 248
    k[31] &= 127
    k[31] |= 64
    return int.from_bytes(k, "little")


def x25519(scalar_int, u):
    x1, x2, z2, x3, z3, swap = u, 1, 0, u, 1, 0
    for t in reversed(range(255)):
        bit = (scalar_int >> t) & 1
        swap ^= bit
        if swap:
            x2, x3, z2, z3 = x3, x2, z3, z2
        swap = bit
        a = (x2 + z2) % P
        aa = a * a % P
        b = (x2 - z2) % P
        bb = b * b % P
        e = (aa - bb) % P
        c = (x3 + z3) % P
        d = (x3 - z3) % P
        da = d * a % P
        cb = c * b % P
        x3 = (da + cb) ** 2 % P
        z3 = x1 * (da - cb) ** 2 % P
        x2 = aa * bb % P
        z2 = e * (aa + 121665 * e) % P
    if swap:
        x2, x3, z2, z3 = x3, x2, z3, z2
    return (x2 * pow(z2, P - 2, P) % P).to_bytes(32, "little")


def ed_pub_to_u(pub):
    y = int.from_bytes(pub, "little") & ((1 << 255) - 1)
    return (1 + y) * pow((1 - y) % P, P - 2, P) % P


def member_scalar(seed):
    return _clamp(hashlib.sha512(seed).digest()[:32])


def shared_secret(member_seed, node_pub):
    return x25519(member_scalar(member_seed), ed_pub_to_u(node_pub))


# ---- AES-128, encryption only (FIPS-197)
def _sbox():
    s, p, q = [0] * 256, 1, 1
    while True:
        p = p ^ ((p << 1) & 0xFF) ^ (0x1B if p & 0x80 else 0)
        q ^= q << 1
        q ^= q << 2
        q ^= q << 4
        q &= 0xFF
        if q & 0x80:
            q ^= 0x09
        x = q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4))
        s[p] = (x ^ 0x63) & 0xFF
        if p == 1:
            break
    s[0] = 0x63
    return s


SBOX = _sbox()


def _xt(b):
    return ((b << 1) ^ 0x1B) & 0xFF if b & 0x80 else b << 1


def _expand(key):
    w = [list(key[i:i + 4]) for i in range(0, 16, 4)]
    rcon = 1
    for i in range(4, 44):
        t = list(w[i - 1])
        if i % 4 == 0:
            t = [SBOX[b] for b in t[1:] + t[:1]]
            t[0] ^= rcon
            rcon = _xt(rcon)
        w.append([a ^ b for a, b in zip(w[i - 4], t)])
    return [sum(w[r * 4:r * 4 + 4], []) for r in range(11)]


def aes128_encrypt_block(key, block):
    rk = _expand(key)
    s = [b ^ k for b, k in zip(block, rk[0])]
    for r in range(1, 11):
        s = [SBOX[b] for b in s]
        s = [s[(i + 4 * (i % 4)) % 16] for i in range(16)]   # ShiftRows, column-major state
        if r != 10:
            m = []
            for c in range(4):
                a = s[4 * c:4 * c + 4]
                m += [_xt(a[0]) ^ _xt(a[1]) ^ a[1] ^ a[2] ^ a[3], a[0] ^ _xt(a[1]) ^ _xt(a[2]) ^ a[2] ^ a[3],
                      a[0] ^ a[1] ^ _xt(a[2]) ^ _xt(a[3]) ^ a[3], _xt(a[0]) ^ a[0] ^ a[1] ^ a[2] ^ _xt(a[3])]
            s = m
        s = [b ^ k for b, k in zip(s, rk[r])]
    return bytes(s)


def _ctr(key16, data):
    out = bytearray()
    for i in range(0, len(data), 16):
        ks = aes128_encrypt_block(key16, (i // 16).to_bytes(16, "big"))
        out += bytes(a ^ b for a, b in zip(data[i:i + 16], ks))
    return bytes(out)


# ---- mecon-kx-v1
def _k(shared, job_id):
    return hmac.new(shared, LABEL + job_id.to_bytes(4, "big"), hashlib.sha256).digest()


def wrap(shared, job_id, key):
    k = _k(shared, job_id)
    ct = _ctr(k[:16], key)
    return ct + hmac.new(k[16:], ct, hashlib.sha256).digest()[:16]


def unwrap(member_seed, node_pub, job_id, wrapped):
    """The exported key, or ValueError when the tag does not verify."""
    k = _k(shared_secret(member_seed, node_pub), job_id)
    ct, tag = wrapped[:-16], wrapped[-16:]
    if not hmac.compare_digest(hmac.new(k[16:], ct, hashlib.sha256).digest()[:16], tag):
        raise ValueError("mecon-kx-v1: tag does not verify")
    return _ctr(k[:16], ct)


if __name__ == "__main__":   # FIPS-197 appendix C.1 and RFC 7748 section 5.2, checked on every run
    assert aes128_encrypt_block(bytes(range(16)), bytes.fromhex("00112233445566778899aabbccddeeff")).hex() == \
        "69c4e0d86a7b0430d8cdb78070b4c55a"
    k = bytes.fromhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4")
    u = int.from_bytes(bytes.fromhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c"), "little")
    assert x25519(_clamp(k), u).hex() == "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552"
    print("mecon_kx self-test: ok")
