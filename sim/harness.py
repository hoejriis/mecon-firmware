"""Test harness: start simulated nodes and talk to them like a client would.

* ``Node``            one simulated node process with its own data directory
* ``CompanionClient`` Companion frame protocol over the node's serial TCP port
* ``CliClient``       the repeater's text CLI over the node's serial TCP port
"""
import os
import signal
import socket
import struct
import subprocess
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = {
    "companion": os.path.join(REPO, ".pio/build/sim_companion/program"),
    "repeater": os.path.join(REPO, ".pio/build/sim_repeater/program"),
    # same Companion code, but its app transport is classed as unauthenticated TCP (a _wifi build)
    "companion_wifi_usb": os.path.join(REPO, ".pio/build/sim_companion_wifi_usb/program"),
    "companion_wifi": os.path.join(REPO, ".pio/build/sim_companion_wifi/program"),
}


class Node:
    def __init__(self, kind, name, workdir, ether_path=None, seed=1, time_scale=1.0, fault=None, rtc=None,
                 binary=None):
        self.kind, self.name = kind, name
        self.dir = os.path.join(str(workdir), name)
        os.makedirs(self.dir, exist_ok=True)
        self.port_file = os.path.join(str(workdir), name + ".port")
        self.log_path = os.path.join(str(workdir), name + ".log")
        self.ether_path, self.seed, self.time_scale = ether_path, seed, time_scale
        self.fault, self.rtc = fault, rtc
        self.binary = binary or BIN[kind]
        self.proc = None
        self.port = None

    def start(self):
        if os.path.exists(self.port_file):
            os.unlink(self.port_file)
        cmd = [self.binary, "--dir", self.dir, "--name", self.name, "--port-file", self.port_file,
               "--seed", str(self.seed), "--time-scale", str(self.time_scale)]
        if self.ether_path:
            cmd += ["--ether", self.ether_path]
        if self.fault:
            cmd += ["--fault", self.fault]
        if self.rtc:
            cmd += ["--rtc", str(self.rtc)]
        self.log = open(self.log_path, "ab")
        self.proc = subprocess.Popen(cmd, stdout=self.log, stderr=self.log)
        end = time.time() + 10
        while time.time() < end:
            if os.path.exists(self.port_file):
                self.port = int(open(self.port_file).read())
                return self
            if self.proc.poll() is not None:
                raise RuntimeError("%s exited early (%s); see %s" % (self.name, self.proc.returncode, self.log_path))
            time.sleep(0.02)
        raise RuntimeError("%s did not publish a serial port" % self.name)

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.send_signal(signal.SIGKILL)
                self.proc.wait()
        return self.proc.returncode if self.proc else None

    def wait_exit(self, timeout=10):
        return self.proc.wait(timeout)

    def restart(self):
        """Power-cycle: same directory, new process."""
        self.stop()
        return self.start()

    def connect(self):
        s = socket.create_connection(("127.0.0.1", self.port), timeout=5)
        return s


class CliClient:
    """Repeater text CLI. The node echoes typed characters, then prints '  -> reply'."""

    def __init__(self, node):
        self.sock = node.connect()
        self.sock.settimeout(0.2)

    def command(self, line, timeout=5.0):
        self.sock.sendall(line.encode() + b"\r")
        buf = b""
        end = time.time() + timeout
        while time.time() < end:
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                chunk = b""
            if chunk == b"" and not buf:
                continue
            buf += chunk
            if b"\n" in buf.split(b"  -> ", 1)[-1] and b"  -> " in buf:
                break
        text = buf.decode(errors="replace")
        if "  -> " in text:
            return text.split("  -> ", 1)[1].strip("\r\n ")
        return ""

    def close(self):
        self.sock.close()


# ---- Companion protocol constants (examples/companion_radio/MyMesh.cpp) ----
CMD_APP_START, CMD_SEND_TXT_MSG, CMD_SEND_CHANNEL_TXT_MSG = 1, 2, 3
CMD_SET_DEVICE_TIME, CMD_ADD_UPDATE_CONTACT, CMD_SYNC_NEXT_MESSAGE = 6, 9, 10
CMD_DEVICE_QUERY, CMD_SET_CHANNEL = 22, 32
RESP_OK, RESP_ERR, RESP_SELF_INFO, RESP_SENT = 0, 1, 5, 6
CMD_EXPORT_PRIVATE_KEY, CMD_IMPORT_PRIVATE_KEY, CMD_SET_DEVICE_PIN = 23, 24, 37
CMD_FACTORY_RESET, CMD_RUN_CLI_COMMAND = 51, 66
RESP_PRIVATE_KEY, RESP_CLI_REPLY = 14, 29
ERR_NOT_PERMITTED = 7
RESP_NO_MORE_MESSAGES = 10
CMD_SEND_LOGIN, PUSH_LOGIN_SUCCESS, PUSH_LOGIN_FAIL = 26, 0x85, 0x86
RESP_CONTACT_MSG_RECV_V3, RESP_CHANNEL_MSG_RECV_V3 = 16, 17
PUSH_MSG_WAITING = 0x83


