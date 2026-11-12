"""Repeater and Room server actions over the device interface (#492; docs/contract/OPERATIONS.md 5.1): clock.reset,
stats.clear, the packet log, radio.temp, the Repeater's neighbours and the Room server's posts. Each is of its CLI line's
class: all are writes but neighbour.list, as `neighbors` is the one registered read."""
import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from test_deployment_authority import wrap  # noqa: E402
from test_device_ops import next_job, settled, shaped  # noqa: E402
from test_lora_management import FAST, target  # noqa: E402
from test_regions import capabilities  # noqa: E402
from test_companion_mqtt import brokers, _tools  # noqa: F401,E402  (fixtures)

KINDS = ["repeater", "room_server"]
NEIGHBOUR_OPS = ("neighbour.list", "neighbour.remove", "neighbour.discover")


@pytest.mark.parametrize("kind", KINDS)
def test_each_action_is_listed_on_its_role_only(ether, nodes, kind):
    d = target(nodes, ether, kind, "al" + kind[:2])
    caps = capabilities(d)
    for op in ("clock.reset", "stats.clear", "log.start", "log.stop", "log.erase", "log.read", "radio.temp"):
        assert op in caps, (op, caps)
    room = kind == "room_server"
    assert all((op in caps) != room for op in NEIGHBOUR_OPS), caps
    assert ("room.post" in caps) == room, caps
    other = d.i.request("neighbour.list" if room else "room.post", None if room else {"text": "hi"})
    assert other["status"] == "failed" and other["error"] == "not-on-this-device", other


@pytest.mark.parametrize("kind", KINDS)
def test_the_packet_log_starts_stops_reads_in_pages_and_erases(ether, nodes, kind):
    d = target(nodes, ether, kind, "lg" + kind[:2])
    empty = shaped("log.read", d.i.request("log.read")["result"])
    assert empty == {"size": 0, "offset": 0, "text": "", "next": None}, empty
    assert d.i.request("log.start")["status"] == "ok"
    assert d.i.request("log.stop")["status"] == "ok"
    lines = "".join("%02d: RX len=%d SNR=8\n" % (i, 40 + i) for i in range(40))   # more than one page
    with open(os.path.join(d.n.dir, "packet_log"), "w") as f:
        f.write(lines)
    got, offset, pages = "", 0, 0
    while offset is not None:
        r = shaped("log.read", d.i.request("log.read", {"offset": offset})["result"])
        assert r["size"] == len(lines) and r["offset"] == offset, r
        got += r["text"]
        offset = r["next"]
        pages += 1
    assert got == lines and pages == 2, (pages, len(got))
    assert d.i.request("log.read", {"offset": len(lines) + 1})["status"] == "invalid"
    assert d.i.request("log.erase")["status"] == "ok"
    assert d.i.request("log.read")["result"]["size"] == 0, "erased"
    assert d.provision()["status"] == "ok"
    r = d.i.request("log.read")
    assert r["status"] == "refused", "of the write class, as the CLI's `log`: %s" % r


@pytest.mark.parametrize("kind", KINDS)
def test_stats_clear_temp_radio_and_clock_reset(ether, nodes, kind):
    d = target(nodes, ether, kind, "st" + kind[:2])
    assert d.i.request("advert.send", {"flood": True})["status"] == "ok"
    time.sleep(2)
    assert d.i.request("stats.clear")["status"] == "ok"
    r = d.i.request("radio.temp", {"radio": "869.618,62.5,8,8", "minutes": 5})
    assert r["status"] == "ok" and shaped("radio.temp", r["result"]) == {"radio": "869.618,62.5,8,8", "minutes": 5}, r
    for bad in ({"radio": "869.525,250,7,5", "minutes": 0}, {"radio": "869.525,250,7,5", "minutes": 1441},
                {"radio": "869.525,250,13,5", "minutes": 5}, {"radio": "869.525,250,7", "minutes": 5}, {"minutes": 5}):
        assert d.i.request("radio.temp", bad)["status"] == "invalid", bad
    assert d.cli.command("get radio").startswith("> " + FAST.split(",")[0]), "the saved radio is untouched"
    r = d.i.request("clock.reset")
    assert r["status"] == "ok", r
    t = d.i.request("clock.get")["result"]["epoch"]   # answered within the 1.5 s before the reboot
    assert t is not None and 1715770351 <= t < 1715770351 + 60, "the CLI's guess: %s" % t
    assert d.n.wait_exit(15) == 75, "then it reboots"


