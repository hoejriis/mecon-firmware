"""The update set's manifest on a device (tools/mecon_release_set.py, docs/contract/UPDATES.md section 3.1): a device accepts what
the tool writes. The tool's other rules are tested in tools/tests/test_mecon_release_set.py."""
import os
import sys

from test_updates import KEY_ID, boot

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_release_set as rs  # noqa: E402


def test_a_device_stages_the_manifest_the_tool_writes(nodes):
    d = boot(nodes, "relset", 33)
    v = {"variant": "sim_companion_standalone", "hardware": d.hello["hardware"], "role": d.hello["role"], "kind": "app"}
    text = rs.manifest_text(v, b"img" * 100, "v1.18.0-mecon.99.0.0", "0" * 40, KEY_ID, 1790000000)
    r = d.stage(text)
    assert r["status"] == "ok", r
