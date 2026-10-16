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
    "room_server": os.path.join(REPO, ".pio/build/sim_room_server/program"),
    "repeater_asan": os.path.join(REPO, ".pio/build/sim_repeater_asan/program"),
    "companion_asan": os.path.join(REPO, ".pio/build/sim_companion_asan/program"),
    "companion_wifi": os.path.join(REPO, ".pio/build/sim_companion_wifi/program"),
    # the standalone Companion: connectivity-mode switching, the real ui-new screens on a recording display
    "companion_standalone": os.path.join(REPO, ".pio/build/sim_companion_standalone/program"),
}


class WifiEnv:
    """The simulated Wi-Fi environment of a node: the access points that exist, with SSID, password (empty = open),
    RSSI and up/down state. The node re-reads the file whenever it changes, so a test can bring a network down or up,
    change its password or its signal while the node runs. Lives next to the node's directory, not in it."""

    def __init__(self, path):
        self.path = str(path)
        self.aps = {}
        self._write()

    def set(self, ssid, password="", rssi=-50, up=True):
        self.aps[ssid] = (password, rssi, up)
        self._write()

    def down(self, ssid):
        pw, rssi, _ = self.aps[ssid]
        self.set(ssid, pw, rssi, False)

    def up(self, ssid):
        pw, rssi, _ = self.aps[ssid]
        self.set(ssid, pw, rssi, True)

    def _write(self):
        tmp = self.path + ".tmp"
        with open(tmp, "w") as f:
            for ssid, (pw, rssi, up) in self.aps.items():
                f.write("%s\t%s\t%d\t%d\n" % (ssid, pw, rssi, 1 if up else 0))
        os.replace(tmp, self.path)


class MqttEnv:
    """The simulated name service of a node: DNS names and the mDNS responders on the network, in a file the node
    re-reads whenever it changes (like WifiEnv). advertise()/withdraw() add and remove a responder for
    _secure-mqtt._tcp (secure=True) or _mqtt._tcp, with TXT site=<site>; host() maps a DNS name to an IPv4 address."""

    def __init__(self, path):
        self.path = str(path)
        self.hosts = {}
        self.responders = []
        self._write()

    def host(self, name, ip):
        self.hosts[name] = ip
        self._write()

    def advertise(self, site, ip, port, secure=True):
        self.responders.append(("secure" if secure else "plain", site, ip, port))
        self._write()

    def withdraw(self, site, ip, port, secure=True):
        self.responders.remove(("secure" if secure else "plain", site, ip, port))
        self._write()

    def _write(self):
        tmp = self.path + ".tmp"
        with open(tmp, "w") as f:
            for name, ip in self.hosts.items():
                f.write("host\t%s\t%s\n" % (name, ip))
            for kind, site, ip, port in self.responders:
                f.write("mdns\t%s\t%s\t%s\t%d\n" % (kind, site, ip, port))
        os.replace(tmp, self.path)


class UiProbe:
    """The standalone Companion's screen and button. The node's recording display appends every distinct frame to a
    file; the button replays the gestures written to another. A frame is the list of (x, y, size, text) it drew."""

    def __init__(self, workdir, name):
        self.frames_path = os.path.join(str(workdir), name + ".frames")
        self.input_path = os.path.join(str(workdir), name + ".input")
        self.ble_path = os.path.join(str(workdir), name + ".ble")
        open(self.input_path, "w").close()

    def press(self, kind):
        """One gesture: click | long | double | triple."""
        assert kind in ("click", "long", "double", "triple")
        with open(self.input_path, "a") as f:
            f.write(kind + "\n")

    def phone(self, connected):
        """Tell the stubbed BLE interface a phone is (not) connected."""
        with open(self.ble_path, "w") as f:
            f.write("connected" if connected else "none")

    def frames(self, since_boot=True):
        """All frames written so far (of the latest boot by default), oldest first: [[(x, y, size, text)]]."""
        out, cur, boots = [], None, 0
        if not os.path.exists(self.frames_path):
            return out
        for ln in open(self.frames_path, errors="replace"):
            ln = ln.rstrip("\n")
            if ln.startswith("BOOT "):
                if since_boot:
                    out = []
                boots += 1
            elif ln.startswith("FRAME "):
                cur = []
            elif ln == "END":
                if cur is not None:
                    out.append(cur)
                cur = None
            elif ln.startswith("T ") and cur is not None:
                parts = ln.split(" ", 4)
                cur.append((int(parts[1]), int(parts[2]), int(parts[3]), parts[4] if len(parts) > 4 else ""))
        return out

    @staticmethod
    def text(frame):
        return "\n".join(t[3] for t in frame)

    def last_text(self):
        fr = self.frames()
        return self.text(fr[-1]) if fr else ""

    def all_text(self):
        """Every line drawn in any frame of every boot, as one string (for the "never a secret" checks)."""
        return "\n".join(self.text(f) for f in self.frames(since_boot=False))

    def wait_text(self, needle, timeout=15.0):
        """Wait until the newest frame shows ``needle``; returns that frame's text (or None on timeout)."""
        end = time.time() + timeout
        while time.time() < end:
            t = self.last_text()
            if needle in t:
                return t
            time.sleep(0.05)
        return None

    def wait_gone(self, needle, timeout=15.0):
        end = time.time() + timeout
        while time.time() < end:
            t = self.last_text()
            if t and needle not in t:
                return t
            time.sleep(0.05)
        return None


