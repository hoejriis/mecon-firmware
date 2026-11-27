"""A release's update set (tools/mecon_release_set.py, docs/contract/UPDATES.md section 3.1): the tool's fixed values match the
firmware's, and its check refuses what was not signed by a published production key."""
import hashlib
import json
import os
import io
import re
import struct
import subprocess
import sys
import zipfile

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


# --- the recovery UF2 of an nRF52 (bootloader-dfu) variant ---

APP = bytes((i * 7 + 3) % 251 + 1 for i in range(700))    # ends inside a 256-byte block; no zero bytes
BASE = 0x27000


def make_uf2(data, base=BASE, family=rs.UF2_FAMILY_NRF52840, gap_at=None, pad=b"\xff"):
    data = data + pad * (-len(data) % 256)
    n = len(data) // 256
    out = b""
    for i in range(n):
        addr = base + i * 256 + (4096 if gap_at is not None and i >= gap_at else 0)
        out += struct.pack("<8I", rs.UF2_MAGIC0, rs.UF2_MAGIC1, rs.UF2_FLAG_FAMILY, addr, 256, i, n, family)
        out += data[i * 256:(i + 1) * 256].ljust(476, b"\0") + struct.pack("<I", rs.UF2_MAGIC_END)
    return out


def make_zip(app=APP):
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w") as z:
        z.writestr("firmware.bin", app)
        z.writestr("firmware.dat", b"init packet")
    return b.getvalue()


@pytest.fixture
def dfu_set(tmp_path, monkeypatch):
    """Images and signed manifests of one nRF52 variant and one ESP32 variant; index not yet run."""
    seed = tmp_path / "seed"
    out = subprocess.run([sys.executable, KEY_TOOL, "generate", str(seed)], capture_output=True, text=True, check=True).stdout
    kid = re.search(r"key_id +(\w+)", out).group(1)
    pub = re.search(r"public_key (\w+)", out).group(1)
    monkeypatch.setattr(rs, "variants", lambda: [
        {"variant": "n", "hardware": "N", "role": "companion", "kind": "bootloader-dfu"},
        {"variant": "e", "hardware": "E", "role": "room", "kind": "app"}])
    images, signed = tmp_path / "images", tmp_path / "signed"
    images.mkdir(); signed.mkdir()
    (images / "n.zip").write_bytes(make_zip())
    (images / "n.uf2").write_bytes(make_uf2(APP))
    (images / "e.bin").write_bytes(b"esp image")
    rs.manifests(str(images), "v0.2.0", "c" * 40, kid, 1790000000, str(tmp_path / "man"))
    for v in ("n", "e"):
        name = v + "-v1.18.0-mecon.0.2.0"
        subprocess.run([sys.executable, KEY_TOOL, "sign", str(seed), str(tmp_path / "man" / (name + ".manifest.txt")),
                        str(signed / (name + ".manifest.json"))], check=True, capture_output=True)
    keys = tmp_path / "keys.json"
    keys.write_text(json.dumps({"keys": [{"key_id": kid, "public_key": pub, "use": "production"}]}))
    return images, signed, keys


def test_a_good_uf2_passes_is_published_and_its_base_recorded(dfu_set):
    images, signed, keys = dfu_set
    rs.index(str(images), "v0.2.0", str(signed))
    rs.verify(str(signed), str(keys))
    idx = {e["variant"]: e for e in json.loads((signed / "update-set.json").read_text())["variants"]}
    n = idx["n"]
    assert n["recovery_uf2"] == "n-v1.18.0-mecon.0.2.0.uf2" and n["recovery_uf2_base"] == "0x27000"
    assert n["recovery_uf2_sha256"] == hashlib.sha256((images / "n.uf2").read_bytes()).hexdigest()
    assert (signed / n["recovery_uf2"]).read_bytes() == (images / "n.uf2").read_bytes()
    assert not any(k.startswith("recovery") for k in idx["e"]) and not (signed / "e-v1.18.0-mecon.0.2.0.uf2").exists()


def test_the_base_is_read_from_the_uf2_not_assumed(dfu_set):
    images, signed, _ = dfu_set
    (images / "n.uf2").write_bytes(make_uf2(APP, base=0x26000))
    rs.index(str(images), "v0.2.0", str(signed))
    assert json.loads((signed / "update-set.json").read_text())["variants"][0]["recovery_uf2_base"] == "0x26000"


def test_a_zero_padded_last_block_is_the_same_image(dfu_set):
    images, signed, _ = dfu_set
    (images / "n.uf2").write_bytes(make_uf2(APP, pad=b"\0"))
    rs.index(str(images), "v0.2.0", str(signed))


