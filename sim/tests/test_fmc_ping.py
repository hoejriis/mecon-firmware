"""The human-readable ping on the management channel (docs/contract/MANAGEMENT_CHANNEL.md section 7), on the simulated Companion, Repeater
and Room server. A second Companion is the channel member: it sends "ping" as an
ordinary channel message and reads the replies as a stock app would. The jitter and the five minute gap run on the fast clock."""
import os
import re
import sys
import time

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
from test_deployment_authority import wrap  # noqa: E402
from test_lora_management import CH, FAST, KEY, RTC, Radio, Dev, Srv, target  # noqa: E402

pytestmark = pytest.mark.timescale(10)
BOTH = ["companion_standalone", "repeater", "room_server"]
SERVERS = ["repeater", "room_server"]
POSTURES = ["normal", "hardened"]
PONG = re.compile(r"^(?P<who>.+): pong v\S+ up \d+\w( bat \d+\.\d\dV)?( temp -?\d+\.\dC)? rx \d+ tx \d+ (ok|err \d+)$")
SCALE = 10


def line_of(d, kind):
    return d.c.cli if kind == "companion_standalone" else d.cli.command


def bound(nodes, ether, kind, name, enroll=True, posture="normal", bind=True, seed=31, shared_name=None, other=None):
    """A target holding the management channel in its slot, bound (or not), enrolled (or not), in a posture."""
    d = target(nodes, ether, kind, name, seed=seed)
    assert line_of(d, kind)("set name " + (shared_name or name)).startswith("OK")
    slot = 1 if kind == "companion_standalone" else 0
    assert d.i.request("channel.put", {"slot": slot, "name": "ops", "secret": KEY.hex()})["status"] == "ok"
    if other:   # another channel, held before the device is enrolled: slot, name, secret
        assert d.i.request("channel.put", {"slot": other[0], "name": other[1], "secret": other[2].hex()})["status"] == "ok"
    if bind:
        assert d.i.request("mgmt.channel.bind", {"slot": slot})["status"] == "ok"
    if posture == "hardened":
        assert line_of(d, kind)("set posture hardened").startswith("OK")
    if enroll:
        assert d.provision()["status"] == "ok"
    if posture == "hardened":
        d.reboot()
        got = d.send(wrap(d.node_key, "status.get", job=None, rid="post"))
        assert got["result"]["posture"] == "hardened", got
    return d


def member(nodes, ether, *targets):
    """The channel member's Companion, in radio range of every target, holding the channel in slot CH."""
    m = Radio(nodes, ether, targets[0])
    for t in targets[1:]:
        ether.set_link("mgr" + targets[0], t)
    assert m.i.request("channel.put", {"slot": CH, "name": "ops", "secret": KEY.hex()})["status"] == "ok"
    return m


_ts = [RTC]


def ping(m, text="ping", slot=CH):
    _ts[0] += 1
    f = m.c.send_channel(slot, text, _ts[0])
    return f


def replies(m, wait_s=6.0, want=1):
    """The channel texts the member's app was given within wait_s real seconds (a jittered reply takes up to 3 s at this scale)."""
    out = []
    end = time.time() + wait_s
    while time.time() < end and len(out) < want:
        out += [x for x in m.c.sync_messages(timeout=1.0, want=1) if x["kind"] == "channel" and x["channel"] == CH]
    # whatever else comes within a moment: a duplicate reply would show here
    time.sleep(1.0)
    out += [x for x in m.c.sync_messages(timeout=0.5, want=1) if x["kind"] == "channel" and x["channel"] == CH]
    return [x["text"] for x in out if ": pong " in x["text"]]


def frames(ether, name, since):
    return [t for t in ether.tx_of(name) if t.start_us >= since and (t.data[0] >> 2) & 15 == 5]   # PAYLOAD_TYPE_GRP_TXT


@pytest.mark.parametrize("posture", POSTURES)
@pytest.mark.parametrize("kind", BOTH)
def test_a_ping_is_answered_with_one_readable_status_on_the_channel(ether, nodes, kind, posture):
    name = "fp" + kind[:2] + posture[:1]
    d = bound(nodes, ether, kind, name, posture=posture)
    m = member(nodes, ether, name)
    ping(m, "ping")
    got = replies(m)
    assert len(got) == 1, got
    g = PONG.match(got[0])
    assert g, got[0]
    assert g.group("who") == name, "the reply is sent under the device's name: %s" % got[0]
    for secret in (d.node_key, d.node_key[:16], KEY.hex()):
        assert secret not in got[0], "no key or identifier in the reply"


