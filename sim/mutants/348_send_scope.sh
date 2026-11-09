# message.send takes a flood scope for that send only (#421 part C)
TS=sim/tests/test_message_ops.py
T1="$TS::test_a_send_takes_a_scope_of_its_own_and_the_default_is_untouched"
mutate send_scope_none_ignored examples/companion_radio/MyMesh.cpp '  if (s.mode == Iface::SendScope::UNSCOPED) m.send_unscoped = true;' '' "$T1"
mutate send_scope_never_restored examples/companion_radio/MyMesh.cpp '  m.send_scope = scope;' '' "$T1"
mutate send_scope_key_not_applied examples/companion_radio/MyMesh.cpp '  memcpy(m.send_scope.key, s.key, sizeof(m.send_scope.key));' '' "$T1"
mutate send_scope_key_alone_taken src/helpers/iface/MessageOps.cpp '  if (sk >= 0 && (sc < 0 || jsonIsNull(*c.doc, sc))) { c.error = "bad-key"; return ST_INVALID; }' '' "$TS::test_a_scope_for_one_send_is_named_as_the_default_is"
