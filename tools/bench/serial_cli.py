#!/usr/bin/env python3
"""Run CLI commands on a mecon-firmware board over USB serial (or a simulated board over TCP).

Two modes:
  frames  a Companion build: each command goes as CMD_RUN_CLI_COMMAND (66) in '<'+len16 framing, and the reply is
          RESP_CODE_CLI_REPLY (29). Non-frame bytes on the line (boot text) are skipped.
  text    a Repeater or Room-server console: the line is sent with '\\r', and the reply is whatever arrives until the
          line has been idle for --idle seconds.

Examples:
  serial_cli.py --port /dev/ttyUSB0 frames "get mecon.mem" "get posture"
  serial_cli.py --port /dev/ttyUSB0 text "ver" "get dutycycle"
  serial_cli.py --tcp 127.0.0.1:5000 frames "ver"          # a simulated board
  serial_cli.py --port /dev/ttyUSB0 reset                    # pulse EN through RTS (ESP32 auto-reset wiring)

Exit status: 0 if every command got a reply, 1 otherwise. Nothing is written except the commands given.
"""
import argparse
import socket
import struct
import sys
import time

CMD_RUN_CLI_COMMAND = 66
RESP_CODE_CLI_REPLY = 29
RESP_CODE_ERR = 1


class Link:
    def __init__(self, port=None, baud=115200, tcp=None):
        self.ser = self.sock = None
        if tcp:
            host, p = tcp.rsplit(":", 1)
            self.sock = socket.create_connection((host, int(p)), timeout=5)
            self.sock.settimeout(0.1)
        else:
            import serial  # pyserial
            self.ser = serial.Serial(port, baud, timeout=0.1)
            self.ser.dtr = False   # do not hold the board in reset or bootloader
            self.ser.rts = False

    def write(self, b):
        (self.sock.sendall if self.sock else self.ser.write)(b)

    def read(self):
        if self.sock:
            try:
                return self.sock.recv(4096)
            except socket.timeout:
                return b""
        return self.ser.read(4096)

    def reset(self):
        if not self.ser:
            raise SystemExit("reset needs a serial port")
        self.ser.rts = True    # EN low
        time.sleep(0.12)
        self.ser.rts = False


def frames_cmd(link, line, timeout):
    link.write(b"<" + struct.pack("<H", 1 + len(line)) + bytes([CMD_RUN_CLI_COMMAND]) + line.encode())
    buf, end = b"", time.time() + timeout
    while time.time() < end:
        buf += link.read()
        while True:
            i = buf.find(b">")
            if i < 0 or len(buf) < i + 3:
                break
            (n,) = struct.unpack_from("<H", buf, i + 1)
            if n == 0 or n > 512:          # not a frame header: skip this byte and resync
                buf = buf[i + 1:]
                continue
            if len(buf) < i + 3 + n:
                break
            f, buf = buf[i + 3:i + 3 + n], buf[i + 3 + n:]
            if f[0] == RESP_CODE_CLI_REPLY:
                return f[1:].decode(errors="replace")
            if f[0] == RESP_CODE_ERR:
                return "ERR frame code %d" % (f[1] if len(f) > 1 else -1)
            # anything else (pushes, unrelated replies) is ignored
    return None


def text_cmd(link, line, timeout, idle):
    link.read()                                   # drop anything pending
    link.write(line.encode() + b"\r")
    out, end, last = b"", time.time() + timeout, time.time()
    while time.time() < end:
        b = link.read()
        if b:
            out += b
            last = time.time()
        elif out and time.time() - last >= idle:
            break
    if not out:
        return None
    text = out.decode(errors="replace").replace("\r", "")
    lines = [l for l in text.split("\n") if l.strip() and l.strip() != line]   # drop the echo
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--tcp", help="host:port of a simulated board")
    ap.add_argument("--timeout", type=float, default=5.0)
    ap.add_argument("--idle", type=float, default=0.6, help="text mode: reply ends after this many idle seconds")
    ap.add_argument("mode", choices=["frames", "text", "reset"])
    ap.add_argument("commands", nargs="*")
    a = ap.parse_args()
    if not a.port and not a.tcp:
        ap.error("--port or --tcp is required")
    link = Link(a.port, a.baud, a.tcp)
    if a.mode == "reset":
        link.reset()
        print("reset pulsed")
        return 0
    ok = True
    for c in a.commands:
        r = frames_cmd(link, c, a.timeout) if a.mode == "frames" else text_cmd(link, c, a.timeout, a.idle)
        print("%s => %s" % (c, r if r is not None else "(no reply)"))
        ok = ok and r is not None
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
