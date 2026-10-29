# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# the delayed contacts save retries a write error a bounded number of times (#148)
mutate lazy_save_never_retries examples/companion_radio/MyMesh.cpp '    uint32_t retry = ok ? 0 : LazySaveRetry::delayMs(++_contacts_retries);' '    uint32_t retry = 0;' $CT_SIM::test_a_save_that_fails_once_is_retried_without_another_change
mutate lazy_save_retries_forever src/helpers/LazySaveRetry.h '  return (attempt >= 1 && attempt <= ATTEMPTS) ? delays[attempt - 1] : 0;' '  return delays[0];' unit:test_lazy_save_retry
mutate lazy_save_count_not_reset_by_a_good_write examples/companion_radio/MyMesh.cpp '    if (ok) _contacts_retries = 0;' '' $CT_SIM::test_a_good_write_resets_the_retry_count
mutate lazy_save_ignores_the_give_up examples/companion_radio/MyMesh.cpp '    if (retry) {' '    if (true) {' $CT_SIM::test_a_save_that_keeps_failing_gives_up_after_three_retries
