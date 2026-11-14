# The frame timing is lora.trace's, apart from status.get, whose signed result it outgrew on an enrolled manager (#567)
T=sim/tests/test_lora_management.py::test_an_enrolled_managers_status_still_fits_a_signed_result
mutate trace_back_in_status examples/companion_radio/MeconIface.cpp '    m->writeRfCounts(w);   // the manager side: replies to this Companion'"'"'s rf.request (#531)' '    m->writeRfCounts(w); m->writeLoraTrace(w);' $T
