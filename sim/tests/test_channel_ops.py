"""channel.list / get / put / remove over the device interface on a simulated standalone Companion
(docs/contract/OPERATIONS.md section 5.1): the native channel table is the one store, the secret is written and never
read, and a channel set through the interface is the same channel the native frames and the mesh use."""
import os
import sys

from sim import harness
from sim.harness import CompanionClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402

RTC = 1792800000
KEY = bytes(range(0x10, 0x20))


def client(n):
    c = CompanionClient(n)
    c.app_start()
    return c, mecon_iface.IfaceClient(c._send, c._read_frame)


def put(i, slot, name, key):
    return i.request("channel.put", {"slot": slot, "name": name, "secret": key.hex()})


def test_a_channel_put_through_the_interface_is_the_native_channel_and_survives_a_reboot(nodes):
    n = nodes("companion_standalone", "ch1", seed=11, rtc=RTC).start()
    c, i = client(n)
    h = i.request("hello")["result"]
    assert {"channel.list", "channel.get", "channel.put", "channel.remove"} <= set(h["capabilities"]), h
    r = put(i, 2, "ops", KEY)
    assert r["status"] == "ok", r
    assert r["result"]["replaced"] is False and r["result"]["key_bits"] == 128, r
    assert c.get_channel(2) == ("ops", KEY), "the native frame reads what the interface wrote"
    g = i.request("channel.get", {"slot": 2})
    assert g["status"] == "ok" and g["result"]["name"] == "ops", g
    for res in (r, g, i.request("channel.list")):
        assert KEY.hex() not in str(res), "a channel secret must never be in a result"
    c.close()
    n.restart()
    c, i = client(n)
    assert c.get_channel(2) == ("ops", KEY), "the table is saved"
    names = [ch["name"] for ch in i.request("channel.list")["result"]["channels"]]
    assert "ops" in names, names


def test_a_native_set_is_seen_by_the_interface_and_remove_clears_it(nodes):
    n = nodes("companion_standalone", "ch2", seed=12, rtc=RTC).start()
    c, i = client(n)
    assert c.set_channel(5, "native", KEY)[0] == harness.RESP_OK
    g = i.request("channel.get", {"slot": 5})["result"]
    assert g["name"] == "native" and g["slot"] == 5, g
    dup = put(i, 6, "copy", KEY)
    assert dup["status"] == "failed" and dup.get("error") == "duplicate-secret", dup
    assert c.get_channel(6) is None
    r = i.request("channel.remove", {"slot": 5})
    assert r["status"] == "ok" and r["result"]["removed"] is True, r
    assert c.get_channel(5) is None, "removed in the native table"
    assert i.request("channel.get", {"slot": 5}).get("error") == "empty-slot"


def test_messages_flow_between_a_channel_set_by_the_interface_and_one_set_natively(ether, nodes):
    a = nodes("companion_standalone", "cha", seed=13, rtc=RTC, ether_path=ether.path).start()
    b = nodes("companion", "chb", seed=14, rtc=RTC, ether_path=ether.path).start()
    ca, ia = client(a)
    cb = CompanionClient(b)
    cb.device_query()   # declares the app version, so messages arrive in the V3 frames the client parses
    cb.app_start()
    assert put(ia, 1, "ops", KEY)["status"] == "ok"
    assert cb.set_channel(1, "ops", KEY)[0] == harness.RESP_OK
    assert ether.wait_for_nodes(["cha", "chb"])
    ether.set_link("cha", "chb")
    assert ca.send_channel(1, "over the managed channel", RTC + 10)[0] == harness.RESP_OK
    got = cb.sync_messages(timeout=60, want=1)
    assert any(m["text"].endswith("over the managed channel") for m in got), got


def test_an_untrusted_link_cannot_put_a_channel(nodes):
    n = nodes("companion_standalone", "ch4", seed=15, rtc=RTC).start()
    c = CompanionClient(n)
    c.app_start()
    assert c.cli("set pin 123456").startswith("> pin is now")   # the published default PIN: an untrusted link
    c.close()
    n.restart()
    b = n.connect_ble(123456)
    b.app_start()
    r = mecon_iface.IfaceClient(b._send, b._read_frame).request("channel.put", {"slot": 3, "name": "x", "secret": KEY.hex()})
    assert r["status"] == "refused", r
    b.close()
    u, _ = client(n)
    assert u.get_channel(3) is None, "nothing was written"


def test_a_failed_save_answers_write_failed_and_keeps_ram_and_flash_alike(nodes, tmp_path):
    # A save that fails must not answer ok, and must not leave RAM holding a slot that flash does not: a key that
    # "removal" left on flash would come back after a reboot.
    fail = tmp_path / "write-fail"
    n = nodes("companion_standalone", "ch5", seed=16, rtc=RTC, extra=["--write-fail", str(fail)]).start()
    c, i = client(n)
    assert c.set_channel(4, "keep", KEY)[0] == harness.RESP_OK
    fail.write_text("/channels2\n")
    r = i.request("channel.remove", {"slot": 4})
    assert r["status"] == "failed" and r.get("error") == "write-failed", r
    assert c.get_channel(4) == ("keep", KEY), "RAM still holds what flash holds"
    other = bytes(range(0x30, 0x40))
    r = put(i, 9, "new", other)
    assert r["status"] == "failed" and r.get("error") == "write-failed", r
    assert c.get_channel(9) is None, "a put that was not saved is not in RAM either"
    fail.unlink()
    c.close()
    n.restart()
    c, i = client(n)
    assert c.get_channel(4) == ("keep", KEY) and c.get_channel(9) is None, "flash agrees after a reboot"
