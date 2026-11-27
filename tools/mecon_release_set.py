#!/usr/bin/env python3
"""A release's update set (docs/contract/UPDATES.md section 3.1). Holds no key: mecon_release_key.py signs what this writes.

    mecon_release_set.py manifests <images dir> <tag> <commit> <key_id> <issued_at> <out dir>
        One <variant>-<version>.manifest.txt per variant in ci/release-variants.json, for the image of that name in
        <images dir>: <variant>.bin for kind app, <variant>.zip for kind bootloader-dfu. A missing image is an error.

    mecon_release_set.py index <images dir> <tag> <signed dir>
        Writes <signed dir>/update-set.json from the signed manifests there, and copies each image in under its asset name.
        A bootloader-dfu variant also needs <images dir>/<variant>.uf2, the same application for the bootloader's USB drive. It
        is decoded and compared with the firmware.bin of that variant's zip, and refused unless they agree; it is copied in as
        <variant>-<version>.uf2 and the entry records its asset, hash and base address.

    mecon_release_set.py verify <signed dir>
        Checks every entry of the index: its signature against a production key in docs/contract/release-keys.json, and its
        image and manifest hashes, and for a listed recovery UF2 its hash and that it decodes to its zip's firmware.bin.
        A test key does not pass.
"""
import hashlib
import json
import os
import shutil
import struct
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UPSTREAM = "v1.18.0"     # MECON_UPSTREAM_BASE (src/helpers/MeconVersion.h)
INTERFACE = "1.0"
# What every image of this release was built with (src/helpers/updates/UpdService.h and UpdFloor.h). A build that changes one
# of these changes it here in the same commit.
LAYOUT, DATA_FORMAT, SECURITY_VERSION, HARDWARE_REV = 1, 1, 1, "1"
MIN_FROM = UPSTREAM + "-mecon.0.0.0"


def fail(msg):
    sys.exit("mecon_release_set: " + msg)


def variants():
    with open(os.path.join(ROOT, "ci", "release-variants.json")) as f:
        return json.load(f)["variants"]


def version_of(tag):
    if not tag.startswith("v") or not all(p.isdigit() for p in tag[1:].split(".")) or len(tag[1:].split(".")) != 3:
        fail("a release tag is vX.Y.Z: %r" % tag)
    return "%s-mecon.%s" % (UPSTREAM, tag[1:])


def image_name(v):
    return v["variant"] + (".zip" if v["kind"] == "bootloader-dfu" else ".bin")


def read_image(images, v):
    path = os.path.join(images, image_name(v))
    if not os.path.isfile(path):
        fail("no image for %s: %s" % (v["variant"], path))
    with open(path, "rb") as f:
        return f.read()


UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
UF2_FAMILY_NRF52840 = 0xADA52840
UF2_FLAG_FAMILY = 0x2000


def uf2_decode(data, what):
    """(family, base address, payload) of a UF2 file. Refuses a malformed block, a mixed family and a gap or overlap in the addresses."""
    if not data or len(data) % 512:
        fail("%s is not a whole number of 512-byte UF2 blocks" % what)
    family = base = None
    payload = bytearray()
    count = len(data) // 512
    for i in range(count):
        b = data[i * 512:(i + 1) * 512]
        m0, m1, flags, addr, size, no, total, fam = struct.unpack_from("<8I", b, 0)
        if m0 != UF2_MAGIC0 or m1 != UF2_MAGIC1 or struct.unpack_from("<I", b, 508)[0] != UF2_MAGIC_END:
            fail("%s: block %d has bad magic numbers" % (what, i))
        if flags != UF2_FLAG_FAMILY or size == 0 or size > 476 or no != i or total != count:
            fail("%s: block %d has flags, size or numbering that are not a family-tagged sequential UF2" % (what, i))
        if i == 0:
            family, base = fam, addr
        elif fam != family:
            fail("%s: block %d changes the family" % (what, i))
        elif addr != base + len(payload):
            fail("%s: block %d is at 0x%x, not contiguous (expected 0x%x)" % (what, i, addr, base + len(payload)))
        payload += b[32:32 + size]
    return family, base, bytes(payload)


def uf2_checked(uf2, zip_bytes, what):
    """The UF2's base address, once it is shown to be the application of that DFU zip (nrfutil's firmware.bin) for the nRF52840."""
    import io
    family, base, payload = uf2_decode(uf2, what)
    if family != UF2_FAMILY_NRF52840:
        fail("%s has family 0x%08x, not 0x%08x" % (what, family, UF2_FAMILY_NRF52840))
    try:
        with zipfile.ZipFile(io.BytesIO(zip_bytes)) as z:
            fw = z.read("firmware.bin")
    except (zipfile.BadZipFile, KeyError):
        fail("the DFU zip for %s has no firmware.bin" % what)
    # The converter pads the last block to 256 bytes with 0xFF, the erased state of flash (0x00 is accepted too); nothing else may differ.
    pad = payload[len(fw):]
    if payload[:len(fw)] != fw or len(pad) >= 256 or set(pad) not in (set(), {0xFF}, {0x00}):
        fail("%s is not the firmware.bin of its signed zip" % what)
    return base


