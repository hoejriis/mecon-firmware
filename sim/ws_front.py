"""A WebSocket front for the MQTT tests (docs/MQTT.md, "WebSocket"): it accepts MQTT over WebSocket, plain or TLS, and
forwards the MQTT bytes to a plain Mosquitto listener, the way a reverse proxy serves a broker as WSS (Ubuntu's
Mosquitto is built without WebSocket listeners). Standard library only, and written from RFC 6455 independently of the
firmware's client, so each checks the other.

It records what the device sent (the request line and headers, whether every frame was masked, the pongs) and can
misbehave on purpose: a wrong Sec-WebSocket-Accept, no subprotocol, or the server's data split into fragments with a
ping between them."""
import base64
import hashlib
import os
import socket
import ssl
import struct
import threading

GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def _recv_exact(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            raise EOFError
        b += c
    return b


class WsFront:
    def __init__(self, ip, port, upstream, path="/mqtt", cert=None, key=None, mode="ok"):
        self.ip, self.port, self.upstream, self.path, self.mode = ip, port, upstream, path, mode
        self.cert, self.key = cert, key
        self.requests = []        # (request line, {header name lower: value})
        self.sni = []             # server names a TLS client presented
        self.unmasked = 0         # client frames without the mask bit
        self.client_frames = 0
        self.pongs = []           # payloads of the pongs the client sent
        self.pings_sent = []
        self.closes = []          # close payloads the client sent
        self.upgrades = 0         # handshakes answered 101
        self._stop = False

    def start(self):
        self._sock = socket.socket()
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self.ip, self.port))
        self._sock.listen(8)
        self._sock.settimeout(0.2)
        self._ctx = None
        if self.cert:
            self._ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            self._ctx.load_cert_chain(self.cert, self.key)
            self._ctx.sni_callback = lambda sock, name, ctx: self.sni.append(name)
        threading.Thread(target=self._accept, daemon=True).start()
        return self

    def stop(self):
        self._stop = True
        self._sock.close()

    def _accept(self):
        while not self._stop:
            try:
                s, _ = self._sock.accept()
            except (socket.timeout, OSError):
                continue
            threading.Thread(target=self._serve, args=(s,), daemon=True).start()

    def _serve(self, s):
        try:
            s.settimeout(30)
            if self._ctx:
                s = self._ctx.wrap_socket(s, server_side=True)
            req = b""
            while b"\r\n\r\n" not in req:
                c = s.recv(1)
                if not c:
                    return
                req += c
                if len(req) > 4096:
                    return
            lines = req.decode("latin-1").split("\r\n")
            headers = {}
            for line in lines[1:]:
                if ":" in line:
                    k, v = line.split(":", 1)
                    headers[k.strip().lower()] = v.strip()
            self.requests.append((lines[0], headers))
            parts = lines[0].split(" ")
            if len(parts) != 3 or parts[0] != "GET" or parts[1] != self.path:
                s.sendall(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
                return
            key = headers.get("sec-websocket-key", "").encode()
            accept = base64.b64encode(hashlib.sha1(key + GUID).digest()).decode()
            if self.mode == "bad_accept":
                accept = base64.b64encode(hashlib.sha1(key + b"x" + GUID).digest()).decode()
            proto = "" if self.mode == "no_protocol" else "Sec-WebSocket-Protocol: mqtt\r\n"
            s.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                       "Sec-WebSocket-Accept: %s\r\n%s\r\n" % (accept, proto)).encode())
            if self.mode in ("bad_accept", "no_protocol"):
                return
            self.upgrades += 1
            up = socket.create_connection(self.upstream, timeout=5)
            up.settimeout(None)
            lock = threading.Lock()
            threading.Thread(target=self._down, args=(up, s, lock), daemon=True).start()
            self._up(s, up, lock)
        except (EOFError, OSError, ssl.SSLError):
            pass
        finally:
            try:
                s.close()
            except OSError:
                pass

    def _send(self, s, lock, op, payload, fin=True):
        n = len(payload)
        h = bytes([(0x80 if fin else 0) | op])
        h += bytes([n]) if n < 126 else (bytes([126]) + struct.pack(">H", n) if n < 65536 else bytes([127]) + struct.pack(">Q", n))
        with lock:
            s.sendall(h + payload)

    def _down(self, up, s, lock):
        """Broker to device: each read becomes a binary frame, or in "fragment" mode two fragments with a ping between."""
        try:
            while True:
                d = up.recv(4096)
                if not d:
                    break
                if self.mode == "fragment" and len(d) > 1:
                    cut = len(d) // 2
                    self._send(s, lock, 0x2, d[:cut], fin=False)
                    tag = os.urandom(4)
                    self.pings_sent.append(tag)
                    self._send(s, lock, 0x9, tag)
                    self._send(s, lock, 0x0, d[cut:], fin=True)
                else:
                    self._send(s, lock, 0x2, d)
        except OSError:
            pass
        try:
            self._send(s, lock, 0x8, struct.pack(">H", 1001))
        except OSError:
            pass

    def _up(self, s, up, lock):
        """Device to broker: unmask each frame and forward the data; answer a close."""
        try:
            while True:
                b0, b1 = _recv_exact(s, 2)
                op, masked, n = b0 & 0x0F, b1 & 0x80, b1 & 0x7F
                if n == 126:
                    n = struct.unpack(">H", _recv_exact(s, 2))[0]
                elif n == 127:
                    n = struct.unpack(">Q", _recv_exact(s, 8))[0]
                mask = _recv_exact(s, 4) if masked else b"\0\0\0\0"
                self.client_frames += 1
                if not masked:
                    self.unmasked += 1
                data = bytes(c ^ mask[i & 3] for i, c in enumerate(_recv_exact(s, n)))
                if op in (0x0, 0x2):
                    up.sendall(data)
                elif op == 0xA:
                    self.pongs.append(data)
                elif op == 0x8:
                    self.closes.append(data)
                    self._send(s, lock, 0x8, data[:2])
                    break
        except (EOFError, OSError, ssl.SSLError):
            pass
        finally:
            try:
                up.close()
            except OSError:
                pass
