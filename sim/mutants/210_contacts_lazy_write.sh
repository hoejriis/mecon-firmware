# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# the lazy contacts write after a burst takes one bucket per pass and drains to the end (#145)
mutate contacts_flush_some_ignores_the_bound $CS '      if (written == max_buckets) { if (more) *more = true; break; }' '' unit:test_contact_store
mutate contacts_flush_some_restarts_at_the_first_bucket $CS '      _flush_from = (b + 1) % _nbuckets;' '' unit:test_contact_store
mutate contacts_lazy_write_stops_after_one_bucket examples/companion_radio/MyMesh.cpp '    if (!ok || !more) dirty_contacts_expiry = 0;' '    dirty_contacts_expiry = 0;' $CT_SIM::test_a_burst_of_adds_is_written_bucket_by_bucket_and_survives_a_power_cut
