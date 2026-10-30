"""The repeater's text CLI over the simulated USB serial port."""
import re

from sim.harness import CliClient


def test_ver_get_set_name(nodes):
    rep = nodes("repeater", "rep1").start()
    cli = CliClient(rep)
    assert re.match(r"v1\.18\.0-mecon\.\S+ \(Build: .+\)", cli.command("ver"))
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


def test_start_ota_is_refused_on_a_device_interface_build(nodes):
    # Issue #143: `start ota` would put the BLE DFU service up, and the bootloader checks no signature. A build with
    # the device interface (every sim repeater) refuses it on the console too, and starts nothing.
    rep = nodes("repeater", "rep_ota").start()
    cli = CliClient(rep)
    reply = cli.command("start ota")
    assert reply.startswith("ERR: start ota is off on this build"), reply
    assert cli.command("get name") == "> Sim Repeater"   # still answering: nothing restarted
    cli.close()
