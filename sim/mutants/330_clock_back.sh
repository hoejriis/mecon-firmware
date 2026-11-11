# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# clock back: from the cable only, never forward, audited, and the unique time follows the clock again
CB=sim/tests/test_clock_back.py
mutate clock_back_unique_stays_ahead examples/companion_radio/MyMesh.cpp '  getRTCClock()->restartUnique();' '' $CB::test_the_clock_and_a_new_message_go_back_and_it_is_audited
mutate clock_back_goes_forward examples/companion_radio/MyMesh.cpp '  if (secs < 1704067200UL || secs >= getRTCClock()->getCurrentTime()) return false;' '  if (secs < 1704067200UL) return false;' $CB::test_it_never_goes_forward_or_below_the_floor
mutate clock_back_below_the_floor examples/companion_radio/MyMesh.cpp '  if (secs < 1704067200UL || secs >= getRTCClock()->getCurrentTime()) return false;' '  if (secs >= getRTCClock()->getCurrentTime()) return false;' $CB::test_it_never_goes_forward_or_below_the_floor
mutate clock_back_not_audited examples/companion_radio/MyMesh.cpp '  Posture::audit().add(secs, Posture::EV_CLOCK_BACK, caller, Posture::W_NONE);' '' $CB::test_the_clock_and_a_new_message_go_back_and_it_is_audited
mutate clock_back_for_any_paired_link src/helpers/cli/CliRegistry.h '    {"clock back",             M_WORD,    ROLE_COMPANION,                 C_DESTRUCTIVE, F_CABLE_ONLY,' '    {"clock back",             M_WORD,    ROLE_COMPANION,                 C_WRITE, 0,' $CB::test_a_paired_link_is_refused_and_audited
