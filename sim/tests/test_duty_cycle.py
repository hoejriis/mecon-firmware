"""Regulatory duty-cycle enforcement, measured on the channel.

The instrument is the ether broker: it stamps every transmission with its own clock and reports the
largest share of any sliding hour a node spent on the air (``Ether.max_duty_in_any_hour``). The nodes'
own counters are never consulted for a verdict.

Time runs at 60x (``SCALE``): one virtual hour is one real minute, so a run of 1.25 virtual hours is
75 s. Every load test runs longer than an hour so that a burst at the start (a full bucket) shows up as
an overshoot inside one sliding hour.

Radio configurations (all SF10, CR 4/5):
  869.525 MHz BW250   channel 869.400-869.650, the 10 % sub-band exactly
  868.1   MHz BW125   channel 868.0375-868.1625, inside the 1 % sub-band 868.0-868.6
  869.55  MHz BW250   channel 869.425-869.675, straddles the 10 % band and the 0.1 % gap above it
  915.0   MHz BW250   outside 863-870 and 433: no regulatory floor
"""
import re
import time

import pytest

from sim.ether import Ether, Tx
from sim.harness import CompanionClient, CliClient

SCALE = 60.0
HOUR_S = 3600.0
RTC = 1760000000
RESP_ERR = 1


# ---------------------------------------------------------------------------------------------- fixtures
@pytest.fixture
def fast_ether(tmp_path):
    e = Ether(str(tmp_path / "ether.sock"), time_scale=SCALE, seed=5).start()
    yield e
    e.stop()


class Companion:
    """A Companion kept busy with channel messages on the Public channel."""

    def __init__(self, nodes, ether, name, seed, freq, bw, sf=10, user=None):
        self.name = name
        self.node = nodes("companion", name, ether_path=ether.path, seed=seed, rtc=RTC, time_scale=SCALE).start()
        self.c = CompanionClient(self.node)
        self.c.device_query()
        self.c.app_start()
        self.c.set_radio(freq, bw, sf)
        if user is not None:
            assert self.c.cli(user).startswith("OK"), user
        self.ts = RTC + 100

    def reboot(self):
        """Power-cycle the node (same directory, radio and prefs persisted) and reconnect."""
        self.c.close()
        self.node.restart()
        self.c = CompanionClient(self.node)
        self.c.device_query()
        self.c.app_start()

    def pump(self, n=4):
        for _ in range(n):
            self.ts += 1
            f = self.c.send_channel(0, "duty cycle load %s %d " % (self.name, self.ts) + "x" * 60, self.ts)
            if f[0] == RESP_ERR:        # queue full: the node is holding packets back
                return

    def cli(self, text):
        return self.c.cli(text)


class Repeater:
    """A Repeater kept busy with flood adverts (CLI `advert`)."""

    def __init__(self, nodes, ether, name, seed, freq, bw, sf=10, user=None, kind="repeater"):
        self.name = name
        node = nodes(kind, name, ether_path=ether.path, seed=seed, rtc=RTC, time_scale=SCALE).start()
        cli = CliClient(node)
        assert cli.command("set radio %s,%s,%d,5" % (freq, bw, sf)).startswith("OK")
        if user:
            assert cli.command(user).startswith("OK"), user
        cli.close()
        self.node = node
        self.t_boot = None
        self.ether = ether
        self.reboot()                     # `set radio` applies at boot

    def reboot(self):
        """Power-cycle; t_boot is the broker's (virtual) time just before the new process starts."""
        self.t_boot = self.ether.now_us()
        self.node.restart()
        self.cli_c = CliClient(self.node)

    def pump(self, n=2):
        for _ in range(n):
            self.cli_c.command("advert", timeout=2.0)

    def cli(self, text):
        return self.cli_c.command(text)


def drive(loads, virtual_hours):
    end = time.time() + virtual_hours * HOUR_S / SCALE
    while time.time() < end:
        for ld in loads:
            ld.pump()
        time.sleep(0.003)


def report(label, ether, name, limit):
    duty = ether.max_duty_in_any_hour(name)
    n = len(ether.tx_of(name))
    print("MEASURED %-8s limit %6.3f %%  broker max sliding hour %7.3f %%  (%d packets)" % (label, limit * 100, duty * 100, n))
    return duty, n


