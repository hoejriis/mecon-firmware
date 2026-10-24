#!/usr/bin/env python3
"""Run the published device-interface fixtures against a real board (docs/contract/fixtures/requests.json).

    mecon_iface_hw.py serial /dev/ttyUSB0          the Companion's USB frames ('<' len16 out, '>' len16 in)
    mecon_iface_hw.py ble AA:BB:CC:DD:EE:FF         the Nordic UART service over an existing bond (needs bleak)

Prints one line per case (PASS/FAIL with the answer) and exits non-zero on any failure. Opening the serial port
resets an ESP32 board; the script waits for it to boot.
"""
import json
import os
import queue
import struct
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mecon_iface  # noqa: E402
import mecon_schema  # noqa: E402

FIXTURES = os.path.join(HERE, "..", "docs", "contract", "fixtures")


def serial_transport(port):
    import serial
    s = serial.Serial(port, 115200, timeout=0.05)
    s.dtr = False
    s.rts = False
    buf = bytearray()

    def send(payload):
        s.write(b"<" + struct.pack("<H", len(payload)) + payload)
        s.flush()

    def read(timeout):
        nonlocal buf
        end = time.time() + timeout
        while time.time() < end:
            buf += s.read(4096)
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

    time.sleep(9)   # the open reset the board
    s.read(65536)
    return send, read


def ble_transport(mac):
    import asyncio
    from bleak import BleakClient
    RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
    TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
    inbox, outbox = queue.Queue(), queue.Queue()

    async def run():
        async with BleakClient(mac, timeout=20.0) as c:
            await c.start_notify(TX, lambda _, d: inbox.put(bytes(d)))
            while True:
                p = await asyncio.get_event_loop().run_in_executor(None, outbox.get)
                if p is None:
                    return
                await c.write_gatt_char(RX, p, response=True)

    threading.Thread(target=lambda: asyncio.run(run()), daemon=True).start()
    time.sleep(4)

    def read(timeout):
        try:
            return inbox.get(timeout=timeout)
        except queue.Empty:
            return None
    return outbox.put, read


def expand(req):
    return json.dumps(req, separators=(",", ":")).replace("PAD600", "x" * 600).encode()


def main():
    kind, where = sys.argv[1], sys.argv[2]
    send, read = serial_transport(where) if kind == "serial" else ble_transport(where)
    send(bytes([1, 3]) + bytes(6) + b"hwtest")   # CMD_APP_START, as a client does
    i = mecon_iface.IfaceClient(send, read)
    cases = json.load(open(os.path.join(FIXTURES, "requests.json")))["cases"]
    fails = 0
    for case in cases:
        if "raw" in case:
            i.send_raw(case["raw"].encode())
            ans = i.wait_result(case["expect"].get("id", ""), timeout=10)
        else:
            i.send_raw(expand(case["request"]))
            ans = i.wait_result(case["request"]["id"], timeout=10)
        exp = case["expect"]
        problems = []
        if ans.get("status") != exp["status"]:
            problems.append("status %s" % ans.get("status"))
        problems += mecon_schema.check(ans, mecon_schema.load("result.json"))
        if "id" in exp and ans.get("id") != exp["id"]:
            problems.append("id %r" % ans.get("id"))
        if "error" in exp and ans.get("error") != exp["error"]:
            problems.append("error %r" % ans.get("error"))
        if "schema" in exp and "result" in ans:
            problems += mecon_schema.check(ans["result"], mecon_schema.load(exp["schema"]))
        fails += bool(problems)
        print("%s %s %s" % ("FAIL" if problems else "PASS", case["name"], problems or ""), flush=True)
    print("%d of %d passed" % (len(cases) - fails, len(cases)))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