def manifest_text(v, image, version, commit, key_id, issued_at):
    m = {"v": 1, "key_id": key_id, "line": "public", "version": version, "build": commit, "upstream": UPSTREAM, "interface": INTERFACE,
         "hardware": v["hardware"], "hardware_rev": [HARDWARE_REV], "role": v["role"], "variant": v["variant"], "layout": LAYOUT,
         "bootloader_min": None, "data_format": DATA_FORMAT, "security_version": SECURITY_VERSION,
         "image": {"size": len(image), "sha256": hashlib.sha256(image).hexdigest()}, "kind": v["kind"], "min_from": MIN_FROM,
         "downgrade": False, "issued_at": issued_at}
    text = json.dumps(m, separators=(",", ":"))
    if len(text) > 640:
        fail("the manifest of %s is %d bytes, over the 640 of UPDATES.md section 4" % (v["variant"], len(text)))
    return text


def manifests(images, tag, commit, key_id, issued_at, out):
    version = version_of(tag)
    os.makedirs(out, exist_ok=True)
    for v in variants():
        text = manifest_text(v, read_image(images, v), version, commit, key_id, int(issued_at))
        with open(os.path.join(out, "%s-%s.manifest.txt" % (v["variant"], version)), "w") as f:
            f.write(text)


def index(images, tag, signed):
    version = version_of(tag)
    entries = []
    for v in variants():
        image = read_image(images, v)
        name = "%s-%s" % (v["variant"], version)
        with open(os.path.join(signed, name + ".manifest.json"), "rb") as f:
            raw = f.read()
        text = json.loads(raw)["manifest"]
        m = json.loads(text)
        if m["image"]["sha256"] != hashlib.sha256(image).hexdigest() or m["variant"] != v["variant"] or m["version"] != version:
            fail("the signed manifest of %s is not for this image" % v["variant"])
        asset = name + image_name(v)[len(v["variant"]):]
        shutil.copyfile(os.path.join(images, image_name(v)), os.path.join(signed, asset))
        extra = {}
        if v["kind"] == "bootloader-dfu":
            uf2_path = os.path.join(images, v["variant"] + ".uf2")
            if not os.path.isfile(uf2_path):
                fail("no recovery UF2 for %s: %s" % (v["variant"], uf2_path))
            with open(uf2_path, "rb") as f:
                uf2 = f.read()
            base = uf2_checked(uf2, image, v["variant"] + ".uf2")
            uf2_asset = name + ".uf2"
            with open(os.path.join(signed, uf2_asset), "wb") as f:
                f.write(uf2)
            extra = {"recovery_uf2": uf2_asset, "recovery_uf2_sha256": hashlib.sha256(uf2).hexdigest(), "recovery_uf2_base": "0x%x" % base}
        entries.append({"variant": v["variant"], "hardware": v["hardware"], "role": v["role"], "kind": v["kind"], "manifest": name + ".manifest.json",
                        "image": asset, "image_sha256": m["image"]["sha256"], "image_size": len(image),
                        "manifest_sha256": hashlib.sha256(text.encode()).hexdigest(), **extra})
    with open(os.path.join(signed, "update-set.json"), "w") as f:
        json.dump({"set": 1, "line": "public", "version": version, "variants": entries}, f, indent=1)


def verify(signed, keys_file=os.path.join(ROOT, "docs", "contract", "release-keys.json")):
    # Here, not at the top: the other commands run where only the signing tool's own dependencies are promised.
    import base64
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    with open(keys_file) as f:
        keys = {k["key_id"]: k for k in json.load(f)["keys"] if k["use"] == "production"}
    with open(os.path.join(signed, "update-set.json")) as f:
        entries = json.load(f)["variants"]
    if len(entries) != len(variants()):
        fail("the index lists %d variants, not %d" % (len(entries), len(variants())))
    for e in entries:
        with open(os.path.join(signed, e["manifest"])) as f:
            asset = json.load(f)
        text = asset["manifest"].encode()
        kid = json.loads(text)["key_id"]
        if kid not in keys:
            fail("%s is signed by %s, not a production key" % (e["variant"], kid))
        try:
            Ed25519PublicKey.from_public_bytes(bytes.fromhex(keys[kid]["public_key"])).verify(
                base64.b64decode(asset["manifest_sig"]), b"MECON-RELEASE-v1\x00" + text)
        except InvalidSignature:
            fail("the signature of %s does not verify" % e["variant"])
        with open(os.path.join(signed, e["image"]), "rb") as f:
            image = f.read()
        if hashlib.sha256(image).hexdigest() != e["image_sha256"] or json.loads(text)["image"]["sha256"] != e["image_sha256"]:
            fail("the image of %s is not the one signed" % e["variant"])
        if hashlib.sha256(text).hexdigest() != e["manifest_sha256"]:
            fail("the index's hash of the manifest of %s is wrong" % e["variant"])
        if e["kind"] == "bootloader-dfu" and "recovery_uf2" not in e:
            fail("%s lists no recovery UF2" % e["variant"])
        if "recovery_uf2" in e:
            with open(os.path.join(signed, e["recovery_uf2"]), "rb") as f:
                uf2 = f.read()
            if hashlib.sha256(uf2).hexdigest() != e["recovery_uf2_sha256"]:
                fail("the recovery UF2 of %s is not the one listed" % e["variant"])
            if "0x%x" % uf2_checked(uf2, image, e["recovery_uf2"]) != e["recovery_uf2_base"]:
                fail("the recovery UF2 of %s is at another address than listed" % e["variant"])
    print("%d variants verified" % len(entries))


def main(argv):
    if len(argv) == 8 and argv[1] == "manifests":
        manifests(*argv[2:])
    elif len(argv) == 5 and argv[1] == "index":
        index(*argv[2:])
    elif len(argv) == 3 and argv[1] == "verify":
        verify(argv[2])
    else:
        fail(__doc__)


if __name__ == "__main__":
    main(sys.argv)