def test_the_repeater_lists_removes_and_discovers_neighbours(ether, nodes):
    a = target(nodes, ether, "repeater", "nba", seed=31)
    b = target(nodes, ether, "repeater", "nbb", seed=32)
    assert ether.wait_for_nodes(["nba", "nbb"])
    ether.set_link("nba", "nbb")
    for d, name in ((a, "nba"), (b, "nbb")):   # a node's first send of a boot waits ~30 s in the simulator
        d.i.request("advert.send")
        settled(ether, name)
    a.i.request("neighbour.list")
    assert b.i.request("advert.send", {"flood": False})["status"] == "ok"
    end, page = time.time() + 20, None
    while time.time() < end:
        page = shaped("neighbour.list", a.i.request("neighbour.list")["result"])
        if page["neighbours"]:
            break
        time.sleep(0.5)
    assert page and len(page["neighbours"]) == 1 and page["next"] is None, page   # b, heard at its first advert
    n = page["neighbours"][0]
    assert n["key"] == b.i.request("identity.get")["result"]["node_key"], n
    assert n["heard_s_ago"] < 60 and n["snr"] == pytest.approx(8, abs=1), n
    assert a.cli.command("neighbors").lower().startswith(n["key"][:8]), "the CLI's list, the same neighbour"
    for bad in ({"key": n["key"][:8]}, {"key": n["key"].upper()}, {}):
        assert a.i.request("neighbour.remove", bad)["status"] == "invalid", bad
    r = a.i.request("neighbour.remove", {"key": n["key"]})
    assert r["status"] == "ok" and shaped("neighbour.remove", r["result"]) == {"key": n["key"]}, r
    assert a.i.request("neighbour.list")["result"]["neighbours"] == []
    sent = len(ether.tx_of("nba"))
    assert a.i.request("neighbour.discover")["status"] == "ok"
    end = time.time() + 10
    while time.time() < end and len(ether.tx_of("nba")) == sent:
        time.sleep(0.2)
    assert len(ether.tx_of("nba")) > sent, "the discovery request went on the air"
    assert a.provision()["status"] == "ok"
    assert a.i.request("neighbour.list")["status"] == "ok", "a read, kept unsigned once enrolled"
    for op, args in (("neighbour.remove", {"key": n["key"]}), ("neighbour.discover", None)):
        assert a.i.request(op, args)["status"] == "refused", op
    r = a.send(wrap(a.node_key, "neighbour.discover", None, job=next_job(a)))
    assert r["status"] == "ok", r


def test_the_room_server_posts(ether, nodes):
    d = target(nodes, ether, "room_server", "rp")
    r = d.i.request("room.post", {"text": "maintenance at noon"})
    assert r["status"] == "ok", r
    assert d.i.request("room.post", {"text": "x" * 150})["status"] == "invalid", "longer than the CLI line holds"
    assert d.i.request("room.post", {"text": ""})["status"] == "invalid"
    assert d.provision()["status"] == "ok"
    assert d.i.request("room.post", {"text": "again"})["status"] == "refused"
    assert d.send(wrap(d.node_key, "room.post", {"text": "signed"}, job=next_job(d)))["status"] == "ok"


@pytest.mark.timescale(10)
def test_the_log_is_not_read_over_the_broker(nodes, tmp_path, brokers):
    from test_server_mqtt import boot as srv_boot, dev_of, tls_broker, up, verified
    b = tls_broker(brokers)
    s = srv_boot(nodes, tmp_path, "repeater_wifi", "lgb", verified(b))
    up(s)
    r = b.iface_request(dev_of(s), {"v": 1, "id": "l1", "op": "log.read"})
    assert r["status"] == "refused" and r["error"] == "not-over-this-link", "the console only, as `log`: %s" % r
    r = b.iface_request(dev_of(s), {"v": 1, "id": "l2", "op": "stats.clear"})
    assert r["status"] == "ok", "a write the verified broker session may make: %s" % r
