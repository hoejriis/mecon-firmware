"""Standalone security postures on the Repeater and Room server (docs/SECURITY_MODEL.md).

Normal is the default. Hardened is entered from the local console and left only with
`set posture normal confirm` on the local console. While the admin password is the stock default,
remote admin over RF is refused in both postures; it opens in Normal once a password is set locally.
Every refusal is an explicit reply and an audit entry; an attempted password is never kept.
"""
import os
import time

import pytest

from sim import harness
from sim.harness import CliClient, CompanionClient, audit_entries, audit_file, files_containing

RTC = 1760000000
PW = "s3cretpw"
HARDENED_REFUSAL = "hardened"


def _setup(ether, nodes, kind="repeater"):
    rep = nodes(kind, "rp", seed=41, rtc=RTC, ether_path=ether.path).start()
    adm = nodes("companion", "ad", seed=42, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes(["rp", "ad"])
    ether.set_link("rp", "ad")
    cli = CliClient(rep)
    pub_hex = cli.command("get public.key").lstrip("> ").strip()
    assert len(pub_hex) == 64, pub_hex
    c = CompanionClient(adm)
    c.device_query()
    c.app_start()
    pub = bytes.fromhex(pub_hex)
    assert c.add_contact(pub, "srv", ctype=2 if kind == "repeater" else 3)[0] == 0
    return rep, cli, c, pub


def _audit(cli):
    return audit_entries(cli.command_all)


def _wait_audit_file(node, pred, timeout=8.0):
    end = time.time() + timeout
    while time.time() < end:
        entries = audit_file(node)
        if entries is not None and pred(entries):
            return entries
        time.sleep(0.1)
    return audit_file(node)


KINDS = ["repeater", "room_server"]


@pytest.mark.timescale(10)
@pytest.mark.parametrize("kind", KINDS)
def test_stock_default_password_does_not_open_remote_admin(ether, nodes, kind):
    """The stock gap itself, with no posture command involved: 1.18.0 gives full admin over the air to anyone
    who sends "password" (the compiled-in default). Positive control: the same login with a set password works."""
    rep, cli, c, pub = _setup(ether, nodes, kind)
    assert not c.login(pub, "password", timeout=15)
    cli.command("password " + PW)
    assert c.login(pub, PW)


@pytest.mark.timescale(10)
@pytest.mark.parametrize("kind", KINDS)
def test_default_admin_password_refuses_remote_admin_in_both_postures(ether, nodes, kind):
    rep, cli, c, pub = _setup(ether, nodes, kind)
    assert cli.command("get posture") == "> normal"              # the default
    assert not c.login(pub, "password", timeout=15)              # Normal: the stock password gets nothing
    assert cli.command("set posture hardened") == "OK - posture hardened"
    assert not c.login(pub, "password", timeout=15)              # Hardened: nothing either
    log = _audit(cli)
    assert any("login-fail remote-admin default-password" in ln for ln in log), log

    assert cli.command("password " + PW).startswith("password now")   # set locally (USB / UART)
    assert c.login(pub, PW), "a set password must open remote admin"  # positive control, Hardened
    assert c.remote_admin(pub, "get posture") == "> hardened"
    assert cli.command("set posture normal confirm") == "OK - posture normal"
    assert c.remote_admin(pub, "set name Remote1") == "OK"       # Normal: as stock, once a password is set
    assert cli.command("get name") == "> Remote1"


@pytest.mark.timescale(10)
@pytest.mark.parametrize("kind", KINDS)
def test_remote_commands_are_refused_while_the_password_is_default_even_with_a_session(ether, nodes, kind):
    # a session that predates the rule: log in with a real password, then the password goes back to the default
    rep, cli, c, pub = _setup(ether, nodes, kind)
    cli.command("password " + PW)
    assert c.login(pub, PW)
    assert c.remote_admin(pub, "set name Open") == "OK"
    cli.command("password password")                             # back to the stock value, locally
    reply = c.remote_admin(pub, "set name Closed")
    assert "password" in reply and reply != "OK", reply
    assert cli.command("get name") == "> Open"
    assert any("refused remote-admin default-password" in ln for ln in _audit(cli))
    # a blank-password login re-opens a stored admin entry: also refused while the password is default ...
    assert not c.login(pub, "", timeout=15)
    # ... and works again once a password is set (positive control)
    cli.command("password " + PW)
    assert c.login(pub, "")


@pytest.mark.timescale(10)
@pytest.mark.parametrize("kind", KINDS)
def test_hardened_remote_admin_is_read_only_plus_reboot(ether, nodes, kind):
    rep, cli, c, pub = _setup(ether, nodes, kind)
    cli.command("password " + PW)
    assert c.login(pub, PW)
    assert c.remote_admin(pub, "set name Normal1") == "OK"       # Normal: a settings change goes through
    assert cli.command("set posture hardened") == "OK - posture hardened"

    assert c.remote_admin(pub, "get name") == "> Normal1"        # reads still work
    assert "v1." in c.remote_admin(pub, "ver")
    refused = ["set name Hacked", "password evil", "set radio 433.0,125,7,5", "advert", "start ota"]
    for cmd in refused:
        reply = c.remote_admin(pub, cmd)
        assert HARDENED_REFUSAL in reply, (cmd, reply)
    assert cli.command("get name") == "> Normal1"                # nothing was applied
    assert cli.command("set name Local1") == "OK"                # the local console is not restricted
    log = _audit(cli)
    assert any(ln.endswith("refused remote-admin remote-write x%d" % len(refused)) for ln in log), log

    c._send(bytes([harness.CMD_SEND_TXT_MSG, 1, 0]) + b"\0\0\0\0" + pub[:6] + b"reboot")   # reboot is allowed
    assert rep.wait_exit(30) == 75


@pytest.mark.timescale(10)
def test_remote_admin_cannot_change_the_posture_and_it_is_audited(ether, nodes):
    rep, cli, c, pub = _setup(ether, nodes)
    cli.command("password " + PW)
    assert c.login(pub, PW)
    # Normal: raising from the air is refused too (trusted local transports only)
    assert "not permitted" in c.remote_admin(pub, "set posture hardened")
    assert cli.command("get posture") == "> normal"
    assert cli.command("set posture hardened") == "OK - posture hardened"
    # Hardened: lowering from the air is refused
    assert "not permitted" in c.remote_admin(pub, "set posture normal confirm")
    assert c.remote_admin(pub, "get posture") == "> hardened"
    assert "not permitted" in c.remote_admin(pub, "get audit")   # the log is for the local console only
    log = _audit(cli)
    assert any(ln.endswith("refused remote-admin posture x2") for ln in log), log
    assert any(ln.endswith("refused remote-admin audit x1") for ln in log), log
    # the local console: lowering needs the confirm token
    assert "confirm" in cli.command("set posture normal")
    assert cli.command("get posture") == "> hardened"
    assert cli.command("set posture normal confirm") == "OK - posture normal"


def test_key_export_and_import_are_refused_in_hardened_even_on_the_console(nodes):
    rep = nodes("repeater", "keys", seed=7, rtc=RTC).start()
    cli = CliClient(rep)
    key = cli.command("get prv.key").lstrip("> ").strip()
    assert len(key) == 128, key                                  # Normal: the console exports it
    assert cli.command("set prv.key " + key).startswith("OK, reboot")      # ... and imports it
    pub = cli.command("get public.key")
    assert cli.command("set posture hardened") == "OK - posture hardened"
    assert "refused" in cli.command("get prv.key") and key not in cli.command("get prv.key")
    assert "refused" in cli.command("set prv.key " + key)
    assert cli.command("get public.key") == pub
    log = _audit(cli)
    assert any("refused cable key-export" in ln for ln in log), log
    assert any("refused cable key-import" in ln for ln in log), log
    assert cli.command("set posture normal confirm") == "OK - posture normal"
    assert cli.command("get prv.key").lstrip("> ").strip() == key       # recoverable from the console


@pytest.mark.timescale(10)
def test_attempted_passwords_are_audited_but_never_kept(ether, nodes):
    rep, cli, c, pub = _setup(ether, nodes)
    cli.command("password " + PW)
    wrong = "Wr0ng-Passw0rd-QQ7"
    assert not c.login(pub, wrong, timeout=15)
    assert not c.login(pub, "password", timeout=15)
    entries = _wait_audit_file(rep, lambda e: any(x[1] == 3 for x in e))      # EV_LOGIN_FAILED reached the flash
    assert entries is not None and any(x[1] == 3 and x[2] == 4 for x in entries), entries
    log = _audit(cli)
    assert any(ln.endswith("login-fail remote-admin bad-password x2") for ln in log), log    # "password" is just wrong now
    assert c.login(pub, PW)                                      # the right one still works
    for text in (wrong,):
        assert files_containing(rep, text) == [], "attempted password found on disk or in the log"
        assert not any(text in ln for ln in log)


def test_posture_and_audit_survive_a_reboot(nodes):
    rep = nodes("repeater", "persist", seed=8, rtc=RTC).start()
    cli = CliClient(rep)
    assert cli.command("set posture hardened") == "OK - posture hardened"
    assert cli.command("set name Keep") == "OK"
    assert _wait_audit_file(rep, lambda e: any(x[1] == 1 for x in e)), "the posture change was not written at once"
    assert len(open(os.path.join(rep.dir, "audit.bin"), "rb").read()) == 268
    cli.close()
    rep.restart()
    cli = CliClient(rep)
    assert cli.command("get posture") == "> hardened"
    assert cli.command("get name") == "> Keep"
    assert any("posture cable hardened" in ln for ln in _audit(cli))


@pytest.mark.parametrize("cut", [1, 40, 200])
def test_a_power_cut_while_lowering_leaves_the_device_hardened(nodes, cut):
    rep = nodes("repeater", "lower%d" % cut, seed=9, rtc=RTC).start()
    cli = CliClient(rep)
    assert cli.command("set name Before") == "OK"
    assert cli.command("set posture hardened") == "OK - posture hardened"
    cli.close()
    rep.stop()
    rep.fault = "/prefs.json.tmp:%d" % cut
    rep.start()
    cli = CliClient(rep)
    cli.sock.sendall(b"set posture normal confirm\r")
    assert rep.wait_exit(10) == 99, "the save never reached the injected fault"
    rep.fault = None
    rep.start()
    cli = CliClient(rep)
    assert cli.command("get posture") == "> hardened"            # the cut never opens the device
    assert cli.command("get name") == "> Before"


@pytest.mark.parametrize("cut", [1, 40, 200])
def test_a_power_cut_while_raising_keeps_the_previous_complete_prefs(nodes, cut):
    rep = nodes("repeater", "raise%d" % cut, seed=10, rtc=RTC).start()
    cli = CliClient(rep)
    assert cli.command("set name Before") == "OK"
    cli.close()
    rep.stop()
    rep.fault = "/prefs.json.tmp:%d" % cut
    rep.start()
    cli = CliClient(rep)
    cli.sock.sendall(b"set posture hardened\r")
    assert rep.wait_exit(10) == 99
    rep.fault = None
    rep.start()
    cli = CliClient(rep)
    assert cli.command("get posture") == "> normal"              # previous complete prefs, nothing half-written
    assert cli.command("get name") == "> Before"
    assert cli.command("set posture hardened") == "OK - posture hardened"       # and it still works


def test_factory_reset_on_the_console_recovers_a_hardened_repeater(nodes):
    rep = nodes("repeater", "reset", seed=11, rtc=RTC).start()
    cli = CliClient(rep)
    assert cli.command("set posture hardened") == "OK - posture hardened"
    assert cli.command("set name Gone") == "OK"
    assert cli.command("erase") == "File system erase: OK"       # works in Hardened: it is the way back
    assert cli.command("get posture") == "> normal"
    log = _audit(cli)
    assert any("reset cable - x1" in ln for ln in log), log
    assert any("posture cable hardened" in ln for ln in log), log      # history is kept through the reset
    cli.close()
    assert _wait_audit_file(rep, lambda e: any(x[1] == 4 for x in e)), "the reset was not written to the audit file"
    rep.restart()
    cli = CliClient(rep)
    assert cli.command("get posture") == "> normal"
    assert cli.command("get name") != "> Gone"
    assert any("reset cable - x1" in ln for ln in _audit(cli))


def test_audit_log_is_fixed_size_and_readable_on_the_console(nodes):
    rep = nodes("repeater", "audit", seed=12, rtc=RTC).start()
    cli = CliClient(rep)
    assert cli.command_all("get audit").startswith("> 0 entries")
    for _ in range(3):
        cli.command("set posture hardened")
        cli.command("set posture normal confirm")
    log = _audit(cli)
    assert len(log) == 2                                          # raise and lower, repeats folded with a count
    assert log[0].endswith("posture cable normal x3") and log[1].endswith("posture cable hardened x3"), log
    assert _wait_audit_file(rep, lambda e: len(e) == 2)
