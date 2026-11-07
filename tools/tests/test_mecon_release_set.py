"""A release's update set (tools/mecon_release_set.py, docs/contract/UPDATES.md section 3.1): the tool's fixed values match the
firmware's, and its check refuses what was not signed by a published production key."""
import hashlib
import json
import os
import re
import subprocess
import sys

import pytest

TOOLS = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, TOOLS)
import mecon_release_set as rs  # noqa: E402

ROOT = rs.ROOT
KEY_TOOL = os.path.join(TOOLS, "mecon_release_key.py")


def source(path):
    with open(os.path.join(ROOT, path)) as f:
        return f.read()


def test_the_tools_fixed_values_are_the_firmwares():
    h = source("src/helpers/updates/UpdService.h")
    assert "#define MECON_LAYOUT %d " % rs.LAYOUT in h and "#define MECON_DATA_FORMAT %d " % rs.DATA_FORMAT in h
    assert '#define MECON_HARDWARE_REV "%s"' % rs.HARDWARE_REV in h
    assert "#define MECON_SECURITY_VERSION %d " % rs.SECURITY_VERSION in source("src/helpers/updates/UpdFloor.h")
    assert '#define MECON_UPSTREAM_BASE "%s"' % rs.UPSTREAM in source("src/helpers/MeconVersion.h")


def test_every_variant_is_a_build_and_none_overrides_the_fixed_values():
    inis = ""
    for name in ("heltec_v3", "heltec_v4", "wio-tracker-l1", "t1000-e"):
        inis += source("variants/%s/platformio.ini" % name)
    envs = re.findall(r"^\[env:([^\]]+)\]", inis, re.M)
    for v in rs.variants():
        assert v["variant"] in envs, v
        assert v["kind"] == ("bootloader-dfu" if v["variant"].startswith(("t1000e", "WioTracker")) else "app"), v
    assert not re.search(r"MECON_(LAYOUT|DATA_FORMAT|HARDWARE_REV|SECURITY_VERSION)=", inis)


def test_every_hardware_name_is_one_the_board_reports():
    boards = {"Heltec V3": "variants/heltec_v3/HeltecV3Board.h", "Heltec V4 OLED": "variants/heltec_v4/HeltecV4Board.cpp",
              "Seeed Wio Tracker L1": "variants/wio-tracker-l1/WioTrackerL1Board.h", "Seeed Tracker T1000-E": "variants/t1000-e/T1000eBoard.h"}
    assert {v["hardware"] for v in rs.variants()} == set(boards)
    for name, path in boards.items():
        assert name in source(path), name


def test_a_tag_that_is_not_a_plain_version_is_refused():
    for bad in ("0.2.0", "v0.2", "v0.2.0-rc1", "v0.2.x"):
        with pytest.raises(SystemExit):
            rs.version_of(bad)
    assert rs.version_of("v0.2.0") == "v1.18.0-mecon.0.2.0"


@pytest.fixture
def signed_set(tmp_path, monkeypatch):
    """A one-variant set, signed by a key made here, with a key list naming it production."""
    seed = tmp_path / "seed"
    out = subprocess.run([sys.executable, KEY_TOOL, "generate", str(seed)], capture_output=True, text=True, check=True).stdout
    assert seed.read_bytes() and seed.read_bytes().hex() not in out, "the seed is never printed"
    pub = re.search(r"public_key (\w+)", out).group(1)
    kid = re.search(r"key_id +(\w+)", out).group(1)
    monkeypatch.setattr(rs, "variants", lambda: [{"variant": "x", "hardware": "H", "role": "room", "kind": "app"}])
    images, manifests, signed = tmp_path / "images", tmp_path / "manifests", tmp_path / "signed"
    images.mkdir(); signed.mkdir()
    (images / "x.bin").write_bytes(b"image bytes")
    rs.manifests(str(images), "v0.2.0", "c" * 40, kid, 1790000000, str(manifests))
    name = "x-v1.18.0-mecon.0.2.0"
    subprocess.run([sys.executable, KEY_TOOL, "sign", str(seed), str(manifests / (name + ".manifest.txt")), str(signed / (name + ".manifest.json"))], check=True, capture_output=True)
    rs.index(str(images), "v0.2.0", str(signed))
    keys = tmp_path / "keys.json"
    keys.write_text(json.dumps({"keys": [{"key_id": kid, "public_key": pub, "use": "production"}]}))
    return signed, name, keys, kid, pub, images


def test_a_properly_signed_set_passes_and_its_index_is_the_contracts(signed_set):
    signed, name, keys, *_ = signed_set
    rs.verify(str(signed), str(keys))
    idx = json.loads((signed / "update-set.json").read_text())
    assert idx["set"] == 1 and idx["line"] == "public" and idx["version"] == "v1.18.0-mecon.0.2.0"
    e = idx["variants"][0]
    assert sorted(e) == sorted(["variant", "hardware", "role", "kind", "manifest", "image", "image_sha256", "image_size", "manifest_sha256"])
    assert e["image"] == name + ".bin" and e["manifest"] == name + ".manifest.json" and (signed / e["image"]).is_file()


def test_verify_refuses_a_changed_image_a_changed_manifest_and_a_key_not_published_as_production(signed_set):
    signed, name, keys, kid, pub, _ = signed_set
    original = (signed / (name + ".bin")).read_bytes()
    (signed / (name + ".bin")).write_bytes(b"other bytes")
    with pytest.raises(SystemExit, match="not the one signed"):
        rs.verify(str(signed), str(keys))
    (signed / (name + ".bin")).write_bytes(original)

    path = signed / (name + ".manifest.json")
    good = path.read_text()
    bad = json.loads(good)
    bad["manifest"] = bad["manifest"].replace('"role":"room"', '"role":"repeater"')
    path.write_text(json.dumps(bad))
    with pytest.raises(SystemExit, match="does not verify"):
        rs.verify(str(signed), str(keys))
    path.write_text(good)

    keys.write_text(json.dumps({"keys": [{"key_id": kid, "public_key": pub, "use": "test"}]}))
    with pytest.raises(SystemExit, match="not a production key"):
        rs.verify(str(signed), str(keys))


def test_the_index_refuses_a_signed_manifest_for_another_image(signed_set, tmp_path):
    signed, _, _, _, _, images = signed_set
    (images / "x.bin").write_bytes(b"different")
    with pytest.raises(SystemExit, match="not for this image"):
        rs.index(str(images), "v0.2.0", str(signed))


def test_the_published_production_keys_match_their_ids_and_three_exist():
    with open(os.path.join(ROOT, "docs", "contract", "release-keys.json")) as f:
        keys = json.load(f)["keys"]
    prod = [k for k in keys if k["use"] == "production"]
    assert len(prod) == 3
    for k in prod:
        assert hashlib.sha256(bytes.fromhex(k["public_key"])).digest()[:8].hex() == k["key_id"], k
