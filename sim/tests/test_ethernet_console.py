"""The Ethernet TCP console of the repeater and room server (RAK4631 `_ethernet` builds, docs/CLI.md).

Its lines used to reach the handler with sender timestamp 0, which the handler reads as the local cable console, so
any host that could open the port ran `get prv.key`, `set prv.key`, `erase` and posture changes. Here the real mains
and handlers run with the console's TCP transport replaced by a loopback listener (variants/sim/SimEthernetCLI.h);
the serial console of the same node is the cable.
"""
import time

import pytest

from sim.harness import CliClient, audit_entries, files_containing

RTC = 1760000000
PW = "s3cretpw"
KINDS = ["repeater_eth", "room_server_eth"]
DENIED = "ERR: not permitted"


def _start(nodes, kind):
    n = nodes(kind, "rp", seed=41, rtc=RTC).start()
    return n, CliClient(n), CliClient(n, ethernet=True)     # node, the cable (serial), a network connection


def _log(cable):
    return audit_entries(cable.command_all)


def _flatten(log):
    return "\n".join(log)


@pytest.mark.parametrize("kind", KINDS)
def test_unauthenticated_connection_gets_no_cable_privileges(nodes, kind):
    rep, cable, eth = _start(nodes, kind)
    key = cable.command("get prv.key").lstrip("> ").strip()
    pub = cable.command("get public.key").lstrip("> ").strip()
    assert len(key) == 128 and len(pub) == 64
    for line in ("get prv.key", "set prv.key " + key, "erase", "set posture hardened", "set posture normal confirm",
                 "get audit", "get posture", "get name", "set name Hijacked", "poweroff", "reboot", "eth.status", "ver"):
        assert eth.command(line) == DENIED, line
    # nothing happened: same identity, same name and posture, no key in any reply
    assert cable.command("get public.key").lstrip("> ").strip() == pub
    assert cable.command("get name") != "> Hijacked" and "Hijacked" not in cable.command("get name")
    assert cable.command("get posture") == "> normal"
    log = _log(cable)
    for what in ("key-export", "key-import", "posture", "audit"):
        assert any(("refused net " + what) in ln for ln in log), (what, "\n".join(log))
    assert not any(key in ln for ln in log)


@pytest.mark.parametrize("kind", KINDS)
def test_serial_console_still_has_cable_privileges(nodes, kind):
    rep, cable, eth = _start(nodes, kind)
    key = cable.command("get prv.key").lstrip("> ").strip()
    assert len(key) == 128
    assert cable.command("set prv.key " + key).startswith("OK, reboot to apply")
    assert cable.command("set posture hardened") == "OK - posture hardened"
    assert cable.command("set posture normal confirm").startswith("OK")
    assert cable.command("get posture") == "> normal"


@pytest.mark.parametrize("kind", KINDS)
def test_login_refused_while_the_admin_password_is_the_default(nodes, kind):
    rep, cable, eth = _start(nodes, kind)
    for pw in ("password", "hello", ""):            # the stock default, the room's guest password, nothing
        assert "off until the admin password is set" in eth.command("login " + pw)
    assert eth.command("get name") == DENIED                    # still not logged in
    log = _log(cable)
    assert any("login-fail net default-password" in ln for ln in log), log


@pytest.mark.parametrize("kind", KINDS)
def test_login_with_a_set_password_opens_remote_admin_not_the_cable(nodes, kind):
    rep, cable, eth = _start(nodes, kind)
    cable.command("password " + PW)
    key = cable.command("get prv.key").lstrip("> ").strip()
    pub = cable.command("get public.key").lstrip("> ").strip()
    wrong = "Wr0ngPw-QQ7zK"
    assert eth.command("login " + wrong) == "ERR: wrong password"
    assert eth.command("get name") == DENIED                    # a failed login opens nothing
    assert eth.command("login " + PW) == "OK - logged in"
    # what a remote admin may do
    assert eth.command("get public.key").lstrip("> ").strip() == pub
    assert eth.command("set name Eth Admin") == "OK"
    assert "Eth Admin" in cable.command("get name")
    # what stays on the cable
    assert eth.command("get prv.key") == DENIED
    assert eth.command("set prv.key " + key) == "ERR: prv.key can only be set from the local console"
    assert eth.command("erase") == DENIED
    assert eth.command("poweroff") == DENIED
    assert eth.command("get audit") == DENIED
    assert eth.command("set posture hardened") == DENIED
    assert eth.command("set posture normal confirm") == DENIED
    assert cable.command("get posture") == "> normal"
    assert cable.command("get public.key").lstrip("> ").strip() == pub
    log = _log(cable)
    for what in ("key-export", "posture", "audit"):       # `set prv.key` from the network is explained, not logged
        assert any(("refused net " + what) in ln for ln in log), (what, "\n".join(log))
    assert any(ln.endswith("login-fail net bad-password x1") for ln in log), log
    # the attempt is never kept: not in the audit log, not on disk, not in the node's log
    assert wrong not in _flatten(log) and PW not in _flatten(log)    # (the set password itself lives in prefs.json)
    assert files_containing(rep, wrong) == []


