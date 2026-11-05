"""clock back on a simulated Companion (docs/CLI_REFERENCE.md): a clock that ran ahead can be set back from the cable, and
only there; it says what that costs, and peers really do ignore the adverts for that long."""
import os
import struct
import sys
import time

from sim.harness import CompanionClient, audit_entries

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402

RTC = 1792800000
DAY = 86400
CMD_GET_DEVICE_TIME, CMD_SET_DEVICE_TIME, RESP_CURR_TIME = 5, 6, 9


def client(n):
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    return c, mecon_iface.IfaceClient(c._send, c._read_frame)


def now(c):
    c._send(bytes([CMD_GET_DEVICE_TIME]))
    end = time.time() + 5
    while time.time() < end:   # the interface's events share this link: only the time reply will do
        f = c._read_frame(1.0)
        if f and f[0] == RESP_CURR_TIME:
            return struct.unpack_from("<I", f, 1)[0]
    raise AssertionError("no time reply")


def ahead(c, by):
    assert c.request(bytes([CMD_SET_DEVICE_TIME]) + struct.pack("<I", now(c) + by))[0] == 0


def test_the_clock_and_a_new_message_go_back_and_it_is_audited(nodes):
    n = nodes("companion_standalone", "cb1", seed=51, rtc=RTC).start()
    c, i = client(n)
    ahead(c, DAY)
    high = i.request("message.send", {"channel": 0, "text": "ahead"}, rid="a1")["result"]["ts"]
    assert high >= RTC + DAY
    r = c.cli("clock back %d" % (RTC + 10))
    assert r.startswith("OK - clock back") and str(DAY - 10)[:3] in r and "adverts" in r, r
    assert now(c) < RTC + 100
    low = i.request("message.send", {"channel": 0, "text": "right"}, rid="a2")["result"]["ts"]
    assert low < RTC + 100, "a new message takes the right time at once, not the highest ever handed out"
    assert any("clock-back" in e for e in audit_entries(c.cli)), audit_entries(c.cli)


def test_it_never_goes_forward_or_below_the_floor(nodes):
    n = nodes("companion_standalone", "cb2", seed=52, rtc=RTC).start()
    c, i = client(n)
    was = now(c)
    for arg in ("%d" % (was + 1000), "1000", "0", "soon", ""):
        r = c.cli(("clock back " + arg).strip())
        assert r.startswith("ERR"), (arg, r)
    assert abs(now(c) - was) < 30


def test_a_paired_link_is_refused_and_audited(nodes):
    n = nodes("companion_standalone", "cb3", seed=53, rtc=RTC).start()
    c, i = client(n)
    assert c.cli("set pin 654321").startswith("> pin is now")
    c.request_reboot()
    assert n.wait_exit(15) == 75
    c.close()
    n.start()
    b = n.connect_ble(654321)
    b.app_start()
    was = now(b)
    r = b.cli("clock back %d" % (was - 100))
    assert r.startswith("ERR"), r
    assert now(b) >= was
    b.close()
    u, _ = client(n)
    assert any("refused ble clock-back" in e for e in audit_entries(u.cli)), audit_entries(u.cli)


def test_an_enrolled_device_can_still_be_set_back_from_the_cable(nodes):
    from test_deployment_authority import Dev
    n = nodes("companion_standalone", "cb5", seed=55, rtc=RTC).start()
    d = Dev(n)
    assert d.provision()["status"] == "ok"
    ahead(d.c, DAY)
    assert d.c.cli("clock back %d" % (RTC + 10)).startswith("OK - clock back")


def test_a_peer_ignores_the_adverts_for_that_long(nodes, ether):
    """The cost the reply states, seen from the peer: why the command says it."""
    na = nodes("companion_standalone", "cb6", ether_path=ether.path, seed=56, rtc=RTC).start()
    nb = nodes("companion_standalone", "cb7", ether_path=ether.path, seed=57, rtc=RTC).start()
    ca, _ = client(na)
    cb, _ = client(nb)
    assert ether.wait_for_nodes(["cb6", "cb7"])
    ether.set_link("cb6", "cb7")
    assert cb.add_contact(ca.pub_key, "alice")[0] == 0

    def heard():
        return [x["adv"] for x in cb.get_contacts()[1] if x["key"] == ca.pub_key][0]

    def advert_seen(after, timeout):
        assert ca.request(bytes([7, 0]))[0] == 0
        end = time.time() + timeout
        while time.time() < end:
            if heard() > after:
                return True
            time.sleep(0.5)
        return False

    ahead(ca, DAY)
    assert advert_seen(heard(), 75), "the advert from ahead is heard"
    mark = heard()
    assert mark >= RTC + DAY
    assert ca.cli("clock back %d" % (RTC + 10)).startswith("OK")
    assert not advert_seen(mark, 40), "an older advert is refused"
    assert heard() == mark
