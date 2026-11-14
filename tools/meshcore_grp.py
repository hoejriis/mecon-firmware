#!/usr/bin/env python3
"""A native MeshCore channel message (GRP_TXT), built as Mesh::createGroupDatagram builds it, so a test manager can hand a
Repeater a packet a stock Companion reads (packet.transmit, #537). Test channels only: the secret is the caller's.
The bytes are Packet::writeTo's: header, path_len, payload (channel hash, 2-byte MAC, AES-128-ECB of timestamp, text type,
"sender: text")."""
import hashlib
import hmac
import struct

from mecon_kx import aes128_encrypt_block

PAYLOAD_TYPE_GRP_TXT, ROUTE_TYPE_FLOOD = 5, 1


def channel_hash(secret16):
    return hashlib.sha256(secret16).digest()[:1]


def grp_txt(secret16, sender, text, timestamp, route=ROUTE_TYPE_FLOOD):
    """The packet's bytes, as an empty-path flood unless another route is asked for."""
    assert len(secret16) == 16
    plain = struct.pack("<I", timestamp) + b"\x00" + ("%s: %s" % (sender, text)).encode()
    plain += b"\x00" * (-len(plain) % 16)                      # Utils::encrypt pads the last block with zeros
    cipher = b"".join(aes128_encrypt_block(secret16, plain[i:i + 16]) for i in range(0, len(plain), 16))
    mac = hmac.new(secret16 + b"\x00" * 16, cipher, hashlib.sha256).digest()[:2]   # HMAC over the 32-byte secret
    payload = channel_hash(secret16) + mac + cipher
    assert len(payload) <= 184, len(payload)
    return bytes([route | (PAYLOAD_TYPE_GRP_TXT << 2), 0]) + payload
