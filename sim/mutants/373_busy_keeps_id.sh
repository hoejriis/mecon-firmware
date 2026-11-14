# A signed request answered busy keeps its job id unused (#555): src/helpers/iface/DeployAuth.cpp
T1=sim/tests/test_deployment_authority.py::test_a_request_answered_busy_keeps_its_id_and_runs_when_sent_again
T2=sim/tests/test_deployment_authority.py::test_a_request_that_ran_is_never_run_again_and_an_older_one_stays_refused
mutate busy_uses_up_id src/helpers/iface/DeployAuth.cpp '  if (not_run && marked >= 0) _dep->marks.m[marked].wm.mark = mark_before;' '  (void)mark_before;' $T1
mutate busy_any_status src/helpers/iface/DeployAuth.cpp '  const bool not_run = n > 0 && answeredBusy(out, (size_t)n);' '  const bool not_run = n > 0;' $T2
mutate busy_cached src/helpers/iface/DeployAuth.cpp '  if (n > 0 && key[0] && cache && !not_run) cache->store(key, SIGNED_OP, out, (size_t)n);' '  if (n > 0 && key[0] && cache) cache->store(key, SIGNED_OP, out, (size_t)n);' $T1
# busy_cached is equivalent while the mark goes back: a resend is then new (above the mark) and the cache is read only for
# a duplicate. Kept so a later change that reads the cache first is caught.
