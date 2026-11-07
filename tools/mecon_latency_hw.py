#!/usr/bin/env python3
"""mecon_latency_hw.py <port> [budget_ms]: how long a real board takes to start answering, over USB, on a Companion. It reads and
changes nothing, and sends nothing on the radio. Exit 1 when a median is over the budget (default 200 ms).

The simulator's flash has no cost, so it cannot show this. v0.2.0 took 936 ms before any answer on a Heltec V4, against 15 ms on the
build before it, and every fixture still passed. Run this on each ESP32 board as part of reading a release."""
import json
import statistics
import struct
import sys
import time

sys.path.insert(0, "tools")
import serial
import mecon_iface

port = sys.argv[1]
budget = float(sys.argv[2]) / 1000 if len(sys.argv) > 2 else 0.2
s = serial.Serial(port, 115200, timeout=0.005)
s.dtr = False
s.rts = False
buf = bytearray()


def send(p):
    s.write(b"<" + struct.pack("<H", len(p)) + p)
    s.flush()


def frame(timeout):
    end = time.time() + timeout
    while time.time() < end:
        buf.extend(s.read(4096))
        while len(buf) >= 3:
            if buf[0:1] != b">":
                del buf[0]
                continue
            n = struct.unpack_from("<H", buf, 1)[0]
            if len(buf) < 3 + n:
                break
            f = bytes(buf[3:3 + n])
            del buf[:3 + n]
            return f
    return None


end = time.time() + 120   # opening the port resets an ESP32
up = False
while time.time() < end and not up:
    send(bytes([1, 3]) + bytes(6) + b"latency")
    f = frame(2.0)
    while f is not None and f[:1] != b"\x05":
        f = frame(0.5)
    up = f is not None
if not up:
    sys.exit("the board never answered")
while frame(1.0) is not None:
    pass

failed = False
serial_no = 0
for op in ("status.get", "hello", "config.schema"):
    took = []
    for _ in range(12):
        serial_no += 1
        rid = "lat-%d" % serial_no
        body = json.dumps({"v": 1, "id": rid, "op": op}, separators=(",", ":")).encode()
        for f in mecon_iface.fragment(body, serial_no & 0xff):
            send(f)
        t0 = time.time()
        first = None
        parts = {}
        limit = time.time() + 8
        while time.time() < limit:
            f = frame(0.2)
            if f is None or f[0] != mecon_iface.FRAME_CODE:
                continue
            if first is None:
                first = time.time() - t0
            p = parts.setdefault(f[1], {"count": f[3], "have": {}})
            p["have"][f[2]] = f[4:]
            if len(p["have"]) == p["count"]:
                if rid.encode() in b"".join(p["have"][i] for i in range(p["count"])):
                    break
        else:
            first = None
        if first is None:
            print("FAIL %s: no whole answer within 8 s" % op, flush=True)
            failed = True
        else:
            took.append(first)
    if took:
        median = statistics.median(took)
        over = median > budget
        failed = failed or over
        print("%s %s: first frame after %.0f ms, median of %d; slowest %.0f ms; budget %.0f ms" % (
            "FAIL" if over else "PASS", op, median * 1000, len(took), max(took) * 1000, budget * 1000), flush=True)
s.close()
sys.exit(1 if failed else 0)