def node_log(node):
    node.log.flush()
    return open(node.log_path, errors="replace").read()


def mqtt_events(node):
    """The simulated network's lines in the node's log, oldest first, as the text after "SIMMQTT "."""
    out = []
    if os.path.exists(node.log_path):
        for ln in open(node.log_path, errors="replace"):
            if ln.startswith("SIMMQTT "):
                out.append(ln[8:].strip())
    return out


def wifi_events(node):
    """The simulated radio's transitions in the node's log, oldest first: [("join"|"up"|"fail"|"lost"|"leave", ssid)]."""
    out = []
    if os.path.exists(node.log_path):
        for ln in open(node.log_path, errors="replace"):
            if ln.startswith("SIMWIFI "):
                parts = ln.split()
                out.append((parts[1], parts[2].split("=", 1)[1]))
    return out


class Node:
    def __init__(self, kind, name, workdir, ether_path=None, seed=1, time_scale=1.0, fault=None, rtc=None, fault_before=None,
                 binary=None, wifi=None, mqtt=None):
        self.kind, self.name = kind, name
        self.dir = os.path.join(str(workdir), name)
        os.makedirs(self.dir, exist_ok=True)
        self.port_file = os.path.join(str(workdir), name + ".port")
        self.log_path = os.path.join(str(workdir), name + ".log")
        self.ether_path, self.seed, self.time_scale = ether_path, seed, time_scale
        self.fault, self.rtc, self.fault_before = fault, rtc, fault_before
        self.binary = binary or BIN[kind]
        if wifi is None and kind == "companion_wifi":
            # the build's compile-time default network, "sim" (open), exists unless the test defines its own environment
            wifi = WifiEnv(os.path.join(str(workdir), name + ".wifi"))
            wifi.set("sim")
        self.wifi = wifi
        self.mqtt = mqtt
        self.ui = UiProbe(workdir, name) if kind == "companion_standalone" else None
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
        if self.fault_before:
            cmd += ["--fault-before", self.fault_before]
        if self.rtc:
            cmd += ["--rtc", str(self.rtc)]
        if self.wifi:
            cmd += ["--wifi-env", self.wifi.path]
        if self.mqtt:
            cmd += ["--mqtt-env", self.mqtt.path]
        if self.ui:
            cmd += ["--ui-frames", self.ui.frames_path, "--ui-input", self.ui.input_path, "--ble-env", self.ui.ble_path]
        self.log = open(self.log_path, "ab")
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="print_stacktrace=1")
        self.proc = subprocess.Popen(cmd, stdout=self.log, stderr=self.log, env=env)
        end = time.time() + 10
        while time.time() < end:
            if os.path.exists(self.port_file):
                self.port = int(open(self.port_file).read())
                return self
            if self.proc.poll() is not None:
                raise RuntimeError("%s exited early (%s); see %s" % (self.name, self.proc.returncode, self.log_path))
            time.sleep(0.02)
        raise RuntimeError("%s did not publish a serial port" % self.name)

    def sanitizer_report(self):
        """Text of any AddressSanitizer/UBSan report this node printed (empty when clean)."""
        self.log.flush()
        text = open(self.log_path, errors="replace").read()
        return text if ("AddressSanitizer" in text or "runtime error" in text) else ""

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

    def command_all(self, line, quiet=0.6, timeout=8.0):
        """Like command(), but returns a multi-line reply whole: reads until the node has been quiet for ``quiet`` s."""
        self.sock.sendall(line.encode() + b"\r")
        buf = b""
        end = time.time() + timeout
        last = None
        while time.time() < end:
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                chunk = b""
            if chunk:
                buf += chunk
                last = time.time()
            elif last is not None and b"  -> " in buf and time.time() - last > quiet:
                break
        text = buf.decode(errors="replace")
        return text.split("  -> ", 1)[1].strip("\r\n ") if "  -> " in text else ""

    def close(self):
        self.sock.close()