class CompanionClient:
    """Minimal Companion client: '<' + len16 framing out, '>' + len16 framing in."""

    def __init__(self, node):
        self.sock = node.connect()
        self.sock.settimeout(0.2)
        self.buf = b""
        self.pushes = []

    def _send(self, payload):
        self.sock.sendall(b"<" + struct.pack("<H", len(payload)) + payload)

    def _read_frame(self, timeout):
        end = time.time() + timeout
        while time.time() < end:
            if len(self.buf) >= 3:
                assert self.buf[0:1] == b">", "bad frame header %r" % self.buf[:8]
                (n,) = struct.unpack_from("<H", self.buf, 1)
                if len(self.buf) >= 3 + n:
                    f = self.buf[3:3 + n]
                    self.buf = self.buf[3 + n:]
                    return f
            try:
                chunk = self.sock.recv(4096)
                if not chunk:
                    return None
                self.buf += chunk
            except socket.timeout:
                pass
        return None

    def request(self, payload, timeout=5.0):
        """Send a command; return the first non-push frame. Pushes (>=0x80) are queued."""
        self._send(payload)
        end = time.time() + timeout
        while time.time() < end:
            f = self._read_frame(0.5)
            if f is None:
                continue
            if f[0] >= 0x80:
                self.pushes.append(f)
                continue
            return f
        raise TimeoutError("no response to command %d" % payload[0])

    def wait_push(self, code, timeout=30.0):
        end = time.time() + timeout
        while time.time() < end:
            for i, f in enumerate(self.pushes):
                if f[0] == code:
                    return self.pushes.pop(i)
            f = self._read_frame(0.5)
            if f is not None:
                (self.pushes if f[0] >= 0x80 else []).append(f)
        return None

    # --- commands
    def device_query(self):
        return self.request(bytes([CMD_DEVICE_QUERY, 3]))

    def app_start(self, app=b"simtest"):
        f = self.request(bytes([CMD_APP_START]) + b"\0" * 7 + app)
        assert f[0] == RESP_SELF_INFO, f
        self.pub_key = f[4:36]
        return f

    def add_contact(self, pub_key, name, out_path_len=0xFF, flags=0, ctype=1):
        f = (bytes([CMD_ADD_UPDATE_CONTACT]) + pub_key + bytes([ctype, flags, out_path_len]) + b"\0" * 64 +
             name.encode().ljust(32, b"\0") + struct.pack("<I", 0))
        return self.request(f)

    def set_channel(self, idx, name, secret):
        assert len(secret) == 16
        return self.request(bytes([CMD_SET_CHANNEL, idx]) + name.encode().ljust(32, b"\0") + secret)

    def cli(self, text):
        """CMD_RUN_CLI_COMMAND; returns the reply text (USB/BLE class transports only)."""
        f = self.request(bytes([CMD_RUN_CLI_COMMAND]) + text.encode())
        assert f[0] == RESP_CLI_REPLY, f
        return f[1:].decode(errors="replace")

    def send_cli_command(self, to_pub, text):
        """Remote CLI: a TXT_TYPE_CLI_COMMAND (3) text to a contact; the node stamps it with its own clock."""
        return self.request(bytes([CMD_SEND_TXT_MSG, 3, 0]) + struct.pack("<I", 0) + to_pub[:6] + text.encode())

    def login(self, to_pub, password, timeout=30.0):
        """Log in to a repeater/room as an admin (or guest); True on PUSH_CODE_LOGIN_SUCCESS."""
        f = self.request(bytes([CMD_SEND_LOGIN]) + to_pub + password.encode())
        assert f[0] == RESP_SENT, f
        return self.wait_push(PUSH_LOGIN_SUCCESS, timeout=timeout) is not None

    def remote_admin(self, to_pub, text):
        """Send a CLI line (TXT_TYPE_CLI_DATA) to a logged-in repeater and return its reply text."""
        f = self.request(bytes([CMD_SEND_TXT_MSG, 1, 0]) + struct.pack("<I", 0) + to_pub[:6] + text.encode())
        assert f[0] == RESP_SENT, f
        msgs = self.sync_messages(timeout=60, want=1)
        assert msgs, "no reply to %r" % text
        return msgs[0]["text"]

    def export_private_key(self):
        f = self.request(bytes([CMD_EXPORT_PRIVATE_KEY]))
        assert f[0] == RESP_PRIVATE_KEY, f
        return f[1:65]

    def send_direct(self, to_pub, text, ts):
        return self.request(bytes([CMD_SEND_TXT_MSG, 0, 0]) + struct.pack("<I", ts) + to_pub[:6] + text.encode())

    def send_channel(self, idx, text, ts):
        return self.request(bytes([CMD_SEND_CHANNEL_TXT_MSG, 0, idx]) + struct.pack("<I", ts) + text.encode())

    def sync_messages(self, timeout=30.0, want=1):
        """Wait for MSG_WAITING pushes and drain queued messages. Returns parsed dicts."""
        out = []
        end = time.time() + timeout
        while len(out) < want and time.time() < end:
            if self.wait_push(PUSH_MSG_WAITING, timeout=max(0.5, min(5.0, end - time.time()))) is None:
                pass
            while True:
                f = self.request(bytes([CMD_SYNC_NEXT_MESSAGE]))
                if f[0] == RESP_NO_MORE_MESSAGES:
                    break
                out.append(self.parse_message(f))
        return out

    @staticmethod
    def parse_message(f):
        if f[0] == RESP_CONTACT_MSG_RECV_V3:    # snr, 2 reserved, pubkey6, path_len, txt_type, ts, text
            return {"kind": "direct", "snr": struct.unpack_from("b", f, 1)[0] / 4.0, "from": f[4:10],
                    "path_len": f[10], "txt_type": f[11], "ts": struct.unpack_from("<I", f, 12)[0],
                    "text": f[16:].decode(errors="replace")}
        if f[0] == RESP_CHANNEL_MSG_RECV_V3:    # snr, 2 reserved, chan_idx, path_len, txt_type, ts, text
            return {"kind": "channel", "snr": struct.unpack_from("b", f, 1)[0] / 4.0, "channel": f[4],
                    "path_len": f[5], "txt_type": f[6], "ts": struct.unpack_from("<I", f, 7)[0],
                    "text": f[11:].decode(errors="replace")}
        raise AssertionError("unexpected message frame %r" % f[:20])

    def close(self):
        self.sock.close()
