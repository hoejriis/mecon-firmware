# Request fixtures run only where the settings they need exist (#418)
T=tools/tests/test_mecon_fixture.py
mutate fixture_needs_ignored tools/mecon_fixture.py '    missing = [k for k in case.get("needs_keys", []) if _norm(k) not in keys]' '    missing = []' "$T"
mutate fixture_absent_ignored tools/mecon_fixture.py '    present = [k for k in case.get("absent_keys", []) if _norm(k) in keys]' '    present = []' "$T"
