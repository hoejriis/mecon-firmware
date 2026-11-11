"""Which request fixtures apply to a device (docs/contract/fixtures/requests.json), shared by the board runner
(tools/mecon_iface_hw.py) and the simulator's runners (sim/tests/test_device_interface*.py).

A case may name the setting keys it needs ("needs_keys") or the keys the device must not have ("absent_keys"), template
keys included ("wifi.profile.{n}.priority"). A case is left out when the device's config.schema does not match, and the
runner prints that it was skipped and why, so a Wi-Fi-less board is not failed for a Wi-Fi setting (#418)."""
import re

_TEMPLATE = re.compile(r"\{[^}]*\}")


def _norm(key):
    return _TEMPLATE.sub("{}", key)


def schema_keys(request):
    """Every key config.schema lists, paged. request(op, args) returns the result envelope."""
    keys, cursor = set(), 0
    while cursor is not None:
        r = request("config.schema", {"cursor": cursor})
        if r.get("status") != "ok":
            break
        keys |= {_norm(k["key"]) for k in r["result"]["keys"]}
        cursor = r["result"].get("next")
    return keys


def applicable(case, keys):
    """(True, "") or (False, the reason to print)."""
    missing = [k for k in case.get("needs_keys", []) if _norm(k) not in keys]
    if missing:
        return False, "the device has no %s" % ", ".join(missing)
    present = [k for k in case.get("absent_keys", []) if _norm(k) in keys]
    if present:
        return False, "the device has %s" % ", ".join(present)
    return True, ""