# ---------------------------------------------------------------------------------------------- the load test
def test_sustained_load_stays_within_the_regulatory_limit(fast_ether, nodes):
    e = fast_ether
    c10 = Companion(nodes, e, "c10", 1, 869.525, 250, user="set af 0")            # user setting: no limit
    c10s = Companion(nodes, e, "c10s", 2, 869.525, 250, user="set dutycycle 5")    # user 5 % is stricter than the 10 % floor
    c1 = Companion(nodes, e, "c1", 3, 868.1, 125, user="set af 0")
    c1b = Companion(nodes, e, "c1b", 4, 868.1, 125, user="set af 9")                # the CLI's strictest, 10 %, is looser than 1 %
    c915 = Companion(nodes, e, "c915", 5, 915.0, 250, user="set af 9")             # no floor: the user's 10 % rules
    cstr = Companion(nodes, e, "cstr", 6, 869.55, 250, user="set af 0")            # straddles: strictest band, 0.1 %
    rr = Repeater(nodes, e, "rr", 7, 868.1, 125, user="set af 0")
    loads = [c10, c10s, c1, c1b, c915, cstr, rr]

    drive(loads, 1.25)

    rows = [  # (label, node, limit as a fraction of the hour)
        ("c10", "c10", 0.10), ("c10s", "c10s", 0.05), ("c1", "c1", 0.01), ("c1b", "c1b", 0.01),
        ("c915", "c915", 0.10), ("cstr", "cstr", 0.001), ("rr", "rr", 0.01),
    ]
    results = {lab: report(lab, e, name, lim) for lab, name, lim in rows}
    for lab, name, lim in rows:
        duty, n = results[lab]
        assert duty <= lim, "%s: %.3f %% of a sliding hour on the air, limit %.3f %%" % (lab, duty * 100, lim * 100)
    # the limits are being used, not just respected by sending nothing
    for lab, lim in (("c10", 0.10), ("c10s", 0.05), ("c1", 0.01), ("c1b", 0.01), ("c915", 0.10), ("rr", 0.01)):
        assert results[lab][0] >= 0.4 * lim, "%s only used %.3f %%" % (lab, results[lab][0] * 100)
    assert results["cstr"][1] >= 2, "the 0.1 % node never transmitted"


def test_frequency_change_applies_without_reboot(fast_ether, nodes):
    e = fast_ether
    cl = Companion(nodes, e, "cl", 1, 868.1, 125, user="set af 0")
    drive([cl], 0.4)
    before = e.max_duty_in_any_hour("cl")
    t_switch = e.now_us()
    cl.c.set_radio(869.525, 250, 10)            # live, no reboot
    drive([cl], 0.85)
    total = e.max_duty_in_any_hour("cl")
    after = e.airtime_us("cl", since_us=t_switch) / 3600e6
    print("MEASURED live-change  before-switch max hour %.3f %% (limit 1)  after-switch airtime %.3f %% of an hour  "
          "max sliding hour overall %.3f %% (limit 10)" % (before * 100, after * 100, total * 100))
    assert before <= 0.01
    assert after > 0.03, "the 10 % limit of the new band was not applied (only %.3f %%)" % (after * 100)
    assert total <= 0.10


# ---------------------------------------------------------------------------------------------- room server
def test_room_server_gets_the_floor_too(fast_ether, nodes):
    e = fast_ether
    room = Repeater(nodes, e, "room", 11, 868.1, 125, user="set af 0", kind="room_server")
    m = BAND.match(room.cli("get dutycycle"))
    assert m and m.group(1) == "868.000-868.600" and m.group(2) == "1.00" and m.group(3) == "1.00%", m
    drive([room], 1.25)
    duty, n = report("room", e, "room", 0.01)
    assert duty <= 0.01, "room server reached %.3f %%" % (duty * 100)
    assert duty >= 0.004


