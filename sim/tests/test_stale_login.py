"""A client whose clock once ran ahead is refused at login until its clock passes that time (issue #112).

The repeater and room server refuse a login whose timestamp is not newer than the client's last accepted one. That
watermark (ClientInfo::last_timestamp) is kept in RAM only, so a reboot clears it. These tests reproduce the lockout
with a controlled clock, check that the refusal is audited as `stale-login` (it used to be silent), that a caller
without a session cannot clear it, and that a reboot restores a fresh login without touching the password.
"""
import struct
import time

import pytest

from sim import harness
from sim.harness import CompanionClient
import test_postures_admin as pa

PW = pa.PW
AHEAD = 24 * 3600   # the client's clock runs a day fast


def _client(node, pub, kind):
    c = CompanionClient(node)
    c.device_query()
    c.app_start()
    assert c.add_contact(pub, "srv", ctype=2 if kind == "repeater" else 3)[0] == 0
    return c


def _setup(ether, nodes, kind):
    """test_postures_admin._setup, keeping the client's node so it can be power-cycled."""
    rep = nodes(kind, "rp", seed=41, rtc=pa.RTC, ether_path=ether.path).start()
    adm = nodes("companion", "ad", seed=42, rtc=pa.RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes(["rp", "ad"])
    ether.set_link("rp", "ad")
    assert ether.wait_tx("rp") and ether.wait_quiet(), "the server never advertised"
    cli = harness.CliClient(rep)
    pub = bytes.fromhex(cli.command("get public.key").lstrip("> ").strip())
    return rep, cli, adm, _client(adm, pub, kind), pub


def _lock_out(ether, nodes, kind):
    """Log in once with the client's clock a day ahead, then power-cycle the client so its clock is right again."""
    rep, cli, adm, c, pub = _setup(ether, nodes, kind)
    cli.command("password " + PW)
    f = c.request(bytes([harness.CMD_SET_DEVICE_TIME]) + struct.pack("<I", pa.RTC + AHEAD))
    assert f[0] == 0, f                                            # RESP_CODE_OK: the clock only moves forward
    assert c.login(pub, PW)                                        # accepted: the watermark is now a day ahead
    adm.restart()                                                  # the client's clock starts again at the real time
    return rep, cli, _client(adm, pub, kind), pub


@pytest.mark.lockstep()
@pytest.mark.parametrize("kind", pa.KINDS)
def test_a_stale_login_is_refused_and_audited(ether, nodes, kind):
    rep, cli, c, pub = _lock_out(ether, nodes, kind)
    assert pa._login_refused(ether, c, pub, PW)                    # the right password, an older timestamp
    log = pa._audit(cli)
    assert any(ln.endswith("login-fail remote-admin stale-login x1") for ln in log), log
    assert not any("bad-password" in ln for ln in log), log       # not mistaken for a wrong password


@pytest.mark.lockstep()
@pytest.mark.parametrize("kind", pa.KINDS)
def test_a_caller_without_a_session_cannot_clear_the_lockout(ether, nodes, kind):
    rep, cli, c, pub = _lock_out(ether, nodes, kind)
    assert pa._login_refused(ether, c, pub, "Wr0ngPw-QQ7zK")      # no session with a wrong password
    f = c.request(bytes([harness.CMD_SEND_TXT_MSG, 1, 0]) + struct.pack("<I", 0) + pub[:6] + b"reboot")
    assert f[0] == harness.RESP_SENT, f                           # a CLI line from a client with no admin session
    assert ether.wait_until_us(ether.now_us() + 30e6, timeout=60), "virtual time stalled"
    assert pa._login_refused(ether, c, pub, PW)                    # still locked out: nothing was cleared


@pytest.mark.lockstep()
@pytest.mark.parametrize("kind", pa.KINDS)
def test_a_reboot_restores_a_fresh_login_and_keeps_the_settings(ether, nodes, kind):
    rep, cli, c, pub = _lock_out(ether, nodes, kind)
    assert pa._login_refused(ether, c, pub, PW)
    sent = len(ether.tx_of("rp"))
    rep.restart()                                                  # the authorized remedy: a restart at the device
    cli = harness.CliClient(rep)
    # wait_tx would return at once on the advert from before the restart: wait for a new transmission instead
    end = time.monotonic() + 60
    while len(ether.tx_of("rp")) <= sent and time.monotonic() < end:
        time.sleep(0.01)
    assert len(ether.tx_of("rp")) > sent and ether.wait_quiet(), "the server never advertised after the restart"
    assert c.login(pub, PW)                                        # the same password, a fresh login
    log = pa._audit(cli)
    assert any(ln.endswith("login-fail remote-admin stale-login x1") for ln in log), log   # the record survived
