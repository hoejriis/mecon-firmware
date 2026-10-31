"""tools/mecon_release_key.py: the production release key tool (docs/contract/UPDATES.md section 3). It makes a key whose
private half lives only in an owner-only file, and signs a manifest the way a device verifies it."""
import base64
import hashlib
import json
import os
import stat
import subprocess
import sys

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

TOOL = os.path.join(os.path.dirname(__file__), "..", "mecon_release_key.py")


def run(*args):
    return subprocess.run([sys.executable, TOOL] + list(args), capture_output=True, text=True)


def test_a_new_key_lives_only_in_an_owner_only_file(tmp_path):
    key = str(tmp_path / "release-1.key")
    r = run("generate", key)
    assert r.returncode == 0, r.stderr
    assert stat.S_IMODE(os.stat(key).st_mode) == 0o600
    seed = open(key, "rb").read()
    assert len(seed) == 32
    out = r.stdout + r.stderr
    assert seed.hex() not in out and base64.b64encode(seed).decode() not in out, "the private key is never printed"
    entry = json.loads(out[out.index("{"):])
    assert entry["use"] == "production" and entry["line"] == "public"
    pub = bytes.fromhex(entry["public_key"])
    assert entry["key_id"] == hashlib.sha256(pub).digest()[:8].hex()
    again = run("public", key)
    assert entry["public_key"] in again.stdout
    r = run("generate", key)
    assert r.returncode != 0 and open(key, "rb").read() == seed, "an existing key is never overwritten"


def test_it_signs_the_exact_manifest_bytes_as_a_device_verifies_them(tmp_path):
    key = str(tmp_path / "k")
    out = run("generate", key).stdout
    entry = json.loads(out[out.index("{"):])
    text = json.dumps({"v": 1, "key_id": entry["key_id"], "version": "v1.18.0-mecon.0.9.0"}, separators=(",", ":"))
    mpath, apath = str(tmp_path / "m.json"), str(tmp_path / "asset.json")
    open(mpath, "w").write(text)
    r = run("sign", key, mpath, apath)
    assert r.returncode == 0, r.stderr
    asset = json.load(open(apath))
    assert asset["manifest"] == text, "the asset carries the signed text unchanged"
    pub = Ed25519PublicKey.from_public_bytes(bytes.fromhex(entry["public_key"]))
    pub.verify(base64.b64decode(asset["manifest_sig"]), b"MECON-RELEASE-v1\x00" + text.encode())   # raises if wrong


def test_it_refuses_a_manifest_for_another_key_and_a_key_others_can_read(tmp_path):
    key = str(tmp_path / "k")
    run("generate", key)
    mpath = str(tmp_path / "m.json")
    open(mpath, "w").write(json.dumps({"v": 1, "key_id": "00" * 8}))
    r = run("sign", key, mpath, str(tmp_path / "a.json"))
    assert r.returncode != 0 and "key_id" in r.stderr
    assert not os.path.exists(str(tmp_path / "a.json"))
    os.chmod(key, 0o640)
    r = run("public", key)
    assert r.returncode != 0 and "chmod 600" in r.stderr
