"""update.offer with source.url on the simulated standalone Companion: the device downloads the image itself over IP, at
most image.size bytes, and the digest decides (docs/contract/UPDATES.md section 6). The simulated network uses real host
sockets (variants/sim/sim_mqtt_net.cpp), so a small HTTP server in the test serves the image; its DNS name comes from the
node's name table. TLS is OpenSSL with a throw-away self-signed certificate: an https download is encrypted, and the
server is not authenticated, because the digest alone decides the image."""
import http.server
import os
import ssl
import tempfile
import threading
import time

import pytest
from sim.harness import MqttEnv, WifiEnv
from sim.mqtt_broker import make_cert
from test_update_install import CHUNK, NEW, OLD, declare, image, job_get, stage
from test_updates import Dev, RTC

import base64
import hashlib


class Server:
    """Serves paths from a table: path -> (status, body, send_length). send_length=False sends no Content-Length (the
    body ends when the connection closes); an int sends that number instead of the body's length."""

    def __init__(self, tls=False):
        self.paths = {}
        self.requests = []
        outer = self

        class H(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def do_GET(self):
                outer.requests.append((self.path, self.headers.get("Host")))
                status, body, length = outer.paths.get(self.path, (404, b"no", True))
                self.send_response(status)
                if length == "chunked":
                    self.send_header("Transfer-Encoding", "chunked")
                elif length is True:
                    self.send_header("Content-Length", str(len(body)))
                elif length is not False:
                    self.send_header("Content-Length", str(length))
                self.send_header("Connection", "close")
                self.end_headers()
                try:
                    self.wfile.write(body)
                except (BrokenPipeError, ConnectionResetError):   # the device stops reading at image.size
                    pass
                self.close_connection = True

            def log_message(self, *a):
                pass

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
        if tls:
            d = tempfile.mkdtemp()
            cert, key, _ = make_cert(d, "fw", "127.0.0.1")
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.load_cert_chain(cert, key)
            self.httpd.socket = ctx.wrap_socket(self.httpd.socket, server_side=True)
        self.port = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def close(self):
        self.httpd.shutdown()


@pytest.fixture
def server():
    s = Server()
    yield s
    s.close()


def boot_wifi(nodes, tmp_path, name, seed):
    """A standalone Companion in Wi-Fi mode on the simulated access point, its image declaring OLD."""
    wenv = WifiEnv(str(tmp_path / (name + ".wifi")))
    wenv.set("sim")
    menv = MqttEnv(str(tmp_path / (name + ".mqtt")))
    menv.host("fw.test", "127.0.0.1")
    n = nodes("companion_standalone", name, wifi=wenv, mqtt=menv, seed=seed, rtc=RTC)
    declare(n, OLD)
    n.start()
    d = Dev(n)
    assert "ssid is now" in d.c.cli("set wifi.profile 0 ssid sim")
    assert d.c.cli("set connectivity.mode wifi").startswith("OK")
    assert n.wait_exit(10) == 75
    n.start()
    return n, Dev(n), wenv


def offer_url(d, msha, url, within_s=120, rid=None):
    return d.i.request("update.offer", {"manifest_sha256": msha, "source": {"url": url},
                                        "confirm": {"within_s": within_s}}, rid=rid)


def wait_state(d, job, states, timeout=30):
    end = time.time() + timeout
    g = job_get(d, job)
    while g["state"] not in states and time.time() < end:
        time.sleep(0.2)
        g = job_get(d, job)
    return g


def test_an_image_downloads_by_name_installs_and_attests(nodes, tmp_path, server):
    img = image(20000)
    server.paths["/fw/new.bin"] = (200, img, True)
    n, d, _ = boot_wifi(nodes, tmp_path, "dl1", 71)
    r = offer_url(d, stage(d, img), "http://fw.test:%d/fw/new.bin" % server.port, rid="dl-1")
    assert r["status"] == "ok", r
    assert r["result"]["job"] == "dl-1" and "transfer_token" not in r["result"], "a download has no chunks, so no token"
    assert n.wait_exit(30) == 75, "downloaded, verified and activated"
    assert server.requests == [("/fw/new.bin", "fw.test:%d" % server.port)], server.requests
    declare(n, NEW)
    n.start()
    d = Dev(n)
    assert d.hello["update"] == {"job": "dl-1", "version": NEW, "sha256": hashlib.sha256(img).hexdigest()}
    assert d.i.request("job.confirm", {"job": "dl-1"})["result"]["state"] == "confirmed"


def test_an_https_download_is_encrypted_and_the_digest_decides(nodes, tmp_path):
    s = Server(tls=True)
    try:
        img = image(9000, b"tls")
        s.paths["/i.bin"] = (200, img, True)
        n, d, _ = boot_wifi(nodes, tmp_path, "dl2", 72)
        r = offer_url(d, stage(d, img), "https://127.0.0.1:%d/i.bin" % s.port)
        assert r["status"] == "ok", r
        assert n.wait_exit(30) == 75, "the self-signed server is not authenticated: the digest decides"
    finally:
        s.close()


def test_an_https_download_of_the_wrong_bytes_fails_on_the_digest(nodes, tmp_path, server):
    img = image(9000, b"right")
    server.paths["/i.bin"] = (200, image(9000, b"wrong"), True)
    n, d, _ = boot_wifi(nodes, tmp_path, "dl3", 73)
    r = offer_url(d, stage(d, img), "http://127.0.0.1:%d/i.bin" % server.port)["result"]
    g = wait_state(d, r["job"], ("failed",))
    assert g["state"] == "failed" and g["result"]["error"] == "digest-mismatch", g
    assert n.proc.poll() is None, "nothing activated"


@pytest.mark.parametrize("entry, error, status", [
    ((404, b"gone", True), "download-status", 404),
    ((200, b"\xe9" * 100, True), "download-size", None),   # a Content-Length that is not image.size
    ((200, b"\xe9" * 100, False), "download-short", None),  # no length, and the server closes before image.size
    ((200, b"64\r\n" + b"\xe9" * 100 + b"\r\n0\r\n\r\n", "chunked"), "download-encoding", None),
])
def test_downloads_that_cannot_be_the_image_fail(nodes, tmp_path, server, entry, error, status):
    img = image(9000, b"x" + error.encode())
    server.paths["/i.bin"] = entry
    n, d, _ = boot_wifi(nodes, tmp_path, "dl4" + error[9:12], 74)
    r = offer_url(d, stage(d, img), "http://127.0.0.1:%d/i.bin" % server.port)["result"]
    g = wait_state(d, r["job"], ("failed",))
    assert g["state"] == "failed" and g["result"]["error"] == error, g
    assert g["result"].get("http_status") == status, g
    assert d.i.request("update.status")["result"]["job"] is None, "the one-job lock is released"


def test_no_more_than_image_size_is_taken(nodes, tmp_path, server):
    """A server that sends more than the image (no length) does not overrun the slot: the device stops at image.size."""
    img = image(9000, b"over")
    server.paths["/i.bin"] = (200, img + b"\x00" * 50000, False)
    n, d, _ = boot_wifi(nodes, tmp_path, "dl5", 75)
    r = offer_url(d, stage(d, img), "http://127.0.0.1:%d/i.bin" % server.port)
    assert r["status"] == "ok", r
    assert n.wait_exit(30) == 75, "the first image.size bytes are the image"


def test_bad_urls_are_refused_and_a_download_takes_no_chunk(nodes, tmp_path, server):
    img = image(9000, b"args")
    server.paths["/slow.bin"] = (200, img, True)
    n, d, _ = boot_wifi(nodes, tmp_path, "dl6", 76)
    msha = stage(d, img)
    for url in ("ftp://127.0.0.1/i.bin", "https://user@h/i.bin", "https://h/" + "a" * 260):
        r = offer_url(d, msha, url)
        assert r["status"] == "invalid", (url, r)
    r = d.i.request("update.offer", {"manifest_sha256": msha, "source": {"url": "http://h/i.bin", "push": True}})
    assert r["status"] == "invalid", "one source"
    r = offer_url(d, msha, "http://127.0.0.1:%d/slow.bin" % server.port)["result"]
    x = d.i.request("update.chunk", {"job": r["job"], "token": "00" * 16, "offset": 0,
                                     "data": base64.b64encode(img[:CHUNK]).decode()})
    assert x["error"] == "bad-token", x


@pytest.mark.timescale(60)
def test_a_download_that_cannot_reach_the_network_times_out(nodes, tmp_path, server):
    img = image(9000, b"down")
    server.paths["/i.bin"] = (200, img, True)
    n, d, wenv = boot_wifi(nodes, tmp_path, "dl7", 77)
    wenv.down("sim")
    time.sleep(1)
    r = offer_url(d, stage(d, img), "http://127.0.0.1:%d/i.bin" % server.port)["result"]
    g = wait_state(d, r["job"], ("failed",), timeout=60)
    assert g["state"] == "failed" and g["result"]["error"] == "download-timeout", g
    assert server.requests == []


def test_a_download_needs_more_heap_than_a_push(nodes, tmp_path, server):
    """Preflight step 12: a download opens its own connection (TLS included) beside the broker's, so it asks for more
    free heap than a push does. At 56 KB free a push passes and a download is refused."""
    img = image(9000, b"heap")
    server.paths["/i.bin"] = (200, img, True)
    n, d, _ = boot_wifi(nodes, tmp_path, "dl8", 78)
    msha = stage(d, img)
    n.heap(56 * 1024)
    time.sleep(0.5)
    r = offer_url(d, msha, "http://127.0.0.1:%d/i.bin" % server.port)
    assert (r["status"], r.get("error")) == ("busy", "low-memory"), r
    r = d.i.request("update.offer", {"manifest_sha256": msha, "source": {"push": True}})
    assert r["status"] == "ok", r