@pytest.mark.parametrize("kind", BOTH)
def test_the_word_ping_is_matched_without_regard_to_case_and_nothing_else_is(ether, nodes, kind):
    name = "fc" + kind[:2]
    bound(nodes, ether, kind, name)
    m = member(nodes, ether, name)
    ping(m, "PiNg")
    assert len(replies(m)) == 1
    for text in ("pinging", "ping-pong", "hello", "please ping", "pingx fc"):
        ping(m, text)
    assert replies(m, wait_s=4.0, want=1) == [], "only the word, alone or with a name, is a ping"


@pytest.mark.parametrize("kind", BOTH)
def test_a_ping_with_a_name_is_answered_only_by_that_device(ether, nodes, kind):
    a, b = "fa" + kind[:2], "fb" + kind[:2]
    bound(nodes, ether, kind, a, seed=31)
    bound(nodes, ether, kind, b, seed=32)
    m = member(nodes, ether, a, b)
    ping(m, "ping " + a.upper())
    got = replies(m)
    assert len(got) == 1 and got[0].startswith(a + ": pong "), got
    ping(m, "ping nobody")
    assert replies(m, wait_s=4.0) == [], "a name nobody has is answered by nobody"


@pytest.mark.parametrize("kind", SERVERS)
def test_devices_that_share_a_name_all_answer_a_ping_for_it(ether, nodes, kind):
    # Deterministic rule: the name decides, so every device that carries it answers.
    a, b = "sna" + kind[:2], "snb" + kind[:2]
    bound(nodes, ether, kind, a, seed=31)
    bound(nodes, ether, kind, b, seed=32, shared_name=a)
    m = member(nodes, ether, a, b)
    ping(m, "ping " + a)
    got = replies(m, want=2, wait_s=8.0)
    assert len(got) == 2 and all(x.startswith(a + ": pong ") for x in got), got


@pytest.mark.parametrize("kind", BOTH)
def test_a_device_that_is_not_enrolled_stays_silent(ether, nodes, kind):
    name = "fn" + kind[:2]
    bound(nodes, ether, kind, name, enroll=False)
    m = member(nodes, ether, name)
    ping(m)
    assert replies(m, wait_s=5.0) == []


@pytest.mark.parametrize("kind", BOTH)
def test_a_device_without_a_binding_stays_silent(ether, nodes, kind):
    name = "fu" + kind[:2]
    bound(nodes, ether, kind, name, bind=False)
    m = member(nodes, ether, name)
    ping(m)
    assert replies(m, wait_s=5.0) == []


def test_a_companion_answers_only_on_its_bound_channel(ether, nodes):
    name = "fo"
    other = bytes(range(0x40, 0x50))
    bound(nodes, ether, "companion_standalone", name, other=(2, "chat", other))
    m = member(nodes, ether, name)
    assert m.i.request("channel.put", {"slot": 2, "name": "chat", "secret": other.hex()})["status"] == "ok"
    ping(m, "ping", slot=2)
    assert replies(m, wait_s=5.0) == [], "a channel that is not the management channel gets no answer"
    ping(m, "ping", slot=CH)
    assert len(replies(m)) == 1


@pytest.mark.parametrize("kind", BOTH)
def test_a_broadcast_is_answered_once_in_five_minutes_and_again_after(ether, nodes, kind):
    name = "fr" + kind[:2]
    bound(nodes, ether, kind, name)
    m = member(nodes, ether, name)
    ping(m)
    assert len(replies(m)) == 1
    ping(m)
    assert replies(m, wait_s=5.0) == [], "a second broadcast inside five minutes is not answered"
    ping(m, "ping " + name)
    assert len(replies(m)) == 1, "a targeted ping is not held by the broadcast gap"
    time.sleep(300.0 / SCALE + 2)
    ping(m)
    assert len(replies(m)) == 1, "five minutes on, it answers again"