@pytest.mark.parametrize("bad,match", [
    ("changed_byte", "not the firmware.bin"),
    ("wrong_family", "family"),
    ("gap", "not contiguous"),
    ("extra_block", "not the firmware.bin"),
    ("padding_that_is_data", "not the firmware.bin"),
    ("bad_magic", "magic"),
    ("truncated", "512-byte"),
])
def test_the_index_refuses_a_uf2_that_is_not_its_zips_application(dfu_set, bad, match):
    images, signed, _ = dfu_set
    u = {"changed_byte": lambda: bytearray(make_uf2(APP)),
         "wrong_family": lambda: make_uf2(APP, family=0xE48BFF56),
         "gap": lambda: make_uf2(APP, gap_at=1),
         "extra_block": lambda: make_uf2(APP + b"\x01" * 256),
         "padding_that_is_data": lambda: make_uf2(APP + b"\x07"),
         "bad_magic": lambda: bytearray(make_uf2(APP)),
         "truncated": lambda: make_uf2(APP)[:-100]}[bad]()
    if bad == "changed_byte":
        u[32 + 5] ^= 1
    if bad == "bad_magic":
        u[0] ^= 1
    (images / "n.uf2").write_bytes(bytes(u))
    with pytest.raises(SystemExit, match=match):
        rs.index(str(images), "v0.2.0", str(signed))


def test_the_index_needs_a_uf2_for_every_bootloader_dfu_variant_and_none_for_an_app(dfu_set):
    images, signed, _ = dfu_set
    (images / "n.uf2").unlink()
    with pytest.raises(SystemExit, match="no recovery UF2 for n"):
        rs.index(str(images), "v0.2.0", str(signed))
    (images / "n.uf2").write_bytes(make_uf2(APP))
    (images / "e.uf2").write_bytes(b"not looked at")      # an ESP32 variant neither needs nor takes one
    rs.index(str(images), "v0.2.0", str(signed))
    assert not (signed / "e-v1.18.0-mecon.0.2.0.uf2").exists()


def test_verify_refuses_a_changed_uf2_a_swapped_uf2_and_an_index_without_one(dfu_set):
    images, signed, keys = dfu_set
    rs.index(str(images), "v0.2.0", str(signed))
    asset = signed / "n-v1.18.0-mecon.0.2.0.uf2"
    good = asset.read_bytes()
    asset.write_bytes(good[:-600] + bytes([good[-600] ^ 1]) + good[-599:])
    with pytest.raises(SystemExit, match="not the one listed"):
        rs.verify(str(signed), str(keys))
    # a UF2 that is internally valid, listed with its own hash, but not the zip's application
    other = make_uf2(APP[:-1] + b"\x09")
    asset.write_bytes(other)
    idx = json.loads((signed / "update-set.json").read_text())
    idx["variants"][0]["recovery_uf2_sha256"] = hashlib.sha256(other).hexdigest()
    (signed / "update-set.json").write_text(json.dumps(idx))
    with pytest.raises(SystemExit, match="not the firmware.bin"):
        rs.verify(str(signed), str(keys))
    del idx["variants"][0]["recovery_uf2"]
    (signed / "update-set.json").write_text(json.dumps(idx))
    with pytest.raises(SystemExit, match="lists no recovery UF2"):
        rs.verify(str(signed), str(keys))


def test_the_real_converter_makes_a_uf2_the_decoder_accepts(tmp_path):
    hexf = tmp_path / "f.hex"
    body = bytes(range(1, 241)) * 3
    lines = [":020000040000FA"]
    for off in range(0, len(body), 16):
        chunk = body[off:off + 16]
        addr = BASE + off
        rec = bytes([len(chunk), (addr >> 8) & 255, addr & 255, 0]) + chunk
        lines.append(":" + rec.hex().upper() + "%02X" % ((-sum(rec)) & 255))
    lines.insert(1, ":02000004%04X%02X" % (BASE >> 16, (-(2 + 4 + (BASE >> 16))) & 255))
    lines.append(":00000001FF")
    hexf.write_text("\n".join(lines) + "\n")
    uf2 = tmp_path / "f.uf2"
    subprocess.run([sys.executable, os.path.join(ROOT, "bin", "uf2conv", "uf2conv.py"), "-c", "-f", "0xADA52840", "-o", str(uf2), str(hexf)],
                   check=True, capture_output=True)
    fam, base, payload = rs.uf2_decode(uf2.read_bytes(), "t")
    assert (fam, base) == (rs.UF2_FAMILY_NRF52840, BASE) and payload[:len(body)] == body
