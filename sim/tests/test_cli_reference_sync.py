"""docs/CLI_REFERENCE.md must list every command the source registers, and nothing the source no longer has.

A text check, no board and no build: it reads the registry (src/helpers/cli/CliRegistry.h), the MECON command
handlers and the reference page.

  - every command registered in CliRegistry.h has a row in the table of each role it is registered for
  - every `get mecon.*` command in MeconCli.h, and every MECON posture, audit, connectivity, Wi-Fi, MQTT, contacts
    and duty-cycle command literal in the source, has a row
  - every row marked MECON names a command that still exists in the source, down to the field of a profile command
  - every `mecon.` name in the page exists in MeconCli.h
It checks names, not behaviour or the Notes text.
"""
import os
import re

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOC = os.path.join(ROOT, "docs", "CLI_REFERENCE.md")

ORIGINS = {"stock", "stock, behaviour changed", "MECON"}
CLASSES = {"read", "write", "secret", "posture", "destructive", "reboot"}

# role bit in the registry -> the doc tables that describe it
ROLE_TABLES = {
    "ROLE_COMPANION": ("companion",),
    "ROLE_REPEATER": ("common", "admin", "repeater", "room", "sensor"),
}


@pytest.fixture(scope="session", autouse=True)
def _binaries_present():
    """Overrides conftest.py's fixture of the same name: this test needs no simulator binaries."""


def _read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def _source_text():
    chunks = []
    for top in ("src", "examples"):
        for d, _, files in os.walk(os.path.join(ROOT, top)):
            for name in files:
                if name.endswith((".h", ".cpp")):
                    with open(os.path.join(d, name), encoding="utf-8", errors="replace") as f:
                        chunks.append(f.read())
    return "\n".join(chunks)


def _cells(line):
    parts = re.split(r"(?<!\\)\|", line.strip())
    return [c.strip().replace("\\|", "|") for c in parts[1:-1]]


def parse_doc():
    """-> {table name: [(commands, origin, class)]} where commands are the backticked spans of the first cell."""
    tables, cur, in_table = {}, None, False
    for line in _read("docs/CLI_REFERENCE.md").splitlines():
        m = re.match(r"<!-- table: ([a-z]+) -->", line)
        if m:
            cur, in_table = m.group(1), False
            tables[cur] = []
            continue
        if cur is None:
            continue
        if not line.startswith("|"):
            if in_table and line.strip() == "":
                cur = None
            continue
        cells = _cells(line)
        if cells[0].startswith("Command") or set(cells[0]) <= {"-"}:
            in_table = True
            continue
        in_table = True
        assert len(cells) == 5, "row without five cells: %r" % line
        cmds = [c.strip() for c in re.findall(r"`([^`]+)`", cells[0])]
        tables[cur].append((cmds, cells[2], cells[3], line))
    return tables


def registry_entries():
    src = _read("src/helpers/cli/CliRegistry.h")
    out = []
    for m in re.finditer(r'\{"([^"]+)",\s+M_(?:EXACT|PREFIX|WORD),\s+([A-Z_| ]+?),\s+C_', src):
        text = m.group(1).strip()
        roles = [r.strip() for r in m.group(2).split("|")]
        out.append((text, roles))
    return out


def mecon_literals():
    """Command literals this project added, found in the source by pattern."""
    names = set()
    pat = re.compile(r'"((?:get|set) (?:posture|audit|connectivity\.mode|dutycycle|mecon\.[a-z.]+|'
                     r'wifi\.[a-z]+|mqtt\.[a-z]+|contacts\.[a-z]+))')
    for rel in ("src/helpers/cli/MeconCli.h", "src/helpers/Posture.h", "src/helpers/connmode/ConnMode.h",
                "src/helpers/wifi/WifiProfiles.cpp", "src/helpers/mqtt/MqttProfiles.cpp",
                "examples/companion_radio/MyMesh.cpp"):
        for m in pat.finditer(_read(rel)):
            names.add(m.group(1))
    # the Companion keeps the stock single-network wifi.* commands; they are stock, listed in the page all the same
    return names


