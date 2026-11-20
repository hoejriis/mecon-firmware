#!/usr/bin/env python3
"""Send one channel message as a scoped flood from a Companion, for testing a Repeater's region rules.

The Companion's flood scope is set for this connection only (CMD_SET_FLOOD_SCOPE_KEY, 54) to the transport key of a region
name: the first 16 bytes of SHA-256 over "#<name>", as for a public hashtag region. One channel text then goes out on the
given channel slot as a flood carrying that region's transport code, and the scope is cleared again. Nothing is saved on the
Companion: its default flood scope (63) and its channels are left as they are.

A name no Repeater has configured gives a scope it does not know: with `region.neutral` on, a Repeater forwards the
flood; off, it drops it. Use a private channel the receiver also holds, never the public one, and one message per run.

Examples:
  scoped_flood.py --port /dev/ttyUSB0 --channel 1 --region bench-unknown "scoped test 1"
  scoped_flood.py --port /dev/ttyACM0 --dtr --channel 1 --region bench-unknown "scoped test 2"   # nRF52 native USB
  scoped_flood.py --tcp 127.0.0.1:5000 --channel 1 --region bench-unknown "sim"                    # a simulated board

Exit status: 0 when the scope was set, the message accepted and the scope cleared; 1 otherwise.
"""
import argparse
import hashlib
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from serial_cli import Link  # noqa: E402

CMD_SEND_CHANNEL_TXT_MSG = 3
CMD_SET_FLOOD_SCOPE_KEY = 54
RESP_CODE_OK, RESP_CODE_ERR, RESP_CODE_SENT = 0, 1, 6


def scope_key(region):
    """The transport key of a public hashtag region: SHA-256 over "#<name>", first 16 bytes."""
    name = region if region.startswith("#") else "#" + region
    return hashlib.sha256(name.encode()).digest()[:16]


def request(link, payload, timeout):
    """Send one Companion frame; return the first reply frame's bytes, or None. Pushes (codes 0x80 and up) are skipped."""
    link.write(b"<" + struct.pack("<H", len(payload)) + payload)
    buf, end = b"", time.time() + timeout
    while time.time() < end:
        buf += link.read()
        while True:
            i = buf.find(b">")
            if i < 0 or len(buf) < i + 3:
                break
            (n,) = struct.unpack_from("<H", buf, i + 1)
            if n == 0 or n > 512:
                buf = buf[i + 1:]
                continue
            if len(buf) < i + 3 + n:
                break
            f, buf = buf[i + 3:i + 3 + n], buf[i + 3 + n:]
            if f[0] < 0x80:
                return f
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--dtr", action="store_true", help="assert DTR: an nRF52 board on native USB needs it")
    ap.add_argument("--tcp", help="host:port of a simulated board")
    ap.add_argument("--timeout", type=float, default=5.0)
    ap.add_argument("--channel", type=int, required=True, help="the channel slot to send on (not 0, the public channel)")
    ap.add_argument("--region", required=True, help="the region name whose transport code the flood carries")
    ap.add_argument("text")
    a = ap.parse_args()
    if not a.port and not a.tcp:
        ap.error("--port or --tcp is required")
    if a.channel <= 0:
        ap.error("--channel must be a private channel slot, not 0")
    link = Link(a.port, a.baud, a.tcp, a.dtr)
    f = request(link, bytes([CMD_SET_FLOOD_SCOPE_KEY, 0]) + scope_key(a.region), a.timeout)
    if not f or f[0] != RESP_CODE_OK:
        print("scope not set: %r" % (f,))
        return 1
    ok = False
    try:
        ts = int(time.time())
        f = request(link, bytes([CMD_SEND_CHANNEL_TXT_MSG, 0, a.channel]) + struct.pack("<I", ts) + a.text.encode(), a.timeout)
        ok = bool(f) and f[0] in (RESP_CODE_OK, RESP_CODE_SENT)
        print("sent on channel %d, scope %s: %s" % (a.channel, a.region, "accepted" if ok else "refused %r" % (f,)))
    finally:
        f = request(link, bytes([CMD_SET_FLOOD_SCOPE_KEY, 0]), a.timeout)   # no key: back to the default scope
        cleared = bool(f) and f[0] == RESP_CODE_OK
        print("scope cleared" if cleared else "scope NOT cleared: %r" % (f,))
    return 0 if ok and cleared else 1


if __name__ == "__main__":
    sys.exit(main())
