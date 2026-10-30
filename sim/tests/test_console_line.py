"""The serial console of the Repeater and the Room server: a line longer than the console's buffer is dropped up to its
end and answered once, rather than run in part with its tail run as a new command."""
import re

import pytest

from sim.harness import CliClient

# the Room server's buffer is MAX_POST_TEXT_LEN + 1, defined as an arithmetic expression ("(160-9)")
_expr = re.search(r"#define MAX_POST_TEXT_LEN\s+([0-9()+\- ]+)", open("examples/simple_room_server/MyMesh.h").read()).group(1)
ROOM_MAX = int(eval(_expr, {"__builtins__": {}}))


# kind, the console buffer's size (the line fills it at size-1 bytes)
@pytest.mark.parametrize("kind,size", [("repeater", 160), ("room_server", ROOM_MAX + 1)])
def test_an_over_long_line_is_dropped_and_its_tail_never_runs(nodes, kind, size):
    node = nodes(kind, "long", seed=5).start()
    cli = CliClient(node)
    assert cli.command("set name Before") == "OK"
    # stock ran the first size-2 bytes as one line, then everything from byte size-1 on as a new command
    reply = cli.command("X" * (size - 1) + "set name Tail")
    assert reply == "ERR: line too long"
    assert cli.command("get name") == "> Before"            # the tail never ran
    assert cli.command("set name After") == "OK"            # the next line is read normally
    assert cli.command("get name") == "> After"
    cli.close()


@pytest.mark.parametrize("kind,size", [("repeater", 160), ("room_server", ROOM_MAX + 1)])
def test_a_line_that_just_fits_still_runs(nodes, kind, size):
    node = nodes(kind, "fits", seed=5).start()
    cli = CliClient(node)
    name = "N" * 20
    pad = size - 2 - len("set name ") - len(name)       # size-2 characters, then CR at size-1
    assert cli.command("set name " + name + " " * pad) == "OK"
    assert cli.command("get name").startswith("> " + name)
    cli.close()
