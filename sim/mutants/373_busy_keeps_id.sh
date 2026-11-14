# A signed request answered busy keeps its job id unused (#555): src/helpers/iface/DeployAuth.cpp
T1=sim/tests/test_deployment_authority.py::test_a_request_answered_busy_keeps_its_id_and_runs_when_sent_again
T2=sim/tests/test_deployment_authority.py::test_a_request_that_ran_is_never_run_again_and_an_older_one_stays_refused
mutate busy_uses_up_id src/helpers/iface/DeployAuth.cpp '  if (not_run && marked >= 0) _dep->marks.m[marked].wm.mark = mark_before;' '  (void)mark_before;' $T1
mutate busy_any_status src/helpers/iface/DeployAuth.cpp '  const bool not_run = n > 0 && answeredBusy(out, (size_t)n);' '  const bool not_run = n > 0;' $T2
# Equivalent, not run: storing the busy as the result anyway. While the mark goes back a resend is new (above the
# mark), and the cache is read only for a duplicate.