@pytest.mark.lockstep()
def test_room_server_gets_the_floor_too_lockstep(ether, nodes):
    """The room-server test on lock-step virtual time (1 ms quanta). The broker stamps every transmission with the
    time the node started it, so the limit is asserted on the channel without slack, and the use of the limit is
    bounded from below by what the firmware's window is documented to give away, not by a tolerance."""
    e = ether
    room = Repeater(nodes, e, "room", 11, 868.1, 125, user="set af 0", kind="room_server")
    m = BAND.match(room.cli("get dutycycle"))
    assert m and m.group(1) == "868.000-868.600" and m.group(2) == "1.00" and m.group(3) == "1.00%", m
    t_boot = room.t_boot
    end = t_boot + 1.25 * HOUR_S * 1e6
    while e.now_us() < end:
        room.pump()
        time.sleep(0.003)
    limit_us = 0.01 * HOUR_S * 1e6
    txs = [t for t in e.tx_of("room") if t.start_us >= t_boot and t.start_us + t.toa_us <= end]
    worst = e.max_airtime_in_window_us("room", 3600e6, since_us=t_boot)
    # Everything the node can leave unused in the busiest hour, from DutyWindow.h and Dispatcher.cpp:
    #  * the window has 121 bins of 30 s, so up to one bin more than the hour is counted: 30/3630 of the limit;
    #  * the hour is seeded at the limit on boot and the seed ages out over the 3630 s window, so the hour that ends
    #    the run (1.25 h after boot) still overlaps the window's first 900 s: (3630 - 3600) s at the limit rate is
    #    the same 30 s, already counted above;
    #  * a packet that does not fit waits, so the last one may be missing: the largest packet sent;
    #  * each packet is charged max(estimate rounded to 1 ms, the send's duration on the 1 ms clock), at most
    #    1 ms more than its time on air: 1 ms per packet in the hour.
    unused = limit_us * 30 / 3630 + max(t.toa_us for t in txs) + 1000 * len(txs)
    print("MEASURED room lock-step: max sliding hour %.6f s of %.0f s (%.4f %%), %d packets, floor %.6f s"
          % (worst / 1e6, limit_us / 1e6, worst / limit_us * 100, len(txs), (limit_us - unused) / 1e6))
    # exact stamps: every start on the 1 ms grid of a barrier, no packet before the previous one ended
    assert all(t.start_us % 1000 == 0 for t in txs)
    assert all(b.start_us >= a.start_us + a.toa_us for a, b in zip(txs, txs[1:]))
    assert worst <= limit_us, "room server reached %.6f s of a %.0f s hour" % (worst / 1e6, limit_us / 1e6)
    assert worst >= limit_us - unused, "room server used only %.6f s of %.0f s" % (worst / 1e6, limit_us / 1e6)


# ---------------------------------------------------------------------------------------------- reboots
def test_reboots_do_not_reset_the_hour(fast_ether, nodes):
    """A node at full rate that is power-cycled every 5 virtual minutes stays within its limit in every sliding
    hour, measured on the channel across all its boots."""
    e = fast_ether
    a = Companion(nodes, e, "r10", 1, 869.525, 250, user="set af 0")
    b = Companion(nodes, e, "r1", 2, 868.1, 125, user="set af 0")
    end = time.time() + 1.3 * HOUR_S / SCALE
    next_reboot = time.time() + 5 * 60 / SCALE
    boots = 0
    while time.time() < end:
        for ld in (a, b):
            ld.pump()
        if time.time() >= next_reboot:
            a.reboot()
            b.reboot()
            boots += 1
            next_reboot = time.time() + 5 * 60 / SCALE
        time.sleep(0.003)
    ra = report("r10", e, "r10", 0.10)
    rb = report("r1", e, "r1", 0.01)
    print("MEASURED reboots: %d power cycles per node" % boots)
    assert boots >= 12
    assert ra[0] <= 0.10, "10 %% node reached %.3f %% across reboots" % (ra[0] * 100)
    assert rb[0] <= 0.01, "1 %% node reached %.3f %% across reboots" % (rb[0] * 100)
    assert ra[0] >= 0.03 and rb[0] >= 0.003, "the nodes barely transmitted"


@pytest.mark.parametrize("freq,bw", [(868.1, 125), (869.525, 250)])
def test_first_advert_delay_after_boot(fast_ether, nodes, freq, bw):
    """Time from boot to the first transmission (the boot advert), with the hour seeded at the limit."""
    e = fast_ether
    name = "fa%d" % int(freq)
    rp = Repeater(nodes, e, name, 9, freq, bw, user="set af 0")      # ends with a reboot
    end = time.time() + 5 * 60 * 60 / SCALE
    first = None
    while time.time() < end and first is None:
        txs = [t for t in e.tx_of(name) if t.start_us >= rp.t_boot]
        if txs:
            first = txs[0]
        time.sleep(0.05)
    assert first is not None, "no transmission within 5 virtual hours of boot"
    delay_s = (first.start_us - rp.t_boot) / 1e6
    print("MEASURED first advert after boot at %s MHz BW %s: %.1f virtual s (packet %d bytes, %.0f ms on air)"
          % (freq, bw, delay_s, first.length, first.toa_us / 1000))
    assert delay_s < 600


