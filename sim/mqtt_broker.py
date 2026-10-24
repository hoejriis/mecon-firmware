"""A real Mosquitto broker for the MQTT tests (docs/MQTT.md): plain or TLS, anonymous or with a password file, bound
to its own loopback address (127.0.0.N) so several brokers can share a port. Certificates are generated at test time
with the openssl command line and are throw-away test material."""
import json
import os
import shutil
import socket
import subprocess
import sys
import time


def require_tools():
    for tool in ("mosquitto", "mosquitto_passwd", "mosquitto_sub", "mosquitto_pub", "openssl"):
        if not shutil.which(tool):
            raise RuntimeError("%s is not installed (apt install mosquitto mosquitto-clients openssl)" % tool)


def make_cert(directory, name, ip):
    """A self-signed EC certificate for `ip`. Returns (cert_path, key_path, sha256 fingerprint as 64 lower-case hex)."""
    cert, key = os.path.join(directory, name + ".crt"), os.path.join(directory, name + ".key")
    subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1", "-nodes",
                    "-keyout", key, "-out", cert, "-days", "2", "-subj", "/CN=test-broker-" + name,
                    "-addext", "subjectAltName=IP:" + ip], check=True, capture_output=True)
    out = subprocess.run(["openssl", "x509", "-in", cert, "-noout", "-fingerprint", "-sha256"], check=True,
                         capture_output=True, text=True).stdout
    fp = out.strip().split("=", 1)[1].replace(":", "").lower()
    assert len(fp) == 64
    return cert, key, fp


def make_ca(directory, name, bits=None):
    """A throw-away certificate authority generated at test time. Returns (cert_path, key_path). `bits` makes it an RSA
    CA of that size (the largest practical CA certificates are RSA-4096 roots); the default is EC P-256."""
    cert, key = os.path.join(directory, name + ".crt"), os.path.join(directory, name + ".key")
    keyargs = ["-newkey", "rsa:%d" % bits] if bits else ["-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1"]
    subprocess.run(["openssl", "req", "-x509"] + keyargs + ["-nodes", "-keyout", key, "-out", cert, "-days", "2",
                    "-subj", "/CN=test-ca-" + name, "-addext", "basicConstraints=critical,CA:TRUE",
                    "-addext", "keyUsage=critical,keyCertSign,cRLSign"], check=True, capture_output=True)
    return cert, key


def make_leaf(directory, name, ca, dns=None, ip=None):
    """A broker certificate signed by `ca` = (cert_path, key_path), with a DNS name and/or an IP address as subject
    alternative names. A new key every call, so a "renewed" certificate under the same CA has a different fingerprint.
    Returns (cert_path, key_path, sha256 fingerprint as 64 lower-case hex)."""
    cert, key = os.path.join(directory, name + ".crt"), os.path.join(directory, name + ".key")
    csr, ext = os.path.join(directory, name + ".csr"), os.path.join(directory, name + ".ext")
    sans = (["DNS:" + dns] if dns else []) + (["IP:" + ip] if ip else [])
    open(ext, "w").write("basicConstraints=CA:FALSE\n" + ("subjectAltName=" + ",".join(sans) + "\n" if sans else ""))
    subprocess.run(["openssl", "req", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1", "-nodes",
                    "-keyout", key, "-out", csr, "-subj", "/CN=test-broker-" + name], check=True, capture_output=True)
    subprocess.run(["openssl", "x509", "-req", "-in", csr, "-CA", ca[0], "-CAkey", ca[1], "-set_serial",
                    str(int.from_bytes(os.urandom(8), "big") >> 1), "-out", cert, "-days", "2", "-extfile", ext],
                   check=True, capture_output=True)
    out = subprocess.run(["openssl", "x509", "-in", cert, "-noout", "-fingerprint", "-sha256"], check=True,
                         capture_output=True, text=True).stdout
    return cert, key, out.strip().split("=", 1)[1].replace(":", "").lower()


def ca_b64(cert_path):
    """The certificate as base64 of its DER, the form a profile stores."""
    der = subprocess.run(["openssl", "x509", "-in", cert_path, "-outform", "DER"], check=True, capture_output=True).stdout
    import base64
    return base64.b64encode(der).decode()


def ca_commands(i, cert_path, piece=48):
    """The console commands that set a CA anchor on profile i, from tools/mqtt_ca_commands.py (so the tool is under
    test too); each is short enough for the device's serial line (79 characters)."""
    tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "mqtt_ca_commands.py")
    pem = subprocess.run(["openssl", "x509", "-in", cert_path], check=True, capture_output=True, text=True).stdout
    pem_path = cert_path + ".pem"
    open(pem_path, "w").write(pem)
    out = subprocess.run([sys.executable, tool, str(i), pem_path, "--piece", str(piece)], check=True,
                         capture_output=True, text=True).stdout
    cmds = out.splitlines()
    assert cmds[0].endswith("ca begin") and cmds[-1].endswith("ca end")
    return cmds


