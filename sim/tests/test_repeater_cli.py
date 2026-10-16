"""The repeater's text CLI over the simulated USB serial port."""
import re

from sim.harness import CliClient


def test_ver_get_set_name(nodes):
    rep = nodes("repeater", "rep1").start()
    cli = CliClient(rep)
    assert re.match(r"v1\.18\.0 \(Build: .+\)", cli.command("ver"))
    assert cli.command("get name") == "> Sim Repeater"
    assert cli.command("set name Hilltop") == "OK"
    assert cli.command("get name") == "> Hilltop"
    cli.close()


def test_name_persists_across_restart(nodes):
    rep = nodes("repeater", "rep2").start()
    cli = CliClient(rep)
    assert cli.command("set name Persisted") == "OK"
    cli.close()
    rep.restart()                    # same directory, new process
    cli = CliClient(rep)
    assert cli.command("get name") == "> Persisted"
    cli.close()


def test_fresh_directory_gets_default_name(nodes):
    rep = nodes("repeater", "rep3").start()
    cli = CliClient(rep)
    assert cli.command("get name") == "> Sim Repeater"
    cli.close()