# ---------------------------------------------------------------------------------------------- the CLI
BAND = re.compile(r"^> band (\S+?)(?: MHz)?, limit (\d+\.\d\d)%, floor (\S+), user (\d+\.\d\d)%, used (\d+) ms \((\d+\.\d\d)%\)$")


def test_get_dutycycle_reports_band_limit_floor_and_use(tmp_path, nodes):
    e = Ether(str(tmp_path / "ether.sock"), seed=3).start()
    try:
        node = nodes("companion", "cc", ether_path=e.path, seed=1, rtc=RTC).start()
        c = CompanionClient(node)
        c.device_query()
        c.app_start()
        c.set_radio(868.1, 125, 10)
        assert c.cli("set af 0").startswith("OK")
        m = BAND.match(c.cli("get dutycycle"))
        assert m, "unexpected reply"
        assert m.group(1) == "868.000-868.600" and m.group(2) == "1.00" and m.group(3) == "1.00%" and m.group(4) == "100.00"
        assert m.group(5) == "36000"          # the hour is seeded at the limit on boot: 1 % of 3600 s
        c.set_radio(869.525, 250, 10)                     # live: the answer follows at once
        m = BAND.match(c.cli("get dutycycle"))
        assert m.group(1) == "869.400-869.650" and m.group(2) == "10.00" and m.group(3) == "10.00%"
        c.set_radio(915.0, 250, 10)
        m = BAND.match(c.cli("get dutycycle"))
        assert m.group(1) == "none" and m.group(3) == "none" and m.group(2) == "100.00"
        c.set_radio(869.55, 250, 10)                      # straddling
        m = BAND.match(c.cli("get dutycycle"))
        assert m.group(2) == "0.10" and m.group(3) == "0.10%"
        assert c.cli("set af 9").startswith("OK")
        c.set_radio(868.1, 125, 10)
        m = BAND.match(c.cli("get dutycycle"))
        assert m.group(2) == "1.00" and m.group(4) == "10.00"      # CLI clamp at 10 %, floor still 1 %

        # repeater: `set radio` applies at boot; `tempradio` applies live
        rep = nodes("repeater", "rp", ether_path=e.path, seed=2, rtc=RTC).start()
        cli = CliClient(rep)
        assert cli.command("set radio 868.1,125,10,5").startswith("OK")
        assert cli.command("set af 0").startswith("OK")
        cli.close()
        rep.restart()
        cli = CliClient(rep)
        m = BAND.match(cli.command("get dutycycle"))
        assert m and m.group(1) == "868.000-868.600" and m.group(2) == "1.00"
        assert cli.command("tempradio 869.525,250,10,5,30").startswith("OK")
        time.sleep(4)                                     # applied 2 s after the reply
        m = BAND.match(cli.command("get dutycycle"))
        assert m and m.group(1) == "869.400-869.650" and m.group(2) == "10.00", m
        cli.close()
    finally:
        e.stop()


# ---------------------------------------------------------------------------------------------- the instrument
def test_broker_instrument_measures_sliding_hours():
    e = Ether("/nonexistent/ether.sock")
    H = 3600e6

    def add(start_s, toa_s):
        e.tx_log.append(Tx("n", start_s * 1e6, int(toa_s * 1e6), 10, b""))

    # 100 s of airtime at t=0, 100 s at t=3000 s, 100 s at t=3700 s: the busiest hour holds two blocks
    add(0, 100); add(3000, 100); add(3700, 100)
    assert abs(e.max_airtime_in_window_us("n", H) - 200e6) < 1
    # a window cut through one transmission counts the part inside it
    e2 = Ether("/nonexistent/ether.sock")
    e2.tx_log.append(Tx("n", 0, int(100e6), 10, b""))
    e2.tx_log.append(Tx("n", int(3550e6), int(100e6), 10, b""))
    assert abs(e2.max_airtime_in_window_us("n", H) - 150e6) < 1       # 3600 s cannot hold both whole
    assert abs(e2.max_airtime_in_window_us("n", 3650e6) - 200e6) < 1
    assert e.max_duty_in_any_hour("nobody") == 0.0
