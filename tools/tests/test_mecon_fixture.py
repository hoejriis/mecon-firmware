"""tools/mecon_fixture.py: a fixture case is run only on a device whose settings it needs (#418)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import mecon_fixture  # noqa: E402


def pages(*lists):
    """A request() that answers config.schema in these pages."""
    def request(op, args):
        assert op == "config.schema"
        n = args["cursor"]
        nxt = n + 1 if n + 1 < len(lists) else None
        return {"status": "ok", "result": {"keys": [{"key": k} for k in lists[n]], "next": nxt}}
    return request


WIFI = mecon_fixture.schema_keys(pages(["name", "tx"], ["wifi.profile.{0-9}.priority", "wifi.profile.{0-9}.ssid"]))
NO_WIFI = mecon_fixture.schema_keys(pages(["name", "tx"]))


def test_every_page_is_read_and_templates_compare_by_shape():
    assert "wifi.profile.{}.priority" in WIFI and "name" in WIFI and len(NO_WIFI) == 2


def test_a_case_that_needs_a_key_runs_only_where_the_key_is():
    case = {"name": "config apply", "needs_keys": ["wifi.profile.{n}.priority"]}
    assert mecon_fixture.applicable(case, WIFI) == (True, "")
    ok, why = mecon_fixture.applicable(case, NO_WIFI)
    assert not ok and "wifi.profile.{n}.priority" in why


def test_its_twin_runs_only_where_the_key_is_not():
    twin = {"name": "config apply, a key this board lacks", "absent_keys": ["wifi.profile.{n}.priority"]}
    assert mecon_fixture.applicable(twin, NO_WIFI) == (True, "")
    assert not mecon_fixture.applicable(twin, WIFI)[0]


def test_a_case_that_names_no_keys_runs_everywhere():
    assert mecon_fixture.applicable({"name": "hello"}, NO_WIFI) == (True, "")
