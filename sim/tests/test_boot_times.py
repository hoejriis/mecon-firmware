"""`get mecon.boot` reports when boot reached each stage (helpers/BootTimes.h), on every role: the instrument for
the boot-time acceptance of the contact-store listing (a stage that is not marked reads n/a), measured on the board rather than from a serial log."""
import re

from sim.harness import CliClient, CompanionClient

RTC = 1760000000
REPLY = re.compile(r"> store_ms=(\d+|n/a) mesh_ms=(\d+|n/a) setup_ms=(\d+|n/a)")


def _times(reply):
    m = REPLY.fullmatch(reply)
    assert m, reply
    return tuple(None if x == "n/a" else int(x) for x in m.groups())


def test_companion_reports_store_mesh_and_setup_in_order(nodes):
    n = nodes("companion", "bt1", rtc=RTC, seed=3).start()
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    store, mesh, setup = _times(c.cli("get mecon.boot"))
    # every stage is marked; the simulated clock may still read 0 ms this early, so the check is presence and order
    assert None not in (store, mesh, setup), (store, mesh, setup)
    assert store <= mesh <= setup, (store, mesh, setup)


def test_repeater_and_room_server_report_mesh_and_setup(nodes):
    for kind in ("repeater", "room_server"):
        node = nodes(kind, kind + "-bt").start()
        cli = CliClient(node)
        store, mesh, setup = _times(cli.command("get mecon.boot"))
        assert store is None, (kind, store)             # no separate store stage on these roles
        assert None not in (mesh, setup) and mesh <= setup, (kind, mesh, setup)
        cli.close()
