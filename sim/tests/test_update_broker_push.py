"""UPDATES.md section 6: a push over the broker session the device already holds, in chunks of at most 1,024 bytes, with no
second connection. The same operations as the cable and BLE, on the Companion and on the server roles."""
import base64
import hashlib
import json
import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from sim.mqtt_broker import require_tools  # noqa: E402,F401
from test_companion_mqtt import brokers, _tools  # noqa: F401,E402  (fixtures)
from test_deployment_authority import provision_args, wrap  # noqa: E402
from test_companion_mqtt import static  # noqa: E402
from test_server_mqtt import boot, dev_of, tls_broker, up, verified  # noqa: E402
from test_update_install import NEW, OLD, declare, image  # noqa: E402
from test_updates import Dev, RELEASE_SEED, sign  # noqa: E402

KINDS = ["repeater_wifi", "room_server_wifi"]
BROKER_CHUNK = 1024
pytestmark = pytest.mark.timescale(10)


def chunk_env(job, token, off, img, size=BROKER_CHUNK, rid="c"):
    return {"v": 1, "id": rid, "op": "update.chunk",
            "args": {"job": job, "token": token, "offset": off, "data": base64.b64encode(img[off:off + size]).decode()}}


def signed_manifest(s, img):
    u = Dev.__new__(Dev)
    u.i = s.i
    u.hello = s.i.request("hello")["result"]
    text = u.manifest(variant="sim_" + s.n.kind, version=NEW, image={"size": len(img), "sha256": hashlib.sha256(img).hexdigest()})
    return text, sign(RELEASE_SEED, text)


def offered(s, b, dev, img, job=1):
    """Staged and offered over the broker; returns (job, token)."""
    assert s.provision()["status"] == "ok"
    text, sig = signed_manifest(s, img)
    r = b.iface_request(dev, {"v": 1, "id": "st", "op": "update.manifest", "args": {"manifest": text, "manifest_sig": sig}})
    assert r["status"] == "ok" and r["result"]["preflight"] == "ok", r
    args = {"manifest_sha256": hashlib.sha256(text.encode()).hexdigest(), "source": {"push": True}, "confirm": {"within_s": 300}}
    r = b.iface_request(dev, wrap(s.node_key, "update.offer", args, job=job))
    assert r is not None and r["status"] == "ok", r
    return r["result"]["job"], r["result"]["transfer_token"]


def server(nodes, tmp_path, b, kind, name, seed=51):
    s = boot(nodes, tmp_path, kind, name, verified(b), seed=seed)
    declare(s.n, OLD)
    s.reboot()
    up(s)
    return s


@pytest.mark.parametrize("kind", KINDS)
def test_a_whole_image_goes_over_the_session_and_installs(nodes, tmp_path, brokers, kind):  # noqa: F811
    b = tls_broker(brokers)
    s = server(nodes, tmp_path, b, kind, "bp" + kind[:2])
    dev = dev_of(s)
    img = image(3000, b"over the session")
    job, token = offered(s, b, dev, img)
    for k, off in enumerate(range(0, len(img), BROKER_CHUNK)):
        r = b.iface_request(dev, chunk_env(job, token, off, img, rid="c%d" % k))
        assert r is not None and r["status"] == "ok" and r["result"]["next"] == min(off + BROKER_CHUNK, len(img)), r
    assert s.n.wait_exit(15) == 75, "activating the image reboots the device"
    declare(s.n, NEW)
    s.n.start()
    s.attach()
    assert s.i.request("hello")["result"]["update"]["sha256"] == hashlib.sha256(img).hexdigest()
    up(s)
    r = b.iface_request(dev, wrap(s.node_key, "job.confirm", {"job": job}, job=100000))
    assert r["status"] == "ok" and r["result"]["state"] == "confirmed", r


def test_a_chunk_stays_inside_the_broker_bound_and_a_bigger_one_goes_unanswered(nodes, tmp_path, brokers):  # noqa: F811
    """1,024 bytes fits the 1,551 bytes the client buffer leaves a request, with the longest job id and offset. A bigger one
    never reaches the device: no answer, and the transfer stands where it was."""
    b = tls_broker(brokers)
    s = server(nodes, tmp_path, b, "repeater_wifi", "bb")
    dev = dev_of(s)
    img = image(5000, b"bound")
    job, token = offered(s, b, dev, img)
    worst = json.dumps({"v": 1, "id": "c" * 31, "op": "update.chunk",
                        "args": {"job": "#" + "f" * 12 + ":4294967295", "token": token, "offset": 4294967295,
                                 "data": base64.b64encode(bytes(BROKER_CHUNK)).decode()}}, separators=(",", ":"))
    assert len(worst) <= 1551, len(worst)
    assert b.iface_request(dev, chunk_env(job, token, 0, img, size=2304, rid="big"), timeout=4) is None
    r = b.iface_request(dev, chunk_env(job, token, 0, img, rid="ok"))
    assert r["status"] == "ok" and r["result"]["next"] == BROKER_CHUNK, r


