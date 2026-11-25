#!/usr/bin/env python3
"""Image bytes over LoRa (docs/contract/LORA_UPDATES.md section 6): the reference codec for a manager, and the vectors in
test/fixtures/lora_bulk_v1.json that the firmware's src/helpers/iface/LoraBulk.cpp reproduces.

    mecon_lora_bulk.py vectors > test/fixtures/lora_bulk_v1.json    write the fixture
    mecon_lora_bulk.py check test/fixtures/lora_bulk_v1.json        recompute and compare

Every integer is little endian. Nothing here is secret: the vectors use a made-up token."""
import hashlib
import hmac
import json
import struct
import sys

BULK_MARKER, ANSWER_MARKER = 0x1E, 0x1F
BULK_ROOM, BULK_HDR, BULK_TAG = 132, 9, 8
BULK_DATA = BULK_ROOM - BULK_HDR - BULK_TAG   # 115
ANSWER_LEN = 22


def keys(token):
    """The two keys a transfer token gives: one for bulk frames, one for block answers."""
    return (hmac.new(token, b"mecon lora bulk v1", hashlib.sha256).digest(),
            hmac.new(token, b"mecon lora answer v1", hashlib.sha256).digest())


def frames_total(transfer_size):
    return (transfer_size + BULK_DATA - 1) // BULK_DATA


def frame_data_len(transfer_size, index):
    total = frames_total(transfer_size)
    if index >= total:
        return 0
    return BULK_DATA if index + 1 < total else transfer_size - index * BULK_DATA


def _tag(key, msg):
    return hmac.new(key, msg, hashlib.sha256).digest()[:BULK_TAG]


def frame(k, job, transfer_size, index, data):
    """One bulk frame; ValueError when `data` is not exactly what frame `index` carries."""
    if len(data) != frame_data_len(transfer_size, index) or not data:
        raise ValueError("frame %d of a %d-byte transfer carries %d bytes, not %d"
                         % (index, transfer_size, frame_data_len(transfer_size, index), len(data)))
    head = bytes([BULK_MARKER]) + struct.pack("<II", job, index) + data
    return head + _tag(k[0], head)


def check_frame(k, job, transfer_size, f):
    """The firmware's verdict on a received frame: (name, index, data)."""
    if len(f) < BULK_HDR + 1 + BULK_TAG or f[0] != BULK_MARKER:
        return "not-bulk", None, None
    fjob, index = struct.unpack_from("<II", f, 1)
    if fjob != job:
        return "other-job", None, None
    want = frame_data_len(transfer_size, index)
    if want == 0:
        return "past-end", None, None
    if len(f) != BULK_HDR + want + BULK_TAG:
        return "bad-length", None, None
    if not hmac.compare_digest(_tag(k[0], f[:BULK_HDR + want]), f[BULK_HDR + want:]):
        return "bad-tag", None, None
    return "ok", index, f[BULK_HDR:BULK_HDR + want]


def answer(k, job, base, nxt, have):
    head = bytes([ANSWER_MARKER]) + struct.pack("<III", job, base, nxt) + bytes([have])
    return head + _tag(k[1], head)


def check_answer(k, job, f):
    """(base, next, have), or None for anything but a well-formed, correctly tagged answer for `job`."""
    if len(f) != ANSWER_LEN or f[0] != ANSWER_MARKER or struct.unpack_from("<I", f, 1)[0] != job:
        return None
    if not hmac.compare_digest(_tag(k[1], f[:14]), f[14:]):
        return None
    base, nxt = struct.unpack_from("<II", f, 5)
    return base, nxt, f[13]


def vectors():
    token = bytes(range(16))
    k = keys(token)
    job, size = 0x01020304, 235            # 115 + 115 + 5: the last frame is 22 bytes, an answer's length
    image = bytes((i * 7 + 3) & 0xFF for i in range(size))
    f = [frame(k, job, size, i, image[i * BULK_DATA:i * BULK_DATA + frame_data_len(size, i)]) for i in range(3)]
    a = answer(k, job, 2, 2, 0b11)
    reflected = bytes([BULK_MARKER]) + a[1:]           # an answer heard, its marker changed, sent at the target
    last_as_answer = bytes([ANSWER_MARKER]) + f[2][1:]  # the 22-byte last frame offered to the manager as an answer
    cases = {
        "frame_0": (f[0], "ok"),
        "frame_last": (f[2], "ok"),
        "answer_as_frame": (a, "not-bulk"),
        "answer_marker_changed_as_frame": (reflected, "bad-tag"),
        "frame_short_before_last": (f[0][:-BULK_TAG - 1] + f[0][-BULK_TAG:], "bad-length"),
        "frame_long_last": (f[2][:-BULK_TAG] + b"\0" + f[2][-BULK_TAG:], "bad-length"),
        "frame_other_job": (bytes([BULK_MARKER]) + struct.pack("<I", job + 1) + f[0][5:], "other-job"),
        "frame_past_end": (bytes([BULK_MARKER]) + struct.pack("<II", job, 3) + bytes(5) + bytes(8), "past-end"),
    }
    out = {
        "contract": "docs/contract/LORA_UPDATES.md section 6",
        "token_hex": token.hex(), "job": job, "transfer_size": size,
        "image_rule": "byte i is (7 * i + 3) mod 256",
        "key_bulk_hex": k[0].hex(), "key_answer_hex": k[1].hex(),
        "frames_hex": [x.hex() for x in f],
        "answer": {"base": 2, "next": 2, "have": 3, "hex": a.hex()},
        "frame_cases": {name: {"hex": b.hex(), "verdict": v} for name, (b, v) in cases.items()},
        "answer_cases": {
            "frame_last_as_answer": {"hex": f[2].hex(), "accepted": False},
            "frame_last_marker_changed_as_answer": {"hex": last_as_answer.hex(), "accepted": False},
            "answer": {"hex": a.hex(), "accepted": True},
        },
    }
    for name, c in out["frame_cases"].items():
        got = check_frame(k, job, size, bytes.fromhex(c["hex"]))[0]
        assert got == c["verdict"], (name, got, c["verdict"])
    for name, c in out["answer_cases"].items():
        assert (check_answer(k, job, bytes.fromhex(c["hex"])) is not None) == c["accepted"], name
    return out


if __name__ == "__main__":
    if sys.argv[1:2] == ["vectors"]:
        print(json.dumps(vectors(), indent=1))
    elif sys.argv[1:2] == ["check"]:
        ok = json.load(open(sys.argv[2])) == vectors()
        print("OK" if ok else "MISMATCH")
        sys.exit(0 if ok else 1)
    else:
        sys.exit(__doc__)