def covered(keyword, spans):
    """True when one of the doc's command spans is `keyword`, or `keyword` followed by arguments."""
    return any(s == keyword or s[:len(keyword) + 1] in (keyword + " ", keyword + "<", keyword + "[") for s in spans)


def _spans(tables, names):
    return [c for n in names for cmds, *_ in tables.get(n, []) for c in cmds]


def find_missing():
    """The checks as a list of problem strings; empty when the page and the source agree."""
    problems = []
    tables = parse_doc()
    for name in ("companion", "common", "admin", "repeater", "room", "sensor"):
        if name not in tables or not tables[name]:
            problems.append("table %r is missing from the page" % name)
    for name, rows in tables.items():
        for cmds, origin, cls, line in rows:
            if name == "rescue":
                continue
            if origin not in ORIGINS:
                problems.append("bad origin %r in: %s" % (origin, line[:80]))
            if not all(c.strip() in CLASSES for c in cls.split("/")):
                problems.append("bad class %r in: %s" % (cls, line[:80]))
    # 1. the registry
    for text, roles in registry_entries():
        keyword = text.strip()
        if keyword == "get":   # the generic read: not one command
            continue
        for role in roles:
            spans = _spans(tables, ROLE_TABLES[role])
            if not covered(keyword, spans):
                problems.append("registered for %s but not in the page: %s" % (role, keyword))
    # 2. MECON commands found in the source
    all_spans = _spans(tables, tables.keys())
    for name in sorted(mecon_literals()):
        if not covered(name.rstrip(), all_spans):
            problems.append("MECON command in the source but not in the page: %s" % name)
    # get mecon.* and the engine's own commands also belong to both families of roles
    for name in re.findall(r'"(get mecon\.[a-z.]+)', _read("src/helpers/cli/MeconCli.h")):
        for table in ("companion", "common"):
            if not covered(name, _spans(tables, (table,))):
                problems.append("%s missing from the %s table" % (name, table))
    # 3. rows marked MECON name something the source still has
    source = _source_text()
    for name, rows in tables.items():
        for cmds, origin, cls, line in rows:
            if origin != "MECON":
                continue
            for c in cmds:
                kw = re.split(r" <| \[", c)[0].strip()
                words = kw.split(" ")
                head = " ".join(words[:2])
                if not (re.search(re.escape('"' + head) + '[ "]', source) or re.search(re.escape('"' + kw) + '[ "]', source)):
                    problems.append("page lists a MECON command the source does not have: %s" % c)
                m = re.match(r"set (mqtt|wifi)\.profile <n> ([a-z]+)", c)
                if m:
                    f = _read("src/helpers/%s/%sProfiles.cpp" % (m.group(1), m.group(1).capitalize()))
                    if '"%s' % m.group(2) not in f:
                        problems.append("page lists a profile field the source does not have: %s" % c)
    # 4. every mecon. name in the page exists
    known = set(re.findall(r'"get (mecon\.[a-z.]+)', _read("src/helpers/cli/MeconCli.h")))
    for n in re.findall(r"get (mecon\.[a-z]+(?:\.[a-z]+)*)", _read("docs/CLI_REFERENCE.md")):
        if n not in known:
            problems.append("page names a mecon command that does not exist: get %s" % n)
    return problems


def test_the_reference_matches_the_source():
    problems = find_missing()
    assert not problems, "docs/CLI_REFERENCE.md is out of step with the source:\n  " + "\n  ".join(problems)


def test_the_registry_is_actually_read():
    """Guards the guard: if the extraction finds nothing, the sync test above would pass for the wrong reason."""
    entries = registry_entries()
    assert len(entries) >= 20, entries
    assert ("get posture", ["ROLE_COMPANION", "ROLE_REPEATER"]) in entries
    assert any(t == "erase" for t, _ in entries)
    assert len(mecon_literals()) >= 20
