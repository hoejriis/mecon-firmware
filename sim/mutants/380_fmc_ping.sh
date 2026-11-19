# The readable ping on the management channel: src/helpers/iface/FmcPing.h, examples/simple_room_server/MyMesh.cpp, examples/companion_radio/MeconIface.cpp, src/helpers/iface/ConsoleIface.cpp
TS=sim/tests/test_fmc_ping.py
# Who answers: enrolled, bound, on the bound channel only
mutate fmc_companion_unenrolled examples/companion_radio/MeconIface.cpp 'return slot >= 0 && slot == _lora_bound_slot && _auth.enrolled(); }' 'return slot >= 0 && slot == _lora_bound_slot; }' $TS::test_a_device_that_is_not_enrolled_stays_silent
mutate fmc_companion_any_channel examples/companion_radio/MeconIface.cpp 'return slot >= 0 && slot == _lora_bound_slot && _auth.enrolled(); }' 'return slot >= 0 && _auth.enrolled(); }' $TS::test_a_companion_answers_only_on_its_bound_channel
mutate fmc_repeater_unenrolled src/helpers/iface/ConsoleIface.cpp '  return s->_lora_key_ok && s->_auth.enrolled();' '  return s->_lora_key_ok;' $TS::test_a_device_that_is_not_enrolled_stays_silent
# The Repeater reads group text only through the interface's verdict
mutate fmc_repeater_no_verdict examples/simple_repeater/MyMesh.cpp '  return s.ping_ok && s.ping_ok(s.user);' '  return s.ping_ok != nullptr;' $TS::test_a_device_that_is_not_enrolled_stays_silent
# The Room server hears group text only through the same verdict, and answers only when its loop runs the tick
mutate fmc_room_no_verdict examples/simple_room_server/MyMesh.cpp '  return s.ping_ok && s.ping_ok(s.user);' '  return s.ping_ok != nullptr;' $TS::test_a_device_that_is_not_enrolled_stays_silent
mutate fmc_room_no_tick examples/simple_room_server/MyMesh.cpp '  fmcTick();' '  (void)0;' $TS::test_a_ping_is_answered_with_one_readable_status_on_the_channel
# What is a ping
mutate fmc_parse_case src/helpers/iface/FmcPing.h '  for (int i = 0; i < 4; i++) if (fmcLower(m[i]) != p[i]) return FMC_NONE;' '  for (int i = 0; i < 4; i++) if (m[i] != p[i]) return FMC_NONE;' $TS::test_the_word_ping_is_matched_without_regard_to_case_and_nothing_else_is
mutate fmc_parse_word_boundary src/helpers/iface/FmcPing.h '  if (*m != '"' '"') return FMC_NONE;
  while (*m == '"' '"') m++;
  size_t n' '  while (*m == '"' '"') m++;
  size_t n' unit:test_fmc_ping
mutate fmc_parse_name_any src/helpers/iface/FmcPing.h '  for (size_t i = 0; i < n; i++) if (fmcLower(m[i]) != fmcLower(nm[i])) return FMC_NONE;' '  for (size_t i = 0; i < n; i++) if (0) return FMC_NONE;' $TS::test_a_ping_with_a_name_is_answered_only_by_that_device
mutate fmc_parse_name_prefix src/helpers/iface/FmcPing.h '  if (n != k) return FMC_NONE;' '  if (n > k) return FMC_NONE;' unit:test_fmc_ping
# A reply is never a ping
mutate fmc_reply_reads_as_ping src/helpers/iface/FmcPing.h '  fmcAdd(out, cap, n, "pong");' '  fmcAdd(out, cap, n, "ping");' unit:test_fmc_ping
# Limits
mutate fmc_no_broadcast_gap src/helpers/iface/FmcPing.h '      if (_bcast_ok && (uint32_t)(now - _bcast_ms) < FMC_BROADCAST_GAP_MS) return false;' '      if (false) return false;' $TS::test_a_broadcast_is_answered_once_in_five_minutes_and_again_after
mutate fmc_no_target_gap src/helpers/iface/FmcPing.h '      if (_target_ok && (uint32_t)(now - _target_ms) < FMC_TARGET_GAP_MS) return false;' '      if (false) return false;' $TS::test_a_targeted_ping_has_a_short_gap_of_its_own
mutate fmc_unbounded_jitter src/helpers/iface/FmcPing.h '      _due = now + rnd % (FMC_BROADCAST_JITTER_MS + 1);' '      _due = now + rnd;' unit:test_fmc_ping
mutate fmc_queue_unbounded src/helpers/iface/FmcPing.h '    if (_pending) return false;   // one reply is waiting: the queue is one deep' '    (void)_pending;' unit:test_fmc_ping
mutate fmc_gap_from_arrival src/helpers/iface/FmcPing.h '    if (_pending_all) { _bcast_ok = true; _bcast_ms = now; } else { _target_ok = true; _target_ms = now; }   // the gap runs from the send' '    (void)now;' unit:test_fmc_ping
mutate fmc_deferred_forever src/helpers/iface/FmcPing.h '    if (_retries < FMC_RETRIES) { _retries++;' '    if (true) {' unit:test_fmc_ping
