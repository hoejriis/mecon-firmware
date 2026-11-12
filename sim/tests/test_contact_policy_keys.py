"""The Companion's contact auto-add, telemetry and advert-location settings as config keys (#507; OPERATIONS.md 5.1, 5.2):
what the app frames 38 and 58 set, which no manager could read before. A heard advert is stored or not as the keys say."""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from test_clock_back import RTC, client  # noqa: E402
from test_jobs import PIN, ble_boot  # noqa: E402
import mecon_iface  # noqa: E402

KEYS = ["contacts.manual_add", "contacts.autoadd.chat", "contacts.autoadd.repeater", "contacts.autoadd.room",
        "contacts.autoadd.sensor", "contacts.autoadd.overwrite", "contacts.autoadd.max_hops", "telemetry.base",
        "telemetry.location", "telemetry.environment", "advert.location"]
CMD_SET_OTHER_PARAMS, CMD_SET_AUTOADD_CONFIG = 38, 58


def values(i, keys=KEYS):
    r = i.request("config.get", {"keys": keys})
    assert r["status"] == "ok", r
    return r["result"]["values"]


def test_the_keys_read_the_defaults_and_follow_the_app_frames(nodes):
    n = nodes("companion_standalone", "cpk", seed=71, rtc=RTC).start()
    c, i = client(n)
    assert values(i) == {"contacts.manual_add": False, "contacts.autoadd.chat": False, "contacts.autoadd.repeater": False,
                         "contacts.autoadd.room": False, "contacts.autoadd.sensor": False,
                         "contacts.autoadd.overwrite": False, "contacts.autoadd.max_hops": 0, "telemetry.base": "deny",
                         "telemetry.location": "deny", "telemetry.environment": "deny", "advert.location": "none"}
    # frame 38: manual add on; base all, location flags, environment deny; advert location share
    assert c.request(bytes([CMD_SET_OTHER_PARAMS, 1, 2 | (1 << 2), 1]))[0] == 0
    # frame 58: chat and overwrite on, max 3 hops
    assert c.request(bytes([CMD_SET_AUTOADD_CONFIG, 0x02 | 0x01, 3]))[0] == 0
    got = values(i)
    assert got["contacts.manual_add"] is True and got["contacts.autoadd.chat"] is True, got
    assert got["contacts.autoadd.overwrite"] is True and got["contacts.autoadd.repeater"] is False, got
    assert got["contacts.autoadd.max_hops"] == 3, got
    assert (got["telemetry.base"], got["telemetry.location"], got["telemetry.environment"]) == ("all", "flags", "deny"), got
    assert got["advert.location"] == "share", got
    r = i.request("config.apply", {"set": {"contacts.manual_add": False, "contacts.autoadd.room": True,
                                           "contacts.autoadd.max_hops": 64, "telemetry.environment": "all",
                                           "advert.location": "none"}})
    assert r["status"] == "ok", r
    got = values(i)
    assert got["contacts.manual_add"] is False and got["contacts.autoadd.room"] is True, got
    assert got["contacts.autoadd.chat"] is True, "the other bits are kept"
    assert got["contacts.autoadd.max_hops"] == 64 and got["telemetry.environment"] == "all", got
    assert c.cli("get contacts.autoadd.room") == "> on" and c.cli("get telemetry.environment") == "> all"
    assert i.request("config.apply", {"set": {"advert.location": "share"}})["status"] == "ok"
    assert values(i, ["advert.location"]) == {"advert.location": "share"} and c.cli("get advert.location") == "> share"
    for bad in ({"contacts.autoadd.max_hops": 65}, {"telemetry.base": "some"}, {"advert.location": "prefs"},
                {"contacts.manual_add": "on"}):
        assert i.request("config.apply", {"set": bad})["status"] == "invalid", bad
    assert c.cli("set contacts.autoadd.max_hops 65").startswith("Error")
    assert c.cli("set telemetry.base some").startswith("Error")


def test_a_phone_on_the_published_default_pin_may_not_change_them(nodes):
    n = ble_boot(nodes, "cpd", 123456)
    b = n.connect_ble(123456)
    b.app_start()
    i = mecon_iface.IfaceClient(b._send, b._read_frame)
    r = i.request("config.apply", {"set": {"contacts.manual_add": True}})
    assert r["status"] == "refused", r
    b.close()
    c, i = client(n)
    assert values(i, ["contacts.manual_add"]) == {"contacts.manual_add": False}


def test_a_heard_advert_is_stored_as_the_keys_say(nodes, ether):
    na = nodes("companion_standalone", "cpa", ether_path=ether.path, seed=72, rtc=RTC).start()
    nb = nodes("companion_standalone", "cpb", ether_path=ether.path, seed=73, rtc=RTC).start()
    ca, _ = client(na)
    cb, ib = client(nb)
    assert ether.wait_for_nodes(["cpa", "cpb"])
    ether.set_link("cpa", "cpb")
    assert ib.request("config.apply", {"set": {"contacts.manual_add": True}})["status"] == "ok"

    def stored():
        return any(x["key"] == ca.pub_key for x in cb.get_contacts()[1])

    def advert(timeout):
        assert ca.request(bytes([7, 0]))[0] == 0
        end = time.time() + timeout
        while time.time() < end:
            if stored():
                return True
            time.sleep(0.5)
        return False

    assert not advert(75), "manual add, and chat contacts not among the auto-added types: heard, not stored"
    assert ib.request("config.apply", {"set": {"contacts.autoadd.chat": True}})["status"] == "ok"
    assert advert(40), "chat contacts auto-added: the next advert is stored"
