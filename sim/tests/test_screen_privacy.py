"""Screen privacy per posture (issue #113, docs/UI.md "Screen privacy"), on the real ui-new code.

A standalone Companion receives direct messages from another Companion over the simulated LoRa channel, and the
frames its screen draws are read back. In Hardened the screen draws no message text and no sender name, and a
preview captured in Normal is hidden as soon as the device turns Hardened. The pairing-PIN rule is covered by
test/test_display_privacy: the simulated USB port always reports a host, so the home screen's PIN line is not
reachable here (see test_connectivity_mode.test_ble_mode_screen_and_home_line).
"""
import pytest

from sim import harness
import test_connectivity_mode as t

pytestmark = pytest.mark.timescale(10)

SECRET = "ExampleSecretText42"   # made-up message text that must never be drawn in Hardened
SENDER = "remote"                # the name the receiving Companion knows the sender by (t._pair)


def _send(r, v, text, ts):
    assert r.send_direct(v.pub_key, text, ts)[0] == harness.RESP_SENT


def test_hardened_draws_no_message_text_and_no_sender(ether, nodes):
    victim, v, r = t._pair(ether, nodes)
    assert v.cli("set posture hardened") == "OK - posture hardened"
    t.home_settled(victim)
    assert victim.ui.wait_text("[H] ", 8), victim.ui.last_text()               # the posture marker
    mark = len(victim.ui.frames())
    _send(r, v, SECRET, t.RTC + 20)
    assert victim.ui.wait_text("message received", 30), victim.ui.last_text()
    drawn = "\n".join(victim.ui.text(f) for f in victim.ui.frames()[mark:])
    assert SECRET not in drawn, drawn
    assert SENDER not in drawn, drawn
    assert "-:" in drawn, drawn                                                # the header with the name hidden


def test_normal_draws_the_message_and_a_live_switch_hides_it(ether, nodes):
    victim, v, r = t._pair(ether, nodes)
    t.home_settled(victim)
    assert "[H] " not in victim.ui.last_text()
    _send(r, v, SECRET, t.RTC + 20)
    assert victim.ui.wait_text(SECRET, 30), victim.ui.last_text()              # positive control: Normal shows it
    assert SENDER in victim.ui.last_text(), victim.ui.last_text()
    assert v.cli("set posture hardened") == "OK - posture hardened"
    assert victim.ui.wait_text("message received", 8), victim.ui.last_text()   # redrawn at once, no new message
    last = victim.ui.last_text()
    assert SECRET not in last and SENDER not in last, last