class SniProbe:
    """A TLS server that records the server name (SNI) each client sends in its ClientHello, completes the handshake
    with a throw-away certificate and closes. The independent signal for "the device sends SNI": mosquitto does not log
    it. names holds one entry per connection: the name, or None when the client sent none."""

    def __init__(self, directory, ip, port):
        import ssl
        self.ip, self.port, self.names, self.connections = ip, port, [], 0
        cert, key, self.fingerprint = make_cert(str(directory), "sni-" + ip, ip)
        self.ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.ctx.load_cert_chain(cert, key)
        self.ctx.sni_callback = lambda sock, name, ctx: self.names.append(name)
        self._stop = False
        self._sock = None

    def start(self):
        import threading
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self.ip, self.port))
        self._sock.listen(8)
        self._sock.settimeout(0.2)

        def run():
            import ssl
            while not self._stop:
                try:
                    c, _ = self._sock.accept()
                except OSError:
                    continue
                self.connections += 1
                try:
                    c.settimeout(3)
                    t = self.ctx.wrap_socket(c, server_side=True)
                    t.close()
                except (OSError, ssl.SSLError):
                    c.close()
                # a client that sent no server_name never reaches the callback
        threading.Thread(target=run, daemon=True).start()
        return self

    def stop(self):
        self._stop = True
        if self._sock:
            self._sock.close()


class Broker:
    def __init__(self, directory, name, ip, port, cert=None, key=None, users=None, cafile=None):
        self.dir, self.name, self.ip, self.port, self.cert = str(directory), name, ip, port, cert
        self.cafile = cafile or cert   # what a test client trusts: the CA when the certificate is CA-signed
        self.log_path = os.path.join(self.dir, name + ".log")
        self.conf = os.path.join(self.dir, name + ".conf")
        lines = ["listener %d %s" % (port, ip), "persistence false", "log_dest file " + self.log_path,
                 "log_type error", "log_type warning", "log_type notice", "log_type information"]
        if cert:
            lines += ["certfile " + cert, "keyfile " + key]
        if users:
            pw = os.path.join(self.dir, name + ".passwd")
            first = True
            for user, password in users.items():
                subprocess.run(["mosquitto_passwd", "-b"] + (["-c"] if first else []) + [pw, user, password],
                               check=True, capture_output=True)
                first = False
            lines += ["allow_anonymous false", "password_file " + pw]
        else:
            lines += ["allow_anonymous true"]
        open(self.conf, "w").write("\n".join(lines) + "\n")
        self.proc = None
        self._probe_connections = self._probe_clients = 0

    def start(self):
        self.proc = subprocess.Popen(["mosquitto", "-c", self.conf], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        end = time.time() + 10
        while time.time() < end:
            if self.proc.poll() is not None:
                raise RuntimeError("mosquitto %s exited: see %s" % (self.name, self.log_path))
            try:
                socket.create_connection((self.ip, self.port), timeout=0.2).close()
            except OSError:
                time.sleep(0.05)
                continue
            time.sleep(0.3)   # let the broker log the readiness probe above, so it is not counted as a device connection
            self._probe_connections = self._raw_connections()
            self._probe_clients = self._raw_clients()
            return self
        raise RuntimeError("mosquitto %s did not listen" % self.name)

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()

    def log(self):
        return open(self.log_path, errors="replace").read() if os.path.exists(self.log_path) else ""

    def _raw_clients(self):
        return sum(1 for ln in self.log().splitlines() if "New client connected" in ln)

    def _raw_connections(self):
        return sum(1 for ln in self.log().splitlines() if "New connection from" in ln)

    def client_connects(self):
        """MQTT sessions the broker accepted ("New client connected"): the broker's own count, independent of the node."""
        return self._raw_clients() - self._probe_clients

    def connections(self):
        """TCP connections the broker accepted at all, handshake or not, apart from the readiness probe of start()."""
        return self._raw_connections() - self._probe_connections

    def retained_status(self, timeout=10):
        """The retained payload on mecon/+/status, or None."""
        cmd = ["mosquitto_sub", "-h", self.ip, "-p", str(self.port), "-t", "mecon/+/status", "-C", "1", "-W", str(timeout)]
        if self.cert:
            cmd += ["--cafile", self.cafile, "--insecure"]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout + 5)
        return r.stdout.strip() or None

    def _client_args(self):
        return ["-h", self.ip, "-p", str(self.port)] + (["--cafile", self.cafile, "--insecure"] if self.cert else [])

    def publish(self, topic, payload, retain=False):
        """Publish one message as a client of this broker."""
        cmd = ["mosquitto_pub"] + self._client_args() + ["-t", topic, "-m", payload] + (["-r"] if retain else [])
        subprocess.run(cmd, check=True, capture_output=True, timeout=15)

    def cli_request(self, node_id, command=None, req_id="t1", timeout=8, raw=None):
        """The command line over MQTT (docs/CLI.md): send one request to the node's request topic and return the
        parsed response from its response topic, or None when none arrives within `timeout` seconds. `raw` sends that
        payload as it is (a malformed request); otherwise the request is {"id": req_id, "cmd": command}."""
        resp = "mecon/%s/cli/resp" % node_id
        sub = subprocess.Popen(["mosquitto_sub"] + self._client_args() + ["-t", resp, "-C", "1", "-W", str(timeout)],
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        time.sleep(0.6)   # the subscription must be in place before the node answers
        payload = raw if raw is not None else json.dumps({"id": req_id, "cmd": command})
        self.publish("mecon/%s/cli/req" % node_id, payload)
        out, _ = sub.communicate(timeout=timeout + 5)
        return json.loads(out) if out.strip() else None


    def iface_request(self, device_id, envelope, timeout=8, raw=None):
        """The device interface over MQTT (docs/contract/DEVICE_INTERFACE.md): publish one request envelope (a dict, or
        `raw` text) to mecon/<device_id>/req and return the parsed envelope from mecon/<device_id>/resp, or None."""
        resp = "mecon/%s/resp" % device_id
        sub = subprocess.Popen(["mosquitto_sub"] + self._client_args() + ["-t", resp, "-C", "1", "-W", str(timeout)],
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        time.sleep(0.6)   # the subscription must be in place before the node answers
        self.publish("mecon/%s/req" % device_id, raw if raw is not None else json.dumps(envelope))
        out, _ = sub.communicate(timeout=timeout + 5)
        return json.loads(out) if out.strip() else None

    def retained_on(self, topic, timeout=5):
        """The retained payload on one exact topic, or None."""
        cmd = ["mosquitto_sub"] + self._client_args() + ["-t", topic, "-C", "1", "-W", str(timeout)]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout + 5)
        return r.stdout.strip() or None


class BlackHole:
    """A broker that accepts the TCP connection and then says nothing: no TLS ServerHello, no CONNACK, ever. A client
    that connects to it waits for its own timeout, which is exactly what stalled a blocking MQTT connect."""

    def __init__(self, ip, port):
        self.ip, self.port = ip, port
        self.accepted = 0
        self._held = []
        self._stop = False
        self._sock = None
        self._thread = None

    def start(self):
        import threading
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self.ip, self.port))
        self._sock.listen(8)
        self._sock.settimeout(0.2)

        def run():
            while not self._stop:
                try:
                    c, _ = self._sock.accept()
                except OSError:
                    continue
                self.accepted += 1
                self._held.append(c)   # held open and never read from or written to

        self._thread = threading.Thread(target=run, daemon=True)
        self._thread.start()
        return self

    def stop(self):
        self._stop = True
        if self._thread:
            self._thread.join(2)
        for c in self._held:
            try:
                c.close()
            except OSError:
                pass
        if self._sock:
            self._sock.close()


