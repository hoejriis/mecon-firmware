#!/usr/bin/env python3
"""Run the published device-interface fixtures against a real board (docs/contract/fixtures/requests.json).

    mecon_iface_hw.py serial /dev/ttyUSB0          the Companion's USB frames ('<' len16 out, '>' len16 in)
    mecon_iface_hw.py ble AA:BB:CC:DD:EE:FF         the Nordic UART service over an existing bond (needs bleak)
    mecon_iface_hw.py mqtt <device_id> <host> <port> <cafile> <user> <pwdfile>
                                                    the broker topics, through mosquitto_pub / mosquitto_sub

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

    return send, read, s.close


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

    worker = threading.Thread(target=lambda: asyncio.run(run()), daemon=True)
    worker.start()
    time.sleep(4)

    def read(timeout):
        try:
            return inbox.get(timeout=timeout)
        except queue.Empty:
            return None
    def close():   # end the writer and let bleak disconnect: a link left up stops the board advertising
        outbox.put(None)
        worker.join(10)
    return outbox.put, read, close


def check_health_stream(i):
    """Subscribe to health, take two events, check them, stop. Returns a list of problems."""
    problems = []
    r = i.request("sub.start", {"stream": "health", "period_s": 5, "lifetime_s": 60}, timeout=10)
    if r.get("status") != "ok":
        return ["sub.start %r" % r]
    got, end = [], time.time() + 20
    while len(got) < 2 and time.time() < end:
        m = i.events.pop(0) if i.events else i.read_message(timeout=1.0)
        if m is not None and m.get("ev") == "health":
            got.append(m)
    if len(got) < 2:
        problems.append("%d health events in 20 s" % len(got))
    else:
        for e in got:
            problems += mecon_schema.check(e, mecon_schema.load("event.json"))
            problems += mecon_schema.check(e["data"], mecon_schema.load("event.health.json"))
        if got[1]["seq"] != got[0]["seq"] + 1:
            problems.append("seq %d then %d" % (got[0]["seq"], got[1]["seq"]))
    if i.request("sub.stop", {"stream": "health"}, timeout=10).get("status") != "ok":
        problems.append("sub.stop")
    return problems


class MqttLink:
    """Requests as whole messages on mecon/<id>/req; one mosquitto_sub collects .../resp and .../ev/health."""

    def __init__(self, dev, host, port, cafile, user, pwdfile):
        import subprocess
        self.dev = dev
        pwd = open(pwdfile).read().strip()
        self.args = ["-h", host, "-p", str(port), "--cafile", cafile, "--insecure", "-u", user, "-P", pwd]
        self.sub = subprocess.Popen(["mosquitto_sub"] + self.args + ["-t", "mecon/%s/resp" % dev, "-t",
                                    "mecon/%s/ev/health" % dev], stdout=subprocess.PIPE, text=True)
        self.lines = queue.Queue()
        threading.Thread(target=lambda: [self.lines.put(l) for l in self.sub.stdout], daemon=True).start()
        self.events = []
        time.sleep(1.0)

    def send_raw(self, msg):
        import subprocess
        subprocess.run(["mosquitto_pub"] + self.args + ["-t", "mecon/%s/req" % self.dev, "-m", msg.decode()],
                       check=True, capture_output=True, timeout=15)

    def read_message(self, timeout=5.0):
        try:
            return json.loads(self.lines.get(timeout=timeout))
        except queue.Empty:
            return None

    def wait_result(self, rid, timeout=10.0):
        end = time.time() + timeout
        while time.time() < end:
            m = self.read_message(max(0.05, end - time.time()))
            if m is None:
                continue
            if "ev" in m:
                self.events.append(m)
            elif m.get("id") in (rid, ""):
                return m
        raise TimeoutError("no result for %s" % rid)

    def request(self, op, args=None, timeout=10.0):
        rid = "hw-%d" % int(time.time() * 1000)
        env = {"v": 1, "id": rid, "op": op}
        if args is not None:
            env["args"] = args
        self.send_raw(json.dumps(env).encode())
        return self.wait_result(rid, timeout)

    def close(self):
        self.sub.terminate()


def expand(req):
    return json.dumps(req, separators=(",", ":")).replace("PAD600", "x" * 600).encode()


def main():
    kind, where = sys.argv[1], sys.argv[2]
    if kind == "mqtt":
        link = MqttLink(*sys.argv[2:8])
        return run_cases(link, link.close)
    send, read, close = serial_transport(where) if kind == "serial" else ble_transport(where)
    # Wait until the board answers CMD_APP_START (as a client does): opening the serial port resets an ESP32, and the
    # first boot after an erase formats the file system, so a fixed delay is not enough.
    end = time.time() + 90
    while time.time() < end:
        send(bytes([1, 3]) + bytes(6) + b"hwtest")
        f = read(2.0)
        while f is not None and f[:1] != b"\x05":
            f = read(0.5)
        if f is not None:
            break
    else:
        sys.exit("the board never answered CMD_APP_START")
    return run_cases(mecon_iface.IfaceClient(send, read), close)


def run_cases(i, close):
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
    problems = check_health_stream(i)
    fails += bool(problems)
    print("%s health stream %s" % ("FAIL" if problems else "PASS", problems or ""), flush=True)
    print("%d of %d passed" % (len(cases) + 1 - fails, len(cases) + 1))
    close()
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