def test_a_chunk_needs_the_jobs_token_over_the_broker(nodes, tmp_path, brokers):  # noqa: F811
    b = tls_broker(brokers)
    s = server(nodes, tmp_path, b, "repeater_wifi", "bt")
    dev = dev_of(s)
    img = image(3000, b"token")
    job, token = offered(s, b, dev, img)
    wrong = "0" * 32 if token != "0" * 32 else "1" * 32
    r = b.iface_request(dev, chunk_env(job, wrong, 0, img))
    assert (r["status"], r.get("error")) == ("refused", "bad-token"), r


def test_the_heap_a_push_asks_for(nodes, tmp_path, brokers):  # noqa: F811
    """26 KB at the check: a Heltec V3 beside a verified session reads 34 to 39 KB idle, about 7 KB more, and ends at 11.7 to 15.6 KB."""
    b = tls_broker(brokers)
    s = server(nodes, tmp_path, b, "repeater_wifi", "bh")
    dev = dev_of(s)
    assert s.provision()["status"] == "ok"
    img = image(3000, b"heap")
    text, sig = signed_manifest(s, img)
    s.n.heap(25 * 1024)
    time.sleep(0.5)
    r = b.iface_request(dev, {"v": 1, "id": "st", "op": "update.manifest", "args": {"manifest": text, "manifest_sig": sig}})
    assert r["status"] == "ok" and r["result"]["preflight"] == "low-memory", r
    args = {"manifest_sha256": hashlib.sha256(text.encode()).hexdigest(), "source": {"push": True}, "confirm": {"within_s": 300}}
    r = b.iface_request(dev, wrap(s.node_key, "update.offer", args, job=1))
    assert (r["status"], r.get("error")) == ("busy", "low-memory"), r
    s.n.heap(26 * 1024)
    time.sleep(0.5)
    r = b.iface_request(dev, wrap(s.node_key, "update.offer", args, job=2))
    assert r["status"] == "ok", r


def test_a_restarted_manager_learns_where_the_transfer_stands(nodes, tmp_path, brokers):  # noqa: F811
    """A chunk at any other offset writes nothing and answers the offset expected. Past the last chunk the transfer is over,
    so the token is dead: bad-token, which tells a manager whose answer was lost that the last chunk landed."""
    b = tls_broker(brokers)
    s = server(nodes, tmp_path, b, "repeater_wifi", "br")
    dev = dev_of(s)
    img = image(3000, b"resume")
    job, token = offered(s, b, dev, img)
    assert b.iface_request(dev, chunk_env(job, token, 0, img, rid="c0"))["result"]["next"] == BROKER_CHUNK
    r = b.iface_request(dev, chunk_env(job, token, 0, img, rid="c1"))
    assert r["status"] == "ok" and r["result"]["next"] == BROKER_CHUNK, "offset 0 again writes nothing: %s" % r
    assert b.iface_request(dev, chunk_env(job, token, BROKER_CHUNK, img, rid="c2"))["result"]["next"] == 2 * BROKER_CHUNK
    last = b.iface_request(dev, chunk_env(job, token, 2 * BROKER_CHUNK, img, rid="c3"))
    assert last["status"] == "ok" and last["result"]["next"] == len(img), last
    r = b.iface_request(dev, chunk_env(job, token, 2 * BROKER_CHUNK, img, rid="c4"), timeout=2)
    assert r is None or (r["status"], r.get("error")) == ("refused", "bad-token"), r


def test_a_session_that_is_not_verified_can_carry_a_push_when_stage_and_offer_are_signed(nodes, tmp_path, brokers):  # noqa: F811
    """Such a session may not read, so an unsigned stage is refused, as UPDATES.md section 6 says, and a signed one is taken.
    Its token crosses a broker nobody has checked: that broker can only spoil chunks, and the digest catches those."""
    b = tls_broker(brokers)
    s = boot(nodes, tmp_path, "repeater_wifi", "bu", static(0, b.ip, b.port, 50, tls=True), seed=52)
    declare(s.n, OLD)
    s.reboot()
    up(s)
    dev = dev_of(s)
    assert s.provision()["status"] == "ok"
    img = image(2000, b"plain")
    text, sig = signed_manifest(s, img)
    stage = {"manifest": text, "manifest_sig": sig}
    r = b.iface_request(dev, {"v": 1, "id": "st", "op": "update.manifest", "args": stage})
    assert (r["status"], r.get("error")) == ("refused", "broker-not-verified"), r
    r = b.iface_request(dev, wrap(s.node_key, "update.manifest", stage, job=1, rid="st2"))
    assert r["status"] == "ok", r
    args = {"manifest_sha256": hashlib.sha256(text.encode()).hexdigest(), "source": {"push": True}, "confirm": {"within_s": 300}}
    r = b.iface_request(dev, wrap(s.node_key, "update.offer", args, job=2))
    assert r["status"] == "ok", r
    job, token = r["result"]["job"], r["result"]["transfer_token"]
    assert b.iface_request(dev, chunk_env(job, token, 0, img, rid="c0"))["result"]["next"] == BROKER_CHUNK
    assert b.iface_request(dev, chunk_env(job, token, BROKER_CHUNK, img, rid="c1"))["result"]["next"] == len(img)
    assert s.n.wait_exit(15) == 75, "activating the image reboots the device"