class SlowProxy:
    """Accepts the TCP connection at once, waits `delay` seconds, then relays it to a real broker. A client that
    connects through it sits in its CONNACK wait and then succeeds: the late success a cancelled attempt can produce."""

    def __init__(self, ip, port, target_ip, target_port, delay):
        self.ip, self.port, self.target, self.delay = ip, port, (target_ip, target_port), delay
        self._stop = False
        self._socks = []
        self._sock = None

    def start(self):
        import threading
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self.ip, self.port))
        self._sock.listen(8)
        self._sock.settimeout(0.2)

        def pump(a, b):
            try:
                while not self._stop:
                    data = a.recv(4096)
                    if not data:
                        break
                    b.sendall(data)
            except OSError:
                pass
            finally:
                for s in (a, b):
                    try:
                        s.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass

        def serve(c):
            time.sleep(self.delay)
            try:
                u = socket.create_connection(self.target, timeout=5)
            except OSError:
                c.close()
                return
            self._socks += [c, u]
            threading.Thread(target=pump, args=(c, u), daemon=True).start()
            threading.Thread(target=pump, args=(u, c), daemon=True).start()

        def run():
            while not self._stop:
                try:
                    c, _ = self._sock.accept()
                except OSError:
                    continue
                self._socks.append(c)
                threading.Thread(target=serve, args=(c,), daemon=True).start()

        threading.Thread(target=run, daemon=True).start()
        return self

    def stop(self):
        self._stop = True
        for s in self._socks:
            try:
                s.close()
            except OSError:
                pass
        if self._sock:
            self._sock.close()