@pytest.mark.parametrize("kind", KINDS)
def test_session_ends_with_the_connection_and_on_logout(nodes, kind):
    rep, cable, eth = _start(nodes, kind)
    cable.command("password " + PW)
    assert eth.command("login " + PW) == "OK - logged in"
    assert eth.command("get public.key").startswith("> ")
    assert eth.command("logout") == "OK - logged out"
    assert eth.command("get public.key") == DENIED
    assert eth.command("login " + PW) == "OK - logged in"
    eth.sock.close()
    again = CliClient(rep, ethernet=True)                      # a new connection starts logged out
    assert again.command("get public.key") == DENIED


@pytest.mark.parametrize("kind", KINDS)
def test_hardened_turns_the_ethernet_console_off(nodes, kind):
    rep, cable, eth = _start(nodes, kind)
    cable.command("password " + PW)
    assert eth.command("login " + PW) == "OK - logged in"
    assert eth.command("get public.key").startswith("> ")
    assert cable.command("set posture hardened") == "OK - posture hardened"
    assert eth.command("get public.key") == DENIED                          # a live session is dropped at once
    assert "off in hardened posture" in eth.command("login " + PW)         # and a new login is refused, with the reason
    assert eth.command("get public.key") == DENIED                          # nothing slipped through
    assert eth.command("get prv.key") == DENIED
    assert cable.command("set posture normal confirm").startswith("OK")
    assert eth.command("get public.key") == DENIED                          # the session did not survive
    assert eth.command("login " + PW) == "OK - logged in"


@pytest.mark.parametrize("kind", KINDS)
def test_repeated_wrong_passwords_lock_logins(nodes, kind):
    rep, cable, eth = _start(nodes, kind)
    cable.command("password " + PW)
    for _ in range(5):
        assert eth.command("login nope") == "ERR: wrong password"
    assert "too many failed logins" in eth.command("login " + PW)         # even the right one waits
    other = CliClient(rep, ethernet=True)
    assert "too many failed logins" in other.command("login " + PW)       # a new connection does not reset it
    assert other.command("get public.key") == DENIED


@pytest.mark.parametrize("kind", KINDS)
def test_a_logged_in_connection_runs_as_a_remote_admin_not_as_timestamp_zero(nodes, kind):
    """The handlers keep a few cable-only commands behind sender timestamp 0 (radio frequency, the statistics dumps, the
    log, the ACL listing). A logged-in Ethernet connection must not reach them, whatever the table says."""
    rep, cable, eth = _start(nodes, kind)
    cable.command("password " + PW)
    assert eth.command("login " + PW) == "OK - logged in"
    freq = cable.command("get freq")
    assert eth.command("set freq 869.1").startswith("unknown config")
    assert eth.command("stats-core") == "Unknown command"
    assert eth.command("stats-radio") == "Unknown command"
    assert eth.command("log") == "Unknown command"
    assert eth.command("get acl") != ""                      # timestamp 0 would list the ACL on the serial port and answer nothing
    assert cable.command("get freq") == freq
    assert cable.command("stats-core").startswith("{")       # the cable still has them


@pytest.mark.parametrize("kind", KINDS)
def test_an_over_long_line_is_refused_and_its_tail_never_runs(nodes, kind):
    rep, cable, eth = _start(nodes, kind)
    cable.command("password " + PW)
    assert eth.command("x" * 400) == "ERR: line too long"                 # before login: the console is not wedged
    assert eth.command("login " + PW) == "OK - logged in"
    pub = eth.command("get public.key").lstrip("> ").strip()
    assert eth.command("y" * 200 + " set name Tail") == "ERR: line too long"
    assert "Tail" not in cable.command("get name")
    assert eth.command("get public.key").lstrip("> ").strip() == pub      # the next line is read whole


@pytest.mark.parametrize("kind", KINDS)
def test_a_new_connection_starts_an_empty_line(nodes, kind):
    rep, cable, eth = _start(nodes, kind)
    cable.command("password " + PW)
    eth.sock.sendall(b"set name Leftover")                                # a partial line, never ended
    time.sleep(1.0)                                                       # read into the line buffer first
    eth2 = CliClient(rep, ethernet=True)                                  # replaces the first connection
    assert eth2.command("login " + PW) == "OK - logged in"