def audit_entries(read):
    """All audit lines from ``get audit`` (newest first), paging through "... next:N". ``read`` runs one CLI command
    and returns the reply text. Each entry is returned as the text "<ts> <event> <caller> <what> x<count>"."""
    out, cmd = [], "get audit"
    for _ in range(40):
        text = read(cmd)
        nxt = None
        for ln in text.splitlines():
            ln = ln.strip()
            if ln.startswith("... next:"):
                nxt = int(ln.split(":")[1])
            elif ln and not ln.startswith(">"):
                out.append(ln)
        if nxt is None:
            break
        cmd = "get audit %d" % nxt
    return out


def audit_file(node):
    """Parse the node's persisted /audit.bin: None when absent or invalid, else a list of
    (ts, event, caller, what, count) oldest first. Checks magic, size and CRC32."""
    import zlib
    path = os.path.join(node.dir, "audit.bin")
    if not os.path.exists(path):
        return None
    b = open(path, "rb").read()
    if len(b) != 268 or b[:4] != b"AUD1" or zlib.crc32(b[:-4]) != struct.unpack("<I", b[-4:])[0]:
        return None
    return [struct.unpack_from("<IBBBB", b, 8 + i * 8) for i in range(b[4])]


def files_containing(node, text):
    """Every file in the node's data directory, and its stdout/stderr log, that contains ``text``."""
    needle = text.encode()
    hits = []
    paths = [node.log_path]
    for root, _, names in os.walk(node.dir):
        paths += [os.path.join(root, n) for n in names]
    for p in paths:
        if os.path.exists(p) and needle in open(p, "rb").read():
            hits.append(p)
    return hits


# ---- Companion protocol constants (examples/companion_radio/MyMesh.cpp) ----
CMD_APP_START, CMD_SEND_TXT_MSG, CMD_SEND_CHANNEL_TXT_MSG = 1, 2, 3
CMD_SET_DEVICE_TIME, CMD_ADD_UPDATE_CONTACT, CMD_SYNC_NEXT_MESSAGE = 6, 9, 10
CMD_DEVICE_QUERY, CMD_SET_CHANNEL = 22, 32
CMD_SET_RADIO_PARAMS = 11
RESP_OK, RESP_ERR, RESP_SELF_INFO, RESP_SENT = 0, 1, 5, 6
CMD_EXPORT_PRIVATE_KEY, CMD_IMPORT_PRIVATE_KEY, CMD_SET_DEVICE_PIN = 23, 24, 37
CMD_FACTORY_RESET, CMD_RUN_CLI_COMMAND = 51, 66
RESP_PRIVATE_KEY, RESP_CLI_REPLY = 14, 29
ERR_NOT_PERMITTED = 7
RESP_NO_MORE_MESSAGES = 10
CMD_GET_CHANNEL, RESP_CHANNEL_INFO = 31, 18
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

    def set_radio(self, freq_mhz, bw_khz, sf, cr=5):
        """CMD_SET_RADIO_PARAMS: applied to the radio at once, no reboot."""
        f = self.request(bytes([CMD_SET_RADIO_PARAMS]) + struct.pack("<II", round(freq_mhz * 1000), round(bw_khz * 1000)) +
                         bytes([sf, cr]))
        assert f[0] == RESP_OK, f
        return f

    def cli(self, text):
        """CMD_RUN_CLI_COMMAND; returns the reply text (USB/BLE class transports only)."""
        f = self.request(bytes([CMD_RUN_CLI_COMMAND]) + text.encode())
        assert f[0] == RESP_CLI_REPLY, f
        return f[1:].decode(errors="replace")

    def send_cli_command(self, to_pub, text):
        """Remote CLI: a TXT_TYPE_CLI_COMMAND (3) text to a contact; the node stamps it with its own clock."""
        return self.request(bytes([CMD_SEND_TXT_MSG, 3, 0]) + struct.pack("<I", 0) + to_pub[:6] + text.encode())

    def get_channel(self, idx):
        """Returns (name, secret16) or None when the slot is empty/invalid."""
        f = self.request(bytes([CMD_GET_CHANNEL, idx]))
        if f[0] != RESP_CHANNEL_INFO:
            return None
        name = f[2:34].split(b"\0")[0].decode(errors="replace")
        return (name, f[34:50]) if name else None

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
