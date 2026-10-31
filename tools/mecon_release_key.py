#!/usr/bin/env python3
"""Production release keys (docs/contract/UPDATES.md section 3): make one, and sign a release manifest with it.

    mecon_release_key.py generate <key file>
        Makes an Ed25519 key from the operating system's random source and writes its 32-byte seed to <key file>, which
        must not exist yet, readable by its owner only (mode 600). Prints the public key, its key_id and the entry for
        docs/contract/release-keys.json, and nothing of the private key. Run it where the key is to live.

    mecon_release_key.py sign <key file> <manifest text file> <out file>
        Signs the manifest's exact bytes (MECON-RELEASE-v1, 0x00, the text) and writes the update-set asset,
        {"manifest": "<text>", "manifest_sig": "<base64>"} (section 3.1), to <out file>. The manifest is checked first:
        one JSON object whose key_id is this key's.

    mecon_release_key.py public <key file>
        Prints the public key and key_id again.

No private key ever goes to stdout, a log, the repository or a build: only the seed file holds it.
"""
import base64
import hashlib
import json
import os
import stat
import sys

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

DOMAIN = b"MECON-RELEASE-v1\x00"


def fail(msg):
    print("error: " + msg, file=sys.stderr)
    sys.exit(2)


def public_of(seed):
    return Ed25519PrivateKey.from_private_bytes(seed).public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)


def key_id(pub):
    return hashlib.sha256(pub).digest()[:8].hex()


def read_seed(path):
    st = os.stat(path)
    if st.st_mode & (stat.S_IRWXG | stat.S_IRWXO):
        fail("%s is readable by others than its owner: chmod 600 it first" % path)
    with open(path, "rb") as f:
        seed = f.read()
    if len(seed) != 32:
        fail("%s is not a 32-byte key seed" % path)
    return seed


def generate(path):
    if os.path.exists(path):
        fail("%s exists: a key is never overwritten" % path)
    seed = os.urandom(32)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)   # created owner-only, never wider for an instant
    with os.fdopen(fd, "wb") as f:
        f.write(seed)
    pub = public_of(seed)
    show(pub)


def show(pub):
    entry = {"line": "public", "key_id": key_id(pub), "public_key": pub.hex(), "use": "production"}
    print("public_key " + pub.hex())
    print("key_id     " + key_id(pub))
    print("release-keys.json entry:")
    print(json.dumps(entry, indent=2))


def sign(path, manifest_path, out_path):
    seed = read_seed(path)
    with open(manifest_path, "rb") as f:
        text = f.read()
    try:
        m = json.loads(text)
    except ValueError:
        fail("the manifest is not JSON")
    if not isinstance(m, dict) or m.get("key_id") != key_id(public_of(seed)):
        fail("the manifest's key_id is not this key's (%s)" % key_id(public_of(seed)))
    sig = Ed25519PrivateKey.from_private_bytes(seed).sign(DOMAIN + text)
    with open(out_path, "w") as f:
        json.dump({"manifest": text.decode("utf-8"), "manifest_sig": base64.b64encode(sig).decode()}, f,
                  separators=(",", ":"))
    print("signed %s with %s -> %s" % (manifest_path, key_id(public_of(seed)), out_path))


def main(argv):
    if len(argv) == 3 and argv[1] == "generate":
        generate(argv[2])
    elif len(argv) == 3 and argv[1] == "public":
        show(public_of(read_seed(argv[2])))
    elif len(argv) == 5 and argv[1] == "sign":
        sign(argv[2], argv[3], argv[4])
    else:
        print(__doc__.strip())
        sys.exit(2)


if __name__ == "__main__":
    main(sys.argv)