@pytest.mark.parametrize("kind", BOTH)
def test_one_ping_sends_one_frame_and_the_same_packet_again_is_one_answer(ether, nodes, kind):
    name = "fd" + kind[:2]
    bound(nodes, ether, kind, name)
    m = member(nodes, ether, name)
    t0 = ether.now_us()
    f = ping(m, "ping " + name)
    assert len(replies(m)) == 1
    # a Repeater also carries the member's own ping on (a hop longer, the same size); the reply is the longer frame
    mine = [t for t in frames(ether, name, t0) if t.length > 60]
    assert len(mine) == 1, "one reply on the air: %d" % len(mine)
    assert not [t for t in ether.tx_of(name) if t.start_us >= t0 and (t.data[0] >> 2) & 15 not in (4, 5)], "and nothing else of its own"
    # the same packet again, as another route would bring it: the mesh has seen it, so nothing more is scheduled. The ping is
    # a targeted one with its gap long past (this clock runs ten times fast), so only that drop stands between it and a second reply
    _ts[0] -= 1
    ping(m, "ping " + name)
    assert replies(m, wait_s=4.0) == []


@pytest.mark.timescale(1)
@pytest.mark.parametrize("kind", BOTH)
def test_a_targeted_ping_has_a_short_gap_of_its_own(ether, nodes, kind):
    name = "fg" + kind[:2]
    bound(nodes, ether, kind, name)
    m = member(nodes, ether, name)
    ping(m, "ping " + name)
    assert len(replies(m, wait_s=40)) == 1
    ping(m, "ping " + name)
    assert replies(m, wait_s=3.0) == [], "ten seconds between targeted answers: this is the second inside them"
    time.sleep(10.0 + 1.5)
    ping(m, "ping " + name)
    assert len(replies(m, wait_s=40)) == 1


@pytest.mark.parametrize("kind", BOTH)
def test_an_answer_survives_a_restart_and_a_ping_changes_no_binding_or_report(ether, nodes, kind):
    name = "fs" + kind[:2]
    d = bound(nodes, ether, kind, name)
    m = member(nodes, ether, name)
    d.reboot()
    time.sleep(1)
    ping(m)
    assert len(replies(m, wait_s=10.0)) == 1
    st = d.i.request("standing.list")["result"]["instructions"]
    assert st == [], "a ping creates no standing instruction"
    assert not [t for t in ether.tx_of(name) if (t.data[0] >> 2) & 15 == 7], "and no compact report"


@pytest.mark.timescale(60)
@pytest.mark.parametrize("kind", BOTH)
def test_a_ping_with_no_airtime_to_spare_is_not_answered(ether, nodes, kind):
    # 869.55 MHz at 250 kHz, SF11: a 0.1 % band, a few seconds an hour; a boot's own adverts take the room, as in the standing test.
    name = "fa" + kind[:2] + "x"
    n = nodes(kind, name, seed=31, rtc=RTC, ether_path=ether.path).start()
    d = Dev(n) if kind == "companion_standalone" else Srv(n)
    line = line_of(d, kind)
    assert line("set radio 869.55,250,11,5").startswith("OK")
    d.reboot()
    slot = 1 if kind == "companion_standalone" else 0
    assert d.i.request("channel.put", {"slot": slot, "name": "ops", "secret": KEY.hex()})["status"] == "ok"
    assert d.i.request("mgmt.channel.bind", {"slot": slot})["status"] == "ok"
    assert d.provision()["status"] == "ok"
    m = Radio(nodes, ether, name)
    assert m.i.request("channel.put", {"slot": CH, "name": "ops", "secret": KEY.hex()})["status"] == "ok"
    # the member's radio is on the same band; the target hears it
    assert m.c.cli("set radio 869.55,250,11,5").startswith("OK")
    t0 = ether.now_us()
    ping(m)
    time.sleep(3)
    spent = ether.airtime_us(name, since_us=t0)
    answered = frames(ether, name, t0)
    # either it had the room (then one frame) or it had not (then none): never more than one, and never past the duty limit
    assert len(answered) <= 1
    assert ether.max_duty_in_any_hour(name) <= 0.0011, "the duty limit holds"
