# A Companion's default flood scope on the managed interface (docs/contract/OPERATIONS.md, "Region scopes")
SC=sim/tests/test_scope.py
mutate scope_key_without_hash src/helpers/iface/ScopeOps.cpp '    snprintf(tagged, sizeof(tagged), "#%s", name);' '    snprintf(tagged, sizeof(tagged), "%s", name);' $SC::test_set_get_clear_on_the_native_prefs
mutate scope_private_without_key src/helpers/iface/ScopeOps.cpp '  } else if (name[0] == '"'"'$'"'"') {' '  } else if (false) {' $SC::test_names_and_keys_are_checked
mutate scope_signed_without_confirm src/helpers/iface/ScopeOps.cpp '  if (conf < 0 && !cable) { c.error = "confirm-required"; return ST_INVALID; }' '' $SC::test_a_signed_change_must_confirm_reverts_unconfirmed_and_stays_confirmed
mutate scope_revert_keeps_change src/helpers/iface/ScopeOps.cpp '  if (ok) ok = had && name[0] ? h.set(h.role_user, name, key) : h.set(h.role_user, nullptr, nullptr);' '' $SC::test_a_signed_change_must_confirm_reverts_unconfirmed_and_stays_confirmed
