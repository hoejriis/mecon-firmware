"""The operation matrix in docs/contract/OPERATIONS.md section 5.1 accounts for every operation a device says it has
(Issue #268). `hello` names a build's operations, so an operation added to a role without a row fails here."""
import os
import re
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402

from sim.harness import CliClient, CompanionClient  # noqa: E402

DOC = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "contract", "OPERATIONS.md")
ROLES = {"companion_standalone": "C", "repeater": "R", "room_server": "S"}


def matrix():
    """{operation or `prefix.*`: role letters} from the table in section 5.1."""
    text = open(DOC).read()
    section = text[text.index("### 5.1 Device-interface operations"):text.index("### 5.2 ")]
    rows = {}
    for line in section.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if not line.startswith("| `") or len(cells) < 7:
            continue
        for op in re.findall(r"`([^`]+)`", cells[0]):
            rows[op] = set(re.findall(r"\b[CRS]\b", cells[5]))
    return rows


def row_for(rows, op):
    if op in rows:
        return rows[op]
    for name, roles in rows.items():
        if name.endswith(".*") and op.startswith(name[:-1]):
            return roles
    return None


def hello(nodes, kind):
    n = nodes(kind, "matrix_" + kind).start()
    if kind == "companion_standalone":
        c = CompanionClient(n)
        c.app_start()
        i = mecon_iface.IfaceClient(c._send, c._read_frame)
    else:
        cli = CliClient(n)
        i = mecon_iface.ConsoleClient(cli.sock.sendall, mecon_iface.socket_lines(cli.sock))
    caps, cursor = [], 0
    while True:   # a hello that does not fit one result is paged
        r = i.request("hello", {"cursor": cursor} if cursor else None)["result"]
        caps += r["capabilities"]
        if "next" not in r:
            return caps
        cursor = r["next"]


def test_the_table_is_read():
    rows = matrix()
    assert len(rows) > 30 and rows["hello"] == {"C", "R", "S"} and "R" not in rows["channel.list"]


@pytest.mark.parametrize("kind,role", ROLES.items())
def test_every_operation_a_device_has_has_a_row_for_its_role(nodes, kind, role):
    rows = matrix()
    ops = [c for c in hello(nodes, kind) if not c.startswith("stream.") and "." in c or c == "hello"]
    assert len(ops) > 10, ops
    missing = [op for op in ops if role not in (row_for(rows, op) or ())]
    assert not missing, "in hello on %s but with no row for role %s in OPERATIONS.md 5.1: %s" % (kind, role, missing)


def test_a_list_of_private_data_is_in_the_private_class():
    """Contact and channel lists are C_PRIVATE in the privilege table, like the inventory stream that carries them. The
    callers the two classes separate (a LoRa admin, a flagged contact, the Ethernet console) reach no Companion operation
    today, so this is read from the source: it has to hold before LoRa management opens a path."""
    source = open(os.path.join(os.path.dirname(DOC), "..", "..", "examples", "companion_radio", "MeconIface.cpp")).read()
    for fn in ("opContList", "opContGet", "opChanList", "opChanGet"):
        assert re.search(r"Status %s\(OpContext& c\) \{ return mayReadPrivate\(c\) \?" % fn, source), fn
    text = open(DOC).read()
    section = text[text.index("### 5.1 "):text.index("### 5.2 ")]
    for op in ("contact.list", "channel.list"):
        row = next(l for l in section.splitlines() if l.startswith("| `%s`" % op))
        assert row.split("|")[4].strip() == "private", row
